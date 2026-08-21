#include "app/TrackballInteractor.h"

#include "imgui.h"

#include <cmath>
#include <algorithm>

namespace app {

static TrackballInteractor* s_active = nullptr;

void TrackballInteractor::attach(GLFWwindow* window, engine::Camera* camera) {
    window_ = window;
    camera_ = camera;
    s_active = this;
    glfwSetScrollCallback(window, scrollCallback);
}

void TrackballInteractor::detach() {
    if (window_) {
        glfwSetScrollCallback(window_, nullptr);
    }
    if (s_active == this) s_active = nullptr;
    window_ = nullptr;
    camera_ = nullptr;
}

void TrackballInteractor::setTarget(const glm::vec3& target) {
    target_ = target;
    updateCamera();
}

void TrackballInteractor::setDistance(float distance) {
    distance_ = std::clamp(distance, minDistance, maxDistance);
    updateCamera();
}

void TrackballInteractor::reset() {
    target_       = glm::vec3(0.0f);
    distance_     = 3.0f;
    rotationQuat_ = glm::identity<glm::quat>();
    updateCamera();
}

// =============================================================================
// Scroll callback
// =============================================================================

void TrackballInteractor::scrollCallback(GLFWwindow* /*window*/, double /*dx*/, double dy) {
    if (s_active) s_active->scrollDelta_ += static_cast<float>(dy);
}

// =============================================================================
// Arcball: project 2D screen coords onto a virtual unit sphere
// =============================================================================

glm::vec3 TrackballInteractor::screenToArcball(float x, float y, float screenW, float screenH) const {
    float nx =  (2.0f * x / screenW) - 1.0f;
    float ny = -(2.0f * y / screenH) + 1.0f;

    float r2 = nx * nx + ny * ny;
    float z  = 0.0f;

    if (r2 < 1.0f) {
        z = std::sqrt(1.0f - r2);
    } else {
        float r = std::sqrt(r2);
        nx /= r; ny /= r;
    }

    return glm::vec3(nx, ny, z);
}

// =============================================================================
// updateCamera
// =============================================================================

void TrackballInteractor::updateCamera() {
    if (!camera_) return;

    glm::vec3 forward = rotationQuat_ * glm::vec3(0.0f, 0.0f, -1.0f);

    camera_->position = target_ - forward * distance_;
    camera_->target   = target_;
}

// =============================================================================
// update
// =============================================================================

void TrackballInteractor::update() {
    if (!window_ || !camera_) return;

    // Don't interfere with ImGui
    ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureMouse) {
        dragging_   = false;
        scrollDelta_ = 0.0f;
        return;
    }

    // --- Scroll zoom ---
    if (scrollDelta_ != 0.0f) {
        float factor = 1.0f - scrollDelta_ * zoomSpeed * 0.1f;
        distance_ = std::clamp(distance_ * factor, minDistance, maxDistance);
        scrollDelta_ = 0.0f;
        updateCamera();
    }

    // --- Mouse drag for rotation ---
    int leftState  = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT);
    int rightState = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT);

    double cx, cy;
    glfwGetCursorPos(window_, &cx, &cy);
    float mx = static_cast<float>(cx);
    float my = static_cast<float>(cy);

    int w, h;
    glfwGetWindowSize(window_, &w, &h);
    if (w == 0 || h == 0) return;

    if (leftState == GLFW_PRESS || rightState == GLFW_PRESS) {
        if (!dragging_) {
            dragging_   = true;
            lastMouseX_ = mx;
            lastMouseY_ = my;
        } else {
            float dx = mx - lastMouseX_;
            float dy = my - lastMouseY_;

            if (dx != 0.0f || dy != 0.0f) {
                glm::vec3 v0 = screenToArcball(lastMouseX_, lastMouseY_,
                                               static_cast<float>(w), static_cast<float>(h));
                glm::vec3 v1 = screenToArcball(mx, my,
                                               static_cast<float>(w), static_cast<float>(h));

                float d = glm::clamp(glm::dot(v0, v1), -1.0f, 1.0f);
                float angle = std::acos(d) * rotationSpeed;
                glm::vec3 axis = glm::cross(v1, v0);

                if (glm::length(axis) > 0.0001f && angle > 0.0001f) {
                    glm::quat delta = glm::angleAxis(angle, glm::normalize(axis));
                    rotationQuat_ = delta * rotationQuat_;
                }

                lastMouseX_ = mx;
                lastMouseY_ = my;
            }
        }
    } else {
        dragging_ = false;
    }

    updateCamera();
}

} // namespace app
