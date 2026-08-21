#include "engine/renderer/renderpass/ForwardPass.h"

#include "engine/scene/Scene.h"
#include "engine/scene/Mesh.h"
#include "engine/scene/MaterialInstance.h"
#include "engine/scene/MaterialTemplate.h"
#include "engine/descriptor/DescriptorSetManager.h"
#include "engine/renderer/PipelineStateDesc.h"

namespace engine {

ForwardPass::ForwardPass() {
    passName = "Forward";
}

void ForwardPass::OnBuildRenderGraph(const FrameContext& ctx) {
    swapchainHandle = ctx.hSwapchain;
    buildExtent     = ctx.renderExtent;
    colorFormat     = ctx.swapchainFormat;
}

void ForwardPass::Setup(RenderGraphBuilder& builder) {
    // Depth 资源
    RGTextureDesc depthDesc{};
    depthDesc.width  = buildExtent.width;
    depthDesc.height = buildExtent.height;
    depthDesc.format = depthFormat;
    depthDesc.usage  = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    hDepth = builder.CreateTexture("ForwardDepth", depthDesc);

    AttachmentDesc colorDesc{};
    colorDesc.loadOp     = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorDesc.storeOp    = VK_ATTACHMENT_STORE_OP_STORE;
    colorDesc.clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    builder.WriteColor(swapchainHandle, colorDesc);

    AttachmentDesc depthAttachDesc{};
    depthAttachDesc.loadOp     = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachDesc.storeOp    = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachDesc.clearValue = {{{1.0f, 0.0f}}};
    builder.WriteDepth(hDepth, depthAttachDesc);

    // 阴影 atlas：声明采样依赖（拓扑排序 + layout 过渡自动完成）。
    // ShadowPass 未注册时句柄无效，跳过即可。
    hShadowDir   = builder.GetTexture("ShadowAtlas_Directional");
    hShadowPoint = builder.GetTexture("ShadowAtlas_Point");
    if (hShadowDir != kInvalidRGTextureHandle) {
        builder.ReadTexture(hShadowDir,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
    if (hShadowPoint != kInvalidRGTextureHandle) {
        builder.ReadTexture(hShadowPoint,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }
}

void ForwardPass::Execute(VkCommandBuffer cmd, const FrameContext& frame,
                          const RGResources& resources) {
    (void)resources;  // 本 pass 不采样 graph 纹理
    if (pipelineLayout == VK_NULL_HANDLE) return;

    const VkExtent2D extent = frame.renderExtent;

    VkViewport viewport{};
    viewport.x        = 0.0f;
    viewport.y        = 0.0f;
    viewport.width    = static_cast<float>(extent.width);
    viewport.height   = static_cast<float>(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    vkCmdSetViewport(cmd, 0, 1, &viewport);

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = extent;
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    // 绑定 Set 0（frame）
    const DescriptorSetHandle& frameSet = frame.frameSet;
    ext::vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout, 0, 1, &frameSet.bufferIndex, &frameSet.offset);

    // 绑定阴影 set（RGResources 契约：graph 纹理的 descriptor 每帧重写）
    if (shadowSet.isValid() && descManager != nullptr) {
        VkImageView dirView = resources.GetImageView(hShadowDir);
        VkImageView ptView  = resources.GetImageView(hShadowPoint);
        if (dirView != VK_NULL_HANDLE && ptView != VK_NULL_HANDLE) {
            VkDescriptorImageInfo dirInfo{};
            dirInfo.imageView   = dirView;
            dirInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            VkDescriptorImageInfo ptInfo{};
            ptInfo.imageView   = ptView;
            ptInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            descManager->writeImage(shadowLayoutId, shadowSet, 0, dirInfo,
                                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
            descManager->writeImage(shadowLayoutId, shadowSet, 1, ptInfo,
                                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);

            ext::vkCmdSetDescriptorBufferOffsetsEXT(
                cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                shadowSetIndex, 1, &shadowSet.bufferIndex, &shadowSet.offset);
        }
    }

    drawOpaqueObjects(cmd, frame);
    // GUI 叠加由独立的 ImGuiPass 负责。
}

void ForwardPass::drawOpaqueObjects(VkCommandBuffer cmd, const FrameContext& frame) {
    Scene* scene = frame.scene;
    if (!scene) return;

    VkPipeline boundPipeline = VK_NULL_HANDLE;
    MaterialInstance* lastMat = nullptr;

    for (auto& obj : scene->getRenderObjects()) {
        if (!obj.mesh || !obj.material) continue;
        if (obj.material->getParams().alphaMode != AlphaMode::Opaque) continue;

        if (obj.material != lastMat) {
            auto* tmpl = obj.material->getTemplate();
            if (tmpl && device != VK_NULL_HANDLE) {
                ShaderVariantKey variantKey;
                variantKey.materialType  = obj.material->getMaterialType();
                variantKey.materialParamHash  = obj.material->getShaderParams().hash();
                variantKey.passParamHash      = this->passParams.hash();
                VkPipeline pipeline = tmpl->getOrCreatePipeline(
                    device, shaderConfig, passName, PipelineStateDesc::Default(),
                    variantKey, obj.material->getShaderParams(),
                    this->passParams, 1, &colorFormat, depthFormat, msaaSamples);
                if (pipeline != boundPipeline) {
                    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
                    boundPipeline = pipeline;
                }
            }

            const DescriptorSetHandle matSet = obj.material->getSet();
            uint32_t setIndex = 1;
            if (obj.material->getTemplate()) {
                setIndex = obj.material->getTemplate()->getMaterialSetIndex();
            }
            ext::vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                pipelineLayout, setIndex, 1, &matSet.bufferIndex, &matSet.offset);
            lastMat = obj.material;
        }

        vkCmdPushConstants(cmd, pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(glm::mat4), &obj.transform);

        obj.mesh->bind(cmd);
        obj.mesh->drawIndexed(cmd);
    }
}

} // namespace engine
