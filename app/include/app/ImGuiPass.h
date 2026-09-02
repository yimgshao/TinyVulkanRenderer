#pragma once

#include "engine/renderer/renderpass/IRenderPass.h"

#include <vulkan/vulkan.h>

namespace app {

class ImGuiPass : public engine::IRenderPass {
public:
    ImGuiPass();

    void Setup(engine::RenderGraphBuilder& builder,
               const engine::RenderGraphBuildContext& ctx) override;
    void Execute(VkCommandBuffer cmd, const engine::FrameContext& frame,
                 const engine::RGResources& resources) override;
};

} // namespace app
