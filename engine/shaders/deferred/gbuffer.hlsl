// deferred/gbuffer.hlsl
//
// Deferred pipeline — GBuffer pass module（材质注入点）。
// 材质实现（evaluateVertexOffset / evaluateMaterial）由 DxcCompiler 在编译期注入，
// 本模块不 include 任何具体材质文件——新增材质无需改动本文件。
// FS 只做材质求值并写 MRT，不做任何光照计算。

#include "common/types.hlsl"
#include "common/vertex_io.hlsl"
#include "materials/material_common.hlsl"

// =============================================================================
// Vertex shader（与 forward/scene_forward.hlsl 相同的变换逻辑）
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
// Fragment shader：写 MRT GBuffer（布局见 docs/deferred_rendering_design.md §4）
//   SV_Target0 (R8G8B8A8_SRGB)       : rgb = baseColor, a = occlusion
//   SV_Target1 (A2R10G10B10_UNORM)   : rgb = 世界法线 [0,1] 编码, a = metallic
//   SV_Target2 (R8G8B8A8_UNORM)      : r = roughness, gba 预留
//   SV_Target3 (R16G16B16A16_SFLOAT) : rgb = emissive（HDR）, a 预留
// =============================================================================

struct GBufferOutput
{
    float4 g0 : SV_Target0;
    float4 g1 : SV_Target1;
    float4 g2 : SV_Target2;
    float4 g3 : SV_Target3;
};

GBufferOutput fragmentMain(VertexOutput input)
{
    // Material evaluation（PBR-neutral properties；MASK 的 discard 在材质内完成）
    MaterialInput mi;
    mi.uv       = input.uv;
    mi.worldPos = input.worldPos;
    mi.normal   = normalize(input.normal);
    mi.viewDir  = normalize(gFrameData.cameraPos.xyz - input.worldPos);
    mi.clipPos  = input.position;
    MaterialProperties mat = evaluateMaterial(mi);

    GBufferOutput output;
    output.g0 = float4(mat.albedo, mat.ao);
    output.g1 = float4(mi.normal * 0.5 + 0.5, mat.metallic);
    output.g2 = float4(mat.roughness, 0.0, 0.0, 0.0);
    output.g3 = float4(mat.emissive, 0.0);
    return output;
}
