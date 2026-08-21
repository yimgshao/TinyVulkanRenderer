#include "engine/scene/Camera.h"

#include <cmath>

namespace engine {

glm::mat4 Camera::getProjectionMatrix(float viewportAspect) const {
    float aspect = aspectRatio;
    if (aspect == 0.0f) {
        aspect = viewportAspect;
        if (aspect == 0.0f) {
            aspect = 16.0f / 9.0f;
        }
    }

    if (type == CameraType::Perspective) {
        if (zfar == 0.0f) {
            // Infinite projection matrix (glTF zfar undefined for perspective)
            const float f = 1.0f / std::tan(yfov * 0.5f);
            glm::mat4 proj(0.0f);
            proj[0][0] = f / aspect;
            proj[1][1] = -f;            // Vulkan Y-flip
            proj[2][2] = 0.0f;
            proj[2][3] = -1.0f;
            proj[3][2] = znear;
            return proj;
        } else {
            // Standard perspective with finite far plane.
            // glm::perspective produces OpenGL-style clip space (Z in [-w,w]).
            // For Vulkan reverse-Z with infinite far, adjust as needed by caller.
            glm::mat4 proj = glm::perspective(yfov, aspect, znear, zfar);
            proj[1][1] *= -1.0f;        // Flip Y for Vulkan clip space
            return proj;
        }
    } else {
        // Orthographic
        glm::mat4 proj = glm::ortho(-xmag, xmag, -ymag, ymag, znear, zfar);
        proj[1][1] *= -1.0f;            // Flip Y for Vulkan clip space
        return proj;
    }
}

glm::mat4 Camera::getViewMatrix(const glm::mat4& cameraWorldTransform) const {
    return glm::inverse(cameraWorldTransform);
}

} // namespace engine