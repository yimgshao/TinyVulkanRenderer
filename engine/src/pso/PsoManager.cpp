#include "engine/pso/PsoManager.h"

#include "engine/scene/VertexLayout.h"
#include "engine/shader/ShaderVariantManager.h"

#include <array>
#include <stdexcept>
#include <vector>

namespace engine {

void PsoManager::init(VkDevice dev, ShaderVariantManager* vm) {
    device         = dev;
    variantManager = vm;
    if (!variantManager) {
        throw std::runtime_error("PsoManager::init: variantManager is required.");
    }
}

void PsoManager::cleanup() {
    for (auto& pair : cache) {
        if (pair.second != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pair.second, nullptr);
        }
    }
    cache.clear();
    variantManager = nullptr;
    device         = VK_NULL_HANDLE;
}

VkPipeline PsoManager::getOrCreate(const GraphicsPSODesc& desc) {
    GraphicsPSOKey key;
    key.passName          = desc.passName;
    key.moduleName        = desc.shaderConfig.moduleName;
    key.vertexLayout      = desc.vertexLayoutName;
    key.materialType      = desc.variantKey.materialType;
    key.materialParamHash = desc.variantKey.materialParamHash;
    key.passParamHash     = desc.variantKey.passParamHash;
    key.state             = desc.state;
    key.depthFormat       = desc.depthFormat;
    key.msaaSamples       = desc.msaaSamples;
    key.colorCount        = std::min(desc.colorCount, GraphicsPSOKey::kMaxColorAttachments);
    for (uint32_t i = 0; i < key.colorCount; ++i) {
        key.colorFormats[i] = desc.colorFormats[i];
    }

    auto it = cache.find(key);
    if (it != cache.end()) {
        return it->second;
    }

    VkPipeline pso = createPSO(desc);
    cache[key] = pso;
    return pso;
}

VkPipeline PsoManager::createPSO(const GraphicsPSODesc& desc) {
    if (desc.layout == VK_NULL_HANDLE) {
        throw std::runtime_error("PsoManager::createPSO: desc.layout is null.");
    }

    auto bytecode = variantManager->GetOrCreateVariant(
        desc.shaderConfig, desc.variantKey, desc.materialParams,
        desc.passParams, desc.materialHeader);
    if (!bytecode) {
        throw std::runtime_error(
            "PsoManager::createPSO: failed to compile shader variant '" +
            desc.shaderConfig.moduleName + "'.");
    }

    // ---- shader stages：按编译结果动态组装 ----
    // 图形管线必须有 vertex stage；fragment 可为空（VS-only 深度管线合法）。
    struct StageSrc {
        VkShaderStageFlagBits        vkStage;
        const std::vector<uint32_t>* spirv;
    };
    std::vector<StageSrc> stageSrcs;
    for (ShaderStage stage : desc.shaderConfig.stages) {
        switch (stage) {
            case ShaderStage::Vertex:
                stageSrcs.push_back({VK_SHADER_STAGE_VERTEX_BIT, &bytecode->vertexSpirv});
                break;
            case ShaderStage::Fragment:
                stageSrcs.push_back({VK_SHADER_STAGE_FRAGMENT_BIT, &bytecode->fragmentSpirv});
                break;
            default:
                throw std::runtime_error(
                    "PsoManager::createPSO: unsupported stage in '" +
                    desc.shaderConfig.moduleName + "' (only Vertex/Fragment).");
        }
    }

    std::vector<VkShaderModule> modules;
    std::vector<VkPipelineShaderStageCreateInfo> stages;
    bool hasVertex = false;
    for (const StageSrc& src : stageSrcs) {
        if (src.spirv->empty()) continue;  // 该 stage 未参与编译（stage 子集）
        VkShaderModule module = createShaderModule(*src.spirv);
        modules.push_back(module);

        VkPipelineShaderStageCreateInfo stageInfo{};
        stageInfo.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stageInfo.stage  = src.vkStage;
        stageInfo.module = module;
        stageInfo.pName  = "main";
        stages.push_back(stageInfo);

        if (src.vkStage == VK_SHADER_STAGE_VERTEX_BIT) hasVertex = true;
    }
    if (!hasVertex) {
        throw std::runtime_error(
            "PsoManager::createPSO: graphics PSO requires a vertex stage ('" +
            desc.shaderConfig.moduleName + "').");
    }

    // ---- dynamic rendering attachment 格式 ----
    VkPipelineRenderingCreateInfo renderingCreateInfo{};
    renderingCreateInfo.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
    renderingCreateInfo.colorAttachmentCount    = desc.colorCount;
    renderingCreateInfo.pColorAttachmentFormats = desc.colorFormats;
    renderingCreateInfo.depthAttachmentFormat   = desc.depthFormat;

    // ---- 顶点输入：布局名为空 = 无顶点输入（全屏 pass）----
    const VertexLayoutDesc* vlayout = nullptr;
    if (!desc.vertexLayoutName.empty()) {
        vlayout = VertexLayoutRegistry::GetByName(desc.vertexLayoutName);
        if (!vlayout) {
            for (VkShaderModule m : modules) vkDestroyShaderModule(device, m, nullptr);
            throw std::runtime_error(
                "PsoManager::createPSO: vertex layout '" + desc.vertexLayoutName +
                "' not found in VertexLayoutRegistry.");
        }
    }

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{};
    vertexInputInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    if (vlayout) {
        vertexInputInfo.vertexBindingDescriptionCount   = 1;
        vertexInputInfo.pVertexBindingDescriptions      = &vlayout->binding;
        vertexInputInfo.vertexAttributeDescriptionCount =
            static_cast<uint32_t>(vlayout->attributes.size());
        vertexInputInfo.pVertexAttributeDescriptions    = vlayout->attributes.data();
    }

    VkPipelineInputAssemblyStateCreateInfo inputAssembly{};
    inputAssembly.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkPipelineViewportStateCreateInfo viewportState{};
    viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewportState.viewportCount = 1;
    viewportState.scissorCount  = 1;

    VkPipelineRasterizationStateCreateInfo rasterizer{};
    rasterizer.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    rasterizer.polygonMode     = desc.state.polygonMode;
    rasterizer.lineWidth       = 1.0f;
    rasterizer.cullMode        = desc.state.cullMode;
    rasterizer.frontFace       = desc.state.frontFace;
    rasterizer.depthBiasEnable = desc.state.depthBiasEnable;

    VkPipelineMultisampleStateCreateInfo multisampling{};
    multisampling.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisampling.rasterizationSamples = desc.msaaSamples;

    VkPipelineDepthStencilStateCreateInfo depthStencil{};
    depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depthStencil.depthTestEnable  = desc.state.depthTestEnable;
    depthStencil.depthWriteEnable = desc.state.depthWriteEnable;
    depthStencil.depthCompareOp   = desc.state.depthCompareOp;

    std::vector<VkPipelineColorBlendAttachmentState> blendAttachments(desc.colorCount);
    for (uint32_t i = 0; i < desc.colorCount; ++i) {
        blendAttachments[i].colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (desc.state.blendEnable && i == 0) {
            blendAttachments[i].blendEnable         = VK_TRUE;
            blendAttachments[i].srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blendAttachments[i].dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachments[i].colorBlendOp        = VK_BLEND_OP_ADD;
            blendAttachments[i].srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachments[i].dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            blendAttachments[i].alphaBlendOp        = VK_BLEND_OP_ADD;
        } else {
            blendAttachments[i].blendEnable = VK_FALSE;
        }
    }

    VkPipelineColorBlendStateCreateInfo colorBlending{};
    colorBlending.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    colorBlending.attachmentCount = static_cast<uint32_t>(blendAttachments.size());
    colorBlending.pAttachments    = blendAttachments.data();

    // CULL_MODE 为静态状态（由 PipelineStateDesc.cullMode 进 PSO），
    // 避免「每个 pass 都必须记得设置」的隐性约定导致未定义行为。
    // DEPTH_BIAS 全局动态：阴影 pass 等可按需调用 vkCmdSetDepthBias。
    std::array<VkDynamicState, 3> dynamicStates = {
        VK_DYNAMIC_STATE_VIEWPORT,
        VK_DYNAMIC_STATE_SCISSOR,
        VK_DYNAMIC_STATE_DEPTH_BIAS};
    VkPipelineDynamicStateCreateInfo dynamicState{};
    dynamicState.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates    = dynamicStates.data();

    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipelineInfo.pNext               = &renderingCreateInfo;
    pipelineInfo.stageCount          = static_cast<uint32_t>(stages.size());
    pipelineInfo.pStages             = stages.data();
    pipelineInfo.pVertexInputState   = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState      = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState   = &multisampling;
    pipelineInfo.pDepthStencilState  = &depthStencil;
    pipelineInfo.pColorBlendState    = &colorBlending;
    pipelineInfo.pDynamicState       = &dynamicState;
    pipelineInfo.layout              = desc.layout;
    pipelineInfo.renderPass          = VK_NULL_HANDLE;

    VkPipeline pso = VK_NULL_HANDLE;
    VkResult result = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1,
                                                &pipelineInfo, nullptr, &pso);
    for (VkShaderModule m : modules) {
        vkDestroyShaderModule(device, m, nullptr);
    }
    if (result != VK_SUCCESS) {
        throw std::runtime_error("PsoManager: failed to create graphics PSO!");
    }
    return pso;
}

VkShaderModule PsoManager::createShaderModule(const std::vector<uint32_t>& code) {
    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size() * sizeof(uint32_t);
    createInfo.pCode    = code.data();

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule) != VK_SUCCESS) {
        throw std::runtime_error("PsoManager: failed to create shader module!");
    }
    return shaderModule;
}

} // namespace engine
