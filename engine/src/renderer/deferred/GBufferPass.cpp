#include "engine/renderer/deferred/GBufferPass.h"

#include "engine/scene/Scene.h"
#include "engine/scene/Mesh.h"
#include "engine/scene/MaterialInstance.h"
#include "engine/scene/MaterialTemplate.h"
#include "engine/descriptor/DescriptorSetManager.h"
#include "engine/renderer/PipelineStateDesc.h"

namespace engine {

GBufferPass::GBufferPass() {
    passName = "GBuffer";
}

void GBufferPass::Setup(RenderGraphBuilder& builder,
                        const RenderGraphBuildContext& ctx) {
    builder.SetShader(shaderConfig);
    builder.SetPassParams(passParams);
    builder.SetPipelineLayout(pipelineLayout);
    builder.SetMaterialHeader(materialHeader);
    builder.SetVertexLayout("StaticMesh");
    builder.SetPipelineState(PipelineStateDesc::Default());
    builder.SetMsaaSamples(msaaSamples);
    builder.SetAutoBindShader(false);

    // 4 张 GBuffer + 1 张 depth，全部按 renderExtent 声明
    //（RG 隐含约定：同 pass 的 MRT 必须同尺寸）。
    // 均为 graph 内部 transient 纹理，随 graph 重建自动适配 resize。
    RGTextureDesc gbufferDesc{};
    gbufferDesc.width  = ctx.renderExtent.width;
    gbufferDesc.height = ctx.renderExtent.height;
    gbufferDesc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT
                       | VK_IMAGE_USAGE_SAMPLED_BIT;  // lighting pass 采样

    const char* kGBufferNames[kGBufferCount] = {
        "GBuffer0", "GBuffer1", "GBuffer2", "GBuffer3",
    };
    for (uint32_t i = 0; i < kGBufferCount; ++i) {
        gbufferDesc.format = kGBufferFormats[i];
        hGBuffer[i] = builder.CreateTexture(kGBufferNames[i], gbufferDesc);

        AttachmentDesc colorDesc{};
        colorDesc.loadOp     = VK_ATTACHMENT_LOAD_OP_CLEAR;
        colorDesc.storeOp    = VK_ATTACHMENT_STORE_OP_STORE;
        colorDesc.clearValue = {{{0.0f, 0.0f, 0.0f, 0.0f}}};
        builder.WriteColor(hGBuffer[i], colorDesc);
    }

    // depth：lighting pass 以只读方式采样（重建世界坐标 + 天空像素剔除），
    // 因此 usage 需要 SAMPLED_BIT。
    RGTextureDesc depthDesc{};
    depthDesc.width  = ctx.renderExtent.width;
    depthDesc.height = ctx.renderExtent.height;
    depthDesc.format = depthFormat;
    depthDesc.usage  = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                     | VK_IMAGE_USAGE_SAMPLED_BIT;
    hDepth = builder.CreateTexture("GBufferDepth", depthDesc);

    AttachmentDesc depthAttachDesc{};
    depthAttachDesc.loadOp     = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depthAttachDesc.storeOp    = VK_ATTACHMENT_STORE_OP_STORE;
    depthAttachDesc.clearValue = {{{1.0f, 0.0f}}};
    builder.WriteDepth(hDepth, depthAttachDesc);

}

void GBufferPass::Execute(VkCommandBuffer cmd, const FrameContext& frame,
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
}

void GBufferPass::drawOpaqueObjects(VkCommandBuffer cmd, const FrameContext& frame) {
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
                // MRT：4 张 GBuffer 格式进 PSO 缓存键
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
