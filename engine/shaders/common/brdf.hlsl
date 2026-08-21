// common/brdf.hlsl
//
// glTF 2.0 Appendix B metallic-roughness BRDF 实现。
// 公式逐条对照 docs/gltf 规范文档 B.3 参考实现：
//   B.3.2  specular_brdf = G·D / (4·NdotL·NdotV)
//          D: Trowbridge-Reitz/GGX，G: Smith 分离遮蔽
//   B.3.3  diffuse_brdf  = color / π（Lambertian）
//   B.3.4  Fresnel       = f0 + (1-f0)(1-|V·H|)^5（Schlick）
//   B.3.5  material      = mix(dielectric_brdf, metal_brdf, metallic)
//   α = roughness²（规范 B.2 定义）
#pragma once

#include "materials/material_common.hlsl"

#define PBR_PI 3.14159265359

// B.3.2 GGX/Trowbridge-Reitz 分布项
float D_GGX(float NdotH, float alpha2)
{
    float denom = NdotH * NdotH * (alpha2 - 1.0) + 1.0;
    return alpha2 / (PBR_PI * denom * denom);
}

// B.3.2 Smith 分离遮蔽-阴影函数（单侧）
float G1_Smith(float NdotX, float alpha2)
{
    return 2.0 * NdotX / (NdotX + sqrt(alpha2 + (1.0 - alpha2) * NdotX * NdotX));
}

/**
 * glTF metallic-roughness BRDF（不含 NdotL 与光源 radiance）。
 * mat  材质参数（albedo/roughness/metallic）
 * N/V/L 法线、视线、光线方向（同一空间，已归一化）
 */
float3 evalPBR(MaterialProperties mat, float3 N, float3 V, float3 L)
{
    float3 H = normalize(L + V);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 1e-5);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    if (NdotL <= 0.0) return float3(0.0, 0.0, 0.0);

    float alpha  = mat.roughness * mat.roughness;   // α = roughness²
    float alpha2 = alpha * alpha;

    // 微表面镜面项（标量）
    float specBRDF = D_GGX(NdotH, alpha2) * G1_Smith(NdotL, alpha2) * G1_Smith(NdotV, alpha2)
                   / (4.0 * NdotL * NdotV);

    float t = pow(1.0 - VdotH, 5.0);

    // B.3.5 metal：F0 = baseColor
    float3 metalBRDF = specBRDF * (mat.albedo + (1.0 - mat.albedo) * t);

    // B.3.5 dielectric：diffuse 与 specular 按 Fresnel(0.04) 混合
    float frDiel = 0.04 + (1.0 - 0.04) * t;
    float3 dielectricBRDF = lerp(mat.albedo / PBR_PI, (float3)specBRDF, frDiel);

    return lerp(dielectricBRDF, metalBRDF, mat.metallic);
}

/**
 * KHR_lights_punctual 光源求值：返回 radiance（color·intensity·attenuation）
 * 与光线方向 L。衰减按扩展规范：1/d²，有 range 时乘平滑截断
 * max(min(1-(d/range)^4,1),0)。方向光（lux）无衰减。
 */
float3 punctualLightRadiance(GPULight light, float3 worldPos, out float3 L)
{
    float attenuation = 1.0;

    if (light.type == 0)
    {
        L = normalize(-light.direction.xyz);
    }
    else
    {
        float3 toLight = light.position.xyz - worldPos;
        float dist = length(toLight);
        L = toLight / max(dist, 1e-6);

        // 平方反比衰减（candela → 照度）
        attenuation = 1.0 / max(dist * dist, 1e-4);
        if (light.range > 0.0)
        {
            float x = saturate(dist / light.range);
            float cutoff = 1.0 - x * x * x * x;
            attenuation *= cutoff * cutoff;
        }

        if (light.type == 2)
        {
            // 聚光锥角衰减
            float theta = dot(L, normalize(-light.direction.xyz));
            float innerCos = cos(light.innerConeAngle);
            float outerCos = cos(light.outerConeAngle);
            float spot = saturate((theta - outerCos) / max(innerCos - outerCos, 1e-5));
            attenuation *= spot;
        }
    }

    return light.color.rgb * light.intensity * attenuation;
}
