#pragma once

#include "engine/renderer/renderpass/IRenderPass.h"
#include "engine/shader/ShaderParam.h"
#include "engine/shader/ShaderVariantManager.h"

#include <vulkan/vulkan.h>

namespace engine {

/**
 * GBufferPass -- 延迟管线几何通道：不透明物体写入 MRT GBuffer + depth。
 *
 * 绘制逻辑通过共享 SceneRenderer 完成（匹配材质的 GBuffer ShaderPass、
 * 绑定 Frame/Material/Object 数据并绘制 Mesh）。本类只声明 4 张 color
 * attachment（kGBufferFormats）+ 1 张 depth，并在 Execute 调用 DrawScene。
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

    void Setup(RenderGraphBuilder& builder,
               const RenderGraphBuildContext& ctx) override;
    void Execute(RenderPassContext& context) override;
    // 构建期注入（由 DeferredRenderer 设置一次）
    VkFormat              depthFormat      = VK_FORMAT_D32_SFLOAT;
    VkSampleCountFlagBits msaaSamples      = VK_SAMPLE_COUNT_1_BIT;
    ShaderParamSet        passParams;       // pass 级 shader variant 参数

private:
    RGTextureHandle hGBuffer[kGBufferCount] = {};
    RGTextureHandle hDepth       = kInvalidRGTextureHandle;

};

} // namespace engine
