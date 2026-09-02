#pragma once

#include "engine/renderer/rendergraph/RGResource.h"

#include <vulkan/vulkan.h>

namespace engine {

/**
 * RenderGraphBuildContext -- RenderGraph 构建期上下文。
 *
 * 只包含 Setup 声明资源时需要的 swapchain 相关状态，避免把 scene、
 * frameSet、guiRender 等仅在逐帧执行阶段有效的数据暴露给 Setup。
 */
struct RenderGraphBuildContext {
    VkFormat        swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D      renderExtent    = {0, 0};
    RGTextureHandle hSwapchain      = kInvalidRGTextureHandle;
};

} // namespace engine
