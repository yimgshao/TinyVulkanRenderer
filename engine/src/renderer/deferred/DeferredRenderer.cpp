#include "engine/renderer/deferred/DeferredRenderer.h"

#include "engine/VulkanContext.h"
#include "engine/renderer/rendergraph/RenderGraph.h"
#include "engine/renderer/deferred/GBufferPass.h"
#include "engine/renderer/deferred/DeferredLightingPass.h"
#include "engine/renderer/renderpass/ShadowPass.h"
#include "engine/pso/PsoManager.h"
#include "engine/shader/ShaderVariantManager.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <iterator>
#include <stdexcept>

namespace engine {

namespace {

/// 内置延迟管线 GBuffer pass 的 shader 模块配置。
/// 引擎核心不硬编码任何具体 pass，内置管线与用户自定义 pass 平级，
/// 各自在自己的模块里构造 ShaderModuleConfig。
ShaderModuleConfig MakeGBufferShaderConfig() {
    ShaderModuleConfig c;
    c.moduleName = "deferred/gbuffer";
    return c;
}

} // anonymous namespace

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

    // 阴影资源（set layout 必须先于材质模板创建，经 extraSetLayouts 注入）
    setupShadows(ctx);

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

    // Material set layout 由 MaterialTemplate 根据 shader reflection 创建。
    // 材质层与 forward 完全共用（PbrMaterial + materials/pbr.hlsl），
    // 材质对「画到 swapchain 还是 GBuffer」无感知。
    MaterialTemplateCreateInfo tmplInfo{};
    tmplInfo.variantManager = variantManager;
    tmplInfo.materialType = materialCfg_.getString("type", "PbrMaterial");
    tmplInfo.materialHeader = materialCfg_.getString("header", "materials/pbr.hlsl");
    tmplInfo.alphaMode      = AlphaMode::Opaque;
    tmplInfo.frameSetLayout = frameSetLayout;
    tmplInfo.descManager    = descManager;
    tmplInfo.extraSetLayouts = {shadowSetLayout};

    defaultMaterialTemplate = std::make_unique<MaterialTemplate>();
    defaultMaterialTemplate->init(device, physicalDevice, tmplInfo);
    // MaterialTemplate::init 内部已把 material layout 注册到 descManager 并存了 LayoutId

    createDefaultPasses(ctx);
}

void DeferredRenderer::cleanup() {
    orderConstraints_.clear();
    passes_.clear();

    if (defaultMaterialTemplate) {
        defaultMaterialTemplate->cleanup(device);
        defaultMaterialTemplate.reset();
    }

    // IBL 资源
    cleanupIBLResources(device, descManager, iblRes_);
    iblTextures_.cleanup(device);
    useIBL_ = false;

    // 阴影资源
    if (shadowPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);
        shadowPipelineLayout = VK_NULL_HANDLE;
    }
    if (shadowSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, shadowSetLayout, nullptr);
        shadowSetLayout = VK_NULL_HANDLE;
    }

    descManager    = nullptr;
    variantManager = nullptr;
    frameSetLayout = VK_NULL_HANDLE;
    physicalDevice = VK_NULL_HANDLE;
    device         = VK_NULL_HANDLE;
}

// ------------------------------------------------------------------
// Shadow resources（与 ForwardRenderer::setupShadows 同一模式）
// ------------------------------------------------------------------

void DeferredRenderer::setupShadows(const FrameContext& ctx) {
    (void)ctx;

    // shadow set layout（material set 之后的全局 set）：
    // 2x SAMPLED_IMAGE + 1x SAMPLER。Set 的分配、写入和绑定由 RenderGraph 完成。
    std::array<VkDescriptorSetLayoutBinding, 3> bindings{};
    for (uint32_t i = 0; i < 3; ++i) {
        bindings[i].binding         = i;
        bindings[i].descriptorType  = (i < 2) ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
                                              : VK_DESCRIPTOR_TYPE_SAMPLER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags      = VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    layoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    layoutInfo.pBindings    = bindings.data();
    if (vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr,
                                    &shadowSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("DeferredRenderer: failed to create shadow set layout.");
    }

}

// ------------------------------------------------------------------
// Default passes
// ------------------------------------------------------------------

void DeferredRenderer::createDefaultPasses(const FrameContext& ctx) {
    // ShadowPass：VS-only 深度 pipeline layout；Shader/PSO 由 RenderGraph 创建。
    VkPushConstantRange pcRange{VK_SHADER_STAGE_VERTEX_BIT, 0, 96};
    VkPipelineLayoutCreateInfo pli{};
    pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount         = 0;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pcRange;
    if (vkCreatePipelineLayout(device, &pli, nullptr,
                               &shadowPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("DeferredRenderer: failed to create shadow pipeline layout.");
    }

    auto shadow = std::make_unique<ShadowPass>();
    shadow->pipelineLayout = shadowPipelineLayout;
    shadow->depthFormat    = VK_FORMAT_D32_SFLOAT;
    shadow->directionalRes = rendererCfg_.getInt("shadow.directionalRes", 2048);
    shadow->pointRes       = rendererCfg_.getInt("shadow.pointRes", 512);
    shadow->maxDirectionalLights = rendererCfg_.getInt("shadow.maxDirectionalLights", 4);
    shadow->maxPointLights       = rendererCfg_.getInt("shadow.maxPointLights", 4);

    // GBufferPass：MRT 几何 pass，pipeline layout 复用材质模板的
    //（[frame, material, shadow]，阴影 set 经 extraSetLayouts 注入）。
    auto gbuffer = std::make_unique<GBufferPass>();
    gbuffer->passName        = "GBuffer";
    gbuffer->pipelineLayout  = defaultMaterialTemplate->getPipelineLayout();
    gbuffer->depthFormat     = VK_FORMAT_D32_SFLOAT;
    gbuffer->msaaSamples     = VK_SAMPLE_COUNT_1_BIT;
    gbuffer->shaderConfig    = MakeGBufferShaderConfig();
    gbuffer->materialHeader  = defaultMaterialTemplate->getMaterialHeader();

    // DeferredLightingPass：全屏光照，PipelineLayout 由 Shader 反射生成。
    auto lighting = std::make_unique<DeferredLightingPass>();
    lighting->passName        = "DeferredLighting";
    lighting->iblSet          = iblRes_.set;
    lighting->iblSetIndex     = 3;
    lighting->useIBL          = useIBL_;
    lighting->useTonemap      = rendererCfg_.getBool("tonemap", true);

    // 执行顺序由 RenderGraph 依赖推导（GBuffer 读 atlas ⇒ Shadow 先执行；
    // Lighting 读 GBuffer ⇒ GBuffer 先执行），这里保持声明顺序一致。
    passes_.push_back(std::move(shadow));
    passes_.push_back(std::move(gbuffer));
    passes_.push_back(std::move(lighting));
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
