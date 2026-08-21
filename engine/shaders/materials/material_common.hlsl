// materials/material_common.hlsl
//
// MaterialInput context, MaterialEvaluation result type,
// and the shared Blinn-Phong lighting function.
#pragma once

#include "common/types.hlsl"

// =============================================================================
// MaterialInput -- per-pixel context passed to evaluateMaterial.
// 新增字段只需扩展此结构体，材质函数签名保持稳定。
// =============================================================================

struct MaterialInput
{
    float2 uv;        // 纹理坐标
    float3 worldPos;  // 世界空间坐标
    float3 normal;    // 世界空间插值法线（已归一化）
    float3 viewDir;   // 指向相机的单位向量
    float4 clipPos;   // SV_Position：xy 可做屏幕 UV，z/w 为深度
};

struct MaterialProperties
{
    float3 albedo;
    float3 emissive;
    float  roughness;
    float  metallic;
    float  ao;
    float  alpha;
};

// =============================================================================
// computeBlinnPhong -- lighting for one light on one surface point
// =============================================================================

float3 computeBlinnPhong(
    MaterialProperties mat,
    float3   N,
    float3   worldPos,
    float3   viewDir,
    GPULight light)
{
    // Derive Blinn-Phong parameters from PBR material properties
    float3 diffuseColor  = mat.albedo * (1.0 - mat.metallic);
    float3 specularColor = lerp((float3)0.04, mat.albedo, mat.metallic);
    float  shininess     = lerp(2.0, 128.0, 1.0 - mat.roughness);

    float3 L;
    float attenuation = 1.0;

    if (light.type == 0)
    {
        L = normalize(-light.direction.xyz);
    }
    else if (light.type == 1)
    {
        L = light.position.xyz - worldPos;
        float dist = length(L);
        L = normalize(L);
        if (light.range > 0.0)
        {
            attenuation = saturate(1.0 - dist / light.range);
            attenuation = attenuation * attenuation;
        }
    }
    else if (light.type == 2)
    {
        L = light.position.xyz - worldPos;
        float dist = length(L);
        L = normalize(L);
        if (light.range > 0.0)
        {
            attenuation = saturate(1.0 - dist / light.range);
            attenuation = attenuation * attenuation;
        }
        float theta = dot(L, normalize(-light.direction.xyz));
        float innerCos = cos(light.innerConeAngle);
        float outerCos = cos(light.outerConeAngle);
        if (theta <= outerCos) attenuation = 0.0;
        else if (theta < innerCos)
        {
            float epsilon = innerCos - outerCos;
            attenuation *= saturate((theta - outerCos) / epsilon);
        }
    }

    float NdotL = max(dot(N, L), 0.0);
    float3 diffuse = diffuseColor * NdotL * light.color.rgb * light.intensity;

    float3 H = normalize(L + viewDir);
    float NdotH = max(dot(N, H), 0.0);
    float3 specular = specularColor * pow(NdotH, shininess) * light.color.rgb * light.intensity;

    return (diffuse + specular) * attenuation;
}
