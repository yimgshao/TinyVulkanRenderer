#include "engine/renderer/IRenderer.h"

#include "engine/scene/Scene.h"
#include "engine/scene/Camera.h"
#include "engine/scene/Light.h"
#include "engine/scene/FrameUBO.h"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cstring>

namespace engine {

void IRenderer::writeFrameUBO(void* dst, const FrameContext& ctx) {
    if (!dst || !ctx.scene) return;

    FrameUBO ubo{};
    const Camera& cam = ctx.scene->getCamera();

    ubo.view = glm::lookAt(cam.position, cam.target,
                           glm::vec3(0.0f, 1.0f, 0.0f));

    const auto extent = ctx.renderExtent;
    const float aspect =
        (extent.height != 0) ? (static_cast<float>(extent.width)
                              / static_cast<float>(extent.height)) : 1.0f;
    ubo.proj = cam.getProjectionMatrix(aspect);
    // 延迟管线由深度重建世界坐标用；前向管线不读该字段，填充成本可忽略
    ubo.invViewProj = glm::inverse(ubo.proj * ubo.view);

    ubo.cameraPos = glm::vec4(cam.position, 1.0f);
    ubo.exposureEV = cam.exposureEV;

    const auto& lights = ctx.scene->getLights();
    ubo.lightCount = static_cast<uint32_t>(
        std::min(lights.size(), static_cast<size_t>(MAX_LIGHTS)));
    for (uint32_t i = 0; i < ubo.lightCount; ++i) {
        lights[i].fill(ubo.lights[i]);
    }

    // 阴影：layer 分配与矩阵与 ShadowPass 共用 buildShadowJobs，保证一致。
    const auto jobs = buildShadowJobs(lights, ctx.scene);
    for (const auto& job : jobs) {
        if (job.lightIndex >= ubo.lightCount) break;  // 光源被 MAX_LIGHTS 截断
        GPULight& gpu = ubo.lights[job.lightIndex];
        gpu.shadowBaseLayer = job.baseLayer;
        gpu.shadowFaceCount = job.faceCount;
        gpu.lightViewProj   = job.viewProjs[0];  // point 光不使用（cube 面重建）
    }

    std::memcpy(dst, &ubo, sizeof(ubo));
}

} // namespace engine
