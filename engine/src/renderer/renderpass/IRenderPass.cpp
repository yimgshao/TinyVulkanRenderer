#include "engine/renderer/renderpass/IRenderPass.h"

#include "engine/pso/PsoManager.h"
#include "engine/renderer/rendergraph/RenderGraph.h"

namespace engine {

VkPipeline IRenderPass::BindShaderVariant(
    VkCommandBuffer cmd,
    const ShaderVariantKey& key,
    const ShaderParamSet& materialParams,
    const std::string& materialHeader) const {
    if (!pipelineRuntime_ || !pipelineRuntime_->psoManager) {
        return VK_NULL_HANDLE;
    }

    GraphicsPSODesc desc = pipelineRuntime_->baseDesc;
    desc.variantKey      = key;
    desc.materialParams  = materialParams;
    if (!materialHeader.empty()) {
        desc.materialHeader = materialHeader;
    }

    VkPipeline pipeline = pipelineRuntime_->psoManager->getOrCreate(desc);
    if (pipeline != VK_NULL_HANDLE) {
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    }
    return pipeline;
}

VkPipelineLayout IRenderPass::GetPipelineLayout() const {
    return pipelineRuntime_ ? pipelineRuntime_->baseDesc.layout : VK_NULL_HANDLE;
}

} // namespace engine
