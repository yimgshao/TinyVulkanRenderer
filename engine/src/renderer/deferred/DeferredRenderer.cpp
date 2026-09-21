#include "engine/renderer/deferred/DeferredRenderer.h"

#include "engine/VulkanContext.h"
#include "engine/renderer/rendergraph/RenderGraph.h"
#include "engine/renderer/deferred/GBufferPass.h"
#include "engine/renderer/deferred/DeferredLightingPass.h"
#include "engine/renderer/renderpass/ShadowPass.h"
#include "engine/renderer/renderpass/TonemapPass.h"
#include "engine/pso/PsoManager.h"
#include "engine/shader/ShaderVariantManager.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace engine {


void DeferredRenderer::validateNewPass(const IRenderPass* pass) const {
    if (!pass) {
        throw std::runtime_error("DeferredRenderer: cannot add a null render pass.");
    }
    if (pass->passName.empty()) {
        throw std::runtime_error("DeferredRenderer: render pass name cannot be empty.");
    }
    const auto duplicate = std::find_if(
        passes_.begin(), passes_.end(), [&](const auto& existing) {
            return existing->passName == pass->passName;
        });
    if (duplicate != passes_.end()) {
        throw std::runtime_error("DeferredRenderer: duplicate render pass name '" +
                                 pass->passName + "'.");
    }
}

void DeferredRenderer::addPass(std::unique_ptr<IRenderPass> pass) {
    validateNewPass(pass.get());
    passes_.push_back(std::move(pass));
}

void DeferredRenderer::insertPassBefore(
    const std::string& targetPassName,
    std::unique_ptr<IRenderPass> pass) {
    validateNewPass(pass.get());
    const auto target = std::find_if(
        passes_.begin(), passes_.end(), [&](const auto& existing) {
            return existing->passName == targetPassName;
        });
    if (target == passes_.end()) {
        throw std::runtime_error("DeferredRenderer: cannot insert pass '" +
                                 pass->passName + "' before unknown pass '" +
                                 targetPassName + "'.");
    }

    IRenderPass* insertedPass = pass.get();
    IRenderPass* targetPass = target->get();
    passes_.insert(target, std::move(pass));
    orderConstraints_.push_back({insertedPass, targetPass});
}

void DeferredRenderer::insertPassAfter(
    const std::string& targetPassName,
    std::unique_ptr<IRenderPass> pass) {
    validateNewPass(pass.get());
    const auto target = std::find_if(
        passes_.begin(), passes_.end(), [&](const auto& existing) {
            return existing->passName == targetPassName;
        });
    if (target == passes_.end()) {
        throw std::runtime_error("DeferredRenderer: cannot insert pass '" +
                                 pass->passName + "' after unknown pass '" +
                                 targetPassName + "'.");
    }

    IRenderPass* targetPass = target->get();
    IRenderPass* insertedPass = pass.get();
    passes_.insert(std::next(target), std::move(pass));
    orderConstraints_.push_back({targetPass, insertedPass});
}

// ------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------

void DeferredRenderer::init(const FrameContext& ctx) {
    if (!ctx.vkContext || !ctx.descManager || !ctx.variantManager) {
        throw std::runtime_error(
            "DeferredRenderer::init requires valid FrameContext");
    }

    device         = ctx.vkContext->device;
    physicalDevice = ctx.vkContext->physicalDevice;
    descManager    = ctx.descManager;
    variantManager = ctx.variantManager;
    frameSetLayout = ctx.frameSetLayout;
    shadowPcfRadius_ = rendererCfg_.getInt("shadow.pcfRadius", 1);


    // IBL 资源（set 3 的 layout 要先于 setupLightingResources 的 pipeline
    // layout 创建）。无环境时加载 1x1 fallback 占位，保证 set 恒可绑定。
    if (!iblPath_.empty()) {
        useIBL_ = iblTextures_.load(iblPath_, ctx.vkContext);
        if (!useIBL_) {
            std::cerr << "[DeferredRenderer] IBL load failed, fallback: "
                      << iblPath_ << "\n";
            iblTextures_.cleanup(device);
        }
    }
    if (!useIBL_) {
        iblTextures_.loadFallback(ctx.vkContext);
    }
    setupIBLResources(device, descManager, iblTextures_, iblRes_);

    createDefaultPasses(ctx);
}

void DeferredRenderer::writeFrameUBO(void* dst, const FrameContext& ctx) {
    if (!dst || !ctx.scene) return;

    IRenderer::writeFrameUBO(dst, ctx);
    static_cast<FrameUBO*>(dst)->shadowPcfRadius = shadowPcfRadius_;
}

void DeferredRenderer::cleanup() {
    orderConstraints_.clear();
    passes_.clear();


    // IBL 资源
    cleanupIBLResources(device, descManager, iblRes_);
    iblTextures_.cleanup(device);
    useIBL_ = false;

    descManager    = nullptr;
    variantManager = nullptr;
    frameSetLayout = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
    device         = VK_NULL_HANDLE;
}

// ------------------------------------------------------------------
// Shadow resources
// ------------------------------------------------------------------


// ------------------------------------------------------------------
// Default passes
// ------------------------------------------------------------------

void DeferredRenderer::createDefaultPasses(const FrameContext& ctx) {
    auto shadow = std::make_unique<ShadowPass>();
    shadow->depthFormat    = VK_FORMAT_D32_SFLOAT;
    shadow->directionalRes = rendererCfg_.getInt("shadow.directionalRes", 2048);
    shadow->pointRes       = rendererCfg_.getInt("shadow.pointRes", 512);
    shadow->maxDirectionalLights = rendererCfg_.getInt("shadow.maxDirectionalLights", 4);
    shadow->maxPointLights       = rendererCfg_.getInt("shadow.maxPointLights", 4);

    auto gbuffer = std::make_unique<GBufferPass>();
    gbuffer->passName        = "GBuffer";
    gbuffer->depthFormat     = VK_FORMAT_D32_SFLOAT;
    gbuffer->msaaSamples     = VK_SAMPLE_COUNT_1_BIT;

    // DeferredLightingPass：全屏光照，PipelineLayout 由 Shader 反射生成。
    auto lighting = std::make_unique<DeferredLightingPass>();
    lighting->passName        = "DeferredLighting";
    lighting->iblSet          = iblRes_.set;
    lighting->iblSetIndex     = 3;
    lighting->useIBL          = useIBL_;
    lighting->useTonemap      = rendererCfg_.getBool("tonemap", true);

    auto shadowPoint = std::make_unique<ShadowPass>(true);
    shadowPoint->depthFormat = shadow->depthFormat;
    shadowPoint->pointRes = shadow->pointRes;
    shadowPoint->maxPointLights = shadow->maxPointLights;

    // 执行顺序由 RenderGraph 依赖推导（GBuffer 读 atlas ⇒ Shadow 先执行；
    // Lighting 读 GBuffer ⇒ GBuffer 先执行），这里保持声明顺序一致。
    passes_.push_back(std::move(shadow));
    passes_.push_back(std::move(shadowPoint));
    passes_.push_back(std::move(gbuffer));
    passes_.push_back(std::move(lighting));
    passes_.push_back(std::make_unique<TonemapPass>(rendererCfg_.getBool("tonemap", true)));
}

// ------------------------------------------------------------------
// RenderGraph hook
// ------------------------------------------------------------------

void DeferredRenderer::buildRenderGraph(RenderGraph& rg,
                                        const RenderGraphBuildContext& ctx) {
    for (auto& pass : passes_) {
        rg.AddPass(pass.get(), ctx);
    }
    for (const auto& constraint : orderConstraints_) {
        if (constraint.before->enabled && constraint.after->enabled) {
            rg.AddExecutionDependency(constraint.before, constraint.after);
        }
    }
}

} // namespace engine
