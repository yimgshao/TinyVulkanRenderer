// common/ibl.hlsl
//
// IBL 环境光照（set 3，与 IBLResources 的 set layout 逐 binding 对齐）。
// split-sum 近似：漫反射 = irradiance(N)，镜面 = prefilter(R, roughness)
// × (F·lut.x + lut.y)。烘焙产物来自 scripts/bake_ibl.py（积分核与
// common/brdf.hlsl 对齐，α = roughness²）。
//
// 使用方需先包含 brdf.hlsl（MaterialProperties 定义）。
#pragma once

#ifndef MAX_REFLECTION_LOD
#define MAX_REFLECTION_LOD 4.0   // 与烘焙默认 5 mips 对齐；当前变体系统仅支持
#endif                           // bool 参数，若改烘焙 mip 数需同步此值

[[vk::binding(0, 3)]] TextureCube  gIBLIrradiance;
[[vk::binding(1, 3)]] TextureCube  gIBLPrefilter;
[[vk::binding(2, 3)]] TextureCube  gIBLEnvCube;
[[vk::binding(3, 3)]] Texture2D    gIBLBrdfLUT;
[[vk::binding(4, 3)]] SamplerState gIBLSampler;

/**
 * 环境光照求值（已含 ao）。
 * mat 材质参数；N/V 法线与视线方向（世界空间，已归一化）。
 */
float3 evaluateIBL(MaterialProperties mat, float3 N, float3 V)
{
    float3 R = reflect(-V, N);
    float NdotV = saturate(dot(N, V));

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), mat.albedo, mat.metallic);
    // 粗糙度修正的 Schlick Fresnel（UE4 惯例）
    float3 F = F0 + (max(1.0 - mat.roughness, F0) - F0) * pow(1.0 - NdotV, 5.0);
    float3 kd = (1.0 - F) * (1.0 - mat.metallic);

    float3 irradiance  = gIBLIrradiance.Sample(gIBLSampler, N).rgb;
    float3 prefiltered = gIBLPrefilter.SampleLevel(
        gIBLSampler, R, mat.roughness * MAX_REFLECTION_LOD).rgb;
    float2 brdf = gIBLBrdfLUT.Sample(
        gIBLSampler, float2(NdotV, mat.roughness)).rg;

    float3 diffuse  = irradiance * mat.albedo * kd;
    float3 specular = prefiltered * (F * brdf.x + brdf.y);
    return (diffuse + specular) * mat.ao;
}

/// 天空背景采样（线性 HDR，调用方负责乘曝光）。
float3 sampleEnvSky(float3 dir)
{
    return gIBLEnvCube.SampleLevel(gIBLSampler, dir, 0.0).rgb;
}
