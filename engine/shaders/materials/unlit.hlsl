// materials/unlit.hlsl
//
// UnlitMaterial — flat color/texture, no lighting.
// 由 DxcCompiler 注入到 pass 模块之前编译（见 DxcCompiler::CompileVariant）。
#pragma once

#include "materials/material_common.hlsl"

struct UnlitParams
{
    float4 tintColor;
};

[[vk::binding(0, 1)]] ConstantBuffer<UnlitParams> gMaterial;

[[vk::combinedImageSampler]] [[vk::binding(1, 1)]] Texture2D colorMap;
[[vk::combinedImageSampler]] [[vk::binding(1, 1)]] SamplerState colorMapSampler;

// 顶点频率：返回模型空间位置偏移（无顶点效果时返回 0，编译器会消除开销）。
float3 evaluateVertexOffset(float3 position, float3 normal, float2 uv)
{
    return float3(0.0, 0.0, 0.0);
}

// 片元频率：材质求值。
MaterialProperties evaluateMaterial(MaterialInput input)
{
    MaterialProperties result;
    float4 texColor = colorMap.Sample(colorMapSampler, input.uv);
    result.albedo    = texColor.rgb * gMaterial.tintColor.rgb;
    result.roughness = 1.0;
    result.metallic  = 0.0;
    result.ao        = 1.0;
    result.emissive  = result.albedo;
    result.alpha     = gMaterial.tintColor.a;
    return result;
}
