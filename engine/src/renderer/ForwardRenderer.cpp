#include "engine/renderer/ForwardRenderer.h"

#include "engine/VulkanContext.h"
#include "engine/renderer/rendergraph/RenderGraph.h"
#include "engine/renderer/renderpass/ForwardPass.h"
#include "engine/renderer/renderpass/ShadowPass.h"
#include "engine/pso/PsoManager.h"
#include "engine/shader/ShaderVariantManager.h"

#include <algorithm>
#include <array>
#include <iterator>
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

void ForwardRenderer::validateNewPass(const IRenderPass* pass) const {
    if (!pass) {
        throw std::runtime_error("ForwardRenderer: cannot add a null render pass.");
    }
    if (pass->passName.empty()) {
        throw std::runtime_error("ForwardRenderer: render pass name cannot be empty.");
    }
    const auto duplicate = std::find_if(
        passes_.begin(), passes_.end(), [&](const auto& existing) {
            return existing->passName == pass->passName;
        });
    if (duplicate != passes_.end()) {
        throw std::runtime_error("ForwardRenderer: duplicate render pass name '" +
                                 pass->passName + "'.");
    }
}

void ForwardRenderer::addPass(std::unique_ptr<IRenderPass> pass) {
    validateNewPass(pass.get());
    passes_.push_back(std::move(pass));
}

void ForwardRenderer::insertPassBefore(
    const std::string& targetPassName,
    std::unique_ptr<IRenderPass> pass) {
    validateNewPass(pass.get());
    const auto target = std::find_if(
        passes_.begin(), passes_.end(), [&](const auto& existing) {
            return existing->passName == targetPassName;
        });
    if (target == passes_.end()) {
        throw std::runtime_error("ForwardRenderer: cannot insert pass '" +
                                 pass->passName + "' before unknown pass '" +
                                 targetPassName + "'.");
    }

    IRenderPass* insertedPass = pass.get();
    IRenderPass* targetPass = target->get();
    passes_.insert(target, std::move(pass));
    orderConstraints_.push_back({insertedPass, targetPass});
}

void ForwardRenderer::insertPassAfter(
    const std::string& targetPassName,
    std::unique_ptr<IRenderPass> pass) {
    validateNewPass(pass.get());
    const auto target = std::find_if(
        passes_.begin(), passes_.end(), [&](const auto& existing) {
            return existing->passName == targetPassName;
        });
    if (target == passes_.end()) {
        throw std::runtime_error("ForwardRenderer: cannot insert pass '" +
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
    orderConstraints_.clear();
    passes_.clear();

    if (defaultMaterialTemplate) {
        defaultMaterialTemplate->cleanup(device);
        defaultMaterialTemplate.reset();
    }

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
// Shadow resources
// ------------------------------------------------------------------

void ForwardRenderer::setupShadows(const FrameContext& ctx) {
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
        throw std::runtime_error("ForwardRenderer: failed to create shadow set layout.");
    }

}

// ------------------------------------------------------------------
// Default passes
// ------------------------------------------------------------------

void ForwardRenderer::createDefaultPasses(const FrameContext& ctx) {
    auto fwd = std::make_unique<ForwardPass>();
    fwd->passName        = "Forward";
    fwd->pipelineLayout  = defaultMaterialTemplate->getPipelineLayout();
    fwd->depthFormat     = VK_FORMAT_D32_SFLOAT;
    fwd->msaaSamples     = VK_SAMPLE_COUNT_1_BIT;
    fwd->shaderConfig    = MakeForwardShaderConfig();
    fwd->materialHeader  = defaultMaterialTemplate->getMaterialHeader();
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
    // ShadowPass：VS-only 深度 pipeline layout；Shader/PSO 由 RenderGraph 创建。
    VkPushConstantRange pcRange{VK_SHADER_STAGE_VERTEX_BIT, 0, 96};
    VkPipelineLayoutCreateInfo pli{};
    pli.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pli.setLayoutCount         = 0;
    pli.pushConstantRangeCount = 1;
    pli.pPushConstantRanges    = &pcRange;
    if (vkCreatePipelineLayout(device, &pli, nullptr,
                               &shadowPipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error("ForwardRenderer: failed to create shadow pipeline layout.");
    }

    auto shadow = std::make_unique<ShadowPass>();
    shadow->pipelineLayout = shadowPipelineLayout;
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
