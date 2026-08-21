/**
 * Mesh geometry with vertex/index GPU buffers.
 *
 * Owns CPU-side vertices/indices during loading, uploads them to
 * device-local VkBuffer/VkDeviceMemory, and then releases the CPU vectors.
 * Provides bind() and drawIndexed() for command buffer recording.
 *
 * Usage example:
 * @code
 *   engine::Mesh chair;
 *   chair.loadFromObj("chair.obj");      // fills CPU vertices/indices
 *   chair.upload(dev, phys, pool, queue); // creates GPU buffers, frees CPU vectors
 *
 *   // Per frame
 *   chair.bind(cmd);
 *   chair.drawIndexed(cmd);
 *
 *   // Teardown
 *   chair.cleanup(dev);
 * @endcode
 */

#pragma once

#include "engine/scene/VertexLayout.h"

#include <vulkan/vulkan.h>
#include <vk_mem_alloc.h>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtx/hash.hpp>

#include <vector>
#include <string>

namespace engine {

/// 默认 Static 顶点 CPU 结构。布局必须与 VertexLayoutId::Static 注册项一致：
///   loc 0: pos      (vec3)   offset 0
///   loc 1: normal   (vec3)   offset 12
///   loc 2: texCoord (vec2)   offset 24
///   stride: 32
/// 新增顶点格式时另起结构体并通过 VertexLayoutRegistry::Register 注册。
struct Vertex {
    glm::vec3 pos;
    glm::vec3 normal;
    glm::vec2 texCoord;

    bool operator==(const Vertex& other) const {
        return pos == other.pos && normal == other.normal &&
               texCoord == other.texCoord;
    }
};

class Mesh {
public:
    void loadFromObj(const std::string& path);
    void setData(std::vector<Vertex> verts, std::vector<uint32_t> inds);
    void upload(VkDevice device, VkPhysicalDevice physicalDevice,
                VkCommandPool commandPool, VkQueue graphicsQueue);
    void cleanup(VkDevice device);

    void bind(VkCommandBuffer cmd) const;
    void drawIndexed(VkCommandBuffer cmd) const;

    uint32_t       getIndexCount() const { return indexCount; }
    VertexLayoutId getLayoutId()   const { return layoutId; }
    void setLayoutId(VertexLayoutId id)  { layoutId = id; }

private:
    std::vector<Vertex>   vertices;
    std::vector<uint32_t> indices;
    uint32_t              indexCount = 0;
    VertexLayoutId        layoutId   = VertexLayoutId::Static;

    VkBuffer      vertexBuffer     = VK_NULL_HANDLE;
    VmaAllocation vertexAllocation = VK_NULL_HANDLE;
    VkBuffer      indexBuffer      = VK_NULL_HANDLE;
    VmaAllocation indexAllocation  = VK_NULL_HANDLE;
};

} // namespace engine

namespace std {
template <> struct hash<engine::Vertex> {
    size_t operator()(engine::Vertex const& vertex) const {
        size_t h = hash<glm::vec3>()(vertex.pos);
        h ^= hash<glm::vec3>()(vertex.normal) << 1;
        h ^= hash<glm::vec2>()(vertex.texCoord) << 2;
        return h;
    }
};
} // namespace std
