// engine/src/renderer/rendergraph/RenderGraph.cpp
#include "engine/renderer/rendergraph/RenderGraph.h"
#include "engine/VulkanUtils.h"

#include <array>
#include <iostream>
#include <optional>
#include <stdexcept>

namespace engine {

// ------------------------------------------------------------------
// RenderGraphBuilder method implementations (tightly coupled with RenderGraph)
// ------------------------------------------------------------------

RGTextureHandle RenderGraphBuilder::CreateTexture(const std::string& name, const RGTextureDesc& desc) {
    if (!owner) return kInvalidRGTextureHandle;
    RGTextureHandle handle = owner->AllocTextureHandle();
    owner->RegisterTexture(handle, name, desc);
    createdTextures.push_back(handle);
    return handle;
}

void RenderGraphBuilder::WriteColor(RGTextureHandle handle, const AttachmentDesc& desc) {
    colorOutputs.push_back({handle, desc});
}

void RenderGraphBuilder::WriteDepth(RGTextureHandle handle, const AttachmentDesc& desc) {
    depthOutputs.push_back({handle, desc});
}

void RenderGraphBuilder::ReadTexture(RGTextureHandle handle, VkPipelineStageFlags2 stage, VkAccessFlags2 access) {
    reads.push_back({handle, stage, access});
}

RGTextureHandle RenderGraphBuilder::GetTexture(const std::string& name) const {
    if (!owner) return kInvalidRGTextureHandle;
    auto it = owner->nameToHandle.find(name);
    if (it != owner->nameToHandle.end()) {
        return it->second;
    }
    return kInvalidRGTextureHandle;
}

// ------------------------------------------------------------------
// Internal helpers
// ------------------------------------------------------------------

static VkImageAspectFlags GetAspectFlags(VkFormat format) {
    switch (format) {
        case VK_FORMAT_D16_UNORM:
        case VK_FORMAT_X8_D24_UNORM_PACK32:
        case VK_FORMAT_D32_SFLOAT:
            return VK_IMAGE_ASPECT_DEPTH_BIT;
        case VK_FORMAT_S8_UINT:
            return VK_IMAGE_ASPECT_STENCIL_BIT;
        case VK_FORMAT_D16_UNORM_S8_UINT:
        case VK_FORMAT_D24_UNORM_S8_UINT:
        case VK_FORMAT_D32_SFLOAT_S8_UINT:
            return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
        default:
            return VK_IMAGE_ASPECT_COLOR_BIT;
    }
}

// ------------------------------------------------------------------
// RenderGraph
// ------------------------------------------------------------------

void RenderGraph::Init(VulkanContext* ctx) {
    context = ctx;
    // 预留 [0] 作为 sentinel
    if (textureInfos.empty()) {
        textureInfos.emplace_back();
        physicalResources.emplace_back();
        resourceStates.emplace_back();
    }
}

void RenderGraph::Cleanup() {
    FreePhysicalResources();
    passes.clear();
    executionOrder_.clear();
    adjacency_.clear();
    inDegree_.clear();
    textureInfos.clear();
    physicalResources.clear();
    resourceStates.clear();
    nameToHandle.clear();
    nextHandle = 1;
    barrierScratch.clear();
    colorAttachmentScratch.clear();
    presentBarrierScratch.clear();
    context = nullptr;
}

RGTextureHandle RenderGraph::AllocTextureHandle() {
    return nextHandle++;
}

void RenderGraph::EnsureCapacity(RGTextureHandle handle) {
    size_t needed = static_cast<size_t>(handle) + 1;
    if (textureInfos.size() < needed) {
        textureInfos.resize(needed);
        physicalResources.resize(needed);
        resourceStates.resize(needed);
    }
}

void RenderGraph::RegisterTexture(RGTextureHandle handle, const std::string& name, const RGTextureDesc& desc) {
    EnsureCapacity(handle);
    nameToHandle[name] = handle;
    textureInfos[handle] = {name, desc};
}

RGTextureHandle RenderGraph::ImportTexture(const std::string& name, VkImage image, VkImageView view,
                                           const RGTextureDesc& desc, RGImportPolicy policy) {
    auto it = nameToHandle.find(name);
    if (it != nameToHandle.end()) {
        return it->second;
    }

    RGTextureHandle handle = AllocTextureHandle();
    RegisterTexture(handle, name, desc);

    PhysicalResource res;
    res.image        = image;
    res.imageView    = view;
    res.imported     = true;
    res.importPolicy = policy;
    physicalResources[handle] = res;

    return handle;
}

void RenderGraph::UpdateImportedTexture(RGTextureHandle handle, VkImage image, VkImageView view) {
    if (handle == kInvalidRGTextureHandle || handle >= physicalResources.size()) return;
    auto& res = physicalResources[handle];
    if (!res.imported) return;
    res.image = image;
    res.imageView = view;
}

VkImageView RenderGraph::GetPhysicalImageView(RGTextureHandle handle) const {
    if (handle == kInvalidRGTextureHandle || handle >= physicalResources.size()) {
        return VK_NULL_HANDLE;
    }
    return physicalResources[handle].imageView;
}

VkImage RenderGraph::GetPhysicalImage(RGTextureHandle handle) const {
    if (handle == kInvalidRGTextureHandle || handle >= physicalResources.size()) {
        return VK_NULL_HANDLE;
    }
    return physicalResources[handle].image;
}

const RGTextureDesc& RenderGraph::GetTextureDesc(RGTextureHandle handle) const {
    static const RGTextureDesc kInvalidDesc{};
    if (handle == kInvalidRGTextureHandle || handle >= textureInfos.size()) {
        return kInvalidDesc;
    }
    return textureInfos[handle].desc;
}

// ------------------------------------------------------------------
// RGResources：Execute 阶段物理资源只读访问器（代理 RenderGraph 查询）
// ------------------------------------------------------------------

VkImageView RGResources::GetImageView(RGTextureHandle handle) const {
    return graph ? graph->GetPhysicalImageView(handle) : VK_NULL_HANDLE;
}

VkImage RGResources::GetImage(RGTextureHandle handle) const {
    return graph ? graph->GetPhysicalImage(handle) : VK_NULL_HANDLE;
}

const RGTextureDesc& RGResources::GetDesc(RGTextureHandle handle) const {
    static const RGTextureDesc kInvalidDesc{};
    return graph ? graph->GetTextureDesc(handle) : kInvalidDesc;
}

void RenderGraph::AddPass(IRenderPass* pass) {
    if (!pass || !pass->enabled) return;

    RGPassNode node;
    node.pass = pass;
    node.builder.SetOwner(this);
    node.pass->Setup(node.builder);
    passes.push_back(std::move(node));
}

void RenderGraph::Compile() {
    FreePhysicalResources();
    AllocatePhysicalResources();
    BuildDependencyGraph();
}

void RenderGraph::AllocatePhysicalResources() {
    for (RGTextureHandle h = 1; h < textureInfos.size(); ++h) {
        const RGTextureInfo& info = textureInfos[h];
        if (info.name.empty()) continue;  // 已被释放或未使用

        PhysicalResource& res = physicalResources[h];
        if (res.imported) continue;
        if (res.image != VK_NULL_HANDLE) continue;  // 已分配

        VkImage image = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        createImageVMA(info.desc.width, info.desc.height, info.desc.arrayLayers,
                       info.desc.mipLevels, info.desc.sampleCount, info.desc.format,
                       VK_IMAGE_TILING_OPTIMAL, info.desc.usage,
                       VMA_MEMORY_USAGE_GPU_ONLY, image, allocation);

        VkImageView view = createImageView(context->device, image, info.desc.format,
                                           GetAspectFlags(info.desc.format),
                                           info.desc.mipLevels, info.desc.arrayLayers);

        res.image      = image;
        res.imageView  = view;
        res.allocation = allocation;
        res.imported   = false;
    }
}

void RenderGraph::FreePhysicalResources() {
    VkDevice device = context ? context->device : VK_NULL_HANDLE;
    for (auto& res : physicalResources) {
        if (res.imported) continue;
        if (res.imageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, res.imageView, nullptr);
            res.imageView = VK_NULL_HANDLE;
        }
        if (res.image != VK_NULL_HANDLE && res.allocation != VK_NULL_HANDLE) {
            vmaDestroyImage(g_vmaAllocator, res.image, res.allocation);
            res.image = VK_NULL_HANDLE;
            res.allocation = VK_NULL_HANDLE;
        }
    }
    // 释放后 layout 跟踪需要重置（下次重新过渡）
    for (auto& s : resourceStates) s = {};
}

// ------------------------------------------------------------------
// 资源依赖推导 + 拓扑排序
// ------------------------------------------------------------------

void RenderGraph::BuildDependencyGraph() {
    const int n = static_cast<int>(passes.size());
    adjacency_.assign(n, {});
    inDegree_.assign(n, 0);

    // Per-resource lastProducer：上一个触碰该资源的 pass 索引
    std::vector<int> lastProducer(textureInfos.size(), -1);

    auto addEdge = [&](int from, int to) {
        if (from < 0 || to < 0 || from == to) return;
        for (int existing : adjacency_[from]) {
            if (existing == to) return;  // 已存在，去重
        }
        adjacency_[from].push_back(to);
        inDegree_[to]++;
    };

    auto touchResource = [&](int passIdx, RGTextureHandle handle) {
        if (handle == kInvalidRGTextureHandle || handle >= textureInfos.size()) return;
        int prev = lastProducer[handle];
        addEdge(prev, passIdx);
        lastProducer[handle] = passIdx;
    };

    for (int pi = 0; pi < n; ++pi) {
        const auto& b = passes[pi].builder;
        for (const auto& r : b.GetReads())        touchResource(pi, r.handle);
        for (const auto& c : b.GetColorOutputs()) touchResource(pi, c.handle);
        for (const auto& d : b.GetDepthOutputs()) touchResource(pi, d.handle);
    }

    TopologicalSort();
}

void RenderGraph::TopologicalSort() {
    const int n = static_cast<int>(passes.size());
    executionOrder_.clear();
    executionOrder_.reserve(n);

    std::queue<int> q;
    for (int i = 0; i < n; ++i)
        if (inDegree_[i] == 0) q.push(i);

    while (!q.empty()) {
        int u = q.front(); q.pop();
        executionOrder_.push_back(u);
        for (int v : adjacency_[u])
            if (--inDegree_[v] == 0) q.push(v);
    }

    if (static_cast<int>(executionOrder_.size()) != n) {
        std::cerr << "[RenderGraph] Cycle detected in resource dependency graph! "
                  << "Falling back to insertion order." << std::endl;
        executionOrder_.clear();
        for (int i = 0; i < n; ++i) executionOrder_.push_back(i);
    }
}

// ------------------------------------------------------------------
// Barrier insertion: 跟踪 per-resource (layout, stage, access)，统一发 barrier2。
// ------------------------------------------------------------------

void RenderGraph::InsertBarriers(VkCommandBuffer cmd, const RGPassNode& node) {
    barrierScratch.clear();

    auto addBarrier = [&](RGTextureHandle handle, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
                          VkImageLayout newLayout) {
        if (handle == kInvalidRGTextureHandle || handle >= physicalResources.size()) return;
        const PhysicalResource& phys = physicalResources[handle];
        if (phys.image == VK_NULL_HANDLE) return;

        const RGTextureInfo& info = textureInfos[handle];
        ResourceState& prev = resourceStates[handle];

        VkPipelineStageFlags2 srcStage = prev.stage;
        VkAccessFlags2        srcAccess = prev.access;
        if (prev.layout == VK_IMAGE_LAYOUT_UNDEFINED) {
            srcStage  = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            srcAccess = VK_ACCESS_2_NONE;
        }

        VkImageMemoryBarrier2 barrier{};
        barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask                = srcStage;
        barrier.srcAccessMask               = srcAccess;
        barrier.dstStageMask                = dstStage;
        barrier.dstAccessMask               = dstAccess;
        barrier.oldLayout                   = prev.layout;
        barrier.newLayout                   = newLayout;
        barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                       = phys.image;
        barrier.subresourceRange.aspectMask = GetAspectFlags(info.desc.format);
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = info.desc.mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = info.desc.arrayLayers;

        barrierScratch.push_back(barrier);

        prev.layout = newLayout;
        prev.stage  = dstStage;
        prev.access = dstAccess;
    };

    for (const auto& c : node.builder.GetColorOutputs()) {
        addBarrier(c.handle,
                   VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
    }
    for (const auto& d : node.builder.GetDepthOutputs()) {
        addBarrier(d.handle,
                   VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                   VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL);
    }
    for (const auto& r : node.builder.GetReads()) {
        VkImageLayout readLayout = VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL;
        if (r.handle != kInvalidRGTextureHandle && r.handle < textureInfos.size()) {
            const VkImageAspectFlags aspect = GetAspectFlags(textureInfos[r.handle].desc.format);
            if (aspect & VK_IMAGE_ASPECT_DEPTH_BIT) {
                readLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
            }
        }
        addBarrier(r.handle, r.stage, r.access, readLayout);
    }

    if (!barrierScratch.empty()) {
        VkDependencyInfo dep{};
        dep.sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount  = static_cast<uint32_t>(barrierScratch.size());
        dep.pImageMemoryBarriers     = barrierScratch.data();
        vkCmdPipelineBarrier2(cmd, &dep);
    }
}

// ------------------------------------------------------------------
// Dynamic Rendering: 根据 pass 输出组装 VkRenderingInfo
// ------------------------------------------------------------------

void RenderGraph::BeginRendering(VkCommandBuffer cmd, const RGPassNode& node) {
    VkExtent2D renderExtent = {0, 0};

    colorAttachmentScratch.clear();
    for (const auto& c : node.builder.GetColorOutputs()) {
        if (c.handle == kInvalidRGTextureHandle || c.handle >= physicalResources.size()) continue;
        const PhysicalResource& phys = physicalResources[c.handle];
        if (phys.image == VK_NULL_HANDLE) continue;
        const RGTextureInfo& info = textureInfos[c.handle];

        if (renderExtent.width == 0) {
            renderExtent.width  = info.desc.width;
            renderExtent.height = info.desc.height;
        }

        VkRenderingAttachmentInfo attach{};
        attach.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attach.imageView   = phys.imageView;
        attach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attach.loadOp      = c.attachment.loadOp;
        attach.storeOp     = c.attachment.storeOp;
        attach.clearValue  = c.attachment.clearValue;
        colorAttachmentScratch.push_back(attach);
    }

    std::optional<VkRenderingAttachmentInfo> depthAttachment;
    for (const auto& d : node.builder.GetDepthOutputs()) {
        if (d.handle == kInvalidRGTextureHandle || d.handle >= physicalResources.size()) continue;
        const PhysicalResource& phys = physicalResources[d.handle];
        if (phys.image == VK_NULL_HANDLE) continue;
        const RGTextureInfo& info = textureInfos[d.handle];

        if (renderExtent.width == 0) {
            renderExtent.width  = info.desc.width;
            renderExtent.height = info.desc.height;
        }

        VkRenderingAttachmentInfo attach{};
        attach.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        attach.imageView   = phys.imageView;
        attach.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attach.loadOp      = d.attachment.loadOp;
        attach.storeOp     = d.attachment.storeOp;
        attach.clearValue  = d.attachment.clearValue;
        depthAttachment = attach;
        break;  // 仅支持一张 depth attachment
    }

    // layerCount 取该 pass 第一个 color/depth output 的 arrayLayers；
    // 无输出时默认 1（这种情况不应发生）。
    uint32_t layerCount = 1;
    for (const auto& c : node.builder.GetColorOutputs()) {
        if (c.handle != kInvalidRGTextureHandle && c.handle < textureInfos.size()) {
            layerCount = textureInfos[c.handle].desc.arrayLayers;
            break;
        }
    }
    if (layerCount == 1) {
        for (const auto& d : node.builder.GetDepthOutputs()) {
            if (d.handle != kInvalidRGTextureHandle && d.handle < textureInfos.size()) {
                layerCount = textureInfos[d.handle].desc.arrayLayers;
                break;
            }
        }
    }

    VkRenderingInfo info{};
    info.sType                 = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.renderArea.offset     = {0, 0};
    info.renderArea.extent     = renderExtent;
    info.layerCount            = layerCount;
    info.colorAttachmentCount  = static_cast<uint32_t>(colorAttachmentScratch.size());
    info.pColorAttachments     = colorAttachmentScratch.data();
    if (depthAttachment.has_value()) {
        info.pDepthAttachment  = &depthAttachment.value();
    }

    vkCmdBeginRendering(cmd, &info);
}

// ------------------------------------------------------------------
// Present transition: 把 imported color attachment 过渡到 PRESENT_SRC_KHR
// ------------------------------------------------------------------

void RenderGraph::EmitPresentTransitions(VkCommandBuffer cmd, const RGPassNode& node) {
    presentBarrierScratch.clear();

    for (const auto& c : node.builder.GetColorOutputs()) {
        if (c.handle == kInvalidRGTextureHandle || c.handle >= physicalResources.size()) continue;
        const PhysicalResource& phys = physicalResources[c.handle];
        if (!phys.imported) continue;
        if (phys.image == VK_NULL_HANDLE) continue;

        const RGTextureInfo& info = textureInfos[c.handle];

        VkImageMemoryBarrier2 barrier{};
        barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.srcStageMask                = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask               = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask                = VK_PIPELINE_STAGE_2_NONE;
        barrier.dstAccessMask               = VK_ACCESS_2_NONE;
        barrier.oldLayout                   = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout                   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        barrier.image                       = phys.image;
        barrier.subresourceRange.aspectMask = GetAspectFlags(info.desc.format);
        barrier.subresourceRange.baseMipLevel   = 0;
        barrier.subresourceRange.levelCount     = info.desc.mipLevels;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount     = info.desc.arrayLayers;
        presentBarrierScratch.push_back(barrier);

        ResourceState& state = resourceStates[c.handle];
        state.layout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        state.stage  = VK_PIPELINE_STAGE_2_NONE;
        state.access = VK_ACCESS_2_NONE;
    }

    if (!presentBarrierScratch.empty()) {
        VkDependencyInfo dep{};
        dep.sType                   = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        dep.imageMemoryBarrierCount = static_cast<uint32_t>(presentBarrierScratch.size());
        dep.pImageMemoryBarriers    = presentBarrierScratch.data();
        vkCmdPipelineBarrier2(cmd, &dep);
    }
}

// ------------------------------------------------------------------
// PerFrame imported 资源生命周期：Execute 末尾自动重置 layout 跟踪
// ------------------------------------------------------------------

void RenderGraph::ResetPerFrameImportedStates() {
    for (RGTextureHandle h = 1; h < physicalResources.size(); ++h) {
        const PhysicalResource& phys = physicalResources[h];
        if (phys.imported && phys.importPolicy == RGImportPolicy::PerFrame) {
            resourceStates[h] = {};
        }
    }
}

// ------------------------------------------------------------------
// Execute: 按资源依赖推导的拓扑序执行
// ------------------------------------------------------------------

void RenderGraph::Execute(VkCommandBuffer cmd, const FrameContext& frame) {
    // Execute 阶段的物理资源只读访问器，逐 pass 透传。
    // 契约见 RGResources 头注释：pass 只允许在 Execute 内解析物理资源。
    const RGResources resources(this);
    for (int idx : executionOrder_) {
        auto& node = passes[idx];
        if (!node.pass || !node.pass->enabled) continue;

        InsertBarriers(cmd, node);
        BeginRendering(cmd, node);
        node.pass->Execute(cmd, frame, resources);
        vkCmdEndRendering(cmd);

        EmitPresentTransitions(cmd, node);
    }

    ResetPerFrameImportedStates();
}

} // namespace engine
