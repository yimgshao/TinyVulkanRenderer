#include "engine/renderer/renderpass/ShadowPass.h"

#include "engine/scene/Scene.h"
#include "engine/scene/Mesh.h"

namespace engine {

// =============================================================================
// ShadowPass
// =============================================================================

ShadowPass::ShadowPass() {
    passName = "Shadow";
}

void ShadowPass::Setup(RenderGraphBuilder& builder) {
    uint32_t dirLayers = maxDirectionalLights;
    uint32_t ptLayers  = maxPointLights * 6;

    if (dirLayers > 0) {
        RGTextureDesc desc{};
        desc.width       = directionalRes;
        desc.height      = directionalRes;
        desc.arrayLayers = dirLayers;
        desc.format      = depthFormat;
        desc.usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                         | VK_IMAGE_USAGE_SAMPLED_BIT;
        hDirAtlas = builder.CreateTexture("ShadowAtlas_Directional", desc);
        builder.WriteDepth(hDirAtlas, {VK_ATTACHMENT_LOAD_OP_CLEAR,
                                        VK_ATTACHMENT_STORE_OP_STORE,
                                        {{{1.0f, 0.0f}}}});
    }

    if (ptLayers > 0) {
        RGTextureDesc desc{};
        desc.width       = pointRes;
        desc.height      = pointRes;
        desc.arrayLayers = ptLayers;
        desc.format      = depthFormat;
        desc.usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                         | VK_IMAGE_USAGE_SAMPLED_BIT;
        hPointAtlas = builder.CreateTexture("ShadowAtlas_Point", desc);
        builder.WriteDepth(hPointAtlas, {VK_ATTACHMENT_LOAD_OP_CLEAR,
                                          VK_ATTACHMENT_STORE_OP_STORE,
                                          {{{1.0f, 0.0f}}}});
    }
}

void ShadowPass::Execute(VkCommandBuffer cmd, const FrameContext& frame,
                         const RGResources& resources) {
    (void)resources;  // 本 pass 不采样 graph 纹理
    Scene* scene = frame.scene;
    if (!scene || pipeline == VK_NULL_HANDLE) return;

    const auto& lights = scene->getLights();

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

    int32_t dirLayer = 0, ptLayer = 0;

    for (const auto& light : lights) {
        if (!light.castsShadows) continue;

        if (light.type == LightType::Point) {
            auto viewProjs = computePointCubeViewProjs(light);
            for (uint32_t face = 0; face < 6; ++face) {
                drawSceneDepth(cmd, frame, viewProjs[face],
                               pointRes, ptLayer + static_cast<int32_t>(face));
            }
            ptLayer += 6;
        } else {
            glm::mat4 viewProj = (light.type == LightType::Directional)
                ? computeDirectionalViewProj(light, scene)
                : computeSpotViewProj(light);
            drawSceneDepth(cmd, frame, viewProj,
                           directionalRes, dirLayer);
            ++dirLayer;
        }
    }
}

void ShadowPass::drawSceneDepth(VkCommandBuffer cmd, const FrameContext& frame,
                                 const glm::mat4& lightViewProj,
                                 uint32_t resolution, int32_t layerIndex) {
    Scene* scene = frame.scene;
    if (!scene) return;

    VkViewport vp{0.0f, 0.0f, static_cast<float>(resolution),
                  static_cast<float>(resolution), 0.0f, 1.0f};
    vkCmdSetViewport(cmd, 0, 1, &vp);

    VkRect2D scissor{{0, 0}, {resolution, resolution}};
    vkCmdSetScissor(cmd, 0, 1, &scissor);

    vkCmdSetDepthBias(cmd, 1.5f, 0.0f, 2.0f);

    for (const auto& obj : scene->getRenderObjects()) {
        if (!obj.mesh) continue;

        struct {
            glm::mat4 mvp;
            int32_t   layerIndex;
            int32_t   _pad[3];
        } pc;
        pc.mvp        = lightViewProj * obj.transform;
        pc.layerIndex = layerIndex;
        pc._pad[0]    = 0;
        pc._pad[1]    = 0;
        pc._pad[2]    = 0;

        vkCmdPushConstants(cmd, pipelineLayout,
                           VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), &pc);

        obj.mesh->bind(cmd);
        obj.mesh->drawIndexed(cmd);
    }
}

} // namespace engine
