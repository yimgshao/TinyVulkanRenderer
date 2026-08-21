// common/fog.hlsl
//
// Fog utilities shared across pipelines.
#pragma once

float3 applyFog(float3 color, float3 fogColor, float distance, float fogDensity)
{
    float fogFactor = exp(-distance * distance * fogDensity * fogDensity);
    fogFactor = clamp(fogFactor, 0.0, 1.0);
    return lerp(fogColor, color, fogFactor);
}
