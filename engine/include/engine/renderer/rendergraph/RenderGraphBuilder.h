#pragma once

#include "engine/renderer/rendergraph/RGResource.h"
#include <vector>
#include <string>

namespace engine {

class RenderGraph;

struct PassColorOutput {
    RGTextureHandle handle = kInvalidRGTextureHandle;
    AttachmentDesc attachment;
};

struct PassDepthOutput {
    RGTextureHandle handle = kInvalidRGTextureHandle;
    AttachmentDesc attachment;
};

struct PassResourceRead {
    RGTextureHandle handle = kInvalidRGTextureHandle;
    VkPipelineStageFlags2 stage = VK_PIPELINE_STAGE_2_NONE;
    VkAccessFlags2 access = VK_ACCESS_2_NONE;
};

class RenderGraphBuilder {
public:
    RenderGraphBuilder() = default;

    RGTextureHandle CreateTexture(const std::string& name, const RGTextureDesc& desc);
    void WriteColor(RGTextureHandle handle, const AttachmentDesc& desc);
    void WriteDepth(RGTextureHandle handle, const AttachmentDesc& desc);
    void ReadTexture(RGTextureHandle handle, VkPipelineStageFlags2 stage, VkAccessFlags2 access);

    /// 按名字查找已创建或导入的纹理句柄（用于跨 pass 引用，如 shadow atlas）。
    RGTextureHandle GetTexture(const std::string& name) const;

    const std::vector<PassColorOutput>& GetColorOutputs() const { return colorOutputs; }
    const std::vector<PassDepthOutput>& GetDepthOutputs() const { return depthOutputs; }
    const std::vector<PassResourceRead>& GetReads() const { return reads; }
    const std::vector<RGTextureHandle>& GetCreatedTextures() const { return createdTextures; }

private:
    void SetOwner(RenderGraph* graph) { owner = graph; }

    RenderGraph* owner = nullptr;
    std::vector<PassColorOutput> colorOutputs;
    std::vector<PassDepthOutput> depthOutputs;
    std::vector<PassResourceRead> reads;
    std::vector<RGTextureHandle> createdTextures;

    friend class RenderGraph;
};

} // namespace engine
