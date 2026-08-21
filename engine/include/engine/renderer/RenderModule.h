#pragma once

#include "engine/VulkanContext.h"
#include "engine/SwapChain.h"
#include "engine/descriptor/DescriptorSetManager.h"
#include "engine/shader/ShaderVariantManager.h"
#include "engine/pso/PsoManager.h"
#include "engine/renderer/rendergraph/RenderGraph.h"
#include "engine/renderer/FrameContext.h"
#include "engine/renderer/IRenderer.h"
#include "engine/scene/FrameUBO.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

namespace engine {

class Scene;

/**
 * RenderModule -- 引擎渲染主循环骨架，引擎中唯一一个实例。
 *
 * 职责：
 *   - 拥有 swapchain、frame-in-flight 同步原语、命令缓冲、全局帧 UBO
 *   - 提供 DescriptorSetManager / ShaderVariantManager 作为引擎级服务
 *   - 拥有唯一的 RenderGraph 实例并驱动其 Execute
 *   - 持有当前 IRenderer，通过 setRenderer / buildGraph 驱动生命周期
 *
 * 不负责：
 *   - 具体的 pass 编排（由 IRenderer 决定）
 *   - 任何材质 / renderer / pass 特有的资源
 *
 * 典型使用：
 *   RenderModule module;
 *   module.init(&ctx, surface, getSize);
 *   module.setRenderer(std::make_unique<ForwardRenderer>());
 *   module.setScene(scene);
 *
 *   while (running) {
 *       module.drawFrame(guiCallback);
 *   }
 *
 *   module.cleanup();
 */
class RenderModule {
public:
    using FramebufferSizeFn = std::function<std::pair<int, int>()>;
    using GuiRenderFn       = FrameContext::GuiRenderFn;

    void init(VulkanContext* ctx, VkSurfaceKHR surface, FramebufferSizeFn getSize);
    void cleanup();

    /// 设置 renderer（拥有权转移）。
    /// 调用者必须在 setRenderer 之前完成 renderer->init() 和自定义 pass 添加。
    /// 最后调用 buildGraph() 编译渲染图。
    void setRenderer(std::unique_ptr<IRenderer> renderer);

    /// 创建一个可供 renderer init / addPass 使用的 FrameContext。
    /// 仅在 init() 之后、setRenderer() 之前调用。
    FrameContext createFrameContext(uint32_t frameIndex = 0) {
        return makeFrameContext(frameIndex);
    }

    /// 编译 RenderGraph（调用 renderer->buildRenderGraph + Compile）。
    /// 在 setRenderer 之后、首次 drawFrame 之前调用。
    void buildGraph();

    void setScene(Scene* s) { scene = s; }

    bool drawFrame(GuiRenderFn guiRender = nullptr);
    void recreateSwapChain();

    // ---- 给 GUI / 应用层使用 ----
    uint32_t              getImageCount()      const;
    VkFormat              getSwapChainFormat() const;
    VkSampleCountFlagBits getMsaaSamples()     const;

    // ---- 给场景加载 / renderer 外部接入使用 ----
    VulkanContext*        getVulkanContext()      const { return context; }
    DescriptorSetManager& getDescriptorManager()       { return descManager; }
    ShaderVariantManager& getShaderVariantManager()    { return shaderVariantManager; }
    PsoManager&           getPsoManager()              { return psoManager; }
    VkDescriptorSetLayout getFrameSetLayout()    const { return frameSetLayout; }
    IRenderer*      getRenderer()          const { return renderer.get(); }

private:
    static constexpr int MAX_FRAMES_IN_FLIGHT = 2;

    VulkanContext* context = nullptr;
    Scene*         scene   = nullptr;

    std::unique_ptr<SwapChain>        swapChain;
    std::unique_ptr<IRenderer>  renderer;

    // 引擎级服务
    DescriptorSetManager  descManager;
    ShaderVariantManager  shaderVariantManager;
    PsoManager            psoManager;
    VkDescriptorSetLayout frameSetLayout = VK_NULL_HANDLE;
    LayoutId              frameLayoutId  = kInvalidLayoutId;

    // 唯一 RenderGraph
    RenderGraph    renderGraph;
    RGTextureHandle hSwapchain = kInvalidRGTextureHandle;

    struct FrameData {
        VkBuffer            uboBuffer     = VK_NULL_HANDLE;
        VmaAllocation       uboAllocation = VK_NULL_HANDLE;
        void*               uboMapped     = nullptr;
        DescriptorSetHandle frameSet;
        VkSemaphore         imageAvailable = VK_NULL_HANDLE;
        VkFence             inFlight       = VK_NULL_HANDLE;
        VkCommandBuffer     commandBuffer  = VK_NULL_HANDLE;
    };
    std::array<FrameData, MAX_FRAMES_IN_FLIGHT> frames;
    uint32_t currentFrame = 0;

    // per-swapchain-image render-finished semaphores
    std::vector<VkSemaphore> imageRenderFinished;

    // ---- internal helpers ----
    void createFrameSetLayout();
    void createFrameResources();
    void createSyncObjects();
    void createCommandBuffers();

    void importSwapchainResource();
    void rebuildRenderGraph();

    void updateFrameUBO(uint32_t frameIndex);
    FrameContext makeFrameContext(uint32_t frameIndex);
};

} // namespace engine
