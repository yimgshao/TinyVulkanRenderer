#include "engine/renderer/renderpass/ShadowPass.h"

#include "engine/scene/Scene.h"
#include "engine/scene/Mesh.h"

namespace engine {

// =============================================================================
// ShadowPass
// =============================================================================

ShadowPass::ShadowPass(bool pointAtlas) : pointAtlas_(pointAtlas) {
    passName = pointAtlas ? "ShadowPoint" : "Shadow";
}

void ShadowPass::Setup(RenderGraphBuilder& builder,
                       const RenderGraphBuildContext& /*ctx*/) {
    uint32_t dirLayers = maxDirectionalLights;
    uint32_t ptLayers  = maxPointLights * 6;

    if (!pointAtlas_) {
        RGTextureDesc desc{};
        desc.width       = directionalRes;
        desc.height      = directionalRes;
        desc.arrayLayers = std::max(1u, dirLayers);
        desc.format      = depthFormat;
        desc.viewType    = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        desc.usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                         | VK_IMAGE_USAGE_SAMPLED_BIT;
        hDirAtlas = builder.CreateTexture("ShadowAtlas_Directional", desc);
        builder.WriteDepth(hDirAtlas, {VK_ATTACHMENT_LOAD_OP_CLEAR,
                                        VK_ATTACHMENT_STORE_OP_STORE,
                                        {{{1.0f, 0.0f}}}});
    }

    if (pointAtlas_) {
        RGTextureDesc desc{};
        desc.width       = pointRes;
        desc.height      = pointRes;
        desc.arrayLayers = std::max(1u, ptLayers);
        desc.format      = depthFormat;
        desc.viewType    = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
        desc.usage       = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT
                         | VK_IMAGE_USAGE_SAMPLED_BIT;
        hPointAtlas = builder.CreateTexture("ShadowAtlas_Point", desc);
        builder.WriteDepth(hPointAtlas, {VK_ATTACHMENT_LOAD_OP_CLEAR,
                                          VK_ATTACHMENT_STORE_OP_STORE,
                                          {{{1.0f, 0.0f}}}});
    }
}

void ShadowPass::Execute(RenderPassContext& context) {
    const auto& frame = context.GetFrame();
    Scene* scene = frame.scene;
    if (!scene) return;

    const auto& lights = scene->getLights();

    int32_t dirLayer = 0, ptLayer = 0;

    for (const auto& light : lights) {
        if (!light.castsShadows || (light.type == LightType::Point) != pointAtlas_) continue;

        if (light.type == LightType::Point) {
            if (ptLayer + 6 > static_cast<int32_t>(maxPointLights * 6)) continue;
            auto viewProjs = computePointCubeViewProjs(light);
            for (uint32_t face = 0; face < 6; ++face) {
                drawSceneDepth(context, viewProjs[face],
                               pointRes, ptLayer + static_cast<int32_t>(face));
            }
            ptLayer += 6;
        } else {
            if (dirLayer >= static_cast<int32_t>(maxDirectionalLights)) continue;
            glm::mat4 viewProj = (light.type == LightType::Directional)
                ? computeDirectionalViewProj(light, scene)
                : computeSpotViewProj(light);
            drawSceneDepth(context, viewProj,
                           directionalRes, dirLayer);
            ++dirLayer;
        }
    }
}

void ShadowPass::drawSceneDepth(RenderPassContext& context, const glm::mat4& lightViewProj,
                                 uint32_t resolution, int32_t layerIndex) {
    VkViewport vp{0.0f, 0.0f, static_cast<float>(resolution),
                  static_cast<float>(resolution), 0.0f, 1.0f};
    context.SetViewport(vp);

    VkRect2D scissor{{0, 0}, {resolution, resolution}};
    context.SetScissor(scissor);

    context.SetDepthBias(1.5f, 0.0f, 0.5f);

    struct alignas(16) ShadowPassData {
        glm::mat4 lightViewProj;
        int32_t layer;
        int32_t padding[3];
    } passData{lightViewProj, layerIndex, {0, 0, 0}};
    context.SetPassData(passData);
    context.DrawScene("ShadowCaster");
}

} // namespace engine
