#pragma once

#include "engine/scene/Camera.h"

#include <GLFW/glfw3.h>

namespace app {

class IInteractor {
public:
    virtual ~IInteractor() = default;

    virtual void attach(GLFWwindow* window, engine::Camera* camera) = 0;
    virtual void detach() = 0;
    virtual void update(float deltaTime) = 0;
};

} // namespace app
