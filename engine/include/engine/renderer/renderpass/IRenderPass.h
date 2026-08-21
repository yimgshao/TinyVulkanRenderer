#pragma once

#include "engine/renderer/rendergraph/RenderGraphBuilder.h"
#include "engine/renderer/rendergraph/RGResources.h"
#include "engine/renderer/FrameContext.h"

#include <vulkan/vulkan.h>

namespace engine {

class IRenderPass {
public:
    virtual ~IRenderPass() = default;

    virtual void Setup(RenderGraphBuilder& builder) = 0;

    /**
     * 录制本 pass 的渲染命令。
     *
     * 资源解析契约（与 RGResources 头注释共同构成正式约定）：
     *   - graph 纹理的物理资源（VkImageView 等）只允许在本函数内通过
     *     resources 解析并使用，禁止缓存到成员变量；
     *   - 引用 graph 纹理的 descriptor 必须在本函数内每帧重写；
     *   - RenderGraph 保证整个 Execute 阶段物理资源有效。
     * 不采样 graph 纹理的 pass 直接忽略 resources 即可。
     */
    virtual void Execute(VkCommandBuffer cmd, const FrameContext& frame,
                         const RGResources& resources) = 0;

    /// 在 buildRenderGraph 中被调用，用于更新 swapchain 相关字段等构建期状态。
    /// 默认空实现：无需更新的 pass 不用重写。
    virtual void OnBuildRenderGraph(const FrameContext& ctx) { (void)ctx; }

    std::string passName;
    bool enabled = true;
};

} // namespace engine
