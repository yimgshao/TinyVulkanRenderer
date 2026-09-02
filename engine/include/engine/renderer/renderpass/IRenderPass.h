#pragma once

#include "engine/renderer/rendergraph/RenderGraphBuilder.h"
#include "engine/renderer/rendergraph/RGResources.h"
#include "engine/renderer/RenderGraphBuildContext.h"
#include "engine/renderer/FrameContext.h"
#include "engine/shader/ShaderParam.h"

#include <vulkan/vulkan.h>
#include <memory>
#include <string>

namespace engine {

struct PassPipelineRuntime;

class IRenderPass {
public:
    virtual ~IRenderPass() = default;

    /// 声明本 pass 的资源读写与 attachment。
    /// 每次 RenderGraph 重建时调用；ctx 仅包含构建期状态。
    virtual void Setup(RenderGraphBuilder& builder,
                       const RenderGraphBuildContext& ctx) = 0;

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

    std::string passName;
    bool enabled = true;

protected:
    /// 通过 RenderGraph 为当前 pass 建立的统一 Pipeline Runtime 选择并绑定变体。
    /// 普通 pass 无需调用；RenderGraph 会自动绑定默认变体。材质 pass 在材质
    /// 改变时调用本函数选择同一 pass shader 的材质变体。
    VkPipeline BindShaderVariant(
        VkCommandBuffer cmd,
        const ShaderVariantKey& key = {},
        const ShaderParamSet& materialParams = {},
        const std::string& materialHeader = "") const;

    VkPipelineLayout GetPipelineLayout() const;

private:
    friend class RenderGraph;
    std::shared_ptr<PassPipelineRuntime> pipelineRuntime_;
};

} // namespace engine
