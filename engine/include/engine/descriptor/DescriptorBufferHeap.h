#pragma once

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#include <vector>

namespace engine {

/**
 * DescriptorBufferHeap -- manages a single GPU buffer that stores descriptor data
 * directly, backed by VK_EXT_descriptor_buffer.
 *
 * Each layout gets its own heap because descriptor set sizes vary by layout.
 * Allocation is a simple bump allocator + free-list over fixed-size slots.
 */
class DescriptorBufferHeap {
public:
    DescriptorBufferHeap() = default;
    ~DescriptorBufferHeap() = default;

    DescriptorBufferHeap(const DescriptorBufferHeap&) = delete;
    DescriptorBufferHeap& operator=(const DescriptorBufferHeap&) = delete;

    void init(VkDevice device, VkPhysicalDevice physicalDevice,
              VkDescriptorSetLayout layout, uint32_t maxSets, uint32_t bufferIndex);
    void cleanup();

    // Allocate a descriptor-set slot, returning the byte offset into the buffer.
    uint64_t allocate();
    void free(uint64_t offset);

    VkBuffer getBuffer() const { return buffer; }
    VkDeviceAddress getDeviceAddress() const;
    VkDeviceSize getSetSize() const { return setSize; }
    uint32_t getBufferIndex() const { return bufferIndex; }

    void* getMappedPtr() const { return mappedPtr; }

private:
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    void* mappedPtr = nullptr;

    VkDeviceSize setSize = 0;
    VkDeviceSize setAlignment = 0;
    uint32_t maxSets = 0;
    uint32_t bufferIndex = 0;

    std::vector<uint64_t> freeOffsets;
    uint64_t nextOffset = 0;

    // Cached function pointer
    PFN_vkGetDescriptorSetLayoutSizeEXT pfnGetDescriptorSetLayoutSizeEXT = nullptr;
};

} // namespace engine
