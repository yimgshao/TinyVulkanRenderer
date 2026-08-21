#pragma once

#include "engine/scene/Mesh.h"
#include "engine/scene/MaterialInstance.h"

#include <glm/glm.hpp>

namespace engine {

/**
 * A single renderable object in the scene.
 *
 * References (does not own) a Mesh and a MaterialInstance.  The transform is
 * the per-object model matrix, typically pushed to the shader via push
 * constant each frame.
 */
struct RenderObject {
    glm::mat4 transform = glm::mat4(1.0f);
    Mesh* mesh = nullptr;
    MaterialInstance* material = nullptr;
};

} // namespace engine
