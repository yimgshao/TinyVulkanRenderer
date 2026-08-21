#include "engine/descriptor/DescriptorBufferHeap.h"
#include "engine/VulkanUtils.h"

#include <stdexcept>
#include <string>

namespace engine {

void DescriptorBufferHeap::init(VkDevice dev, VkPhysicalDevice phys,
                                VkDescriptorSetLayout layout, uint32_t capacity,
                                uint32_t bufIdx) {
    device = dev;
    physicalDevice = phys;
    maxSets = capacity;
    bufferIndex = bufIdx;

    pfnGetDescriptorSetLayoutSizeEXT =
        (PFN_vkGetDescriptorSetLayoutSizeEXT)vkGetDeviceProcAddr(
            device, "vkGetDescriptorSetLayoutSizeEXT");
    if (!pfnGetDescriptorSetLayoutSizeEXT) {
        throw std::runtime_error(
            "vkGetDescriptorSetLayoutSizeEXT not available");
    }

    // Query the raw size required for one descriptor set of this layout.
    VkDeviceSize layoutSize = 0;
    pfnGetDescriptorSetLayoutSizeEXT(device, layout, &layoutSize);

    // Query alignment requirement from descriptor-buffer properties.
    VkPhysicalDeviceDescriptorBufferPropertiesEXT dbProps{};
    dbProps.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;
    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &dbProps;
    vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

    setAlignment = dbProps.descriptorBufferOffsetAlignment;
    if (setAlignment == 0) setAlignment = 1;

    // Each slot is rounded up to the required alignment.
    setSize = ((layoutSize + setAlignment - 1) / setAlignment) * setAlignment;

    // Create the descriptor buffer.
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = setSize * maxSets;
    bufferInfo.usage = VK_BUFFER_USAGE_RESOURCE_DESCRIPTOR_BUFFER_BIT_EXT |
                       VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_CPU_TO_GPU;

    if (vmaCreateBuffer(g_vmaAllocator, &bufferInfo, &allocInfo, &buffer,
                        &allocation, nullptr) != VK_SUCCESS) {
        throw std::runtime_error("failed to create descriptor buffer!");
    }

    vmaMapMemory(g_vmaAllocator, allocation, &mappedPtr);
}

void DescriptorBufferHeap::cleanup() {
    if (mappedPtr) {
        vmaUnmapMemory(g_vmaAllocator, allocation);
        mappedPtr = nullptr;
    }
    if (buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(g_vmaAllocator, buffer, allocation);
        buffer = VK_NULL_HANDLE;
        allocation = VK_NULL_HANDLE;
    }
    freeOffsets.clear();
    nextOffset = 0;
    device = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
}

uint64_t DescriptorBufferHeap::allocate() {
    if (!freeOffsets.empty()) {
        uint64_t offset = freeOffsets.back();
        freeOffsets.pop_back();
        return offset;
    }
    if (nextOffset + setSize > setSize * maxSets) {
        throw std::runtime_error(
            "DescriptorBufferHeap out of capacity!");
    }
    uint64_t offset = nextOffset;
    nextOffset += setSize;
    return offset;
}

void DescriptorBufferHeap::free(uint64_t offset) {
    freeOffsets.push_back(offset);
}

VkDeviceAddress DescriptorBufferHeap::getDeviceAddress() const {
    VkBufferDeviceAddressInfo addrInfo{};
    addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addrInfo.buffer = buffer;
    return vkGetBufferDeviceAddress(device, &addrInfo);
}

} // namespace engine
