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

    void Setup(RenderGraphBuilder& builder) override;
    void Execute(VkCommandBuffer cmd, const FrameContext& frame,
                 const RGResources& resources) override;
    void OnBuildRenderGraph(const FrameContext& ctx) override;

    // 构建期注入（由 DeferredRenderer 设置一次）
    VkPipelineLayout pipelineLayout   = VK_NULL_HANDLE;  // [frame, gbuffer, shadow]
    VkDevice         device           = VK_NULL_HANDLE;
    VkFormat         colorFormat      = VK_FORMAT_UNDEFINED;  // swapchain 格式
    RGTextureHandle  swapchainHandle  = kInvalidRGTextureHandle;
    VkExtent2D       buildExtent      = {0, 0};

    // ---- GBuffer 采样 set（构建期注入；image 部分每帧 Execute 重写）----
    DescriptorSetManager* descManager     = nullptr;
    DescriptorSetHandle   gbufferSet      = DescriptorSetHandle::invalid();
    LayoutId              gbufferLayoutId = kInvalidLayoutId;
    uint32_t              gbufferSetIndex = 1;

    // ---- 阴影采样（构建期注入；与 GBufferPass 共用同一个 shadow set）----
    DescriptorSetHandle   shadowSet       = DescriptorSetHandle::invalid();
    LayoutId              shadowLayoutId  = kInvalidLayoutId;
    uint32_t              shadowSetIndex  = 2;

    // ---- IBL 采样（构建期注入；描述符在 renderer init 时一次写好，
    //      Execute 只绑定不重写。useIBL=false 时绑 fallback 占位）----
    DescriptorSetHandle   iblSet          = DescriptorSetHandle::invalid();
    uint32_t              iblSetIndex     = 3;
    bool                  useIBL          = false;
    bool                  useTonemap      = true;

private:
    VkPipeline pipeline = VK_NULL_HANDLE;  // OnBuildRenderGraph 经 PsoManager 创建/更新

    RGTextureHandle hGBuffer[4]  = {};
    RGTextureHandle hDepth       = kInvalidRGTextureHandle;
    RGTextureHandle hShadowDir   = kInvalidRGTextureHandle;
    RGTextureHandle hShadowPoint = kInvalidRGTextureHandle;
};

} // namespace engine
