#include "engine/renderer/deferred/DeferredLightingPass.h"

#include "engine/descriptor/DescriptorSetManager.h"
#include "engine/pso/PsoManager.h"
#include "engine/renderer/PipelineStateDesc.h"

namespace engine {

DeferredLightingPass::DeferredLightingPass() {
    passName = "DeferredLighting";
}

void DeferredLightingPass::Setup(RenderGraphBuilder& builder,
                                 const RenderGraphBuildContext& ctx) {
    // 全屏光照 PSO：无顶点输入（vertexLayoutName = ""）、无 depth attachment
    // （深度经采样读取，见类注释），故必须关闭深度测试。
    // PSO 由 PsoManager 缓存，swapchain 重建（格式可能变化）时按需重建。
    ShaderModuleConfig shader{};
    shader.moduleName = "deferred/lighting";
    // 变体参数仅支持 bool（DxcCompiler::ResolveParamValue 按 getBool 取值），
    // 且 camelCase→UPPER_SNAKE 逐大写字母加下划线：
    // 参数名必须避开连续大写（useIBL 会变 USE_I_B_L），故用 useIbl。
    shader.genericValueParams = {"useIbl", "useTonemap"};
    builder.SetShader(shader);
    ShaderParamSet params;
    params.set("useIbl", useIBL);
    params.set("useTonemap", useTonemap);
    builder.SetPassParams(params);
    builder.SetVertexLayout("");
    PipelineStateDesc state = PipelineStateDesc::DeferredLighting();
    state.cullMode        = VK_CULL_MODE_NONE;
    state.depthTestEnable = VK_FALSE;
    builder.SetPipelineState(state);

    // GBuffer + depth：PS 阶段采样（GBufferPass 是 lastProducer，
    // 依赖边与 layout 过渡由 RenderGraph 自动完成）。
    const char* kGBufferNames[4] = {
        "GBuffer0", "GBuffer1", "GBuffer2", "GBuffer3",
    };
    for (uint32_t i = 0; i < 4; ++i) {
        hGBuffer[i] = builder.ReadTexture(kGBufferNames[i]);
    }
    hDepth = builder.ReadTexture("GBufferDepth");

    hShadowDir   = builder.ReadTexture("ShadowAtlas_Directional");
    hShadowPoint = builder.ReadTexture("ShadowAtlas_Point");

    // 输出：swapchain（CLEAR + STORE；无几何像素由 FS discard 保留 clear 颜色）
    AttachmentDesc colorDesc{};
    colorDesc.loadOp     = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorDesc.storeOp    = VK_ATTACHMENT_STORE_OP_STORE;
    colorDesc.clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    builder.WriteColor(ctx.hSwapchain, colorDesc);
}

void DeferredLightingPass::Execute(VkCommandBuffer cmd, const FrameContext& frame,
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

    // 绑定 Set 3（IBL；描述符在 renderer init 时一次写好，无需每帧重写）
    if (iblSet.isValid()) {
        ext::vkCmdSetDescriptorBufferOffsetsEXT(
            cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GetPipelineLayout(),
            iblSetIndex, 1, &iblSet.bufferIndex, &iblSet.offset);
    }

    // 全屏三角形：3 顶点，无顶点输入
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace engine
