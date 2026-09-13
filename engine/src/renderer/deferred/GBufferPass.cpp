#include "engine/renderer/deferred/GBufferPass.h"

#include "engine/scene/Scene.h"
#include "engine/scene/Mesh.h"
#include "engine/scene/Material.h"
#include "engine/descriptor/DescriptorSetManager.h"
#include "engine/renderer/PipelineStateDesc.h"

namespace engine {

GBufferPass::GBufferPass() {
    passName = "GBuffer";
}

void GBufferPass::Setup(RenderGraphBuilder& builder,
                        const RenderGraphBuildContext& ctx) {
    builder.SetPassParams(passParams);
    builder.SetMsaaSamples(msaaSamples);

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

void GBufferPass::Execute(RenderPassContext& context) {
    context.DrawScene("GBuffer");
}

} // namespace engine
