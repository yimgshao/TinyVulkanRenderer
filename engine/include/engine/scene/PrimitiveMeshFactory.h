#pragma once

namespace engine {

class Mesh;
class Scene;
class VulkanContext;

/**
 * Creates commonly used primitive meshes owned by a Scene.
 *
 * The returned mesh is uploaded immediately and remains owned by the Scene.
 * Additional primitive types can be added here without placing geometry
 * construction code in the application layer.
 */
class PrimitiveMeshFactory final {
public:
    PrimitiveMeshFactory() = delete;

    /// Creates a cube centered at the origin. sideLength must be positive.
    static Mesh* createCube(Scene& scene,
                            VulkanContext& context,
                            float sideLength = 2.0f);
};

} // namespace engine
