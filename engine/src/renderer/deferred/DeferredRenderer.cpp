#include "engine/renderer/deferred/DeferredRenderer.h"

#include "engine/VulkanContext.h"
#include "engine/renderer/rendergraph/RenderGraph.h"
#include "engine/renderer/deferred/GBufferPass.h"
#include "engine/renderer/deferred/DeferredLightingPass.h"
#include "engine/renderer/renderpass/ShadowPass.h"
#include "engine/pso/PsoManager.h"
#include "engine/shader/ShaderVariantManager.h"

#include <array>
#include <iostream>
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

    // GBuffer 采样资源 + lighting pipeline layout
    setupLightingResources(ctx);

    // Material set layout 由 MaterialTemplate 根据 shader reflection 创建。
    // 材质层与 forward 完全共用（PbrMaterial + materials/pbr.hlsl），
    // 材质对「画到 swapchain 还是 GBuffer」无感知。
    MaterialTemplateCreateInfo tmplInfo{};
    tmplInfo.variantManager = variantManager;
    tmplInfo.psoManager     = ctx.psoManager;
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
    passes_.clear();

    if (defaultMaterialTemplate) {
        defaultMaterialTemplate->cleanup(device);
        defaultMaterialTemplate.reset();
    }

    // GBuffer 采样资源
    if (gbufferSet.isValid() && descManager) {
        descManager->free(gbufferLayoutId, gbufferSet);
        gbufferSet = DescriptorSetHandle::invalid();
    }
    gbufferLayoutId = kInvalidLayoutId;
    if (lightingPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, lightingPipelineLayout, nullptr);
        lightingPipelineLayout = VK_NULL_HANDLE;
    }
    if (gbufferSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, gbufferSetLayout, nullptr);
        gbufferSetLayout = VK_NULL_HANDLE;
    }
    if (gbufferSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, gbufferSampler, nullptr);
        gbufferSampler = VK_NULL_HANDLE;
    }

    // IBL 资源
    cleanupIBLResources(device, descManager, iblRes_);
    iblTextures_.cleanup(device);
    useIBL_ = false;

    // 阴影资源
    if (shadowSet.isValid() && descManager) {
        descManager->free(shadowLayoutId, shadowSet);
        shadowSet = DescriptorSetHandle::invalid();
    }
    shadowLayoutId = kInvalidLayoutId;
    if (shadowPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(device, shadowPipelineLayout, nullptr);
        shadowPipelineLayout = VK_NULL_HANDLE;
    }
    if (shadowSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(device, shadowSetLayout, nullptr);
        shadowSetLayout = VK_NULL_HANDLE;
    }
    if (shadowSampler != VK_NULL_HANDLE) {
        vkDestroySampler(device, shadowSampler, nullptr);
        shadowSampler = VK_NULL_HANDLE;
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

    // 1. comparison sampler（硬件 PCF）
    VkSamplerCreateInfo sci{};
    sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter    = VK_FILTER_LINEAR;
    sci.minFilter    = VK_FILTER_LINEAR;
    sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.compareEnable = VK_TRUE;
    sci.compareOp     = VK_COMPARE_OP_LESS_OR_EQUAL;
    if (vkCreateSampler(device, &sci, nullptr, &shadowSampler) != VK_SUCCESS) {
        throw std::runtime_error("DeferredRenderer: failed to create shadow sampler.");
    }

    // 2. shadow set layout（material set 之后的全局 set）：2x SAMPLED_IMAGE + 1x SAMPLER
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

    shadowLayoutId = descManager->registerLayout(shadowSetLayout, /*maxSets=*/1);
    shadowSet      = descManager->allocate(shadowLayoutId);

    // sampler 对象不变，一次写好；atlas 纹理部分由 GBufferPass /
    // DeferredLightingPass 按 RGResources 契约在 Execute 中每帧重写。
    VkDescriptorImageInfo samplerInfo{};
    samplerInfo.sampler = shadowSampler;
    descManager->writeImage(shadowLayoutId, shadowSet, 2, samplerInfo,
                            VK_DESCRIPTOR_TYPE_SAMPLER);
}

// ------------------------------------------------------------------
// GBuffer sampling resources（lighting pass 的 set 1 + pipeline layout）
// ------------------------------------------------------------------

void DeferredRenderer::setupLightingResources(const FrameContext& ctx) {
    (void)ctx;

    // 1. GBuffer 采样 sampler：全屏 1:1 采样，NEAREST + CLAMP 即可
    VkSamplerCreateInfo sci{};
    sci.sType        = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter    = VK_FILTER_NEAREST;
    sci.minFilter    = VK_FILTER_NEAREST;
    sci.mipmapMode   = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    if (vkCreateSampler(device, &sci, nullptr, &gbufferSampler) != VK_SUCCESS) {
        throw std::runtime_error("DeferredRenderer: failed to create gbuffer sampler.");
    }

    // 2. gbuffer set layout（lighting pass 的 set 1，与 deferred/lighting.hlsl
    //    的 vk::binding 逐条对齐）：
    //    binding 0-3: GBuffer0-3 SAMPLED_IMAGE
    //    binding 4:   GBufferDepth  SAMPLED_IMAGE
    //    binding 5:   SAMPLER
    std::array<VkDescriptorSetLayoutBinding, 6> bindings{};
    for (uint32_t i = 0; i < 6; ++i) {
        bindings[i].binding         = i;
        bindings[i].descriptorType  = (i < 5) ? VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE
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
                                    &gbufferSetLayout) != VK_SUCCESS) {
        throw std::runtime_error("DeferredRenderer: failed to create gbuffer set layout.");
    }

    gbufferLayoutId = descManager->registerLayout(gbufferSetLayout, /*maxSets=*/1);
    gbufferSet      = descManager->allocate(gbufferLayoutId);

    // sampler 对象不变，一次写好；GBuffer 纹理部分由 DeferredLightingPass
    // 按 RGResources 契约在 Execute 中每帧重写。
    VkDescriptorImageInfo samplerInfo{};
    samplerInfo.sampler = gbufferSampler;
    descManager->writeImage(gbufferLayoutId, gbufferSet, 5, samplerInfo,
                            VK_DESCRIPTOR_TYPE_SAMPLER);

    // 3. lighting pipeline layout = [frame, gbuffer, shadow, ibl]，无 push constant
    std::array<VkDescriptorSetLayout, 4> setLayouts = {
        frameSetLayout, gbufferSetLayout, shadowSetLayout, iblRes_.setLayout,
    };
    VkPipelineLayoutCreateInfo pli{};
    pli.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pli.pSetLayouts    = setLayouts.data();
    if (vkCreatePipelineLayout(device, &pli, nullptr,
                               &lightingPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error(
            "DeferredRenderer: failed to create lighting pipeline layout.");
    }
}

// ------------------------------------------------------------------
// Default passes
// ------------------------------------------------------------------

void DeferredRenderer::createDefaultPasses(const FrameContext& ctx) {
    // ShadowPass：VS-only 深度 PSO，经 PsoManager 创建（无材质），
    // 与 ForwardRenderer::createDefaultPasses 相同。
    VkPushConstantRange pcRange{VK_SHADER_STAGE_VERTEX_BIT, 0, 80};
    VkPipelineLayoutCreateInfo pli{};
    pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount         = 0;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pcRange;
    if (vkCreatePipelineLayout(device, &pli, nullptr,
                               &shadowPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("DeferredRenderer: failed to create shadow pipeline layout.");
    }

    GraphicsPSODesc shadowPsoDesc{};
    shadowPsoDesc.shaderConfig.moduleName = "common/shadow_depth";
    shadowPsoDesc.shaderConfig.stages     = {ShaderStage::Vertex};
    shadowPsoDesc.variantKey              = {};   // 无材质类型
    shadowPsoDesc.materialHeader          = "";   // 独立编译，无材质头
    shadowPsoDesc.vertexLayoutName        = "StaticMesh";
    shadowPsoDesc.state                   = PipelineStateDesc::Default();
    shadowPsoDesc.state.depthBiasEnable   = VK_TRUE;
    shadowPsoDesc.colorCount              = 0;
    shadowPsoDesc.depthFormat             = VK_FORMAT_D32_SFLOAT;
    shadowPsoDesc.msaaSamples             = VK_SAMPLE_COUNT_1_BIT;
    shadowPsoDesc.layout                  = shadowPipelineLayout;
    shadowPsoDesc.passName                = "Shadow";

    auto shadow = std::make_unique<ShadowPass>();
    shadow->device         = device;
    shadow->pipelineLayout = shadowPipelineLayout;
    shadow->pipeline       = ctx.psoManager->getOrCreate(shadowPsoDesc);
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
    gbuffer->device          = device;
    gbuffer->depthFormat     = VK_FORMAT_D32_SFLOAT;
    gbuffer->msaaSamples     = VK_SAMPLE_COUNT_1_BIT;
    gbuffer->buildExtent     = ctx.renderExtent;
    gbuffer->shaderConfig    = MakeGBufferShaderConfig();
    // 阴影采样接线（RGResources 契约的 descriptor 由 pass 每帧重写）
    gbuffer->descManager     = descManager;
    gbuffer->shadowSet       = shadowSet;
    gbuffer->shadowLayoutId  = shadowLayoutId;
    gbuffer->shadowSetIndex  = defaultMaterialTemplate->getMaterialSetIndex() + 1;

    // DeferredLightingPass：全屏光照，
    // pipeline layout = [frame, gbuffer, shadow, ibl]。
    auto lighting = std::make_unique<DeferredLightingPass>();
    lighting->passName        = "DeferredLighting";
    lighting->pipelineLayout  = lightingPipelineLayout;
    lighting->device          = device;
    lighting->colorFormat     = ctx.swapchainFormat;
    lighting->swapchainHandle = ctx.hSwapchain;
    lighting->buildExtent     = ctx.renderExtent;
    lighting->descManager     = descManager;
    lighting->gbufferSet      = gbufferSet;
    lighting->gbufferLayoutId = gbufferLayoutId;
    lighting->gbufferSetIndex = 1;
    lighting->shadowSet       = shadowSet;
    lighting->shadowLayoutId  = shadowLayoutId;
    lighting->shadowSetIndex  = 2;
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
                                        const FrameContext& ctx) {
    for (auto& pass : passes_) {
        pass->OnBuildRenderGraph(ctx);
        rg.AddPass(pass.get());
    }
}

} // namespace engine
