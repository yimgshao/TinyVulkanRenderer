/**
 * Single Vulkan image with mipmaps, view, and sampler.
 *
 * Represents one GPU texture (e.g. one albedo map, one normal map,
 * one metallic-roughness map).  Owns its VkImage, VkDeviceMemory,
 * VkImageView, and VkSampler.  Format is chosen by the caller at load
 * time so that normal maps can use UNORM while albedo uses sRGB.
 *
 * Usage example:
 * @code
 *   // 1. Load an albedo map
 *   engine::Texture albedo;
 *   albedo.load("albedo.png", dev, phys, pool, queue,
 *               VK_FORMAT_R8G8B8A8_SRGB);
 *
 *   // 2. Load a normal map
 *   engine::Texture normal;
 *   normal.load("normal.png", dev, phys, pool, queue,
 *               VK_FORMAT_R8G8B8A8_UNORM);
 *
 *   // 3. Query descriptor info for descriptor-set writes
 *   VkDescriptorImageInfo info = albedo.descriptorInfo();
 *
 *   // 4. Teardown
 *   albedo.cleanup(dev);
 * @endcode
 */

#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>

namespace engine {

class Texture {
public:
    void load(const std::string& filepath, VkDevice device,
              VkPhysicalDevice physicalDevice, VkCommandPool commandPool,
              VkQueue graphicsQueue,
              VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
    void cleanup(VkDevice device);

    VkImageView imageView() const { return textureImageView; }
    VkSampler sampler() const { return textureSampler; }
    VkDescriptorImageInfo descriptorInfo() const;

private:
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkImage textureImage = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView textureImageView = VK_NULL_HANDLE;
    VkSampler textureSampler = VK_NULL_HANDLE;
    uint32_t mipLevels = 1;

    void createSampler(VkDevice dev);
};

} // namespace engine
