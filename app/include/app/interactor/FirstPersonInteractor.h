#pragma once

#include "app/interactor/IInteractor.h"

#include <glm/glm.hpp>

namespace app {

class FirstPersonInteractor final : public IInteractor {
public:
    void attach(GLFWwindow* window, engine::Camera* camera) override;
    void detach() override;
    void update(float deltaTime) override;

    void reset();

    float moveSpeed       = 3.0f;
    float lookSensitivity = 0.0025f;

private:
    GLFWwindow*     window_ = nullptr;
    engine::Camera* camera_ = nullptr;

    glm::vec3 defaultPosition_ = glm::vec3(0.0f);
    glm::vec3 defaultTarget_   = glm::vec3(0.0f, 0.0f, -1.0f);

    float yaw_          = 0.0f;
    float pitch_        = 0.0f;
    float lookDistance_ = 1.0f;

    bool   looking_              = false;
    bool   previousSpacePressed_ = false;
    double lastMouseX_           = 0.0;
    double lastMouseY_           = 0.0;

    void syncAnglesFromCamera();
    glm::vec3 forward() const;
    void stopLooking();
};

} // namespace app
