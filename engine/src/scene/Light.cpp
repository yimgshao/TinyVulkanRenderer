#include "engine/scene/Light.h"

#include "engine/scene/Scene.h"
#include "engine/scene/Camera.h"

#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

namespace engine {

void Light::fill(GPULight& out) const {
    out.type           = static_cast<uint32_t>(type);
    out.intensity      = intensity;
    out.range          = range;
    out.innerConeAngle = innerConeAngle;
    out.color          = glm::vec4(color, 0.0f);
    out.position       = glm::vec4(position, 1.0f);
    out.direction      = glm::vec4(direction, 0.0f);
    out.outerConeAngle = outerConeAngle;
    // 阴影字段：layer 分配与矩阵由 buildShadowJobs 在 writeFrameUBO 时统一填入，
    // 此处给「不投影」的安全默认值。
    out.shadowBias       = shadowBias;
    out.shadowBaseLayer  = -1;
    out.shadowFaceCount  = 0;
    out.lightViewProj    = glm::mat4(1.0f);
}

// ------------------------------------------------------------------
// Shadow view/projection helpers
//
// 约定：光源投影使用 ZO 变体（orthoZO/perspectiveZO，NDC z∈[0,1]），
// 避免 GL 风格矩阵的近端几何被 Vulkan 裁剪体（0≤z_c≤w_c）切掉。
// 采样端 common/shadow.hlsl 使用 refDepth = ndc.z*0.5+0.5，
// 与视口深度变换（[-1,1]→[min,max] 线性映射）保持一致。
// 注意：矩阵（ZO）与重映射（*0.5+0.5）必须成对使用，单独改一侧都会出错。
// ------------------------------------------------------------------

namespace {

glm::vec3 safeUpFor(const glm::vec3& dir) {
    // 视线接近竖直时 (0,1,0) 与 dir 共线，换用 (0,0,1)
    return (std::abs(dir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f)
                                     : glm::vec3(0.0f, 1.0f, 0.0f);
}

} // anonymous namespace

glm::mat4 computeDirectionalViewProj(const Light& light, const Scene* scene) {
    // 第一版简化：以相机 target 为中心的固定半径正交盒。
    // TODO: 改为场景 AABB 驱动的紧致包围盒。
    constexpr float kRadius   = 20.0f;
    constexpr float kDistance = 40.0f;
    constexpr float kNear     = 0.1f;
    constexpr float kFar      = 80.0f;

    glm::vec3 center(0.0f);
    if (scene) center = scene->getCamera().target;

    const glm::vec3 dir = glm::normalize(light.direction);
    const glm::mat4 view = glm::lookAt(center - dir * kDistance, center,
                                       safeUpFor(dir));
    const glm::mat4 proj = glm::orthoZO(-kRadius, kRadius, -kRadius, kRadius,
                                        kNear, kFar);
    return proj * view;
}

glm::mat4 computeSpotViewProj(const Light& light) {
    const float zFar = (light.range > 0.0f) ? light.range : 25.0f;
    const glm::vec3 dir = glm::normalize(light.direction);
    const glm::mat4 view = glm::lookAt(light.position, light.position + dir,
                                       safeUpFor(dir));
    const glm::mat4 proj = glm::perspectiveZO(light.outerConeAngle * 2.0f,
                                              1.0f, 0.01f, zFar);
    return proj * view;
}

std::vector<glm::mat4> computePointCubeViewProjs(const Light& light) {
    // 面朝向与 common/shadow.hlsl calcShadowPoint 的手工面映射逐面对齐，
    // 修改任何一侧都必须同步另一侧。
    const float zFar  = (light.range > 0.0f) ? light.range : 25.0f;
    const float zNear = 0.01f;  // 与 calcShadowPoint 中的 zNear 一致
    const glm::mat4 proj = glm::perspectiveZO(glm::pi<float>() / 2.0f, 1.0f,
                                              zNear, zFar);

    static const glm::vec3 kDirs[6] = {
        { 1.0f,  0.0f,  0.0f},  // +X
        {-1.0f,  0.0f,  0.0f},  // -X
        { 0.0f,  1.0f,  0.0f},  // +Y
        { 0.0f, -1.0f,  0.0f},  // -Y
        { 0.0f,  0.0f,  1.0f},  // +Z
        { 0.0f,  0.0f, -1.0f},  // -Z
    };
    static const glm::vec3 kUps[6] = {
        { 0.0f,  1.0f,  0.0f},
        { 0.0f,  1.0f,  0.0f},
        { 0.0f,  0.0f, -1.0f},
        { 0.0f,  0.0f,  1.0f},
        { 0.0f,  1.0f,  0.0f},
        { 0.0f,  1.0f,  0.0f},
    };

    std::vector<glm::mat4> result(6);
    for (int face = 0; face < 6; ++face) {
        const glm::mat4 view = glm::lookAt(light.position,
                                           light.position + kDirs[face],
                                           kUps[face]);
        result[face] = proj * view;
    }
    return result;
}

std::vector<ShadowJob> buildShadowJobs(const std::vector<Light>& lights,
                                       const Scene* scene) {
    std::vector<ShadowJob> jobs;
    int32_t dirLayer = 0;
    int32_t ptLayer  = 0;

    for (uint32_t i = 0; i < lights.size(); ++i) {
        const Light& light = lights[i];
        if (!light.castsShadows) continue;

        if (light.type == LightType::Point) {
            // cube 阴影重建需要有限 range（calcShadowPoint 以 range 为 zFar）
            if (light.range <= 0.0f) continue;

            ShadowJob job;
            job.lightIndex = i;
            job.baseLayer  = ptLayer;
            job.faceCount  = 6;
            const auto vps = computePointCubeViewProjs(light);
            for (int f = 0; f < 6; ++f) job.viewProjs[f] = vps[f];
            jobs.push_back(job);
            ptLayer += 6;
        } else {
            ShadowJob job;
            job.lightIndex   = i;
            job.baseLayer    = dirLayer;
            job.faceCount    = 1;
            job.viewProjs[0] = (light.type == LightType::Directional)
                ? computeDirectionalViewProj(light, scene)
                : computeSpotViewProj(light);
            jobs.push_back(job);
            ++dirLayer;
        }
    }
    return jobs;
}

} // namespace engine