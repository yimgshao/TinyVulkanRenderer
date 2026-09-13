#pragma once

#include "engine/pso/PSO.h"
#include "engine/renderer/rendergraph/RenderGraphBuilder.h"
#include "engine/shader/ShaderAssetManager.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace engine {

class DescriptorSetManager;
class PsoManager;
class RenderPassContext;

struct SceneRenderPassInfo {
    size_t passIndex = 0;
    GraphicsPSODesc targets;
    const std::vector<PassResourceRead>* reads = nullptr;
    const std::vector<std::string>* comparisonSamplers = nullptr;
};

/// Shared scene draw service. It owns material-pass pipeline/layout caches and
/// records the common object/material bind + indexed-draw loop.
class SceneRenderer {
public:
    SceneRenderer() = default;
    ~SceneRenderer();
    SceneRenderer(const SceneRenderer&) = delete;
    SceneRenderer& operator=(const SceneRenderer&) = delete;

    void init(VkDevice device, DescriptorSetManager& descriptors, PsoManager& pso,
              ShaderAssetManager& assets, VkDescriptorSetLayout frameLayout);
    void cleanup();
    void drawScene(RenderPassContext& context, std::string_view shaderPassTag);

private:
    struct PassSlot {
        DescriptorSetHandle set = DescriptorSetHandle::invalid();
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };
    struct Entry {
        ShaderAsset* asset = nullptr;
        const ShaderPass* pass = nullptr;
        GraphicsPSODesc desc;
        DescriptorSetLayoutDesc passInterface;
        LayoutId passLayoutId = kInvalidLayoutId;
        uint32_t passDataBinding = UINT32_MAX;
        uint32_t passDataSize = 0;
        std::array<std::vector<PassSlot>, 2> passSlots;
    };
    struct PreparedPass {
        size_t passIndex = 0;
        std::string tag;
        std::vector<Entry> entries;
    };

    PreparedPass& getOrPrepare(const SceneRenderPassInfo& passInfo, std::string_view tag);
    PassSlot& getSlot(Entry& entry, uint32_t frameIndex, uint32_t invocation);
    void destroyEntry(Entry& entry);

    VkDevice device_ = VK_NULL_HANDLE;
    DescriptorSetManager* descriptors_ = nullptr;
    PsoManager* pso_ = nullptr;
    ShaderAssetManager* assets_ = nullptr;
    VkDescriptorSetLayout frameLayout_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkSampler comparisonSampler_ = VK_NULL_HANDLE;
    std::vector<PreparedPass> preparedPasses_;
};

} // namespace engine
