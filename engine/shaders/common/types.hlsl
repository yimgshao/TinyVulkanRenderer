// common/types.hlsl
//
// Shared types and buffer bindings used across all pipelines.
// Descriptor layout:
//   Set 0, Binding 0: PerFrameData (ConstantBuffer) — per-frame camera + lights
//   Push Constant:    PerObjectData (model matrix)
#pragma once

// =============================================================================
// GPULight -- aligned with C++ std140 layout
// =============================================================================

struct GPULight
{
    uint   type;
    float  intensity;
    float  range;
    float  innerConeAngle;
    float4 color;
    float4 position;
    float4 direction;
    float  outerConeAngle;
    float  shadowBias;
    int    shadowBaseLayer;   // <0 表示不投影
    int    shadowFaceCount;   // 1 = dir/spot, 6 = point cube
    column_major float4x4 lightViewProj;  // point 光未使用（shader 侧按 cube 面重建）
};

// =============================================================================
// PerFrameData -- aligned with C++ FrameUBO
// =============================================================================

struct PerFrameData
{
    column_major float4x4 view;
    column_major float4x4 proj;
    column_major float4x4 invViewProj;  // inverse(proj * view)，延迟管线由深度重建世界坐标用
    float4   cameraPos;
    uint     lightCount;
    float    exposureEV;   // EV100 曝光，输出前 result *= exp2(exposureEV)
    GPULight lights[8];
};

// =============================================================================
// Global frame data -- explicit binding at Set 0, Binding 0
// =============================================================================

[[vk::binding(0, 0)]]
ConstantBuffer<PerFrameData> gFrameData;

// =============================================================================
// Push Constant
// =============================================================================

struct PerObjectData
{
    column_major float4x4 model;
};

[[vk::push_constant]]
ConstantBuffer<PerObjectData> gObjectData;
