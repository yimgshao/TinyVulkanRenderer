/**
 * Camera matching glTF 2.0 camera definition.
 *
 * glTF cameras define only projection parameters.  The camera's world-space
 * position and orientation are defined by the node transform that references
 * the camera (i.e. the camera node).
 *
 * Two types are supported:
 *   - Perspective  : aspectRatio, yfov, znear, zfar(optional, 0=infinite)
 *   - Orthographic : xmag, ymag, znear, zfar(required)
 *
 * Usage example:
 * @code
 *   engine::Camera cam;
 *   cam.type        = engine::CameraType::Perspective;
 *   cam.yfov        = glm::pi<float>() / 3.0f;
 *   cam.aspectRatio = 16.0f / 9.0f;
 *   cam.znear       = 0.1f;
 *   cam.zfar        = 100.0f;
 *
 *   // View matrix comes from the camera node's world transform
 *   glm::mat4 view = cam.getViewMatrix(cameraNodeWorldMatrix);
 *   glm::mat4 proj = cam.getProjectionMatrix();
 * @endcode
 */

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/constants.hpp>
#include <cstdint>

namespace engine {

enum class CameraType : uint32_t {
    Perspective  = 0,
    Orthographic = 1,
};

struct Camera {
    CameraType type = CameraType::Perspective;

    // Perspective parameters (used when type == Perspective)
    float aspectRatio = 0.0f;          // 0 = auto from viewport
    float yfov        = glm::pi<float>() / 4.0f;

    // Orthographic parameters (used when type == Orthographic)
    float xmag = 1.0f;
    float ymag = 1.0f;

    // World-space position (usually driven by the camera node transform)
    glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f);

    // World-space look-at target
    glm::vec3 target = glm::vec3(0.0f);

    // Common clipping planes
    float znear = 0.01f;
    float zfar  = 100.0f;   // Perspective: 0 = infinite. Orthographic: required.

    // EV100 曝光（log2 刻度，0 = 不缩放）。glTF 规范把物理光照到像素亮度
    // 的转换留给实现的相机曝光设置，由 app/UI 调节。
    float exposureEV = 0.0f;

    // Projection matrix. For perspective with aspectRatio==0, viewportAspect is used.
    glm::mat4 getProjectionMatrix(float viewportAspect = 0.0f) const;

    // View matrix from camera node world transform.
    // glTF: camera transform is defined by node; view = inverse(world).
    glm::mat4 getViewMatrix(const glm::mat4& cameraWorldTransform) const;
};

} // namespace engine