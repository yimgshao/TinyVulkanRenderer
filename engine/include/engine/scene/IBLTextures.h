#pragma once

#include "engine/VulkanContext.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>
#include <string>

namespace engine {

/**
 * IBLTextures -- .ibl 烘焙产物加载器（scripts/bake_ibl.py 输出）。
 *
 * 从产物目录加载 4 张图：irradiance / prefilter / env_cube（cubemap，
 * R16G16B16A16_SFLOAT）与 brdf_lut（2D，R16G16_SFLOAT）。
 * 全部纹理常驻 SHADER_READ_ONLY_OPTIMAL，描述符在装配后写一次即可，
 * 不进 RenderGraph。
 *
 * 无真实环境时 loadFallback() 生成 1x1 黑纹理占位，保证 IBL 描述符集
 * 恒可绑定（shader 侧由 USE_IBL 变体决定是否真正采样）。
 */
class IBLTextures {
public:
    /// 真实加载。任一文件缺失/格式不符 → 清理已分配资源并返回 false。
    bool load(const std::string& dir, VulkanContext* ctx);

    /// 1x1 黑纹理占位（irradiance/prefilter/envCube 共享一张黑 cube）。
    bool loadFallback(VulkanContext* ctx);

    void cleanup(VkDevice device);

    /// 真实环境已加载（fallback 不算）→ 对应 shader 变体 USE_IBL=1。
    bool hasReal() const { return hasReal_; }

    // 未加载真实环境时返回 fallback 占位视图，保证 set 恒可绑定
    VkImageView irradianceView() const {
        return hasReal_ ? irradiance_.view : fallbackCube_.view;
    }
    VkImageView prefilterView() const {
        return hasReal_ ? prefilter_.view : fallbackCube_.view;
    }
    VkImageView envCubeView() const {
        return hasReal_ ? envCube_.view : fallbackCube_.view;
    }
    VkImageView brdfLutView() const {
        return hasReal_ ? brdfLut_.view : fallbackLut_.view;
    }
    VkSampler   sampler()        const { return sampler_; }
    uint32_t    prefilterMipLevels() const {
        return hasReal_ ? prefilter_.mipLevels : 1;
    }

private:
    struct IBLImage {
        VkImage       image     = VK_NULL_HANDLE;
        VmaAllocation alloc     = VK_NULL_HANDLE;
        VkImageView   view      = VK_NULL_HANDLE;
        uint32_t      width     = 0;
        uint32_t      height    = 0;
        uint32_t      layers    = 0;
        uint32_t      mipLevels = 0;
        VkFormat      format    = VK_FORMAT_UNDEFINED;
        bool          cube      = false;
    };

    /// 解析 + 上传一张 .ibl。失败返回 false（资源由 cleanup 统一释放）。
    bool loadOne(const std::string& path, IBLImage& out, VulkanContext* ctx);
    /// 上传 payload（逐 mip 连续数据，每层 HxWxC float16 紧密排列）。
    bool upload(IBLImage& img, const void* payload, size_t payloadSize,
                VulkanContext* ctx);
    void createSampler(VkDevice device, uint32_t maxMipLevels);

    IBLImage  irradiance_;
    IBLImage  prefilter_;
    IBLImage  envCube_;
    IBLImage  brdfLut_;
    // fallback 占位资源（hasReal_=false 时上面 4 个 view 别名到这两张图）
    IBLImage  fallbackCube_;
    IBLImage  fallbackLut_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    bool      hasReal_ = false;
};

} // namespace engine
