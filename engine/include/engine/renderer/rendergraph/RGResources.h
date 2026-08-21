#pragma once

#include "engine/renderer/rendergraph/RGResource.h"

#include <vulkan/vulkan.h>

namespace engine {

class RenderGraph;

/**
 * RGResources -- RenderGraph 物理资源的只读访问器。
 *
 * 契约（IRenderPass::Execute 的正式约定）：
 *   1. pass 可以持久持有逻辑句柄（RGTextureHandle），但禁止缓存任何
 *      物理句柄（VkImage / VkImageView）到成员变量；
 *   2. 逻辑 -> 物理的解析只允许发生在 IRenderPass::Execute 内，
 *      通过本访问器完成；
 *   3. 引用 graph 纹理的 descriptor 必须在 Execute 内每帧重写。
 *
 * 作为交换，RenderGraph 承诺：整个 Execute 阶段物理资源保持有效
 * （Compile 先于任何 Execute，且帧内不会销毁物理资源）。
 *
 * 本类只能由 RenderGraph 构造（私有构造 + friend），因此 pass 在
 * Setup / OnBuildRenderGraph 等阶段无法拿到实例 -- 「何时可以解析」
 * 由类型系统强制，而非依赖开发者自觉。
 */
class RGResources {
public:
    /// 获取 RG 纹理对应的物理 VkImageView（仅 Execute 阶段有效，禁止缓存）。
    VkImageView GetImageView(RGTextureHandle handle) const;

    /// 获取 RG 纹理对应的物理 VkImage（仅 Execute 阶段有效，禁止缓存）。
    VkImage GetImage(RGTextureHandle handle) const;

    /// 获取 RG 纹理的逻辑描述（尺寸/格式/usage 等，生命周期与 graph 相同）。
    const RGTextureDesc& GetDesc(RGTextureHandle handle) const;

private:
    friend class RenderGraph;

    explicit RGResources(const RenderGraph* graph) : graph(graph) {}

    const RenderGraph* graph = nullptr;
};

} // namespace engine
