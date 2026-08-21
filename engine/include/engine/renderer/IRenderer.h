#pragma once

#include "engine/renderer/FrameContext.h"
#include "engine/scene/FrameUBO.h"

namespace engine {

class RenderGraph;

/**
 * IRenderer -- 一个完整渲染管线的最小抽象。
 *
 * RenderModule 持有唯一的 IRenderer 实例并驱动其生命周期。
 * 一个具体管线（Forward / Deferred / PathTracing 等）只需关注：
 *   - 自身需要的材质模板 / descriptor layout 创建（init / cleanup）
 *   - 向 RenderGraph 注册自己的 pass（buildRenderGraph）
 *   - 每帧向已注册 pass 注入可变数据（onFrame，可选）
 *
 * 调用顺序（RenderModule 内部）：
 *
 *   setRenderer()
 *     -> renderer->init(ctx)              // 一次性资源
 *
 *   buildGraph()
 *     -> renderer->buildRenderGraph(rg, ctx)
 *
 *   每帧 drawFrame()：
 *     -> renderer->onFrame(ctx)           // 注入 frameIndex / scene / ...
 *     -> renderGraph.Execute(cmd)
 *
 *   swapchain 重建：
 *     -> renderGraph.Cleanup()
 *     -> renderer->buildRenderGraph(rg, ctx)
 *
 *   cleanup():
 *     -> renderer->cleanup()
 */
class IRenderer {
public:
    virtual ~IRenderer() = default;

    /// 管线类型标识（"Forward", "Deferred", "PathTracer" 等）。
    virtual const char* getPipelineName() const = 0;

    /// 一次性初始化：创建材质模板、descriptor layout 等持久资源。
    /// pipeline 可以缓存 ctx 中的服务指针（descManager / variantManager 等）。
    virtual void init(const FrameContext& ctx) = 0;

    /// 一次性清理。被调用时 RenderModule 已经 vkDeviceWaitIdle 过。
    virtual void cleanup() = 0;

    /// 构建该管线的 RenderGraph 拓扑（注册 pass、声明输入输出）。
    /// 在 init 之后以及每次 swapchain 重建时被调用。
    virtual void buildRenderGraph(RenderGraph& rg, const FrameContext& ctx) = 0;

    /// 每帧 RenderGraph 执行前调用，向已注册 pass 注入可变状态。
    /// 默认空实现：对于完全无状态的 pipeline 可不重写。
    virtual void onFrame(const FrameContext& ctx) { (void)ctx; }

    /// 写当前帧的 FrameUBO 到 dst。RenderModule 在每帧 UBO 写入时调用。
    /// 默认实现写出 view/proj/cameraPos 与场景 lights（与原 RenderModule 行为一致）。
    /// 自定义 pipeline 可重写以注入 IBL / SH / 自定义矩阵等额外字段；
    /// 但必须保证写入起始 sizeof(FrameUBO) 字节符合 HLSL `PerFrameData` 布局，
    /// 因为 RenderModule 注册的描述符 range 就是这个大小。
    virtual void writeFrameUBO(void* dst, const FrameContext& ctx);
};

} // namespace engine
