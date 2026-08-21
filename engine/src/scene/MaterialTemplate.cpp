#include "engine/scene/MaterialTemplate.h"
#include "engine/pso/PsoManager.h"
#include "engine/shader/ShaderVariantManager.h"

#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace engine {

void MaterialTemplate::init(VkDevice dev, VkPhysicalDevice phys,
                            const MaterialTemplateCreateInfo& createInfo) {
    device         = dev;
    physicalDevice = phys;
    descManager    = createInfo.descManager;
    psoManager     = createInfo.psoManager;
    alphaMode      = createInfo.alphaMode;
    variantManager = createInfo.variantManager;
    materialType   = createInfo.materialType;
    materialHeader = createInfo.materialHeader;

    if (!variantManager) {
        throw std::runtime_error(
            "MaterialTemplate::init: variantManager is required.");
    }
    if (!psoManager) {
        throw std::runtime_error(
            "MaterialTemplate::init: psoManager is required.");
    }

    // 1. 编译材质接口模块 -> 取反射
    // 材质资源接口（set 1）声明在材质头文件中，与任何 pass 无关；
    // 用一个最小的接口模块（仅 fragmentMain 调用 evaluateMaterial）做反射载体。
    ShaderModuleConfig interfaceCfg;
    interfaceCfg.moduleName = "materials/material_interface";
    interfaceCfg.stages     = {ShaderStage::Fragment};

    ShaderParamSet emptyParams;
    ShaderVariantKey defaultKey;
    defaultKey.materialType = materialType;
    auto defaultVariant = variantManager->GetOrCreateVariant(
        interfaceCfg, defaultKey, emptyParams, emptyParams, materialHeader);
    if (!defaultVariant) {
        throw std::runtime_error(
            "MaterialTemplate::init: failed to compile material interface variant.");
    }
    defaultReflection = &defaultVariant->reflection;

    // 2. 找 material 所在 set：跳过 set 0，取第一个非空 set
    const DescriptorSetLayoutDesc* matSet = nullptr;
    for (const auto& s : defaultReflection->sets) {
        if (s.setIndex == 0) continue;
        if (s.bindings.empty()) continue;
        matSet = &s;
        materialSetIndex = s.setIndex;
        break;
    }
    if (!matSet) {
        throw std::runtime_error(
            "MaterialTemplate::init: no material set found in shader reflection.");
    }

    // 3. 用反射构造 material descriptor set layout
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    bindings.reserve(matSet->bindings.size());
    for (const auto& b : matSet->bindings) {
        VkDescriptorSetLayoutBinding lb{};
        lb.binding         = b.binding;
        lb.descriptorType  = b.descriptorType;
        lb.descriptorCount = b.descriptorCount;
        lb.stageFlags      = b.stageFlags;
        bindings.push_back(lb);
    }

    VkDescriptorSetLayoutCreateInfo matLayoutInfo{};
    matLayoutInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    matLayoutInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
    matLayoutInfo.bindingCount = static_cast<uint32_t>(bindings.size());
    matLayoutInfo.pBindings    = bindings.data();

    if (vkCreateDescriptorSetLayout(device, &matLayoutInfo, nullptr,
                                    &setLayout) != VK_SUCCESS) {
        throw std::runtime_error(
            "MaterialTemplate::init: failed to create material descriptor set layout.");
    }

    // 3.5 注册到 DescriptorSetManager
    if (descManager) {
        materialLayoutId = descManager->registerLayout(setLayout, /*maxSets=*/1000);
    }

    // 4. 填补中间 set 占位（material set index > 1 时，pipeline layout 需要连续的 set）
    if (materialSetIndex > 1) {
        VkDescriptorSetLayoutCreateInfo dummyInfo{};
        dummyInfo.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dummyInfo.flags        = VK_DESCRIPTOR_SET_LAYOUT_CREATE_DESCRIPTOR_BUFFER_BIT_EXT;
        dummyInfo.bindingCount = 0;
        dummyInfo.pBindings    = nullptr;
        if (vkCreateDescriptorSetLayout(device, &dummyInfo, nullptr,
                                        &dummySetLayout) != VK_SUCCESS) {
            throw std::runtime_error(
                "MaterialTemplate::init: failed to create dummy descriptor set layout.");
        }
    }

    // 5. pipelineLayout = [frame, dummy*, material, extraSetLayouts...]
    // extraSetLayouts 是 renderer 级全局 set 的注入点（见 CreateInfo 注释）。
    std::vector<VkDescriptorSetLayout> setLayouts(
        materialSetIndex + 1 + createInfo.extraSetLayouts.size(), VK_NULL_HANDLE);
    setLayouts[0] = createInfo.frameSetLayout;
    for (uint32_t i = 1; i < materialSetIndex; ++i) {
        setLayouts[i] = dummySetLayout;
    }
    setLayouts[materialSetIndex] = setLayout;
    for (size_t i = 0; i < createInfo.extraSetLayouts.size(); ++i) {
        setLayouts[materialSetIndex + 1 + i] = createInfo.extraSetLayouts[i];
    }

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType          = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
    pipelineLayoutInfo.pSetLayouts    = setLayouts.data();

    VkPushConstantRange pcRange{};
    if (defaultReflection->pushConstant) {
        pcRange = *defaultReflection->pushConstant;
        pipelineLayoutInfo.pushConstantRangeCount = 1;
        pipelineLayoutInfo.pPushConstantRanges    = &pcRange;
    }

    if (vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr,
                               &pipelineLayout) != VK_SUCCESS) {
        throw std::runtime_error(
            "MaterialTemplate::init: failed to create pipeline layout.");
    }
}

VkPipeline MaterialTemplate::getOrCreatePipeline(
    VkDevice dev, const ShaderModuleConfig& shaderConfig,
    const std::string& passName,
    const PipelineStateDesc& state,
    const ShaderVariantKey& shaderVariant,
    const ShaderParamSet& materialParams,
    const ShaderParamSet& passParams,
    uint32_t colorAttachmentCount, const VkFormat* pColorFormats,
    VkFormat depthFormat, VkSampleCountFlagBits msaaSamples) {
    (void)dev;  // device 由 PsoManager 持有

    GraphicsPSODesc desc{};
    desc.shaderConfig     = shaderConfig;
    desc.variantKey       = shaderVariant;
    desc.materialParams   = materialParams;
    desc.passParams       = passParams;
    desc.materialHeader   = materialHeader;
    desc.vertexLayoutName = "StaticMesh";
    desc.state            = state;
    // 透明材质不写深度（材质语义，由材质侧在填 desc 时修正）
    if (alphaMode == AlphaMode::Blend) {
        desc.state.depthWriteEnable = VK_FALSE;
    }
    desc.colorCount       = std::min(colorAttachmentCount,
                                     GraphicsPSODesc::kMaxColorAttachments);
    for (uint32_t i = 0; i < desc.colorCount; ++i) {
        desc.colorFormats[i] = pColorFormats[i];
    }
    desc.depthFormat      = depthFormat;
    desc.msaaSamples      = msaaSamples;
    desc.layout           = pipelineLayout;
    desc.passName         = passName;

    return psoManager->getOrCreate(desc);
}

void MaterialTemplate::cleanup(VkDevice dev) {
    // PSO 由引擎级 PsoManager 统一拥有与销毁，此处不再处理。

    if (pipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(dev, pipelineLayout, nullptr);
        pipelineLayout = VK_NULL_HANDLE;
    }
    if (dummySetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, dummySetLayout, nullptr);
        dummySetLayout = VK_NULL_HANDLE;
    }
    if (setLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(dev, setLayout, nullptr);
        setLayout = VK_NULL_HANDLE;
    }
    materialSetIndex  = 0;
    materialLayoutId  = kInvalidLayoutId;
    defaultReflection = nullptr;
    descManager       = nullptr;
    psoManager        = nullptr;
    physicalDevice    = VK_NULL_HANDLE;
    device            = VK_NULL_HANDLE;
}

} // namespace engine
