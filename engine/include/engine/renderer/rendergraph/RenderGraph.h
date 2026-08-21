#pragma once

#include "engine/renderer/rendergraph/RGResource.h"
#include "engine/renderer/rendergraph/RGResources.h"
#include "engine/renderer/rendergraph/RenderGraphBuilder.h"
#include "engine/renderer/renderpass/IRenderPass.h"
#include "engine/renderer/FrameContext.h"
#include "engine/VulkanContext.h"
#include "engine/VulkanUtils.h"

#include <vector>
#include <memory>
#include <unordered_map>
#include <string>
#include <queue>

namespace engine {

struct RGTextureInfo {
    std::string   name;
    RGTextureDesc desc;
};

struct RGPassNode {
    IRenderPass* pass = nullptr;  // RenderGraph 不拥有 pass 所有权
    RenderGraphBuilder builder;
};

class RenderGraph {
public:
    void Init(VulkanContext* ctx);
    void Cleanup();

    void AddPass(IRenderPass* pass);
    void Compile();
    void Execute(VkCommandBuffer cmd, const FrameContext& frame);

    RGTextureHandle ImportTexture(const std::string& name,
                                  VkImage image, VkImageView view,
                                  const RGTextureDesc& desc,
                                  RGImportPolicy policy = RGImportPolicy::Persistent);
    void UpdateImportedTexture(RGTextureHandle handle, VkImage image, VkImageView view);

private:
    friend class RenderGraphBuilder;
    friend class RGResources;

    // ---- 物理资源查询：仅供 RGResources 在 Execute 阶段代理访问 ----
    // pass 不应直接调用；统一经 IRenderPass::Execute 传入的 RGResources 解析。
    VkImageView        GetPhysicalImageView(RGTextureHandle handle) const;
    VkImage            GetPhysicalImage(RGTextureHandle handle) const;
    const RGTextureDesc& GetTextureDesc(RGTextureHandle handle) const;

    RGTextureHandle AllocTextureHandle();
    void RegisterTexture(RGTextureHandle handle, const std::string& name, const RGTextureDesc& desc);
    void EnsureCapacity(RGTextureHandle handle);

    void AllocatePhysicalResources();
    void FreePhysicalResources();

    void BuildDependencyGraph();
    void TopologicalSort();

    void InsertBarriers(VkCommandBuffer cmd, const RGPassNode& node);
    void BeginRendering(VkCommandBuffer cmd, const RGPassNode& node);
    void EmitPresentTransitions(VkCommandBuffer cmd, const RGPassNode& node);
    void ResetPerFrameImportedStates();

    VulkanContext* context = nullptr;

    RGTextureHandle nextHandle = 1;
    std::unordered_map<std::string, RGTextureHandle> nameToHandle;

    struct PhysicalResource {
        VkImage        image       = VK_NULL_HANDLE;
        VkImageView    imageView   = VK_NULL_HANDLE;
        VmaAllocation  allocation  = VK_NULL_HANDLE;
        bool           imported    = false;
        RGImportPolicy importPolicy = RGImportPolicy::Persistent;
    };

    struct ResourceState {
        VkImageLayout         layout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkPipelineStageFlags2 stage  = VK_PIPELINE_STAGE_2_NONE;
        VkAccessFlags2        access = VK_ACCESS_2_NONE;
    };

    // 三个 vector 永远保持等长，按 RGTextureHandle 直接索引（[0] 保留为 sentinel）。
    std::vector<RGTextureInfo>    textureInfos;
    std::vector<PhysicalResource> physicalResources;
    std::vector<ResourceState>    resourceStates;

    std::vector<RGPassNode> passes;
    std::vector<int>        executionOrder_;   // 拓扑排序后的 pass 索引

    // 复用 scratch（避免每帧分配）
    std::vector<VkImageMemoryBarrier2>     barrierScratch;
    std::vector<VkRenderingAttachmentInfo> colorAttachmentScratch;
    std::vector<VkImageMemoryBarrier2>     presentBarrierScratch;

    // 依赖图临时数据（Compile 时构建，排序后保留到 Execute）
    std::vector<std::vector<int>> adjacency_;
    std::vector<int>              inDegree_;
};

} // namespace engine
