#pragma once

#include "app/Window.h"
#include "app/ConfigLoader.h"
#include "app/ImGuiLayer.h"
#include "engine/VulkanContext.h"
#include "engine/config/Config.h"
#include "engine/scene/Scene.h"
#include "engine/renderer/RenderModule.h"

#include <vulkan/vulkan.h>
#include <memory>

namespace app {

class Application {
public:
    explicit Application(ConfigLoader configLoader = {});

    void run();

private:
    Window                 window;
    engine::VulkanContext  context;
    engine::RenderModule   renderModule;
    std::unique_ptr<engine::Scene> scene;

    // 根配置：配置文件为初始值来源，Controls 的运行时修改也写回这里
    engine::Config config;
    ConfigLoader configLoader;
    ImGuiLayer imgui;

    void rebuildRenderer();
};

} // namespace app
