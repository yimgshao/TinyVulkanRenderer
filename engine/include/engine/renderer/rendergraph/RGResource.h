#pragma once

#include <vulkan/vulkan.h>
#include <string>
#include <cstdint>

namespace engine {

using RGTextureHandle = uint32_t;
inline constexpr RGTextureHandle kInvalidRGTextureHandle = 0;

struct RGTextureDesc {
    uint32_t width       = 0;
    uint32_t height      = 0;
    uint32_t arrayLayers = 1;
    uint32_t mipLevels   = 1;
    VkFormat format      = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits sampleCount = VK_SAMPLE_COUNT_1_BIT;
    VkImageUsageFlags usage = 0;
};

struct AttachmentDesc {
    VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    VkClearValue clearValue = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
};

/**
 * Imported 资源的生命周期策略。
 *   Persistent : 跨帧保留 layout 跟踪（适用于 shadow atlas、IBL LUT 等持久导入资源）
 *   PerFrame   : Execute 完成后由 RenderGraph 自动把 layout 跟踪重置为 UNDEFINED
 *                （适用于 swapchain image，每帧都从 UNDEFINED 重新过渡）
 */
enum class RGImportPolicy {
    Persistent,
    PerFrame,
};

} // namespace engine
