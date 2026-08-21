#include "engine/descriptor/DescriptorSetManager.h"

#include <stdexcept>

namespace engine {
namespace ext {

PFN_vkGetDescriptorSetLayoutSizeEXT vkGetDescriptorSetLayoutSizeEXT = nullptr;
PFN_vkGetDescriptorSetLayoutBindingOffsetEXT
    vkGetDescriptorSetLayoutBindingOffsetEXT = nullptr;
PFN_vkGetDescriptorEXT vkGetDescriptorEXT = nullptr;
PFN_vkCmdBindDescriptorBuffersEXT vkCmdBindDescriptorBuffersEXT = nullptr;
PFN_vkCmdSetDescriptorBufferOffsetsEXT vkCmdSetDescriptorBufferOffsetsEXT =
    nullptr;

} // namespace ext

namespace {

inline uint32_t idx(LayoutId id) { return static_cast<uint32_t>(id); }

} // namespace

void DescriptorSetManager::init(VkDevice dev, VkPhysicalDevice phys) {
    device = dev;
    physicalDevice = phys;

    ext::vkGetDescriptorSetLayoutSizeEXT =
        (PFN_vkGetDescriptorSetLayoutSizeEXT)vkGetDeviceProcAddr(
            device, "vkGetDescriptorSetLayoutSizeEXT");
    ext::vkGetDescriptorSetLayoutBindingOffsetEXT =
        (PFN_vkGetDescriptorSetLayoutBindingOffsetEXT)vkGetDeviceProcAddr(
            device, "vkGetDescriptorSetLayoutBindingOffsetEXT");
    ext::vkGetDescriptorEXT = (PFN_vkGetDescriptorEXT)vkGetDeviceProcAddr(
        device, "vkGetDescriptorEXT");
    ext::vkCmdBindDescriptorBuffersEXT =
        (PFN_vkCmdBindDescriptorBuffersEXT)vkGetDeviceProcAddr(
            device, "vkCmdBindDescriptorBuffersEXT");
    ext::vkCmdSetDescriptorBufferOffsetsEXT =
        (PFN_vkCmdSetDescriptorBufferOffsetsEXT)vkGetDeviceProcAddr(
            device, "vkCmdSetDescriptorBufferOffsetsEXT");

    if (!ext::vkGetDescriptorSetLayoutSizeEXT ||
        !ext::vkGetDescriptorSetLayoutBindingOffsetEXT ||
        !ext::vkGetDescriptorEXT || !ext::vkCmdBindDescriptorBuffersEXT ||
        !ext::vkCmdSetDescriptorBufferOffsetsEXT) {
        throw std::runtime_error(
            "VK_EXT_descriptor_buffer function pointers not available");
    }
}

void DescriptorSetManager::cleanup() {
    for (auto& entry : layouts) {
        if (entry.heap)   entry.heap->cleanup();
        entry.writer.reset();
        entry.heap.reset();
    }
    layouts.clear();
    device = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
}

LayoutId DescriptorSetManager::registerLayout(VkDescriptorSetLayout layout,
                                              uint32_t maxSets) {
    const uint32_t id = static_cast<uint32_t>(layouts.size());

    LayoutEntry entry;
    entry.layout = layout;
    entry.heap   = std::make_unique<DescriptorBufferHeap>();
    entry.heap->init(device, physicalDevice, layout, maxSets, id);
    entry.writer = std::make_unique<DescriptorSetWriter>();
    entry.writer->init(device, physicalDevice, layout);

    layouts.push_back(std::move(entry));
    return LayoutId{id};
}

DescriptorSetHandle DescriptorSetManager::allocate(LayoutId id) {
    const uint32_t i = idx(id);
    if (i >= layouts.size()) {
        throw std::runtime_error("DescriptorSetManager::allocate: invalid LayoutId");
    }
    DescriptorSetHandle h;
    h.bufferIndex = layouts[i].heap->getBufferIndex();
    h.offset      = layouts[i].heap->allocate();
    return h;
}

void DescriptorSetManager::free(LayoutId id, DescriptorSetHandle handle) {
    const uint32_t i = idx(id);
    if (i >= layouts.size()) return;
    layouts[i].heap->free(handle.offset);
}

void DescriptorSetManager::writeBuffer(LayoutId id, DescriptorSetHandle handle,
                                       uint32_t binding,
                                       const VkDescriptorBufferInfo& info) {
    const uint32_t i = idx(id);
    if (i >= layouts.size()) {
        throw std::runtime_error("DescriptorSetManager::writeBuffer: invalid LayoutId");
    }
    layouts[i].writer->writeBuffer(handle.offset, layouts[i].heap.get(),
                                    binding, info);
}

void DescriptorSetManager::writeImage(LayoutId id, DescriptorSetHandle handle,
                                      uint32_t binding,
                                      const VkDescriptorImageInfo& info,
                                      VkDescriptorType type) {
    const uint32_t i = idx(id);
    if (i >= layouts.size()) {
        throw std::runtime_error("DescriptorSetManager::writeImage: invalid LayoutId");
    }
    layouts[i].writer->writeImage(handle.offset, layouts[i].heap.get(),
                                   binding, info, type);
}

uint32_t DescriptorSetManager::getBufferIndex(LayoutId id) const {
    const uint32_t i = idx(id);
    if (i >= layouts.size()) {
        throw std::runtime_error("DescriptorSetManager::getBufferIndex: invalid LayoutId");
    }
    return layouts[i].heap->getBufferIndex();
}

std::vector<DescriptorSetManager::BufferBindingInfo>
DescriptorSetManager::getBufferBindings() const {
    std::vector<BufferBindingInfo> result;
    result.reserve(layouts.size());
    // layouts 按 LayoutId 顺序存，结果数组下标 = bufferIndex，
    // 调用方可以直接 vkCmdBindDescriptorBuffersEXT 而不必再排序。
    for (const auto& entry : layouts) {
        result.push_back({entry.heap->getBuffer(),
                          entry.heap->getDeviceAddress()});
    }
    return result;
}

} // namespace engine
