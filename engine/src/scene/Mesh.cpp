#include "engine/scene/Mesh.h"
#include "engine/VulkanUtils.h"

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>

#include <cstring>
#include <stdexcept>
#include <unordered_map>

namespace engine {

void Mesh::setData(std::vector<Vertex> verts, std::vector<uint32_t> inds) {
    vertices = std::move(verts);
    indices = std::move(inds);
}

void Mesh::loadFromObj(const std::string& path) {
    tinyobj::attrib_t attrib;
    std::vector<tinyobj::shape_t> shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                          path.c_str())) {
        throw std::runtime_error(warn + err);
    }

    vertices.clear();
    indices.clear();

    std::unordered_map<Vertex, uint32_t> uniqueVertices;

    for (const auto& shape : shapes) {
        for (const auto& idx : shape.mesh.indices) {
            Vertex v{};
            v.pos = {attrib.vertices[3 * idx.vertex_index + 0],
                     attrib.vertices[3 * idx.vertex_index + 1],
                     attrib.vertices[3 * idx.vertex_index + 2]};

            if (idx.normal_index >= 0 && !attrib.normals.empty()) {
                v.normal = {attrib.normals[3 * idx.normal_index + 0],
                            attrib.normals[3 * idx.normal_index + 1],
                            attrib.normals[3 * idx.normal_index + 2]};
            }

            if (idx.texcoord_index >= 0 && !attrib.texcoords.empty()) {
                v.texCoord = {
                    attrib.texcoords[2 * idx.texcoord_index + 0],
                    1.0f - attrib.texcoords[2 * idx.texcoord_index + 1]};
            }

            if (uniqueVertices.count(v) == 0) {
                uniqueVertices[v] =
                    static_cast<uint32_t>(vertices.size());
                vertices.push_back(v);
            }
            indices.push_back(uniqueVertices[v]);
        }
    }
}

void Mesh::upload(VkDevice device, VkPhysicalDevice physicalDevice,
                  VkCommandPool commandPool, VkQueue graphicsQueue) {
    // Vertex buffer
    VkDeviceSize bufferSize = sizeof(vertices[0]) * vertices.size();
    VkBuffer stagingBuffer;
    VmaAllocation stagingAlloc;
    createBufferVMA(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer, stagingAlloc);

    void* data;
    vmaMapMemory(g_vmaAllocator, stagingAlloc, &data);
    memcpy(data, vertices.data(), static_cast<size_t>(bufferSize));
    vmaUnmapMemory(g_vmaAllocator, stagingAlloc);

    createBufferVMA(bufferSize,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                    VMA_MEMORY_USAGE_GPU_ONLY, vertexBuffer, vertexAllocation);
    copyBuffer(device, commandPool, graphicsQueue, stagingBuffer, vertexBuffer,
               bufferSize);

    vmaDestroyBuffer(g_vmaAllocator, stagingBuffer, stagingAlloc);

    // Index buffer
    bufferSize = sizeof(indices[0]) * indices.size();
    createBufferVMA(bufferSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VMA_MEMORY_USAGE_CPU_ONLY, stagingBuffer, stagingAlloc);

    vmaMapMemory(g_vmaAllocator, stagingAlloc, &data);
    memcpy(data, indices.data(), static_cast<size_t>(bufferSize));
    vmaUnmapMemory(g_vmaAllocator, stagingAlloc);

    createBufferVMA(bufferSize,
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                    VMA_MEMORY_USAGE_GPU_ONLY, indexBuffer, indexAllocation);
    copyBuffer(device, commandPool, graphicsQueue, stagingBuffer, indexBuffer,
               bufferSize);

    vmaDestroyBuffer(g_vmaAllocator, stagingBuffer, stagingAlloc);

    indexCount = static_cast<uint32_t>(indices.size());

    std::vector<Vertex>().swap(vertices);
    std::vector<uint32_t>().swap(indices);
}

void Mesh::cleanup(VkDevice device) {
    vmaDestroyBuffer(g_vmaAllocator, indexBuffer, indexAllocation);
    vmaDestroyBuffer(g_vmaAllocator, vertexBuffer, vertexAllocation);
}

void Mesh::bind(VkCommandBuffer cmd) const {
    VkBuffer vertexBuffers[] = {vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(cmd, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(cmd, indexBuffer, 0, VK_INDEX_TYPE_UINT32);
}

void Mesh::drawIndexed(VkCommandBuffer cmd) const {
    vkCmdDrawIndexed(cmd, indexCount, 1, 0, 0, 0);
}

} // namespace engine
