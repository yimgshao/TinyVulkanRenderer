// materials/pbr.hlsl
//
// PbrMaterial — glTF 2.0 metallic-roughness 材质。
// 参数块与 CPU 侧 MaterialParams（MaterialInstance.h）std140 顺序对齐。
// 由 DxcCompiler 注入到 pass 模块之前编译（见 DxcCompiler::CompileVariant）。
#pragma once

#include "materials/material_common.hlsl"

struct PbrParams
{
    float4 baseColorFactor;
    float4 emissiveFactor;
    float  metallicFactor;
    float  roughnessFactor;
    float  normalScale;
    float  occlusionStrength;
    float  alphaCutoff;
    uint   alphaMode;      // 0=OPAQUE 1=MASK 2=BLEND
    uint   doubleSided;
};

[[vk::binding(0, 1)]] ConstantBuffer<PbrParams> gMaterial;

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

// 片元频率：材质求值（glTF metallic-roughness 语义）。
MaterialProperties evaluateMaterial(MaterialInput input)
{
    MaterialProperties result;

    float4 texColor = baseColorMap.Sample(baseColorMapSampler, input.uv);
    result.albedo = texColor.rgb * gMaterial.baseColorFactor.rgb;
    result.alpha  = texColor.a * gMaterial.baseColorFactor.a;

    // alphaMode MASK：低于 cutoff 的片元丢弃
    if (gMaterial.alphaMode == 1 && result.alpha < gMaterial.alphaCutoff)
        discard;

    // ORM 打包约定（与 Blender 导出器的 occlusion-roughness-metallic 打包一致）：
    // R=AO G=roughness B=metallic。无 occlusionTexture 时 loader 把
    // occlusionStrength 置 0，AO 退化为 1.0。
    float3 orm = ormMap.Sample(ormMapSampler, input.uv).rgb;
    result.ao        = lerp(1.0, orm.r, gMaterial.occlusionStrength);
    result.roughness = clamp(orm.g * gMaterial.roughnessFactor, 0.0, 1.0);
    result.metallic  = clamp(orm.b * gMaterial.metallicFactor, 0.0, 1.0);

    // emissive = 贴图 × factor（glTF：贴图 sRGB，factor 为线性乘子）
    result.emissive = emissiveMap.Sample(emissiveMapSampler, input.uv).rgb
                    * gMaterial.emissiveFactor.rgb;

    return result;
}
