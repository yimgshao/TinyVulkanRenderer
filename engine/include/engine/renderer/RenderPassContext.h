#pragma once

#include "engine/renderer/FrameContext.h"
#include "engine/renderer/rendergraph/RGResources.h"

#include <cstddef>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>
#include <vulkan/vulkan.h>

namespace engine {

class RenderGraph;
class SceneRenderer;
struct SceneRenderPassInfo;

/// Per-execution interface exposed to a RenderPass. Scene geometry goes through
/// DrawScene; unusual workloads may use GetCommandBuffer as an explicit escape hatch.
class RenderPassContext {
public:
    VkCommandBuffer GetCommandBuffer() const { return commandBuffer_; }
    const FrameContext& GetFrame() const { return *frame_; }
    const RGResources& GetResources() const { return *resources_; }

    void DrawScene(std::string_view shaderPassTag);

    template <typename T>
    void SetPassData(const T& value) {
        static_assert(std::is_trivially_copyable_v<T>, "PassData must be trivially copyable");
        SetPassDataBytes(std::as_bytes(std::span{&value, size_t{1}}));
    }
    void ClearPassData() { passData_.clear(); }

    void SetViewport(const VkViewport& viewport) const {
        vkCmdSetViewport(commandBuffer_, 0, 1, &viewport);
    }
    void SetScissor(const VkRect2D& scissor) const {
        vkCmdSetScissor(commandBuffer_, 0, 1, &scissor);
    }
    void SetDepthBias(float constantFactor, float clamp, float slopeFactor) const {
        vkCmdSetDepthBias(commandBuffer_, constantFactor, clamp, slopeFactor);
    }

private:
    friend class RenderGraph;
    friend class SceneRenderer;
    RenderPassContext(VkCommandBuffer commandBuffer, const FrameContext& frame,
                      const RGResources& resources, SceneRenderer& sceneRenderer,
                      const SceneRenderPassInfo& passInfo)
        : commandBuffer_(commandBuffer), frame_(&frame), resources_(&resources),
          sceneRenderer_(&sceneRenderer), passInfo_(&passInfo) {}

    void SetPassDataBytes(std::span<const std::byte> bytes) {
        passData_.assign(bytes.begin(), bytes.end());
    }

    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    const FrameContext* frame_ = nullptr;
    const RGResources* resources_ = nullptr;
    SceneRenderer* sceneRenderer_ = nullptr;
    const SceneRenderPassInfo* passInfo_ = nullptr;
    std::vector<std::byte> passData_;
    uint32_t drawInvocation_ = 0;
};

} // namespace engine
