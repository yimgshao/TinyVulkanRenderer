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

void ForwardPass::Setup(RenderGraphBuilder& builder,
                        const RenderGraphBuildContext& ctx) {
    builder.SetShader(shaderConfig);
    builder.SetPassParams(passParams);
    builder.SetPipelineLayout(pipelineLayout);
    builder.SetMaterialHeader(materialHeader);
    builder.SetVertexLayout("StaticMesh");
    builder.SetPipelineState(PipelineStateDesc::Default());
    builder.SetMsaaSamples(msaaSamples);
    builder.SetAutoBindShader(false);

    colorFormat = ctx.swapchainFormat;

    // Depth 资源
    RGTextureDesc depthDesc{};
    depthDesc.width  = ctx.renderExtent.width;
    depthDesc.height = ctx.renderExtent.height;
    depthDesc.format = depthFormat;
    depthDesc.usage  = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    hDepth = builder.CreateTexture("ForwardDepth", depthDesc);

    AttachmentDesc colorDesc{};
    colorDesc.loadOp     = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorDesc.storeOp    = VK_ATTACHMENT_STORE_OP_STORE;
    colorDesc.clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    builder.WriteColor(ctx.hSwapchain, colorDesc);

    AttachmentDesc depthAttachDesc{};
    depthAttachDesc.loadOp     = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachDesc.storeOp    = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depthAttachDesc.clearValue = {{{1.0f, 0.0f}}};
    builder.WriteDepth(hDepth, depthAttachDesc);

    hShadowDir   = builder.ReadTexture("ShadowAtlas_Directional");
    hShadowPoint = builder.ReadTexture("ShadowAtlas_Point");
}

void ForwardPass::Execute(VkCommandBuffer cmd, const FrameContext& frame,
                          const RGResources& resources) {
    (void)resources;
    if (GetPipelineLayout() == VK_NULL_HANDLE) return;

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
        GetPipelineLayout(), 0, 1, &frameSet.bufferIndex, &frameSet.offset);

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
            if (tmpl) {
                ShaderVariantKey variantKey;
                variantKey.materialType  = obj.material->getMaterialType();
                variantKey.materialParamHash  = obj.material->getShaderParams().hash();
                variantKey.passParamHash      = this->passParams.hash();
                VkPipeline pipeline = BindShaderVariant(
                    cmd, variantKey, obj.material->getShaderParams(),
                    tmpl->getMaterialHeader());
                if (pipeline != boundPipeline) {
                    boundPipeline = pipeline;
                }
            }

            const DescriptorSetHandle matSet = obj.material->getSet();
            uint32_t setIndex = 1;
            if (obj.material->getTemplate()) {
                setIndex = obj.material->getTemplate()->getMaterialSetIndex();
            }
            ext::vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                GetPipelineLayout(), setIndex, 1, &matSet.bufferIndex, &matSet.offset);
            lastMat = obj.material;
        }

        vkCmdPushConstants(cmd, GetPipelineLayout(),
                           VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(glm::mat4), &obj.transform);

        obj.mesh->bind(cmd);
        obj.mesh->drawIndexed(cmd);
    }
}

} // namespace engine
