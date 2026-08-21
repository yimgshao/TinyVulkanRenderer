// materials/blinn_phong.hlsl
//
// BlinnPhongMaterial — parameter block, textures, and material evaluation.
// 由 DxcCompiler 注入到 pass 模块之前编译（见 DxcCompiler::CompileVariant）。
#pragma once

#include "materials/material_common.hlsl"

struct BlinnPhongParams
{
    float4 baseColorFactor;
    float4 emissiveFactor;
    float  metallicFactor;
    float  roughnessFactor;
};

[[vk::binding(0, 1)]] ConstantBuffer<BlinnPhongParams> gMaterial;

[[vk::combinedImageSampler]] [[vk::binding(1, 1)]] Texture2D baseColorMap;
[[vk::combinedImageSampler]] [[vk::binding(1, 1)]] SamplerState baseColorMapSampler;
[[vk::combinedImageSampler]] [[vk::binding(2, 1)]] Texture2D ormMap;
[[vk::combinedImageSampler]] [[vk::binding(2, 1)]] SamplerState ormMapSampler;
[[vk::combinedImageSampler]] [[vk::binding(3, 1)]] Texture2D normalMap;
[[vk::combinedImageSampler]] [[vk::binding(3, 1)]] SamplerState normalMapSampler;
[[vk::combinedImageSampler]] [[vk::binding(4, 1)]] Texture2D emissiveMap;
[[vk::combinedImageSampler]] [[vk::binding(4, 1)]] SamplerState emissiveMapSampler;

// 顶点频率：返回模型空间位置偏移（无顶点效果时返回 0，编译器会消除开销）。
float3 evaluateVertexOffset(float3 position, float3 normal, float2 uv)
{
    return float3(0.0, 0.0, 0.0);
}

// 片元频率：材质求值。
MaterialProperties evaluateMaterial(MaterialInput input)
{
    MaterialProperties result;

    float4 texColor = baseColorMap.Sample(baseColorMapSampler, input.uv);
    result.albedo    = texColor.rgb * gMaterial.baseColorFactor.rgb;
    result.emissive  = gMaterial.emissiveFactor.rgb;
    result.alpha     = texColor.a * gMaterial.baseColorFactor.a;

    float3 orm = ormMap.Sample(ormMapSampler, input.uv).rgb;
    result.ao        = orm.r;
    result.roughness = orm.g * gMaterial.roughnessFactor;
    result.metallic  = orm.b * gMaterial.metallicFactor;

    return result;
}
