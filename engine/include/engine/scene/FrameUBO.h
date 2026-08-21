#pragma once

#include "engine/scene/Light.h"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>

#include <cstdint>

namespace engine {

/**
 * 全局每帧 UBO。
 *
 * 内存布局必须与 scene_forward.hlsl 中的 PerFrameData 完全对齐（std140 兼容）。
 * 任何字段调整都需同步：
 *   1) 此结构体定义
 *   2) HLSL `PerFrameData`（common/types.hlsl）
 *   3) `IRenderer::writeFrameUBO` 的默认实现
 *   4) `RenderModule` 中的 UBO buffer 大小（自动取 sizeof(FrameUBO)）
 *
 * 自定义渲染管线如需扩展每帧字段，可通过重写
 * `IRenderer::writeFrameUBO` 并写到 dst 起始位置；
 * 至少要保持 `FrameUBO` 头部布局一致，因为 RenderModule 的 frame set 描述符
 * 范围按 sizeof(FrameUBO) 注册。
 */
struct alignas(16) FrameUBO {
    alignas(16) glm::mat4 view;
    alignas(16) glm::mat4 proj;
    alignas(16) glm::mat4 invViewProj;  // inverse(proj * view)，延迟管线由深度重建世界坐标用
    alignas(16) glm::vec4 cameraPos;
    uint32_t              lightCount;
    float                 exposureEV = 0.0f;  // EV100 曝光（log2 刻度）
    GPULight              lights[MAX_LIGHTS];
};

} // namespace engine
