#pragma once
#include "engine/renderer/renderpass/IRenderPass.h"
namespace engine {
class TonemapPass : public IRenderPass {
public:
    explicit TonemapPass(bool enabled) : tonemap_(enabled) { passName = "Tonemap"; }
    void Setup(RenderGraphBuilder& builder, const RenderGraphBuildContext& ctx) override {
        ShaderModuleConfig shader;
        shader.moduleName = "post/tonemap";
        shader.genericValueParams = {"useTonemap"};
        builder.SetShader(shader);
        ShaderParamSet params; params.set("useTonemap", tonemap_);
        builder.SetPassParams(params);
        builder.ReadTexture("SceneColor");
        builder.WriteColor(ctx.hSwapchain);
    }
    void Execute(RenderPassContext& context) override {
        const auto cmd = context.GetCommandBuffer();
        const auto& frame = context.GetFrame();
        ext::vkCmdSetDescriptorBufferOffsetsEXT(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, GetPipelineLayout(),
            0, 1, &frame.frameSet.bufferIndex, &frame.frameSet.offset);
        vkCmdDraw(cmd, 3, 1, 0, 0);
    }
private:
    bool tonemap_;
};
}
