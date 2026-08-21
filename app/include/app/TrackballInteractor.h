#pragma once

#include "engine/scene/Camera.h"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace app {

class TrackballInteractor {
public:
    void attach(GLFWwindow* window, engine::Camera* camera);
    void detach();

    /// Call each frame after pollEvents() and before rendering.
    void update();

    void setTarget(const glm::vec3& target);
    void setDistance(float distance);
    void reset();

    float rotationSpeed = 0.8f;
    float zoomSpeed     = 0.1f;
    float minDistance   = 0.1f;
    float maxDistance   = 100.0f;

private:
    GLFWwindow*     window_  = nullptr;
    engine::Camera* camera_  = nullptr;

    glm::vec3  target_       = glm::vec3(0.0f);
    float      distance_     = 3.0f;

    glm::quat  rotationQuat_ = glm::identity<glm::quat>();

    bool  dragging_    = false;
    float lastMouseX_  = 0.0f;
    float lastMouseY_  = 0.0f;

    float scrollDelta_ = 0.0f;

    glm::vec3 screenToArcball(float x, float y, float screenW, float screenH) const;
    void updateCamera();

    static void scrollCallback(GLFWwindow* window, double dx, double dy);
};

} // namespace app
