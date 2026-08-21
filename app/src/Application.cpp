#include "app/Application.h"
#include "app/ConfigLoader.h"
#include "app/TrackballInteractor.h"
#include "app/ImGuiPass.h"

#include "engine/renderer/ForwardRenderer.h"
#include "engine/renderer/deferred/DeferredRenderer.h"
#include "engine/scene/GLTFLoader.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include <stdexcept>
#include <iostream>

namespace app {

void Application::run(const char* configPath) {
    const int WIDTH = 800;
    const int HEIGHT = 600;

    // 0. 加载配置（缺失时全默认，行为与硬编码一致）
    config = ConfigLoader::load(configPath ? configPath : "configs/main.json");
    activePipeline =
        (config.section("renderer").getString("type", "deferred") == "forward") ? 0 : 1;

    // 1. Create window
    window.init(WIDTH, HEIGHT, "Vulkan + ImGui");

    // 2. Create Vulkan instance
    auto extensions = window.getRequiredInstanceExtensions();
    context.createInstance(extensions);

    // 3. Create surface
    window.createSurface(context.instance);

    // 4. Create Vulkan device
    context.createDevice(window.getSurface());

    // 5. Initialize render module (swapchain / sync / global services)
    renderModule.init(&context, window.getSurface(), [&]() {
        return window.getFramebufferSize();
    });

    // 6. Create renderer, load scene, compile render graph
    setPipeline(activePipeline);

    // 8. Initialize ImGui
    initImGui();

    // 9. Setup trackball camera interactor
    TrackballInteractor trackball;
    trackball.attach(window.getHandle(), &scene->getCamera());
    trackball.setDistance(4.0f);

    // 10. Main loop
    while (!window.shouldClose()) {
        window.pollEvents();

        if (window.framebufferResized) {
            auto [w, h] = window.getFramebufferSize();
            if (w != 0 && h != 0) {
                renderModule.recreateSwapChain();
                window.framebufferResized = false;
            }
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        trackball.update();

        renderImGui();

        // B/C 类配置改动：帧末统一重建一次管线
        if (pipelineRebuildRequested) {
            pipelineRebuildRequested = false;
            setPipeline(activePipeline);
        }

        ImGui::Render();

        auto guiRender = [](VkCommandBuffer commandBuffer) {
            ImDrawData* drawData = ImGui::GetDrawData();
            if (drawData) {
                ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
            }
        };

        if (!renderModule.drawFrame(guiRender)) {
            auto [w, h] = window.getFramebufferSize();
            if (w != 0 && h != 0) {
                renderModule.recreateSwapChain();
            }
        }
    }

    vkDeviceWaitIdle(context.device);

    cleanupImGui();
    // Scene must be cleaned up before RenderModule (and VMA) because meshes/
    // materials hold VMA allocations.
    if (scene) {
        scene->cleanup(context.device);
        scene.reset();
    }
    renderModule.cleanup();
    window.destroySurface(context.instance);
    context.cleanup();
    window.cleanup();
}

void Application::setPipeline(int type) {
    activePipeline = type;

    const engine::Config& rendererCfg = config.section("renderer");
    const engine::Config& materialCfg = config.section("material");
    // IBL 环境目录（仅 deferred 接入；ibl.enabled=false 时视同无路径）
    const std::string iblPath =
        config.getBool("ibl.enabled", true) ? config.getString("ibl", "") : "";

    // 1. Create and init renderer, then add custom passes
    std::unique_ptr<engine::IRenderer> renderer;
    engine::MaterialTemplate* matTemplate = nullptr;
    if (type == 1) {
        auto r = std::make_unique<engine::DeferredRenderer>(rendererCfg, materialCfg,
                                                            iblPath);
        r->init(renderModule.createFrameContext());
        r->addPass(std::make_unique<app::ImGuiPass>());
        matTemplate = r->getDefaultMaterialTemplate();
        renderer = std::move(r);
    } else {
        auto r = std::make_unique<engine::ForwardRenderer>(rendererCfg, materialCfg);
        r->init(renderModule.createFrameContext());
        r->addPass(std::make_unique<app::ImGuiPass>());
        matTemplate = r->getDefaultMaterialTemplate();
        renderer = std::move(r);
    }

    // 2. 材质实例绑定在材质模板上：旧场景引用旧渲染器的模板，
    //    必须先于 setRenderer（其内部销毁旧模板）清理，再用新模板重载场景
    if (scene) {
        scene->cleanup(context.device);
        scene.reset();
    }
    scene = engine::GLTFLoader::loadScene(
        config.getString("scene", "scenes/desktop"), &context, matTemplate);

    // 单平行光测试：覆盖场景光源为一盏平行光（测完删除本段，恢复场景原始光源）
    // glTF 不携带阴影标记（KHR_lights_punctual 无此字段），由 app 决定哪些光投影
    auto& sceneLights = scene->getLights();
    sceneLights.clear();
    engine::Light sun{};
    sun.type         = engine::LightType::Directional;
    sun.color        = glm::vec3(1.0f, 0.98f, 0.95f);
    sun.intensity    = 2.0f;
    sun.direction    = glm::normalize(glm::vec3(-1.0f, -2.0f, -1.0f));
    sun.castsShadows = rendererCfg.getBool("shadow.enabled", false);
    sceneLights.push_back(sun);

    // 3. Hand renderer to RenderModule and compile render graph
    renderModule.setRenderer(std::move(renderer));
    renderModule.buildGraph();
    renderModule.setScene(scene.get());
}

void Application::initImGui() {
    // Create ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Style
    ImGui::StyleColorsDark();

    // Initialize GLFW backend
    ImGui_ImplGlfw_InitForVulkan(window.getHandle(), true);

    // Create descriptor pool for ImGui
    VkDescriptorPoolSize poolSizes[] = {
        { VK_DESCRIPTOR_TYPE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
        { VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000 },
        { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
        { VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000 }
    };

    VkDescriptorPoolCreateInfo poolInfo = {};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000 * 11;  // IM_ARRAYSIZE(poolSizes) = 11
    poolInfo.poolSizeCount = 11;
    poolInfo.pPoolSizes = poolSizes;

    if (vkCreateDescriptorPool(context.device, &poolInfo, nullptr, &imguiDescriptorPool) != VK_SUCCESS) {
        throw std::runtime_error("failed to create ImGui descriptor pool!");
    }

    // Initialize Vulkan backend (Dynamic Rendering)
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.Instance = context.instance;
    initInfo.PhysicalDevice = context.physicalDevice;
    initInfo.Device = context.device;
    initInfo.QueueFamily = context.graphicsFamily;
    initInfo.Queue = context.graphicsQueue;
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = imguiDescriptorPool;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = renderModule.getImageCount();
    initInfo.UseDynamicRendering = true;

    VkFormat colorFormat = renderModule.getSwapChainFormat();
    VkPipelineRenderingCreateInfo renderingInfo{};
    renderingInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    renderingInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;

    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = renderingInfo;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

    ImGui_ImplVulkan_Init(&initInfo);
}

void Application::cleanupImGui() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    if (imguiDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context.device, imguiDescriptorPool, nullptr);
        imguiDescriptorPool = VK_NULL_HANDLE;
    }
}

void Application::renderImGui() {
    // Simple demo UI
    ImGui::Begin("Vulkan Renderer");
    ImGui::Text("Hello from ImGui!");
    ImGui::Separator();
    ImGui::Text("This is a simple GUI overlay");
    ImGui::Text("on top of the Vulkan scene.");
    ImGui::End();

    // Another window with controls
    ImGui::Begin("Controls");
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
        1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    int pipeline = activePipeline;
    if (ImGui::Combo("Pipeline", &pipeline, "Forward\0Deferred\0")) {
        setPipeline(pipeline);
    }
    if (scene) {
        // EV100 曝光：0 = 不缩放，-1 = 亮度减半，+1 = 翻倍
        ImGui::SliderFloat("Exposure (EV)", &scene->getCamera().exposureEV,
                           -15.0f, 5.0f);

        // A 类：阴影开关 —— 即时生效（castsShadows 是每帧数据，下一帧生效）
        bool shadowOn = config.section("renderer").getBool("shadow.enabled", false);
        if (ImGui::Checkbox("Shadows", &shadowOn)) {
            config.set("renderer.shadow.enabled", shadowOn);
            for (auto& l : scene->getLights()) {
                l.castsShadows = shadowOn;
            }
        }

        // B 类：雾效 —— shader 变体需重编译，经帧末管线重建生效
        if (activePipeline == 0) {
            bool fog = config.section("renderer").getBool("passParams.useFog", false);
            if (ImGui::Checkbox("Fog", &fog)) {
                config.set("renderer.passParams.useFog", fog);
                pipelineRebuildRequested = true;
            }
        }

        // B 类：IBL 开关 —— 经帧末管线重建生效（仅 deferred 且配置了环境目录时显示）
        if (activePipeline == 1 && !config.getString("ibl", "").empty()) {
            bool ibl = config.getBool("ibl.enabled", true);
            if (ImGui::Checkbox("IBL", &ibl)) {
                config.set("ibl.enabled", ibl);
                pipelineRebuildRequested = true;
            }
        }

        // B 类：Tonemap 开关 —— 两条管线通用，经帧末管线重建生效
        {
            bool tonemap = config.section("renderer").getBool("tonemap", true);
            if (ImGui::Checkbox("Tonemap", &tonemap)) {
                config.set("renderer.tonemap", tonemap);
                pipelineRebuildRequested = true;
            }
        }
    }
    if (ImGui::Button("Quit")) {
        glfwSetWindowShouldClose(window.getHandle(), GLFW_TRUE);
    }
    ImGui::End();
}

} // namespace app
