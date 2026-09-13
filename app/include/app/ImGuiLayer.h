#pragma once

#include "app/Window.h"
#include "engine/VulkanContext.h"
#include "engine/config/Config.h"
#include "engine/renderer/RenderModule.h"
#include "engine/renderer/renderpass/IRenderPass.h"
#include "engine/scene/Scene.h"

#include <memory>
#include <vulkan/vulkan.h>

namespace app {

struct ImGuiFrameActions {
    bool rebuildRenderer = false;
    bool quit = false;
};

/// Owns the app-side ImGui context, GLFW/Vulkan backends, controls and render bridge.
class ImGuiLayer {
public:
    void init(Window& window, engine::VulkanContext& context,
              engine::RenderModule& renderModule);
    void cleanup(engine::VulkanContext& context);

    void beginFrame();
    ImGuiFrameActions drawControls(engine::Scene* scene, engine::Config& config);
    void endFrame();
    void recordDrawCommands(VkCommandBuffer commandBuffer) const;

    std::unique_ptr<engine::IRenderPass> createRenderPass() const;

private:
    VkDescriptorPool descriptorPool_ = VK_NULL_HANDLE;
};

} // namespace app
