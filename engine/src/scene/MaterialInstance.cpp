#include "engine/scene/MaterialInstance.h"
#include "engine/shader/ShaderVariantManager.h"
#include "engine/VulkanUtils.h"

#include <cstring>
#include <vector>

namespace engine {

void MaterialInstance::init(MaterialTemplate* tmpl) {
    template_ = tmpl;

    descriptorSet = template_->getDescManager()->allocate(
        template_->getMaterialLayoutId());

    VkDeviceSize bufferSize = sizeof(MaterialParams);
    createBufferVMA(bufferSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                    VMA_MEMORY_USAGE_CPU_TO_GPU, uboBuffer, uboAllocation);

    vmaMapMemory(g_vmaAllocator, uboAllocation, &uboMapped);
}

void MaterialInstance::cleanup(VkDevice /*device*/) {
    if (uboMapped) {
        vmaUnmapMemory(g_vmaAllocator, uboAllocation);
        uboMapped = nullptr;
    }
    if (uboBuffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(g_vmaAllocator, uboBuffer, uboAllocation);
        uboBuffer = VK_NULL_HANDLE;
    }

    if (template_ && descriptorSet.isValid()) {
        template_->getDescManager()->free(template_->getMaterialLayoutId(),
                                           descriptorSet);
    }
    descriptorSet = DescriptorSetHandle::invalid();
    template_ = nullptr;
}

void MaterialInstance::setParams(const MaterialParams& p) {
    params = p;
}

void MaterialInstance::writeDescriptorSet() {
    memcpy(uboMapped, &params, sizeof(MaterialParams));

    const ShaderReflection* refl = template_->getReflection();
    auto* mgr = template_->getDescManager();
    if (!refl) return;

    const LayoutId layoutId         = template_->getMaterialLayoutId();
    const uint32_t materialSetIndex = template_->getMaterialSetIndex();
    const DescriptorSetLayoutDesc* matSet = refl->findSet(materialSetIndex);
    if (!matSet) return;

    // 1) 找 material 隐式 UBO：set 内第一个 UNIFORM_BUFFER binding
    for (const auto& b : matSet->bindings) {
        if (b.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
            VkDescriptorBufferInfo bufferInfo{};
            bufferInfo.buffer = uboBuffer;
            bufferInfo.offset = 0;
            bufferInfo.range  = sizeof(MaterialParams);
            mgr->writeBuffer(layoutId, descriptorSet, b.binding, bufferInfo);
            break;
        }
    }

    // 2) 贴图按名字查 binding，名字与 HLSL shader 中的资源名一致
    auto writeIfBound = [&](std::string_view name, Texture* tex) {
        if (!tex) return;
        auto info = tex->descriptorInfo();
        if (info.imageView == VK_NULL_HANDLE) return;
        auto* b = refl->findBinding(materialSetIndex, name);
        if (b) {
            mgr->writeImage(layoutId, descriptorSet, b->binding, info);
        }
    };
    writeIfBound("baseColorMap", baseColor);
    writeIfBound("ormMap",       orm);
    writeIfBound("normalMap",    normal);
    writeIfBound("emissiveMap",  emissive);
}

} // namespace engine
