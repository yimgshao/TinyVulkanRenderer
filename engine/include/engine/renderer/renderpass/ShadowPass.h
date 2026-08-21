#pragma once

#include "engine/renderer/renderpass/IRenderPass.h"

#include <glm/glm.hpp>
#include <vulkan/vulkan.h>
#include <cstdint>

namespace engine {

/**
 * ShadowPass — 多光源阴影深度通道。
 *
 * 使用 SV_RenderTargetArrayIndex 一次 BeginRendering 覆盖全部 atlas 层，
 * 不对每层单独 begin/end。
 *
 * 管线独立创建（不经 MaterialTemplate）：vertex-only, depth-only。
 */
class ShadowPass : public IRenderPass {
public:
    ShadowPass();

    void Setup(RenderGraphBuilder& builder) override;
    void Execute(VkCommandBuffer cmd, const FrameContext& frame,
                 const RGResources& resources) override;

    // ---- 构建期注入（由 ForwardRenderer::createDefaultPasses 设置） ----
    VkDevice         device           = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout   = VK_NULL_HANDLE;
    VkPipeline       pipeline         = VK_NULL_HANDLE;
    VkFormat         depthFormat      = VK_FORMAT_D32_SFLOAT;

    /// 最多支持的投射阴影定向光 + 聚光数（决定 directional atlas 层数）。
    uint32_t maxDirectionalLights = 4;
    /// 最多支持的投射阴影点光数（每个点光 6 层）。
    uint32_t maxPointLights       = 4;
    /// 定向光 atlas 分辨率。
    uint32_t directionalRes = 2048;
    /// 点光 atlas 分辨率。
    uint32_t pointRes       = 512;

private:
    RGTextureHandle hDirAtlas   = kInvalidRGTextureHandle;
    RGTextureHandle hPointAtlas = kInvalidRGTextureHandle;

    void drawSceneDepth(VkCommandBuffer cmd, const FrameContext& frame,
                        const glm::mat4& lightViewProj,
                        uint32_t resolution, int32_t layerIndex);
};

} // namespace engine
