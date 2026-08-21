#pragma once

#include "engine/renderer/renderpass/IRenderPass.h"

#include <vulkan/vulkan.h>

namespace app {

class ImGuiPass : public engine::IRenderPass {
public:
    ImGuiPass();

    void Setup(engine::RenderGraphBuilder& builder) override;
    void Execute(VkCommandBuffer cmd, const engine::FrameContext& frame,
                 const engine::RGResources& resources) override;
    void OnBuildRenderGraph(const engine::FrameContext& ctx) override;

    engine::RGTextureHandle swapchainHandle = engine::kInvalidRGTextureHandle;
};

} // namespace app
