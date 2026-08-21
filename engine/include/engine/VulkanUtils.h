#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <vector>

namespace engine {

// ------------------------------------------------------------------
// VMA global allocator
// ------------------------------------------------------------------

extern VmaAllocator g_vmaAllocator;

void initVMA(VkInstance instance, VkPhysicalDevice physicalDevice,
             VkDevice device);
void cleanupVMA();

// ------------------------------------------------------------------
// Command buffer & layout helpers
// ------------------------------------------------------------------

VkCommandBuffer beginSingleTimeCommands(VkDevice device,
                                        VkCommandPool commandPool);
void endSingleTimeCommands(VkDevice device, VkCommandPool commandPool,
                           VkQueue graphicsQueue,
                           VkCommandBuffer commandBuffer);

void copyBuffer(VkDevice device, VkCommandPool commandPool,
                VkQueue graphicsQueue, VkBuffer srcBuffer, VkBuffer dstBuffer,
                VkDeviceSize size);

VkImageView createImageView(VkDevice device, VkImage image, VkFormat format,
                            VkImageAspectFlags aspectFlags,
                            uint32_t mipLevels,
                            uint32_t arrayLayers = 1,
                            VkImageViewType viewType = VK_IMAGE_VIEW_TYPE_MAX_ENUM);

void transitionImageLayout(VkDevice device, VkCommandPool commandPool,
                           VkQueue graphicsQueue, VkImage image,
                           VkFormat format, VkImageLayout oldLayout,
                           VkImageLayout newLayout, uint32_t mipLevels);

void copyBufferToImage(VkDevice device, VkCommandPool commandPool,
                       VkQueue graphicsQueue, VkBuffer buffer, VkImage image,
                       uint32_t width, uint32_t height);

void generateMipmaps(VkDevice device, VkPhysicalDevice physicalDevice,
                     VkCommandPool commandPool, VkQueue graphicsQueue,
                     VkImage image, VkFormat imageFormat, int32_t texWidth,
                     int32_t texHeight, uint32_t mipLevels);

VkFormat findSupportedFormat(VkPhysicalDevice physicalDevice,
                             const std::vector<VkFormat>& candidates,
                             VkImageTiling tiling,
                             VkFormatFeatureFlags features);

bool hasStencilComponent(VkFormat format);

// ------------------------------------------------------------------
// VMA-based helpers (used by scene resources)
// ------------------------------------------------------------------

void createBufferVMA(VkDeviceSize size, VkBufferUsageFlags usage,
                     VmaMemoryUsage memoryUsage, VkBuffer& buffer,
                     VmaAllocation& allocation);

void createImageVMA(uint32_t width, uint32_t height, uint32_t arrayLayers,
                    uint32_t mipLevels, VkSampleCountFlagBits numSamples,
                    VkFormat format, VkImageTiling tiling,
                    VkImageUsageFlags usage, VmaMemoryUsage memoryUsage,
                    VkImage& image, VmaAllocation& allocation,
                    VkImageCreateFlags flags = 0);

} // namespace engine
