#pragma once

#include "engine/scene/Mesh.h"
#include "engine/scene/Material.h"

#include <glm/glm.hpp>

namespace engine {
class Material;

/**
 * A single renderable object in the scene.
 *
 * References (does not own) a Mesh and a Material.  The transform is
 * the per-object model matrix, typically pushed to the shader via push
 * constant each frame.
 */
struct RenderObject {
    glm::mat4 transform = glm::mat4(1.0f);
    Mesh* mesh = nullptr;
    Material* material = nullptr;
};

} // namespace engine
