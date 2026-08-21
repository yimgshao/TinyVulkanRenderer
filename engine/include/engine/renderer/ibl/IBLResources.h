#pragma once

#include "engine/descriptor/DescriptorSetManager.h"

#include <vulkan/vulkan.h>

namespace engine {

class IBLTextures;

/**
 * IBLResources -- IBL 描述符集装配（deferred / forward 共享，set 3）。
 *
 * 布局与 engine/shaders/common/ibl.hlsl 的 vk::binding 一一对应：
 *   binding 0: irradiance  (TextureCube,  SAMPLED_IMAGE)
 *   binding 1: prefilter   (TextureCube,  SAMPLED_IMAGE)
 *   binding 2: envCube     (TextureCube,  SAMPLED_IMAGE)
 *   binding 3: brdfLUT     (Texture2D,    SAMPLED_IMAGE)
 *   binding 4: sampler     (SAMPLER)
 *
 * IBL 纹理是引擎自有持久资源（常驻 SHADER_READ_ONLY_OPTIMAL，
 * 不进 RenderGraph），描述符在 setup 时一次写好，运行期不重写。
 */
struct IBLResources {
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    LayoutId              layoutId  = kInvalidLayoutId;
    DescriptorSetHandle   set       = DescriptorSetHandle::invalid();
};

/// 创建 set layout + registerLayout/allocate + 一次性写全部描述符。
/// 纹理与 sampler 取自 IBLTextures（真实或 fallback 均可）。
void setupIBLResources(VkDevice device, DescriptorSetManager* descManager,
                       const IBLTextures& textures, IBLResources& out);

void cleanupIBLResources(VkDevice device, DescriptorSetManager* descManager,
                         IBLResources& res);

} // namespace engine
