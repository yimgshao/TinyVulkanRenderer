/**
 * Light definitions matching glTF KHR_lights_punctual extension.
 *
 * Supports three light types:
 *   - Directional : infinite distance, no position, only direction
 *   - Point       : emits in all directions from a position, has range
 *   - Spot        : cone-shaped emission, has position, direction, range, angles
 *
 * GPU layout (GPULight) is std140-compatible for UBO usage.
 *
 * Usage example:
 * @code
 *   // 1. Create a directional "sun" light
 *   engine::Light sun;
 *   sun.type      = engine::LightType::Directional;
 *   sun.color     = glm::vec3(1.0f, 0.98f, 0.95f);
 *   sun.intensity = 2.5f;
 *   sun.direction = glm::normalize(glm::vec3(-1.0f, -2.0f, -1.0f));
 *
 *   // 2. Create a point light
 *   engine::Light lamp;
 *   lamp.type      = engine::LightType::Point;
 *   lamp.color     = glm::vec3(1.0f, 0.8f, 0.6f);
 *   lamp.intensity = 10.0f;
 *   lamp.position  = glm::vec3(2.0f, 3.0f, 1.0f);
 *   lamp.range     = 10.0f;   // 0 = infinite
 *
 *   // 3. Fill GPU buffer for shader
 *   engine::GPULight gpuSun;
 *   sun.fill(gpuSun);
 * @endcode
 */

#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <cstdint>
#include <vector>

namespace engine {

enum class LightType : uint32_t {
    Directional = 0,
    Point       = 1,
    Spot        = 2,
};

// CPU-side light definition (KHR_lights_punctual)
struct Light {
    LightType type        = LightType::Directional;
    glm::vec3 color       = glm::vec3(1.0f);
    float     intensity   = 1.0f;

    // Point / Spot only
    glm::vec3 position    = glm::vec3(0.0f);
    float     range       = 0.0f;          // 0 = infinite

    // Directional / Spot only
    glm::vec3 direction   = glm::vec3(0.0f, 0.0f, -1.0f);

    // Spot only (radians)
    float innerConeAngle  = 0.0f;
    float outerConeAngle  = glm::pi<float>() / 4.0f;

    // Shadow
    bool  castsShadows = false;
    float shadowBias   = 0.002f;

    void fill(struct GPULight& out) const;
};

// Shadow view/projection helpers.
class Scene;  // forward declaration（与 Scene.h 的 class 声明一致）
glm::mat4 computeDirectionalViewProj(const Light& light, const Scene* scene);
glm::mat4 computeSpotViewProj(const Light& light);
std::vector<glm::mat4> computePointCubeViewProjs(const Light& light);

/**
 * ShadowJob -- 单个投影光源的 atlas 层分配 + 视图投影矩阵。
 *
 * 由 buildShadowJobs 统一分配，ShadowPass（绘制深度）与
 * IRenderer::writeFrameUBO（填 GPU 灯光参数）共用同一份结果，
 * 保证两处的 layer 分配与矩阵永远一致。
 */
struct ShadowJob {
    uint32_t lightIndex    = 0;
    int32_t  baseLayer     = 0;
    int32_t  faceCount     = 1;                 // 1 = dir/spot, 6 = point cube
    glm::mat4 viewProjs[6] = {};                // faceCount 张有效
};

/// 遍历 lights 中所有 castsShadows 的光源，按序分配 atlas 层。
/// directional/spot 占 1 层（directional atlas），point 占 6 层（point atlas）。
std::vector<ShadowJob> buildShadowJobs(const std::vector<Light>& lights,
                                       const Scene* scene);

// std140-compatible GPU layout
// Size: 144 bytes (multiple of 16 for array alignment)
struct alignas(16) GPULight {
    uint32_t type;
    float intensity;
    float range;
    float innerConeAngle;
    glm::vec4 color;      // vec3 stored as vec4 for std140
    glm::vec4 position;   // vec3 stored as vec4
    glm::vec4 direction;  // vec3 stored as vec4
    float    outerConeAngle;
    float    shadowBias;
    int32_t  shadowBaseLayer  = -1;   // <0 表示不投影
    int32_t  shadowFaceCount  = 0;
    glm::mat4 lightViewProj;          // point 光未使用（shader 侧按 cube 面重建）
};

static_assert(sizeof(GPULight) == 144,
              "GPULight size must be 144 bytes for std140 array alignment");

constexpr uint32_t MAX_LIGHTS = 8;

} // namespace engine