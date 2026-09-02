#pragma once

#include "engine/renderer/renderpass/IRenderPass.h"
#include "engine/shader/ShaderParam.h"
#include "engine/shader/ShaderVariantManager.h"

#include <vulkan/vulkan.h>

namespace engine {

/**
 * ForwardPass -- 前向光照通道，向 swapchain 写一遍场景 + GUI 叠加。
 *
 * 不持有任何「每帧注入」的可变字段。所有 per-frame 状态
 * （scene / frameSet / guiRender / renderExtent ...）一律从 FrameContext 读取。
 *
 * 仅保留 pipeline layout / 格式 / 采样数等 Pipeline 变体缓存所需字段。
 */
class ForwardPass : public IRenderPass {
public:
    ForwardPass();

    void Setup(RenderGraphBuilder& builder,
               const RenderGraphBuildContext& ctx) override;
    void Execute(VkCommandBuffer cmd, const FrameContext& frame,
                 const RGResources& resources) override;
    // 构建期注入（由 pipeline.buildRenderGraph 设置一次）
    VkPipelineLayout      pipelineLayout   = VK_NULL_HANDLE;
    VkFormat              colorFormat      = VK_FORMAT_UNDEFINED;
    VkFormat              depthFormat      = VK_FORMAT_D32_SFLOAT;
    VkSampleCountFlagBits msaaSamples      = VK_SAMPLE_COUNT_1_BIT;
    ShaderParamSet        passParams;       // pass 级 shader variant 参数
    ShaderModuleConfig    shaderConfig;     // 本 pass 的 shader 模块配置（材质由模板注入）
    std::string           materialHeader;

private:
    RGTextureHandle hDepth       = kInvalidRGTextureHandle;
    RGTextureHandle hShadowDir   = kInvalidRGTextureHandle;
    RGTextureHandle hShadowPoint = kInvalidRGTextureHandle;

    void drawOpaqueObjects(VkCommandBuffer cmd, const FrameContext& frame);
};

} // namespace engine
