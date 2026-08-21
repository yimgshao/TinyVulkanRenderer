#include "app/ImGuiPass.h"

namespace app {

ImGuiPass::ImGuiPass() {
    passName = "ImGui";
}

void ImGuiPass::OnBuildRenderGraph(const engine::FrameContext& ctx) {
    swapchainHandle = ctx.hSwapchain;
}

void ImGuiPass::Setup(engine::RenderGraphBuilder& builder) {
    engine::AttachmentDesc colorDesc{};
    colorDesc.loadOp  = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    builder.WriteColor(swapchainHandle, colorDesc);
}

void ImGuiPass::Execute(VkCommandBuffer cmd, const engine::FrameContext& frame,
                        const engine::RGResources& resources) {
    (void)resources;  // 本 pass 不采样 graph 纹理
    if (frame.guiRender) {
        frame.guiRender(cmd);
    }
}

} // namespace app
