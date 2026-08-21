#pragma once

#include <vulkan/vulkan.h>
#include <cstddef>
#include <functional>  // std::hash（下方显式特化所需）

namespace engine {

/**
 * PipelineStateDesc — per-pass pipeline 渲染状态。
 *
 * 与 MaterialTemplate::getOrCreatePipeline 配合使用：
 * - 作为 GraphicsPSOKey（PsoManager 缓存键）的一部分，区分不同 pass 的 PSO 变体
 * - 默认值匹配经典前向渲染（背面剔除、逆时针正面、深度写入）
 * - 延迟管线 GBuffer pass 通常用 Default()；Lighting pass 用 DisableDepthWrite()
 */
struct PipelineStateDesc {
    VkCullModeFlags cullMode          = VK_CULL_MODE_BACK_BIT;
    VkFrontFace     frontFace         = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    VkPolygonMode   polygonMode       = VK_POLYGON_MODE_FILL;
    VkBool32        depthTestEnable   = VK_TRUE;
    VkBool32        depthWriteEnable  = VK_TRUE;
    VkCompareOp     depthCompareOp    = VK_COMPARE_OP_LESS;
    VkBool32        blendEnable       = VK_FALSE;
    /// 开启后 pass 可用 vkCmdSetDepthBias（动态状态）设置深度偏移，阴影深度 pass 用。
    VkBool32        depthBiasEnable   = VK_FALSE;

    /// 经典前向渲染默认值
    static PipelineStateDesc Default() { return {}; }

    /// 适合延迟光照 pass：关闭深度写入，保留深度测试（等于）
    static PipelineStateDesc DeferredLighting() {
        PipelineStateDesc s;
        s.depthWriteEnable = VK_FALSE;
        s.depthCompareOp   = VK_COMPARE_OP_EQUAL;
        return s;
    }

    bool operator==(const PipelineStateDesc& o) const noexcept {
        return cullMode         == o.cullMode
            && frontFace        == o.frontFace
            && polygonMode      == o.polygonMode
            && depthTestEnable  == o.depthTestEnable
            && depthWriteEnable == o.depthWriteEnable
            && depthCompareOp   == o.depthCompareOp
            && blendEnable      == o.blendEnable
            && depthBiasEnable  == o.depthBiasEnable;
    }

    bool operator!=(const PipelineStateDesc& o) const noexcept { return !(*this == o); }
};

} // namespace engine

namespace std {
template<>
struct hash<engine::PipelineStateDesc> {
    size_t operator()(const engine::PipelineStateDesc& s) const noexcept {
        size_t h = 0;
        auto mix = [&h](size_t v) {
            h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        };
        mix(static_cast<size_t>(s.cullMode));
        mix(static_cast<size_t>(s.frontFace));
        mix(static_cast<size_t>(s.polygonMode));
        mix(static_cast<size_t>(s.depthTestEnable));
        mix(static_cast<size_t>(s.depthWriteEnable));
        mix(static_cast<size_t>(s.depthCompareOp));
        mix(static_cast<size_t>(s.blendEnable));
        mix(static_cast<size_t>(s.depthBiasEnable));
        return h;
    }
};
} // namespace std
