#include "app/ImGuiPass.h"

namespace app {

ImGuiPass::ImGuiPass() {
    passName = "ImGui";
}

void ImGuiPass::Setup(engine::RenderGraphBuilder& builder,
                      const engine::RenderGraphBuildContext& ctx) {
    engine::AttachmentDesc colorDesc{};
    colorDesc.loadOp  = VK_ATTACHMENT_LOAD_OP_LOAD;
    colorDesc.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    builder.WriteColor(ctx.hSwapchain, colorDesc);
}

void ImGuiPass::Execute(engine::RenderPassContext& context) {
    const auto cmd = context.GetCommandBuffer();
    const auto& frame = context.GetFrame();
    if (frame.guiRender) {
        frame.guiRender(cmd);
    }
}

} // namespace app
