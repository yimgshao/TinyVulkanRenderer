#include "engine/descriptor/DescriptorSetWriter.h"

#include <cstring>
#include <stdexcept>

namespace engine {

void DescriptorSetWriter::init(VkDevice dev, VkPhysicalDevice phys,
                               VkDescriptorSetLayout layout) {
    device = dev;
    physicalDevice = phys;
    layout_ = layout;

    pfnGetDescriptorEXT = (PFN_vkGetDescriptorEXT)vkGetDeviceProcAddr(
        device, "vkGetDescriptorEXT");
    pfnGetDescriptorSetLayoutBindingOffsetEXT =
        (PFN_vkGetDescriptorSetLayoutBindingOffsetEXT)vkGetDeviceProcAddr(
            device, "vkGetDescriptorSetLayoutBindingOffsetEXT");

    if (!pfnGetDescriptorEXT || !pfnGetDescriptorSetLayoutBindingOffsetEXT) {
        throw std::runtime_error(
            "VK_EXT_descriptor_buffer function pointers not available");
    }

    // Cache descriptor sizes.
    VkPhysicalDeviceDescriptorBufferPropertiesEXT dbProps{};
    dbProps.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_BUFFER_PROPERTIES_EXT;
    VkPhysicalDeviceProperties2 props2{};
    props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    props2.pNext = &dbProps;
    vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

    uniformBufferDescriptorSize = dbProps.uniformBufferDescriptorSize;
    combinedImageSamplerDescriptorSize =
        dbProps.combinedImageSamplerDescriptorSize;
    sampledImageDescriptorSize = dbProps.sampledImageDescriptorSize;
    samplerDescriptorSize = dbProps.samplerDescriptorSize;

    // Query total set size.
    auto pfnGetSize = (PFN_vkGetDescriptorSetLayoutSizeEXT)vkGetDeviceProcAddr(
        device, "vkGetDescriptorSetLayoutSizeEXT");
    VkDeviceSize rawSize = 0;
    pfnGetSize(device, layout, &rawSize);
    setSize = rawSize;
}

void DescriptorSetWriter::writeBuffer(uint64_t setOffset,
                                      DescriptorBufferHeap* heap,
                                      uint32_t binding,
                                      const VkDescriptorBufferInfo& info) {
    VkDeviceSize bindingOffset = 0;
    pfnGetDescriptorSetLayoutBindingOffsetEXT(device, layout_, binding,
                                               &bindingOffset);

    // VK_EXT_descriptor_buffer requires VkDescriptorAddressInfoEXT
    // instead of VkDescriptorBufferInfo for buffer descriptors.
    VkBufferDeviceAddressInfo addrInfo{};
    addrInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addrInfo.buffer = info.buffer;
    VkDeviceAddress bufferAddr = vkGetBufferDeviceAddress(device, &addrInfo);

    VkDescriptorAddressInfoEXT addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_ADDRESS_INFO_EXT;
    addressInfo.address = bufferAddr + info.offset;
    addressInfo.range = info.range;
    addressInfo.format = VK_FORMAT_UNDEFINED;

    VkDescriptorGetInfoEXT getInfo{};
    getInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    getInfo.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    getInfo.data.pUniformBuffer = &addressInfo;

    VkDeviceSize descSize = uniformBufferDescriptorSize;
    char* dst = static_cast<char*>(heap->getMappedPtr()) + setOffset +
                bindingOffset;
    pfnGetDescriptorEXT(device, &getInfo, static_cast<size_t>(descSize), dst);
}

void DescriptorSetWriter::writeImage(uint64_t setOffset,
                                     DescriptorBufferHeap* heap,
                                     uint32_t binding,
                                     const VkDescriptorImageInfo& info,
                                     VkDescriptorType type) {
    VkDeviceSize bindingOffset = 0;
    pfnGetDescriptorSetLayoutBindingOffsetEXT(device, layout_, binding,
                                               &bindingOffset);

    VkDescriptorGetInfoEXT getInfo{};
    getInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT;
    getInfo.type = type;

    VkDeviceSize descSize = 0;
    switch (type) {
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            getInfo.data.pCombinedImageSampler = &info;
            descSize = combinedImageSamplerDescriptorSize;
            break;
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
            getInfo.data.pSampledImage = &info;
            descSize = sampledImageDescriptorSize;
            break;
        case VK_DESCRIPTOR_TYPE_SAMPLER:
            getInfo.data.pSampler = &info.sampler;
            descSize = samplerDescriptorSize;
            break;
        default:
            throw std::runtime_error(
                "DescriptorSetWriter::writeImage: unsupported descriptor type");
    }

    char* dst = static_cast<char*>(heap->getMappedPtr()) + setOffset +
                bindingOffset;
    pfnGetDescriptorEXT(device, &getInfo, static_cast<size_t>(descSize), dst);
}

} // namespace engine
