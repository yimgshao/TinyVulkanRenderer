// forward/scene_forward.hlsl
//
// Forward pipeline — vertex + fragment shader entry points.
// 材质实现（evaluateVertexOffset / evaluateMaterial）由 DxcCompiler 在编译期注入，
// 本模块不 include 任何具体材质文件——新增材质无需改动本文件。
// 布尔变体以 -D 宏注入：-DUSE_FOG=0/1, -DUSE_NORMAL_MAP=0/1, -DUSE_TONEMAP=0/1。

#include "common/types.hlsl"
#include "common/vertex_io.hlsl"
#include "common/fog.hlsl"
#include "common/shadow.hlsl"
#include "common/brdf.hlsl"
#include "common/tonemap.hlsl"
#include "materials/material_common.hlsl"

// =============================================================================
// Vertex shader
// =============================================================================

VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;

    // Material vertex-stage hook (model-space position offset)
    float3 displaced = input.position
        + evaluateVertexOffset(input.position, input.normal, input.uv);

    float4 worldPos = mul(gObjectData.model, float4(displaced, 1.0));
    output.position = mul(gFrameData.proj, mul(gFrameData.view, worldPos));
    output.worldPos = worldPos.xyz;

    output.normal = normalize(mul((float3x3)gObjectData.model, input.normal));
    output.uv     = input.uv;

    return output;
}

// =============================================================================
// Fragment shader
// =============================================================================

float4 fragmentMain(VertexOutput input) : SV_Target
{
    // 1. Material evaluation (PBR-neutral properties)
    MaterialInput mi;
    mi.uv       = input.uv;
    mi.worldPos = input.worldPos;
    mi.normal   = normalize(input.normal);
    mi.viewDir  = normalize(gFrameData.cameraPos.xyz - input.worldPos);
    mi.clipPos  = input.position;
    MaterialProperties mat = evaluateMaterial(mi);

    // 2. Normal
    float3 N = mi.normal;
#if USE_NORMAL_MAP
    N = normalize(N + float3(0.0, 0.1, 0.0));
#endif

    // 3. View direction
    float3 V = mi.viewDir;

    // 4. Ambient
    float3 result = mat.albedo * mat.ao * 0.05;

    // 5. glTF BRDF for each light（punctual 衰减内建于 radiance 计算）
    for (uint i = 0; i < gFrameData.lightCount; i++)
    {
        GPULight light = gFrameData.lights[i];
        float3 L;
        float3 radiance = punctualLightRadiance(light, input.worldPos, L);
        float shadow = calcShadow(input.worldPos, light);
        result += evalPBR(mat, N, V, L) * radiance * max(dot(N, L), 0.0) * shadow;
    }

    // 6. Emissive
    result += mat.emissive;

    // 7. Fog
#if USE_FOG
    float viewDistance = length(gFrameData.cameraPos.xyz - input.worldPos);
    result = applyFog(result, float3(0.5, 0.6, 0.7), viewDistance, 0.02);
#endif

    // 8. 曝光（EV100，线性缩放）+ tonemap（线性 HDR → [0,1]）
    result *= exp2(gFrameData.exposureEV);
    result = applyTonemap(result);

    return float4(result, mat.alpha);
}
