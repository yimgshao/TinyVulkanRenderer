// common/shadow.hlsl
//
// Shadow map sampling functions.
// Uses GPULight shadow metadata (lightViewProj, shadowBaseLayer, shadowFaceCount, shadowBias)
// from the frame UBO (Set 0) together with the shadow atlas textures (Set 2).
#pragma once

#include "types.hlsl"

// =============================================================================
// Set 2 — Shadow atlas textures and comparison sampler
// =============================================================================

[[vk::binding(0, 2)]] Texture2DArray         ShadowAtlas_Directional;
[[vk::binding(1, 2)]] Texture2DArray         ShadowAtlas_Point;
[[vk::binding(2, 2)]] SamplerComparisonState gShadowSampler;

// =============================================================================
// Directional / Spot light shadow
// =============================================================================

float sampleShadowDirectionalPCF(float2 uv, int layer, float refDepth)
{
    int radius = gFrameData.shadowPcfRadius;
    if (radius <= 0)
    {
        return ShadowAtlas_Directional.SampleCmpLevelZero(
            gShadowSampler, float3(uv, float(layer)), refDepth);
    }

    uint width, height, layerCount;
    ShadowAtlas_Directional.GetDimensions(width, height, layerCount);
    float2 texelSize = 1.0 / float2(width, height);

    float visibility = 0.0;
    int sampleCount = 0;
    [loop]
    for (int y = -radius; y <= radius; ++y)
    {
        [loop]
        for (int x = -radius; x <= radius; ++x)
        {
            float2 sampleUV = uv + float2(x, y) * texelSize;
            visibility += ShadowAtlas_Directional.SampleCmpLevelZero(
                gShadowSampler, float3(sampleUV, float(layer)), refDepth);
            ++sampleCount;
        }
    }
    return visibility / float(sampleCount);
}

float calcShadowDirectional(float3 worldPos, GPULight light)
{
    if (light.shadowBaseLayer < 0) return 1.0;

    float4 ls  = mul(light.lightViewProj, float4(worldPos, 1.0));
    float3 ndc = ls.xyz / ls.w;
    float2 uv  = ndc.xy * 0.5 + 0.5;
    // lightViewProj uses GLM's ZO projection variants, so Vulkan NDC Z is
    // already in [0, 1]. Only XY needs the clip-space-to-UV remap above.
    float  refDepth = ndc.z;

    if (any(uv < 0.0) || any(uv > 1.0)) return 1.0;

    return sampleShadowDirectionalPCF(
        uv, light.shadowBaseLayer, refDepth - light.shadowBias);
}

// =============================================================================
// Point light shadow (cubemap 6-face mapping)
// =============================================================================

float sampleShadowPointPCF(float2 uv, int layer, float refDepth)
{
    int radius = gFrameData.shadowPcfRadius;
    if (radius <= 0)
    {
        return ShadowAtlas_Point.SampleCmpLevelZero(
            gShadowSampler, float3(uv, float(layer)), refDepth);
    }

    uint width, height, layerCount;
    ShadowAtlas_Point.GetDimensions(width, height, layerCount);
    float2 texelSize = 1.0 / float2(width, height);

    float visibility = 0.0;
    int sampleCount = 0;
    [loop]
    for (int y = -radius; y <= radius; ++y)
    {
        [loop]
        for (int x = -radius; x <= radius; ++x)
        {
            float2 sampleUV = uv + float2(x, y) * texelSize;
            visibility += ShadowAtlas_Point.SampleCmpLevelZero(
                gShadowSampler, float3(sampleUV, float(layer)), refDepth);
            ++sampleCount;
        }
    }
    return visibility / float(sampleCount);
}

float calcShadowPoint(float3 worldPos, GPULight light)
{
    if (light.shadowBaseLayer < 0) return 1.0;

    float3 L = worldPos - light.position.xyz;
    float dist = length(L);
    if (dist > light.range) return 1.0;

    float absX = abs(L.x), absY = abs(L.y), absZ = abs(L.z);
    int   face;
    float2 uv;
    float  refDist;  // 视图空间 z（朝向为负）

    // 面映射与 Light.cpp computePointCubeViewProjs 的 6 个视图矩阵逐面对齐：
    // NDC = (eye.x, eye.y) / (-eye.z)，eye.z = refDist。
    if (absX >= absY && absX >= absZ)
    {
        if (L.x > 0.0) { uv = float2( L.z,  L.y) / L.x; refDist = -L.x; face = 0; }
        else           { uv = float2( L.z, -L.y) / L.x; refDist =  L.x; face = 1; }
    }
    else if (absY >= absX && absY >= absZ)
    {
        if (L.y > 0.0) { uv = float2(-L.x, -L.z) / L.y; refDist = -L.y; face = 2; }
        else           { uv = float2( L.x, -L.z) / L.y; refDist =  L.y; face = 3; }
    }
    else
    {
        if (L.z > 0.0) { uv = float2(-L.x,  L.y) / L.z; refDist = -L.z; face = 4; }
        else           { uv = float2(-L.x, -L.y) / L.z; refDist =  L.z; face = 5; }
    }

    uv = uv * 0.5 + 0.5;

    float zNear = 0.01;
    float zFar  = light.range;
    float ndcZ  = (zFar + zNear) / (zFar - zNear)
                + (2.0 * zFar * zNear / (zFar - zNear)) / (-refDist);
    float refDepth = ndcZ * 0.5 + 0.5;

    int layer = light.shadowBaseLayer + face;
    return sampleShadowPointPCF(
        uv, layer, refDepth - light.shadowBias);
}

// =============================================================================
// Unified entry point
// =============================================================================

float calcShadow(float3 worldPos, GPULight light)
{
    if (light.shadowBaseLayer < 0) return 1.0;
    return (light.shadowFaceCount >= 6)
        ? calcShadowPoint(worldPos, light)
        : calcShadowDirectional(worldPos, light);
}
