#include "app/Application.h"
#include "app/ConfigLoader.h"
#include "app/interactor/FirstPersonInteractor.h"
#include "app/interactor/IInteractor.h"
#include "app/interactor/TrackballInteractor.h"

#include "engine/renderer/deferred/DeferredRenderer.h"
#include "engine/scene/GLTFLoader.h"
#include "engine/scene/PrimitiveMeshFactory.h"

#include <iostream>
#include <filesystem>
#include <memory>
#include <utility>
#include <glm/gtc/matrix_transform.hpp>
#include "imgui.h"

namespace app {

Application::Application(ConfigLoader loader) : configLoader(std::move(loader)) {}

void Application::run() {
    const int WIDTH = 800;
    const int HEIGHT = 600;

    // 0. All application configuration is resolved from the configs directory.
    config = ConfigLoader::load("main.json");
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
    }, configLoader.customShaderSearchPaths());

    configLoader.registerShaderAssets(renderModule.getShaderAssetManager());

    // 6. Create renderer, load scene, compile render graph
    rebuildRenderer();

    // 8. Initialize ImGui
    imgui.init(window, context, renderModule);

    // 9. Setup the configured camera interactor.
    std::unique_ptr<IInteractor> interactor;
    const std::string interactorType = config.getString("interactor", "trackball");
    if (interactorType == "first_person") {
        interactor = std::make_unique<FirstPersonInteractor>();
        interactor->attach(window.getHandle(), &scene->getCamera());
    } else {
        if (interactorType != "trackball") {
            std::cerr << "[Config] unknown interactor '" << interactorType
                      << "', falling back to 'trackball'.\n";
        }
        auto trackball = std::make_unique<TrackballInteractor>();
        trackball->attach(window.getHandle(), &scene->getCamera());
        trackball->setDistance(4.0f);
        interactor = std::move(trackball);
    }

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

        imgui.beginFrame();

        interactor->update(ImGui::GetIO().DeltaTime);

        const auto actions = imgui.drawControls(scene.get(), config);
        if (actions.rebuildRenderer) rebuildRenderer();
        if (actions.quit) window.requestClose();

        imgui.endFrame();

        auto guiRender = [this](VkCommandBuffer commandBuffer) {
            imgui.recordDrawCommands(commandBuffer);
        };

        if (!renderModule.drawFrame(guiRender)) {
            auto [w, h] = window.getFramebufferSize();
            if (w != 0 && h != 0) {
                renderModule.recreateSwapChain();
            }
        }
    }

    interactor->detach();
    vkDeviceWaitIdle(context.device);

    imgui.cleanup(context);
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

void Application::rebuildRenderer() {
    const engine::Config& rendererCfg = config.section("renderer");
    // ibl.enabled=false 时视同无 IBL 路径。
    const std::string iblPath =
        config.getBool("ibl.enabled", true) ? config.getString("ibl.path", "") : "";

    auto renderer = std::make_unique<engine::DeferredRenderer>(rendererCfg, iblPath);
    renderer->init(renderModule.createFrameContext());
    renderer->addPass(imgui.createRenderPass());

    // Materials belong to the scene; changing renderer does not recreate them.
    if (!scene) {
        scene = engine::GLTFLoader::loadScene(config.getString("scene", "scenes/desktop"), &context,
            renderModule.getShaderAssetManager().find("Builtin/PBR"));
        if (auto* shader = renderModule.getShaderAssetManager().find("Example/AnimatedColor")) {
            // Default custom shader test cube
            engine::RenderObject cube;
            cube.mesh = engine::PrimitiveMeshFactory::createCube(*scene, context);
            cube.material = scene->createMaterial(*shader);
            cube.transform = glm::translate(glm::mat4(1), glm::vec3(-1.0f, 0.5f, -1.0f))
                           * glm::scale(glm::mat4(1), glm::vec3(0.5f));
            scene->addRenderObject(cube);
        }
    }

    // Default light
    auto& sceneLights = scene->getLights();
    sceneLights.clear();
    engine::Light sun{};
    sun.type         = engine::LightType::Directional;
    sun.color        = glm::vec3(1.0f, 0.98f, 0.95f);
    sun.intensity    = 10.0f;
    sun.direction    = glm::normalize(glm::vec3(-1.0f, -2.0f, -1.0f));
    sun.castsShadows = rendererCfg.getBool("shadow.enabled", false);
    sceneLights.push_back(sun);

    // 3. Hand renderer to RenderModule and compile render graph
    renderModule.setRenderer(std::move(renderer));
    renderModule.buildGraph();
    renderModule.setScene(scene.get());
}

} // namespace app
