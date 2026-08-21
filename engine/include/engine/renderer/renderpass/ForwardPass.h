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
 * 仅保留构建期决定的字段：
 *   - swapchainHandle / pipelineLayout / 格式 / 采样数（pipeline 变体缓存所需）
 *   - buildExtent（用于在 Setup 里声明 depth 资源尺寸；swapchain 重建时同步重新构建）
 */
class ForwardPass : public IRenderPass {
public:
    ForwardPass();

    void Setup(RenderGraphBuilder& builder) override;
    void Execute(VkCommandBuffer cmd, const FrameContext& frame,
                 const RGResources& resources) override;
    void OnBuildRenderGraph(const FrameContext& ctx) override;

    // 构建期注入（由 pipeline.buildRenderGraph 设置一次）
    RGTextureHandle       swapchainHandle  = kInvalidRGTextureHandle;
    VkPipelineLayout      pipelineLayout   = VK_NULL_HANDLE;
    VkDevice              device           = VK_NULL_HANDLE;
    VkFormat              colorFormat      = VK_FORMAT_UNDEFINED;
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
    RGTextureHandle hDepth       = kInvalidRGTextureHandle;
    RGTextureHandle hShadowDir   = kInvalidRGTextureHandle;
    RGTextureHandle hShadowPoint = kInvalidRGTextureHandle;

    void drawOpaqueObjects(VkCommandBuffer cmd, const FrameContext& frame);
};

} // namespace engine
