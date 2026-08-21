#pragma once

#include "engine/renderer/renderpass/IRenderPass.h"
#include "engine/shader/ShaderParam.h"
#include "engine/shader/ShaderVariantManager.h"

#include <vulkan/vulkan.h>

namespace engine {

/**
 * GBufferPass -- 延迟管线几何通道：不透明物体写入 MRT GBuffer + depth。
 *
 * 绘制逻辑与 ForwardPass 几乎相同（绑 set0、阴影 set、按材质缓存 PSO、
 * push constant model、只画 Opaque），差异仅在：
 *   - PSO 的 shaderConfig 指向 deferred/gbuffer 模块
 *   - 4 张 color attachment（kGBufferFormats）+ 1 张 depth
 *
 * 不持有任何「每帧注入」的可变字段，per-frame 状态一律从 FrameContext 读取。
 */
class GBufferPass : public IRenderPass {
public:
    /// GBuffer MRT 布局（Setup 的纹理声明与 PSO colorFormats 共用同一份，
    /// 与 deferred/gbuffer.hlsl 的 SV_Target0..3 一一对应）。
    static constexpr uint32_t kGBufferCount = 4;
    static constexpr VkFormat kGBufferFormats[kGBufferCount] = {
        VK_FORMAT_R8G8B8A8_SRGB,             // GBuffer0: baseColor.rgb + occlusion.a
        VK_FORMAT_A2R10G10B10_UNORM_PACK32,  // GBuffer1: 世界法线[0,1].rgb + metallic.a
        VK_FORMAT_R8G8B8A8_UNORM,            // GBuffer2: roughness.r（其余通道预留）
        VK_FORMAT_R16G16B16A16_SFLOAT,       // GBuffer3: emissive.rgb（HDR）
    };

    GBufferPass();

    void Setup(RenderGraphBuilder& builder) override;
    void Execute(VkCommandBuffer cmd, const FrameContext& frame,
                 const RGResources& resources) override;
    void OnBuildRenderGraph(const FrameContext& ctx) override;

    // 构建期注入（由 DeferredRenderer 设置一次）
    VkPipelineLayout      pipelineLayout   = VK_NULL_HANDLE;
    VkDevice              device           = VK_NULL_HANDLE;
    VkFormat              depthFormat      = VK_FORMAT_D32_SFLOAT;
    VkSampleCountFlagBits msaaSamples      = VK_SAMPLE_COUNT_1_BIT;
    VkExtent2D            buildExtent      = {0, 0};
    ShaderParamSet        passParams;       // pass 级 shader variant 参数
    ShaderModuleConfig    shaderConfig;     // 本 pass 的 shader 模块配置（材质由模板注入）

    // ---- 阴影采样（构建期注入；shadowSet 无效则不绑阴影 set）----
    DescriptorSetManager* descManager    = nullptr;
    DescriptorSetHandle   shadowSet      = DescriptorSetHandle::invalid();
    LayoutId              shadowLayoutId = kInvalidLayoutId;
    uint32_t              shadowSetIndex = 2;

private:
    RGTextureHandle hGBuffer[kGBufferCount] = {};
    RGTextureHandle hDepth       = kInvalidRGTextureHandle;
    RGTextureHandle hShadowDir   = kInvalidRGTextureHandle;
    RGTextureHandle hShadowPoint = kInvalidRGTextureHandle;

    void drawOpaqueObjects(VkCommandBuffer cmd, const FrameContext& frame);
};

} // namespace engine
