#pragma once

#include "engine/descriptor/DescriptorBufferHeap.h"

#include <vulkan/vulkan.h>
#include <vector>

namespace engine {

/**
 * DescriptorSetWriter -- converts traditional VkWriteDescriptorSet operations
 * into raw descriptor bytes written directly into a DescriptorBufferHeap.
 *
 * Queries binding offsets on demand via vkGetDescriptorSetLayoutBindingOffsetEXT
 * so each write is just vkGetDescriptorEXT + memcpy.
 */
class DescriptorSetWriter {
public:
    DescriptorSetWriter() = default;
    ~DescriptorSetWriter() = default;

    DescriptorSetWriter(const DescriptorSetWriter&) = delete;
    DescriptorSetWriter& operator=(const DescriptorSetWriter&) = delete;

    void init(VkDevice device, VkPhysicalDevice physicalDevice,
              VkDescriptorSetLayout layout);

    // Write a buffer descriptor (e.g. UBO, SSBO) at the given set offset.
    void writeBuffer(uint64_t setOffset, DescriptorBufferHeap* heap,
                     uint32_t binding, const VkDescriptorBufferInfo& info);

    // Write an image descriptor at the given set offset.
    // type 必须与 set layout 中该 binding 声明的类型一致，
    // 支持 COMBINED_IMAGE_SAMPLER / SAMPLED_IMAGE / SAMPLER。
    void writeImage(uint64_t setOffset, DescriptorBufferHeap* heap,
                    uint32_t binding, const VkDescriptorImageInfo& info,
                    VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

private:
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDescriptorSetLayout layout_ = VK_NULL_HANDLE;
    VkDeviceSize setSize = 0;

    // Cached descriptor sizes from VkPhysicalDeviceDescriptorBufferPropertiesEXT
    VkDeviceSize uniformBufferDescriptorSize = 0;
    VkDeviceSize combinedImageSamplerDescriptorSize = 0;
    VkDeviceSize sampledImageDescriptorSize = 0;
    VkDeviceSize samplerDescriptorSize = 0;

    // Cached function pointers
    PFN_vkGetDescriptorEXT pfnGetDescriptorEXT = nullptr;
    PFN_vkGetDescriptorSetLayoutBindingOffsetEXT pfnGetDescriptorSetLayoutBindingOffsetEXT = nullptr;
};

} // namespace engine
