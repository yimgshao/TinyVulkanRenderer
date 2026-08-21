#pragma once

#include "engine/pso/PSO.h"

#include <vulkan/vulkan.h>

#include <unordered_map>

namespace engine {

class ShaderVariantManager;

/**
 * PsoManager -- 引擎级图形 PSO 工厂 + 缓存。
 *
 * 对应 UE PipelineStateCache 的精简版：
 *   - 输入是纯描述结构 GraphicsPSODesc，任何调用方都可以查询/创建；
 *   - 缓存键为描述的纯值投影（GraphicsPSOKey），命中即返回；
 *   - 所有 VkPipeline 的生命周期归本管理器，cleanup 统一销毁。
 *
 * 与 ShaderVariantManager 的分工：
 *   ShaderVariantManager 负责「HLSL -> SPIR-V 变体」的编译与缓存；
 *   PsoManager 负责「SPIR-V 变体 + 渲染状态 -> VkPipeline」的组装与缓存。
 *
 * 由 RenderModule 拥有，init 于 ShaderVariantManager 之后，
 * cleanup 于 device 销毁之前。
 */
class PsoManager {
public:
    PsoManager() = default;
    ~PsoManager() = default;

    PsoManager(const PsoManager&) = delete;
    PsoManager& operator=(const PsoManager&) = delete;

    void init(VkDevice device, ShaderVariantManager* variantManager);
    void cleanup();

    /// 查询或创建 PSO。desc.layout 必须有效。
    VkPipeline getOrCreate(const GraphicsPSODesc& desc);

private:
    VkPipeline createPSO(const GraphicsPSODesc& desc);
    VkShaderModule createShaderModule(const std::vector<uint32_t>& code);

    VkDevice              device         = VK_NULL_HANDLE;
    ShaderVariantManager* variantManager = nullptr;

    std::unordered_map<GraphicsPSOKey, VkPipeline, GraphicsPSOKeyHash> cache;
};

} // namespace engine
