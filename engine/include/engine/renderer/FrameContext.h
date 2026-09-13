#pragma once

#include "engine/descriptor/DescriptorSetManager.h"
#include "engine/renderer/rendergraph/RGResource.h"

#include <vulkan/vulkan.h>
#include <functional>

namespace engine {

class VulkanContext;
class Scene;
class ShaderVariantManager;

/**
 * FrameContext -- RenderModule 与 IRenderer 之间的统一数据通道。
 *
 * 用途：
 *   1. init 阶段：pipeline 通过 ctx 拿到引擎级服务
 *      （VulkanContext、DescriptorSetManager、ShaderVariantManager、
 *        frameSetLayout 等）。
 *   2. onFrame / Execute 阶段：提供每帧可变数据（frameIndex、frameSet、
 *      scene、guiRender），由 RenderModule 在 drawFrame 内填好。
 *
 * init 时每帧字段可能为占位值，pipeline 不应依赖它们；引擎级服务字段
 * 保证有效。RenderGraph 构建使用独立的 RenderGraphBuildContext。
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

    // ---- 全局共享资源（init 起即有效） ----
    VkDescriptorSetLayout frameSetLayout = VK_NULL_HANDLE;
    VkExtent2D            renderExtent   = {0, 0};

    // ---- 每帧字段（仅 onFrame 阶段有意义） ----
    Scene*                scene             = nullptr;
    uint32_t              frameIndex        = 0;
    float                 timeSeconds       = 0.0f;
    DescriptorSetHandle   frameSet          = {};
    GuiRenderFn           guiRender;
};

} // namespace engine
