#pragma once

#include "app/Window.h"
#include "engine/VulkanContext.h"
#include "engine/config/Config.h"
#include "engine/scene/Scene.h"
#include "engine/renderer/RenderModule.h"

#include <vulkan/vulkan.h>
#include <memory>

namespace app {

class Application {
public:
    /// @param configPath 入口配置文件路径，nullptr 时用默认 configs/main.json
    void run(const char* configPath = nullptr);

private:
    Window                 window;
    engine::VulkanContext  context;
    engine::RenderModule   renderModule;
    std::unique_ptr<engine::Scene> scene;

    // 根配置：配置文件为初始值来源，Controls 的运行时修改也写回这里
    engine::Config config;
    // B/C 类参数改动后置位，帧末统一经 setPipeline 重建一次
    bool pipelineRebuildRequested = false;

    VkDescriptorPool imguiDescriptorPool = VK_NULL_HANDLE;

    void initImGui();
    void cleanupImGui();
    void renderImGui();

    // 渲染管线切换：0 = Forward，1 = Deferred（默认）。
    // 材质实例绑定在材质模板上，切换时按新渲染器的模板重载场景。
    int  activePipeline = 1;
    void setPipeline(int type);
};

} // namespace app
