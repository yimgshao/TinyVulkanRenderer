#include "app/ImGuiLayer.h"

#include "app/ImGuiPass.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include <iterator>
#include <stdexcept>

namespace app {

void ImGuiLayer::init(Window& window, engine::VulkanContext& context,
                      engine::RenderModule& renderModule) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForVulkan(window.getHandle(), true);

    VkDescriptorPoolSize poolSizes[] = {
        {VK_DESCRIPTOR_TYPE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000},
        {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1000},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000},
        {VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, 1000},
    };
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 1000 * static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.poolSizeCount = static_cast<uint32_t>(std::size(poolSizes));
    poolInfo.pPoolSizes = poolSizes;
    if (vkCreateDescriptorPool(context.device, &poolInfo, nullptr, &descriptorPool_) != VK_SUCCESS)
        throw std::runtime_error("failed to create ImGui descriptor pool!");

    ImGui_ImplVulkan_InitInfo initInfo{};
    initInfo.ApiVersion = VK_API_VERSION_1_3;
    initInfo.Instance = context.instance;
    initInfo.PhysicalDevice = context.physicalDevice;
    initInfo.Device = context.device;
    initInfo.QueueFamily = context.graphicsFamily;
    initInfo.Queue = context.graphicsQueue;
    initInfo.PipelineCache = VK_NULL_HANDLE;
    initInfo.DescriptorPool = descriptorPool_;
    initInfo.MinImageCount = 2;
    initInfo.ImageCount = renderModule.getImageCount();
    initInfo.UseDynamicRendering = true;

    const VkFormat colorFormat = renderModule.getSwapChainFormat();
    VkPipelineRenderingCreateInfo renderingInfo{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachmentFormats = &colorFormat;
    renderingInfo.depthAttachmentFormat = VK_FORMAT_D32_SFLOAT;
    initInfo.PipelineInfoMain.PipelineRenderingCreateInfo = renderingInfo;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    ImGui_ImplVulkan_Init(&initInfo);
}

void ImGuiLayer::cleanup(engine::VulkanContext& context) {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    if (descriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(context.device, descriptorPool_, nullptr);
        descriptorPool_ = VK_NULL_HANDLE;
    }
}

void ImGuiLayer::beginFrame() {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

ImGuiFrameActions ImGuiLayer::drawControls(engine::Scene* scene,
                                            engine::Config& config) {
    ImGuiFrameActions actions;

    ImGui::Begin("Vulkan Renderer");
    ImGui::Text("Hello from ImGui!");
    ImGui::Separator();
    ImGui::Text("This is a simple GUI overlay");
    ImGui::Text("on top of the Vulkan scene.");
    ImGui::End();

    ImGui::Begin("Controls");
    ImGui::Text("Application average %.3f ms/frame (%.1f FPS)",
                1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
    if (scene) {
        ImGui::SliderFloat("Exposure (EV)", &scene->getCamera().exposureEV, -15.0f, 5.0f);

        bool shadowOn = config.section("renderer").getBool("shadow.enabled", false);
        if (ImGui::Checkbox("Shadows", &shadowOn)) {
            config.set("renderer.shadow.enabled", shadowOn);
            for (auto& light : scene->getLights()) light.castsShadows = shadowOn;
        }

        if (!config.getString("ibl.path", "").empty()) {
            bool ibl = config.getBool("ibl.enabled", true);
            if (ImGui::Checkbox("IBL", &ibl)) {
                config.set("ibl.enabled", ibl);
                actions.rebuildRenderer = true;
            }
        }

        bool tonemap = config.section("renderer").getBool("tonemap", true);
        if (ImGui::Checkbox("Tonemap", &tonemap)) {
            config.set("renderer.tonemap", tonemap);
            actions.rebuildRenderer = true;
        }
    }
    actions.quit = ImGui::Button("Quit");
    ImGui::End();
    return actions;
}

void ImGuiLayer::endFrame() { ImGui::Render(); }

void ImGuiLayer::recordDrawCommands(VkCommandBuffer commandBuffer) const {
    if (ImDrawData* drawData = ImGui::GetDrawData())
        ImGui_ImplVulkan_RenderDrawData(drawData, commandBuffer);
}

std::unique_ptr<engine::IRenderPass> ImGuiLayer::createRenderPass() const {
    return std::make_unique<ImGuiPass>();
}

} // namespace app
