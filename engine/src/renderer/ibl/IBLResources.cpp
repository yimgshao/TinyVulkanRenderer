#include "engine/renderer/ibl/IBLResources.h"

#include "engine/scene/IBLTextures.h"

#include <array>
#include <stdexcept>

namespace engine {

void setupIBLResources(VkDevice device, DescriptorSetManager* descManager,
                       const IBLTextures& textures, IBLResources& out) {
    // 1. set layout：4x SAMPLED_IMAGE + 1x SAMPLER（FRAGMENT 阶段）
    std::array<VkDescriptorSetLayoutBinding, 5> bindings{};
    for (uint32_t i = 0; i < bindings.size(); ++i) {
        bindings[i].binding         = i;
        bindings[i].descriptorType  = (i < 4) ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                              : VK_DESCRIPTOR_TYPE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings    = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                    &out.setLayout) != VK_SUCCESS) {
        throw std::runtime_error("setupIBLResources: failed to create set layout.");
    }

    out.layoutId = descManager->registerLayout(out.setLayout, /*maxSets=*/1);
    out.set      = descManager->allocate(out.layoutId);

    // 2. 一次性写全部描述符（IBL 纹理持久，不每帧重写）
    const VkImageView views[4] = {
        textures.irradianceView(),
        textures.prefilterView(),
        textures.envCubeView(),
        textures.brdfLutView(),
    };
    for (uint32_t i = 0; i < 4; ++i) {
        VkDescriptorImageInfo info{};
        info.imageView   = views[i];
        info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        descManager->writeImage(out.layoutId, out.set, i, info,
                                VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE);
    }
    VkDescriptorImageInfo samplerInfo{};
    samplerInfo.sampler = textures.sampler();
    descManager->writeImage(out.layoutId, out.set, 4, samplerInfo,
                            VK_DESCRIPTOR_TYPE_SAMPLER);
}

void cleanupIBLResources(VkDevice device, DescriptorSetManager* descManager,
                         IBLResources& res) {
    if (res.set.isValid() && descManager) {
        descManager->free(res.layoutId, res.set);
        res.set = DescriptorSetHandle::invalid();
    }
    res.layoutId = kInvalidLayoutId;
    if (res.setLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, res.setLayout, nullptr);
        res.setLayout = VK_NULL_HANDLE;
    }
}

} // namespace engine
