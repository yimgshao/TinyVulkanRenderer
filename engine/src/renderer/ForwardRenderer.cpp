#include "engine/renderer/ForwardRenderer.h"

#include "engine/VulkanContext.h"
#include "engine/renderer/rendergraph/RenderGraph.h"
#include "engine/renderer/renderpass/ForwardPass.h"
#include "engine/renderer/renderpass/ShadowPass.h"
#include "engine/pso/PsoManager.h"
#include "engine/shader/ShaderVariantManager.h"

#include <array>
#include <stdexcept>

namespace engine {

namespace {

/// 内置前向管线的 shader 模块配置。
/// 引擎核心不硬编码任何具体 pass，内置管线与用户自定义 pass 平级，
/// 各自在自己的模块里构造 ShaderModuleConfig。
ShaderModuleConfig MakeForwardShaderConfig() {
    ShaderModuleConfig c;
    c.moduleName = "forward/scene_forward";
    c.genericValueParams = {"useFog", "useNormalMap", "useTonemap"};
    return c;
}

} // anonymous namespace

// ------------------------------------------------------------------
// Lifecycle
// ------------------------------------------------------------------

void ForwardRenderer::init(const FrameContext& ctx) {
    if (!ctx.vkContext || !ctx.descManager || !ctx.variantManager) {
        throw std::runtime_error(
            "ForwardRenderer::init requires valid FrameContext");
    }

    device         = ctx.vkContext->device;
    physicalDevice = ctx.vkContext->physicalDevice;
    descManager    = ctx.descManager;
    variantManager = ctx.variantManager;
    frameSetLayout = ctx.frameSetLayout;

    // 阴影资源（set layout 必须先于材质模板创建，经 extraSetLayouts 注入）
    setupShadows(ctx);

    // Material set layout 由 MaterialTemplate 根据 shader reflection 创建
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

void ForwardRenderer::cleanup() {
    passes_.clear();

    if (defaultMaterialTemplate) {
        defaultMaterialTemplate->cleanup(device);
        defaultMaterialTemplate.reset();
    }

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
// Shadow resources
// ------------------------------------------------------------------

void ForwardRenderer::setupShadows(const FrameContext& ctx) {
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
        throw std::runtime_error("ForwardRenderer: failed to create shadow sampler.");
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
        throw std::runtime_error("ForwardRenderer: failed to create shadow set layout.");
    }

    shadowLayoutId = descManager->registerLayout(shadowSetLayout, /*maxSets=*/1);
    shadowSet      = descManager->allocate(shadowLayoutId);

    // sampler 对象不变，一次写好；atlas 纹理部分由 ForwardPass 按 RGResources
    // 契约在 Execute 中每帧重写。
    VkDescriptorImageInfo samplerInfo{};
    samplerInfo.sampler = shadowSampler;
    descManager->writeImage(shadowLayoutId, shadowSet, 2, samplerInfo,
                            VK_DESCRIPTOR_TYPE_SAMPLER);
}

// ------------------------------------------------------------------
// Default passes
// ------------------------------------------------------------------

void ForwardRenderer::createDefaultPasses(const FrameContext& ctx) {
    auto fwd = std::make_unique<ForwardPass>();
    fwd->passName        = "Forward";
    fwd->pipelineLayout  = defaultMaterialTemplate->getPipelineLayout();
    fwd->device          = device;
    fwd->depthFormat     = VK_FORMAT_D32_SFLOAT;
    fwd->msaaSamples     = VK_SAMPLE_COUNT_1_BIT;
    fwd->swapchainHandle = ctx.hSwapchain;
    fwd->buildExtent     = ctx.renderExtent;
    fwd->colorFormat     = ctx.swapchainFormat;
    fwd->shaderConfig    = MakeForwardShaderConfig();
    // passParams 整节灌入：配置缺失时不设任何值，由 shader 默认行为接管
    if (rendererCfg_.has("passParams")) {
        for (const auto& [k, v] : rendererCfg_.section("passParams").values()) {
            if (auto b = std::get_if<bool>(&v)) {
                fwd->passParams.set(k, *b);
            } else if (auto d = std::get_if<double>(&v)) {
                fwd->passParams.set(k, static_cast<float>(*d));
            }
        }
    }
    // tonemap 是 renderer 级开关（两条管线统一的配置键），显式设置
    fwd->passParams.set("useTonemap", rendererCfg_.getBool("tonemap", true));
    // 阴影采样接线（RGResources 契约的 descriptor 由 pass 每帧重写）
    fwd->descManager     = descManager;
    fwd->shadowSet       = shadowSet;
    fwd->shadowLayoutId  = shadowLayoutId;
    fwd->shadowSetIndex  = defaultMaterialTemplate->getMaterialSetIndex() + 1;

    // ShadowPass：VS-only 深度 PSO，经 PsoManager 创建（无材质）
    VkPushConstantRange pcRange{VK_SHADER_STAGE_VERTEX_BIT, 0, 80};
    VkPipelineLayoutCreateInfo pli{};
    pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount         = 0;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pcRange;
    if (vkCreatePipelineLayout(device, &pli, nullptr,
                               &shadowPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("ForwardRenderer: failed to create shadow pipeline layout.");
    }

    GraphicsPSODesc psoDesc{};
    psoDesc.shaderConfig.moduleName = "common/shadow_depth";
    psoDesc.shaderConfig.stages     = {ShaderStage::Vertex};
    psoDesc.variantKey              = {};   // 无材质类型
    psoDesc.materialHeader          = "";   // 独立编译，无材质头
    psoDesc.vertexLayoutName        = "StaticMesh";
    psoDesc.state                   = PipelineStateDesc::Default();
    psoDesc.state.depthBiasEnable   = VK_TRUE;
    psoDesc.colorCount              = 0;
    psoDesc.depthFormat             = VK_FORMAT_D32_SFLOAT;
    psoDesc.msaaSamples             = VK_SAMPLE_COUNT_1_BIT;
    psoDesc.layout                  = shadowPipelineLayout;
    psoDesc.passName                = "Shadow";

    auto shadow = std::make_unique<ShadowPass>();
    shadow->device         = device;
    shadow->pipelineLayout = shadowPipelineLayout;
    shadow->pipeline       = ctx.psoManager->getOrCreate(psoDesc);
    shadow->depthFormat    = VK_FORMAT_D32_SFLOAT;
    shadow->directionalRes = rendererCfg_.getInt("shadow.directionalRes", 2048);
    shadow->pointRes       = rendererCfg_.getInt("shadow.pointRes", 512);
    shadow->maxDirectionalLights = rendererCfg_.getInt("shadow.maxDirectionalLights", 4);
    shadow->maxPointLights       = rendererCfg_.getInt("shadow.maxPointLights", 4);

    // 执行顺序由 RenderGraph 依赖推导（Forward 读 atlas ⇒ Shadow 先执行），
    // 这里保持声明顺序：Shadow 在 Forward 之前插入。
    passes_.insert(passes_.begin(), std::move(shadow));
    passes_.insert(passes_.begin() + 1, std::move(fwd));
}

// ------------------------------------------------------------------
// RenderGraph hook
// ------------------------------------------------------------------

void ForwardRenderer::buildRenderGraph(RenderGraph& rg,
                                       const FrameContext& ctx) {
    for (auto& pass : passes_) {
        pass->OnBuildRenderGraph(ctx);
        rg.AddPass(pass.get());
    }
}

} // namespace engine
