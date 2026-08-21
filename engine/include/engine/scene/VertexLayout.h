#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <vector>

namespace engine {

/// 顶点布局 ID。新增格式时在此追加（保持已有 ID 数值不变）。
enum class VertexLayoutId : uint32_t {
    Static = 0, ///< 默认静态网格 (pos / normal / uv)
    // Skinned, UI, ... 后续可扩展
    Count
};

/// 单一顶点格式的描述：用于 pipeline 创建 + Mesh 自描述。
/// 当前 binding 固定为 0（一个 VkBuffer 一个 binding）；将来若要拆 stream 再扩展。
struct VertexLayoutDesc {
    std::string                                     name;        ///< 与 PipelineVariantKey::vertexType 匹配的字符串
    VkVertexInputBindingDescription                 binding{};
    std::vector<VkVertexInputAttributeDescription>  attributes;
};

/**
 * 进程级顶点布局注册表。
 *
 * - 引擎启动时第一次 Get/GetByName 触发内建格式 (Static) 注册
 * - 用户代码可通过 Register 注册自定义布局，配合相应的 Mesh CPU 结构
 * - MaterialTemplate 根据 PipelineVariantKey::vertexType 在此查询，作为
 *   pipeline VkPipelineVertexInputState 的唯一信息源
 */
class VertexLayoutRegistry {
public:
    static void Register(VertexLayoutId id, VertexLayoutDesc desc);
    static const VertexLayoutDesc* Get(VertexLayoutId id);
    static const VertexLayoutDesc* GetByName(const std::string& name);
};

} // namespace engine
