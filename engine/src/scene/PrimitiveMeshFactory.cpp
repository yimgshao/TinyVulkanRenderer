#include "engine/scene/PrimitiveMeshFactory.h"

#include "engine/VulkanContext.h"
#include "engine/scene/Mesh.h"
#include "engine/scene/Scene.h"

#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <utility>
#include <vector>

namespace engine {

Mesh* PrimitiveMeshFactory::createCube(Scene& scene,
                                       VulkanContext& context,
                                       float sideLength) {
    if (!std::isfinite(sideLength) || sideLength <= 0.0f) {
        throw std::invalid_argument("Cube side length must be finite and positive");
    }

    const float h = sideLength * 0.5f;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    vertices.reserve(24);
    indices.reserve(36);

    const auto appendFace = [&vertices, &indices](
        const glm::vec3& v0,
        const glm::vec3& v1,
        const glm::vec3& v2,
        const glm::vec3& v3,
        const glm::vec3& normal) {
        const uint32_t base = static_cast<uint32_t>(vertices.size());
        vertices.push_back({v0, normal, {0.0f, 0.0f}});
        vertices.push_back({v1, normal, {1.0f, 0.0f}});
        vertices.push_back({v2, normal, {1.0f, 1.0f}});
        vertices.push_back({v3, normal, {0.0f, 1.0f}});
        indices.insert(indices.end(), {
            base, base + 1, base + 2,
            base, base + 2, base + 3
        });
    };

    appendFace({ h, -h, -h}, {-h, -h, -h}, {-h,  h, -h}, { h,  h, -h}, { 0,  0, -1});
    appendFace({-h, -h,  h}, { h, -h,  h}, { h,  h,  h}, {-h,  h,  h}, { 0,  0,  1});
    appendFace({-h, -h, -h}, {-h, -h,  h}, {-h,  h,  h}, {-h,  h, -h}, {-1,  0,  0});
    appendFace({ h, -h,  h}, { h, -h, -h}, { h,  h, -h}, { h,  h,  h}, { 1,  0,  0});
    appendFace({-h, -h, -h}, { h, -h, -h}, { h, -h,  h}, {-h, -h,  h}, { 0, -1,  0});
    appendFace({-h,  h, -h}, {-h,  h,  h}, { h,  h,  h}, { h,  h, -h}, { 0,  1,  0});

    auto* mesh = scene.createMesh();
    mesh->setData(std::move(vertices), std::move(indices));
    mesh->upload(context.device, context.physicalDevice,
                 context.commandPool, context.graphicsQueue);
    return mesh;
}

} // namespace engine
