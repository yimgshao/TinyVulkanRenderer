#include "app/interactor/FirstPersonInteractor.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>

namespace app {

namespace {

constexpr glm::vec3 kWorldUp(0.0f, 1.0f, 0.0f);
constexpr float kMinLookDistance = 0.0001f;
constexpr float kPitchLimit = glm::radians(89.0f);

bool isKeyPressed(GLFWwindow* window, int key) {
    return glfwGetKey(window, key) == GLFW_PRESS;
}

} // namespace

void FirstPersonInteractor::attach(GLFWwindow* window, engine::Camera* camera) {
    detach();

    window_ = window;
    camera_ = camera;
    if (!window_ || !camera_) return;

    defaultPosition_ = camera_->position;
    defaultTarget_ = camera_->target;
    syncAnglesFromCamera();
}

void FirstPersonInteractor::detach() {
    stopLooking();
    window_ = nullptr;
    camera_ = nullptr;
    previousSpacePressed_ = false;
}

void FirstPersonInteractor::reset() {
    if (!camera_) return;

    camera_->position = defaultPosition_;
    camera_->target = defaultTarget_;
    syncAnglesFromCamera();
}

void FirstPersonInteractor::syncAnglesFromCamera() {
    if (!camera_) return;

    const glm::vec3 view = camera_->target - camera_->position;
    lookDistance_ = glm::length(view);

    glm::vec3 direction(0.0f, 0.0f, -1.0f);
    if (lookDistance_ > kMinLookDistance) {
        direction = view / lookDistance_;
    } else {
        lookDistance_ = 1.0f;
        camera_->target = camera_->position + direction;
    }

    pitch_ = std::asin(std::clamp(direction.y, -1.0f, 1.0f));
    yaw_ = std::atan2(direction.x, -direction.z);
}

glm::vec3 FirstPersonInteractor::forward() const {
    const float cosPitch = std::cos(pitch_);
    return glm::vec3(
        cosPitch * std::sin(yaw_),
        std::sin(pitch_),
        -cosPitch * std::cos(yaw_));
}

void FirstPersonInteractor::stopLooking() {
    if (looking_ && window_) {
        glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    looking_ = false;
}

void FirstPersonInteractor::update(float deltaTime) {
    if (!window_ || !camera_) return;

    ImGuiIO& io = ImGui::GetIO();
    const bool windowFocused = glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_TRUE;
    const bool rightPressed =
        glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    const bool spacePressed = isKeyPressed(window_, GLFW_KEY_SPACE);

    if (spacePressed && !previousSpacePressed_ && windowFocused &&
        !io.WantCaptureKeyboard && !io.WantTextInput) {
        reset();
    }
    previousSpacePressed_ = spacePressed;

    const bool shouldLook = windowFocused && rightPressed && !io.WantCaptureMouse;
    if (!shouldLook) {
        stopLooking();
        return;
    }

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(window_, &mouseX, &mouseY);

    if (!looking_) {
        looking_ = true;
        lastMouseX_ = mouseX;
        lastMouseY_ = mouseY;
        glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    } else {
        const float deltaX = static_cast<float>(mouseX - lastMouseX_);
        const float deltaY = static_cast<float>(mouseY - lastMouseY_);
        lastMouseX_ = mouseX;
        lastMouseY_ = mouseY;

        yaw_ += deltaX * lookSensitivity;
        pitch_ = std::clamp(pitch_ - deltaY * lookSensitivity,
                            -kPitchLimit, kPitchLimit);
    }

    const glm::vec3 viewForward = forward();

    // Once RMB look mode has started outside ImGui, the interactor owns its
    // navigation keys until RMB is released. ImGui keyboard capture can stay
    // asserted for a focused window and must not suppress WASDQE here.
    const glm::vec3 right = glm::normalize(glm::cross(viewForward, kWorldUp));
    glm::vec3 movement(0.0f);

    if (isKeyPressed(window_, GLFW_KEY_W)) movement += viewForward;
    if (isKeyPressed(window_, GLFW_KEY_S)) movement -= viewForward;
    if (isKeyPressed(window_, GLFW_KEY_D)) movement += right;
    if (isKeyPressed(window_, GLFW_KEY_A)) movement -= right;
    if (isKeyPressed(window_, GLFW_KEY_E)) movement += kWorldUp;
    if (isKeyPressed(window_, GLFW_KEY_Q)) movement -= kWorldUp;

    const float frameTime = std::clamp(deltaTime, 0.0f, 0.1f);
    camera_->position += movement * moveSpeed * frameTime;

    camera_->target = camera_->position + viewForward * lookDistance_;
}

} // namespace app
