#pragma once

#include "engine/renderer/rendergraph/RGResource.h"
#include "engine/renderer/PipelineStateDesc.h"
#include "engine/shader/ShaderParam.h"
#include "engine/shader/ShaderVariantManager.h"
#include <vector>
#include <string>

namespace engine {

class RenderGraph;

struct PassShaderDesc {
    PassShaderDesc() {
        pipelineState.depthTestEnable  = VK_FALSE;
        pipelineState.depthWriteEnable = VK_FALSE;
        pipelineState.cullMode         = VK_CULL_MODE_NONE;
    }

    ShaderModuleConfig   shaderConfig;
    ShaderParamSet       passParams;
    PipelineStateDesc    pipelineState{};
    std::string          vertexLayout;
    VkPipelineLayout     pipelineLayout = VK_NULL_HANDLE;
    VkSampleCountFlagBits msaaSamples   = VK_SAMPLE_COUNT_1_BIT;
    bool                 declared       = false;
    bool                 autoBind       = true;
    std::vector<std::string> comparisonSamplers;
};

struct PassColorOutput {
    RGTextureHandle handle = kInvalidRGTextureHandle;
    AttachmentDesc attachment;
};

struct PassDepthOutput {
    RGTextureHandle handle = kInvalidRGTextureHandle;
    AttachmentDesc attachment;
    bool readOnly = false;
};

struct PassResourceRead {
    RGTextureHandle handle = kInvalidRGTextureHandle;
    VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
    std::string resourceName;
};

class RenderGraphBuilder {
public:
    RenderGraphBuilder() = default;

    RGTextureHandle CreateTexture(const std::string& name, const RGTextureDesc& desc);
    void WriteColor(RGTextureHandle handle, const AttachmentDesc& desc = {});
    void WriteColorPreserve(RGTextureHandle handle);
    void WriteColorPreserve(const std::string& name) { WriteColorPreserve(FindTexture(name)); }
    void WriteDepth(RGTextureHandle handle, const AttachmentDesc& desc = {});
    RGTextureHandle ReadTexture(const std::string& resourceName);
    RGTextureHandle ReadTexture(const std::string& resourceName, const std::string& shaderName);
    void ReadDepthAttachment(const std::string& resourceName);

    /// 声明本图形 pass 使用的 shader。路径相对 shader 搜索目录且不含扩展名。
    void SetShader(const std::string& moduleName);
    void SetShader(const ShaderModuleConfig& config);
    void SetPassParams(const ShaderParamSet& params);
    void SetPipelineState(const PipelineStateDesc& state);
    void SetVertexLayout(const std::string& layoutName);

    /// 内置材质 pass 使用：Pipeline 仍由 RenderGraph 统一请求和绑定，
    /// 这里只提供由材质/renderer 资源接口决定的 layout。
    void SetPipelineLayout(VkPipelineLayout layout);
    void SetMsaaSamples(VkSampleCountFlagBits samples);
    void SetAutoBindShader(bool enabled);
    void SetComparisonSampler(const std::string& name) { shaderDesc.comparisonSamplers.push_back(name); }

    const std::vector<PassColorOutput>& GetColorOutputs() const { return colorOutputs; }
    const std::vector<PassDepthOutput>& GetDepthOutputs() const { return depthOutputs; }
    const std::vector<PassResourceRead>& GetReads() const { return reads; }
    const std::vector<RGTextureHandle>& GetCreatedTextures() const { return createdTextures; }
    const PassShaderDesc& GetShaderDesc() const { return shaderDesc; }

private:
    RGTextureHandle FindTexture(const std::string& name) const;
    void SetOwner(RenderGraph* graph) { owner = graph; }

    RenderGraph* owner = nullptr;
    std::vector<PassColorOutput> colorOutputs;
    std::vector<PassDepthOutput> depthOutputs;
    std::vector<PassResourceRead> reads;
    std::vector<RGTextureHandle> createdTextures;
    PassShaderDesc shaderDesc;

    friend class RenderGraph;
};

} // namespace engine
