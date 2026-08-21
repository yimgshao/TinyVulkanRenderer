#include "engine/renderer/deferred/DeferredLightingPass.h"

#include "engine/descriptor/DescriptorSetManager.h"
#include "engine/pso/PsoManager.h"
#include "engine/renderer/PipelineStateDesc.h"

namespace engine {

DeferredLightingPass::DeferredLightingPass() {
    passName = "DeferredLighting";
}

void DeferredLightingPass::OnBuildRenderGraph(const FrameContext& ctx) {
    swapchainHandle = ctx.hSwapchain;
    buildExtent     = ctx.renderExtent;
    colorFormat     = ctx.swapchainFormat;

    // 全屏光照 PSO：无顶点输入（vertexLayoutName = ""）、无 depth attachment
    // （深度经采样读取，见类注释），故必须关闭深度测试。
    // PSO 由 PsoManager 缓存，swapchain 重建（格式可能变化）时按需重建。
    GraphicsPSODesc psoDesc{};
    psoDesc.shaderConfig.moduleName = "deferred/lighting";
    // 变体参数仅支持 bool（DxcCompiler::ResolveParamValue 按 getBool 取值），
    // 且 camelCase→UPPER_SNAKE 逐大写字母加下划线：
    // 参数名必须避开连续大写（useIBL 会变 USE_I_B_L），故用 useIbl。
    psoDesc.shaderConfig.genericValueParams = {"useIbl", "useTonemap"};
    psoDesc.passParams.set("useIbl", useIBL);
    psoDesc.passParams.set("useTonemap", useTonemap);
    psoDesc.variantKey              = {};   // 无材质类型
    psoDesc.materialHeader          = "";   // 独立编译，无材质头
    psoDesc.vertexLayoutName        = "";   // 全屏三角形，SV_VertexID 生成
    psoDesc.state                   = PipelineStateDesc::DeferredLighting();
    psoDesc.state.cullMode          = VK_CULL_MODE_NONE;
    psoDesc.state.depthTestEnable   = VK_FALSE;
    psoDesc.colorCount              = 1;
    psoDesc.colorFormats[0]         = colorFormat;
    psoDesc.depthFormat             = VK_FORMAT_UNDEFINED;
    psoDesc.msaaSamples             = VK_SAMPLE_COUNT_1_BIT;
    psoDesc.layout                  = pipelineLayout;
    psoDesc.passName                = passName;
    pipeline = ctx.psoManager->getOrCreate(psoDesc);
}

void DeferredLightingPass::Setup(RenderGraphBuilder& builder) {
    // GBuffer + depth：PS 阶段采样（GBufferPass 是 lastProducer，
    // 依赖边与 layout 过渡由 RenderGraph 自动完成）。
    const char* kGBufferNames[4] = {
        "GBuffer0", "GBuffer1", "GBuffer2", "GBuffer3",
    };
    for (uint32_t i = 0; i < 4; ++i) {
        hGBuffer[i] = builder.GetTexture(kGBufferNames[i]);
        if (hGBuffer[i] != kInvalidRGTextureHandle) {
            builder.ReadTexture(hGBuffer[i],
                                VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
        }
    }
    hDepth = builder.GetTexture("GBufferDepth");
    if (hDepth != kInvalidRGTextureHandle) {
        builder.ReadTexture(hDepth,
                            VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }

    // 阴影 atlas：与 forward 相同的采样依赖声明。
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

    // 输出：swapchain（CLEAR + STORE；无几何像素由 FS discard 保留 clear 颜色）
    AttachmentDesc colorDesc{};
    colorDesc.loadOp     = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorDesc.storeOp    = VK_ATTACHMENT_STORE_OP_STORE;
    colorDesc.clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    builder.WriteColor(swapchainHandle, colorDesc);
}

void DeferredLightingPass::Execute(VkCommandBuffer cmd, const FrameContext& frame,
                                   const RGResources& resources) {
    if (pipeline == VK_NULL_HANDLE || pipelineLayout == VK_NULL_HANDLE) return;

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

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    // 绑定 Set 0（frame）
    const DescriptorSetHandle& frameSet = frame.frameSet;
    ext::vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipelineLayout, 0, 1, &frameSet.bufferIndex, &frameSet.offset);

    // 重写并绑定 Set 1（GBuffer 采样；RGResources 契约：引用 graph 纹理的
    // descriptor 必须每帧重写。sampler binding 5 已在 init 时一次写好）。
    if (gbufferSet.isValid() && descManager != nullptr) {
        bool allValid = true;
        for (uint32_t i = 0; i < 4; ++i) {
            VkImageView view = resources.GetImageView(hGBuffer[i]);
            if (view == VK_NULL_HANDLE) { allValid = false; break; }
            VkDescriptorImageInfo info{};
            info.imageView   = view;
            info.imageLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
            descManager->writeImage(gbufferLayoutId, gbufferSet, i, info,
                                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        }
        VkImageView depthView = resources.GetImageView(hDepth);
        if (depthView == VK_NULL_HANDLE) allValid = false;
        else {
            VkDescriptorImageInfo depthInfo{};
            depthInfo.imageView   = depthView;
            depthInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            descManager->writeImage(gbufferLayoutId, gbufferSet, 4, depthInfo,
                                    VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
        }

        if (allValid) {
            ext::vkCmdSetDescriptorBufferOffsetsEXT(
                cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
                gbufferSetIndex, 1, &gbufferSet.bufferIndex, &gbufferSet.offset);
        }
    }

    // 重写并绑定 Set 2（阴影 atlas，与 ForwardPass 相同的模式）
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

    // 绑定 Set 3（IBL；描述符在 renderer init 时一次写好，无需每帧重写）
    if (iblSet.isValid()) {
        ext::vkCmdSetDescriptorBufferOffsetsEXT(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout,
            iblSetIndex, 1, &iblSet.bufferIndex, &iblSet.offset);
    }

    // 全屏三角形：3 顶点，无顶点输入
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace engine
