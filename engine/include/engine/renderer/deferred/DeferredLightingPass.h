#pragma once

#include "engine/renderer/renderpass/IRenderPass.h"

#include <vulkan/vulkan.h>

namespace engine {

/**
 * DeferredLightingPass -- 延迟管线全屏光照通道。
 *
 * 采样 GBufferPass 写出的 4 张 GBuffer + depth，全屏三角形做 PBR 光照
 * （复用 FrameUBO 光源与阴影 atlas），输出到 swapchain。
 *
 * 深度不作为 attachment 复用，而是经 SAMPLED 读在 FS 内采样：
 * 世界坐标重建本身就需要逐像素深度值，采样后 depth == clear 值（1.0）
 * 的像素直接 discard，等效实现设计中的天空像素裁剪。
 *
 * Descriptor 布局（与 deferred/lighting.hlsl 的 vk::binding 一一对应）：
 *   Set 0: PerFrameData UBO（frame set，RenderModule 拥有）
 *   Set 1: GBuffer 采样集（4x GBuffer + depth SAMPLED_IMAGE + SAMPLER）
 *   Set 2: 阴影 atlas（与 forward 共用同一 shadow set layout）
 */
class DeferredLightingPass : public IRenderPass {
public:
    DeferredLightingPass();

    void Setup(RenderGraphBuilder& builder,
               const RenderGraphBuildContext& ctx) override;
    void Execute(RenderPassContext& context) override;
    // ---- IBL 采样（构建期注入；描述符在 renderer init 时一次写好，
    //      Execute 只绑定不重写。useIBL=false 时绑 fallback 占位）----
    DescriptorSetHandle   iblSet          = DescriptorSetHandle::invalid();
    uint32_t              iblSetIndex     = 3;
    bool                  useIBL          = false;
    bool                  useTonemap      = true;

private:
    RGTextureHandle hGBuffer[4]  = {};
    RGTextureHandle hDepth       = kInvalidRGTextureHandle;
    RGTextureHandle hShadowDir   = kInvalidRGTextureHandle;
    RGTextureHandle hShadowPoint = kInvalidRGTextureHandle;
};

} // namespace engine
