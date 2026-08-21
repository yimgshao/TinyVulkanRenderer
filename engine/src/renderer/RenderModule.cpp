#include "engine/renderer/RenderModule.h"

#include "engine/VulkanUtils.h"
#include "engine/scene/Scene.h"

#include <cstring>
#include <stdexcept>

namespace engine {

// ------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------

void RenderModule::init(VulkanContext* ctx, VkSurfaceKHR surface,
                        FramebufferSizeFn getSize) {
    context = ctx;
    initVMA(context->instance, context->physicalDevice, context->device);

    swapChain = std::make_unique<SwapChain>();
    swapChain->init(ctx, surface, std::move(getSize));

    renderGraph.Init(context);

    createFrameSetLayout();

    descManager.init(context->device, context->physicalDevice);
    frameLayoutId = descManager.registerLayout(frameSetLayout, MAX_FRAMES_IN_FLIGHT);

#ifdef ENGINE_DEFAULT_SHADER_DIR
    shaderVariantManager.Init(ENGINE_DEFAULT_SHADER_DIR);
#else
    shaderVariantManager.Init("engine/shaders");
#endif

    psoManager.init(context->device, &shaderVariantManager);

    createFrameResources();
    createSyncObjects();
    createCommandBuffers();
}

void RenderModule::cleanup() {
    if (!context) return;
    VkDevice device = context->device;

    vkDeviceWaitIdle(device);

    if (renderer) {
        renderer->cleanup();
        renderer.reset();
    }

    renderGraph.Cleanup();

    // PSO 统一销毁（pipeline 不依赖 descriptor/shader 模块，只需先于 device 销毁）
    psoManager.cleanup();

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (frames[i].imageAvailable != VK_NULL_HANDLE)
            vkDestroySemaphore(device, frames[i].imageAvailable, nullptr);
        if (frames[i].inFlight != VK_NULL_HANDLE)
            vkDestroyFence(device, frames[i].inFlight, nullptr);
    }
    for (auto sem : imageRenderFinished) {
        vkDestroySemaphore(device, sem, nullptr);
    }
    imageRenderFinished.clear();

    for (auto& frame : frames) {
        if (frame.uboMapped) {
            vmaUnmapMemory(g_vmaAllocator, frame.uboAllocation);
            frame.uboMapped = nullptr;
        }
        if (frame.uboBuffer != VK_NULL_HANDLE) {
            vmaDestroyBuffer(g_vmaAllocator, frame.uboBuffer, frame.uboAllocation);
            frame.uboBuffer = VK_NULL_HANDLE;
        }
    }

    descManager.cleanup();

    if (frameSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, frameSetLayout, nullptr);
        frameSetLayout = VK_NULL_HANDLE;
    }

    shaderVariantManager.Cleanup();

    if (swapChain) {
        swapChain->cleanup();
        swapChain.reset();
    }

    cleanupVMA();
    context = nullptr;
}

// ------------------------------------------------------------------
// Renderer switching
// ------------------------------------------------------------------

void RenderModule::setRenderer(std::unique_ptr<IRenderer> p) {
    if (!context) {
        throw std::runtime_error(
            "RenderModule::setRenderer called before init()");
    }

    vkDeviceWaitIdle(context->device);

    if (renderer) {
        renderGraph.Cleanup();
        renderer->cleanup();
        renderer.reset();
    }

    renderer = std::move(p);
}

void RenderModule::buildGraph() {
    if (!renderer) return;
    rebuildRenderGraph();
}

// ------------------------------------------------------------------
// Frame loop
// ------------------------------------------------------------------

bool RenderModule::drawFrame(GuiRenderFn guiRender) {
    if (!renderer) return false;

    VkDevice device = context->device;
    auto& frame = frames[currentFrame];

    vkWaitForFences(device, 1, &frame.inFlight, VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    if (!swapChain->acquireNextImage(imageIndex, frame.imageAvailable)) {
        return false;
    }

    updateFrameUBO(currentFrame);

    vkResetFences(device, 1, &frame.inFlight);
    vkResetCommandBuffer(frame.commandBuffer, 0);

    // 把当前 swapchain image 重新导入到 RenderGraph
    const auto& images = swapChain->getImages();
    const auto& views  = swapChain->getImageViews();
    if (hSwapchain != kInvalidRGTextureHandle &&
        imageIndex < images.size() && imageIndex < views.size()) {
        renderGraph.UpdateImportedTexture(hSwapchain, images[imageIndex],
                                          views[imageIndex]);
    }

    // 构造本帧 ctx，先让 renderer 有机会做每帧准备工作（默认 no-op）
    FrameContext ctx = makeFrameContext(currentFrame);
    ctx.guiRender    = std::move(guiRender);
    renderer->onFrame(ctx);

    // 录制命令
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if (vkBeginCommandBuffer(frame.commandBuffer, &beginInfo) != VK_SUCCESS) {
        throw std::runtime_error("failed to begin recording command buffer!");
    }

    auto bufferBindings = descManager.getBufferBindings();
    if (!bufferBindings.empty()) {
        std::vector<VkDescriptorBufferBindingInfoEXT> bindingInfos;
        bindingInfos.reserve(bufferBindings.size());
        for (const auto& b : bufferBindings) {
            VkDescriptorBufferBindingInfoEXT info{};
            info.sType   = VK_STRUCTURE_TYPE_DESCRIPTOR_BUFFER_BINDING_INFO_EXT;
            info.address = b.address;
            info.usage   = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT;
            bindingInfos.push_back(info);
        }
        ext::vkCmdBindDescriptorBuffersEXT(frame.commandBuffer,
            static_cast<uint32_t>(bindingInfos.size()), bindingInfos.data());
    }

    renderGraph.Execute(frame.commandBuffer, ctx);

    if (vkEndCommandBuffer(frame.commandBuffer) != VK_SUCCESS) {
        throw std::runtime_error("failed to record command buffer!");
    }

    // 提交
    VkSemaphore waitSemaphores[]   = {frame.imageAvailable};
    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};

    VkSubmitInfo submitInfo{};
    submitInfo.sType                = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount   = 1;
    submitInfo.pWaitSemaphores      = waitSemaphores;
    submitInfo.pWaitDstStageMask    = waitStages;
    submitInfo.commandBufferCount   = 1;
    submitInfo.pCommandBuffers      = &frame.commandBuffer;

    VkSemaphore signalSemaphores[]  = {imageRenderFinished[imageIndex]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores    = signalSemaphores;

    if (vkQueueSubmit(context->graphicsQueue, 1, &submitInfo,
                      frame.inFlight) != VK_SUCCESS) {
        throw std::runtime_error("failed to submit draw command buffer!");
    }

    bool presentOk = swapChain->present(imageIndex,
                                        imageRenderFinished[imageIndex]);

    // PerFrame import 的 layout 跟踪由 RenderGraph 在 Execute 末尾自动重置，
    // 此处不再需要外部调用 ResetResourceState。

    currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    return presentOk;
}

void RenderModule::recreateSwapChain() {
    VkDevice device = context->device;
    vkDeviceWaitIdle(device);

    for (auto sem : imageRenderFinished) {
        vkDestroySemaphore(device, sem, nullptr);
    }
    imageRenderFinished.clear();

    if (swapChain) swapChain->recreate();

    uint32_t imageCount = swapChain ? swapChain->getImageCount() : 3;
    imageRenderFinished.resize(imageCount);

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    for (uint32_t i = 0; i < imageCount; i++) {
        if (vkCreateSemaphore(device, &semaphoreInfo, nullptr,
                              &imageRenderFinished[i]) != VK_SUCCESS) {
            throw std::runtime_error(
                "failed to recreate image render-finished semaphore!");
        }
    }

    rebuildRenderGraph();
}

// ------------------------------------------------------------------
// Getters
// ------------------------------------------------------------------

uint32_t RenderModule::getImageCount() const {
    return swapChain ? swapChain->getImageCount() : 0;
}

VkFormat RenderModule::getSwapChainFormat() const {
    return swapChain ? swapChain->getFormat() : VK_FORMAT_UNDEFINED;
}

VkSampleCountFlagBits RenderModule::getMsaaSamples() const {
    return context ? context->msaaSamples : VK_SAMPLE_COUNT_1_BIT;
}

// ------------------------------------------------------------------
// Internal
// ------------------------------------------------------------------

void RenderModule::createFrameSetLayout() {
    VkDescriptorSetLayoutBinding frameBinding{};
    frameBinding.binding         = 0;
    frameBinding.descriptorCount = 1;
    frameBinding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    frameBinding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT |
                                   VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo info{};
    info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    info.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    info.bindingCount = 1;
    info.pBindings    = &frameBinding;

    if (vkCreateDescriptorSetLayout(context->device, &info, nullptr,
                                    &frameSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("failed to create frame descriptor set layout!");
    }
}

void RenderModule::createFrameResources() {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        frames[i].frameSet = descManager.allocate(frameLayoutId);

        VkDeviceSize bufferSize = sizeof(FrameUBO);
        createBufferVMA(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                        VMA_MEMORY_USAGE_CPU_TO_GPU,
                        frames[i].uboBuffer, frames[i].uboAllocation);

        vmaMapMemory(g_vmaAllocator, frames[i].uboAllocation,
                     &frames[i].uboMapped);

        VkDescriptorBufferInfo bufferInfo{};
        bufferInfo.buffer = frames[i].uboBuffer;
        bufferInfo.offset = 0;
        bufferInfo.range  = bufferSize;

        descManager.writeBuffer(frameLayoutId, frames[i].frameSet, 0, bufferInfo);
    }
}

void RenderModule::createSyncObjects() {
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        if (vkCreateSemaphore(context->device, &semaphoreInfo, nullptr,
                              &frames[i].imageAvailable) != VK_SUCCESS ||
            vkCreateFence(context->device, &fenceInfo, nullptr,
                          &frames[i].inFlight) != VK_SUCCESS) {
            throw std::runtime_error(
                "failed to create synchronization objects!");
        }
    }

    uint32_t imageCount = swapChain ? swapChain->getImageCount() : 3;
    imageRenderFinished.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; i++) {
        if (vkCreateSemaphore(context->device, &semaphoreInfo, nullptr,
                              &imageRenderFinished[i]) != VK_SUCCESS) {
            throw std::runtime_error(
                "failed to create image render-finished semaphore!");
        }
    }
}

void RenderModule::createCommandBuffers() {
    for (int i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool        = context->commandPool;
        allocInfo.level              = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(context->device, &allocInfo,
                                     &frames[i].commandBuffer) != VK_SUCCESS) {
            throw std::runtime_error("failed to allocate command buffers!");
        }
    }
}

void RenderModule::importSwapchainResource() {
    RGTextureDesc desc{};
    desc.width  = swapChain->getExtent().width;
    desc.height = swapChain->getExtent().height;
    desc.format = swapChain->getFormat();
    desc.usage  = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    const auto& images = swapChain->getImages();
    const auto& views  = swapChain->getImageViews();
    if (!images.empty() && !views.empty()) {
        // swapchain image 每帧从 UNDEFINED 重新过渡，使用 PerFrame 策略让
        // RenderGraph 自动在 Execute 末尾重置 layout 跟踪。
        hSwapchain = renderGraph.ImportTexture("SwapChain", images[0],
                                               views[0], desc,
                                               RGImportPolicy::PerFrame);
    }
}

void RenderModule::rebuildRenderGraph() {
    // RenderGraph::Cleanup() 末尾会把它持有的 context 置空，所以重建前必须
    // 再 Init 一次，否则 Compile 阶段 AllocatePhysicalResources 会解引用 nullptr。
    renderGraph.Cleanup();
    renderGraph.Init(context);
    hSwapchain = kInvalidRGTextureHandle;
    importSwapchainResource();

    FrameContext ctx = makeFrameContext(0);
    renderer->buildRenderGraph(renderGraph, ctx);

    renderGraph.Compile();
}

void RenderModule::updateFrameUBO(uint32_t frameIndex) {
    if (!renderer || !scene) return;
    FrameContext ctx = makeFrameContext(frameIndex);
    renderer->writeFrameUBO(frames[frameIndex].uboMapped, ctx);
}

FrameContext RenderModule::makeFrameContext(uint32_t frameIndex) {
    FrameContext ctx{};
    ctx.vkContext        = context;
    ctx.descManager      = &descManager;
    ctx.variantManager   = &shaderVariantManager;
    ctx.psoManager       = &psoManager;
    ctx.frameSetLayout   = frameSetLayout;
    ctx.swapchainFormat  = getSwapChainFormat();
    ctx.renderExtent     = swapChain ? swapChain->getExtent() : VkExtent2D{0, 0};
    ctx.hSwapchain       = hSwapchain;
    ctx.scene            = scene;
    ctx.frameIndex       = frameIndex;
    ctx.frameSet         = frames[frameIndex].frameSet;
    return ctx;
}

} // namespace engine
