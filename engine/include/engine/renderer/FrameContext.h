#pragma once

#include "engine/descriptor/DescriptorSetManager.h"
#include "engine/renderer/rendergraph/RGResource.h"

#include <vulkan/vulkan.h>
#include <functional>

namespace engine {

class VulkanContext;
class Scene;
class ShaderVariantManager;
class PsoManager;

/**
 * FrameContext -- RenderModule 与 IRenderer 之间的统一数据通道。
 *
 * 用途：
 *   1. init / buildRenderGraph 阶段：pipeline 通过 ctx 拿到引擎级服务
 *      （VulkanContext、DescriptorSetManager、ShaderVariantManager、
 *        PsoManager、frameSetLayout、swapchainFormat、renderExtent）。
 *   2. onFrame 阶段：附加每帧可变数据（frameIndex、frameSet、scene、
 *        hSwapchain、guiRender），由 RenderModule 在 drawFrame 内填好。
 *
 * 在 init / buildRenderGraph 时，"每帧字段" (frameIndex / frameSet /
 * hSwapchain / guiRender) 可能为占位值，pipeline 不应依赖它们；
 * 引擎级服务字段保证有效。
 *
 * `frameSet.bufferIndex` 已经携带 descriptor buffer 槽位号，pass 直接读
 * `ctx.frame.frameSet.bufferIndex` 就行；不需要再额外传 frameBufferIndex。
 */
struct FrameContext {
    using GuiRenderFn = std::function<void(VkCommandBuffer)>;

    // ---- 引擎级服务（init 起即有效） ----
    VulkanContext*        vkContext       = nullptr;
    DescriptorSetManager* descManager     = nullptr;
    ShaderVariantManager* variantManager  = nullptr;
    PsoManager*           psoManager      = nullptr;

    // ---- 全局共享资源（init 起即有效） ----
    VkDescriptorSetLayout frameSetLayout  = VK_NULL_HANDLE;
    VkFormat              swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D            renderExtent    = {0, 0};
    RGTextureHandle       hSwapchain      = kInvalidRGTextureHandle;

    // ---- 每帧字段（仅 onFrame 阶段有意义） ----
    Scene*                scene             = nullptr;
    uint32_t              frameIndex        = 0;
    DescriptorSetHandle   frameSet          = {};
    GuiRenderFn           guiRender;
};

} // namespace engine
