#pragma once

#include "engine/renderer/PipelineStateDesc.h"
#include "engine/shader/ShaderParam.h"
#include "engine/shader/ShaderVariantManager.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <string>

namespace engine {

/**
 * GraphicsPSODesc -- 图形 PSO 的完整纯值描述。
 *
 * 对应 UE 的 FGraphicsPipelineStateInitializer：任何调用方（材质模板、
 * 光照 pass、阴影 pass ...）都可以构造它交给 PsoManager 查询/创建，
 * PSO 对象的生命周期归 PsoManager，调用方只持有查询结果。
 *
 * 职责划分：
 *   - shader 变体相关字段由「知道自己在画什么」的一方填写
 *     （材质模板填材质参数，pass 填 pass 参数与模块配置）；
 *   - 渲染状态 / attachment 格式由「知道渲染目标」的一方（pass）填写；
 *   - PsoManager 不解释任何字段的语义，只忠实组装。
 */
struct GraphicsPSODesc {
    static constexpr uint32_t kMaxColorAttachments = 8;

    // ---- shader 变体 ----
    ShaderModuleConfig shaderConfig;
    ShaderVariantKey   variantKey;
    ShaderParamSet     materialParams;
    ShaderParamSet     passParams;
    std::string        materialHeader;   // 可为空 → 非材质 pass 独立编译

    // ---- 顶点输入 ----
    /// VertexLayoutRegistry 中的布局名；空字符串 = 无顶点输入（全屏 pass）。
    std::string        vertexLayoutName = "StaticMesh";

    // ---- 固定功能状态 + dynamic rendering ----
    PipelineStateDesc  state;
    uint32_t           colorCount = 0;
    VkFormat           colorFormats[kMaxColorAttachments] = {};
    VkFormat           depthFormat    = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineLayout   layout = VK_NULL_HANDLE;

    // ---- 缓存键维度（不参与 PSO 创建） ----
    std::string        passName;         // 仅用于区分同模块不同 pass 的缓存
};

/// PSO 缓存键：desc 的纯值投影（指针/句柄字段除外）。
struct GraphicsPSOKey {
    static constexpr uint32_t kMaxColorAttachments = GraphicsPSODesc::kMaxColorAttachments;

    std::string           passName;
    std::string           moduleName;
    std::string           vertexLayout;
    std::string           materialType;
    uint64_t              materialParamHash = 0;
    uint64_t              passParamHash     = 0;
    PipelineStateDesc     state;
    VkFormat              depthFormat = VK_FORMAT_UNDEFINED;
    VkSampleCountFlagBits msaaSamples = VK_SAMPLE_COUNT_1_BIT;
    uint32_t              colorCount  = 0;
    VkFormat              colorFormats[kMaxColorAttachments] = {};

    bool operator==(const GraphicsPSOKey& o) const noexcept {
        if (materialType      != o.materialType)      return false;
        if (materialParamHash != o.materialParamHash) return false;
        if (passParamHash     != o.passParamHash)     return false;
        if (!(state == o.state)) return false;
        if (depthFormat != o.depthFormat) return false;
        if (msaaSamples != o.msaaSamples) return false;
        if (colorCount  != o.colorCount)  return false;
        if (passName    != o.passName)    return false;
        if (moduleName  != o.moduleName)  return false;
        if (vertexLayout != o.vertexLayout) return false;
        for (uint32_t i = 0; i < colorCount; ++i) {
            if (colorFormats[i] != o.colorFormats[i]) return false;
        }
        return true;
    }
};

struct GraphicsPSOKeyHash {
    size_t operator()(const GraphicsPSOKey& k) const noexcept {
        auto mix = [](size_t& h, size_t v) {
            h ^= v + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        };
        size_t h = std::hash<std::string>{}(k.passName);
        mix(h, std::hash<std::string>{}(k.moduleName));
        mix(h, std::hash<std::string>{}(k.vertexLayout));
        mix(h, std::hash<std::string>{}(k.materialType));
        // 整数字段直接取值混合（主流实现的整数 hash 即恒等）
        mix(h, static_cast<size_t>(k.materialParamHash));
        mix(h, static_cast<size_t>(k.passParamHash));
        mix(h, std::hash<PipelineStateDesc>{}(k.state));
        mix(h, static_cast<size_t>(k.depthFormat));
        mix(h, static_cast<size_t>(k.msaaSamples));
        mix(h, static_cast<size_t>(k.colorCount));
        for (uint32_t i = 0; i < k.colorCount; ++i) {
            mix(h, static_cast<size_t>(k.colorFormats[i]));
        }
        return h;
    }
};

} // namespace engine
