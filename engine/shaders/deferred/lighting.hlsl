// deferred/lighting.hlsl
//
// Deferred pipeline — 全屏光照 pass（独立编译，不经材质注入）。
// VS：SV_VertexID 生成全屏三角形，无顶点输入（PSO 侧 vertexLayoutName = ""）。
// FS：采样 GBuffer 重建材质参数，由深度反推世界坐标，
//     光照流程（ambient + 逐光源 evalPBR × 阴影 + emissive + 曝光）
//     与 forward/scene_forward.hlsl 保持一致。

#include "common/types.hlsl"
#include "common/shadow.hlsl"
#include "common/brdf.hlsl"
#include "common/ibl.hlsl"
#include "common/tonemap.hlsl"

// 布尔变体以 -D 宏注入：-DUSE_IBL=0/1, -DUSE_TONEMAP=0/1。
#ifndef USE_IBL
#define USE_IBL 0
#endif

// =============================================================================
// Set 1 — GBuffer 采样集（与 DeferredRenderer 的 gbuffer set layout 逐 binding 对齐）
// =============================================================================

[[vk::binding(0, 1)]] Texture2D    gGBuffer0;        // baseColor.rgb + occlusion.a（SRGB）
[[vk::binding(1, 1)]] Texture2D    gGBuffer1;        // 世界法线 [0,1] 编码.rgb + metallic.a
[[vk::binding(2, 1)]] Texture2D    gGBuffer2;        // roughness.r
[[vk::binding(3, 1)]] Texture2D    gGBuffer3;        // emissive.rgb（HDR）
[[vk::binding(4, 1)]] Texture2D    gGBufferDepth;    // GBuffer pass 深度（只读采样）
[[vk::binding(5, 1)]] SamplerState gGBufferSampler;

// =============================================================================
// Vertex shader：全屏三角形
// =============================================================================

struct FullscreenOutput
{
    float4 position : SV_Position;
    float2 uv       : TEXCOORD0;
};

FullscreenOutput vertexMain(uint vertexID : SV_VertexID)
{
    FullscreenOutput output;
    // 3 个顶点覆盖全屏：(0,0) (2,0) (0,2)，插值出的 uv 同时作为屏幕 UV
    float2 uv = float2((vertexID << 1) & 2, vertexID & 2);
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    output.uv       = uv;
    return output;
}

// =============================================================================
// Fragment shader：GBuffer 重建 + PBR 光照
// =============================================================================

float4 fragmentMain(FullscreenOutput input) : SV_Target
{
    float2 uv = input.uv;

    // 深度为 clear 值（1.0）说明无几何像素：IBL 开启时采样环境 cubemap
    // 作为天空背景（世界方向由 invViewProj 还原），否则 discard 保留 clear 色。
    float depth = gGBufferDepth.Sample(gGBufferSampler, uv).r;
    if (depth >= 1.0)
    {
#if USE_IBL
        float4 farPos4 = mul(gFrameData.invViewProj, float4(uv * 2.0 - 1.0, 1.0, 1.0));
        float3 skyDir  = normalize(farPos4.xyz / farPos4.w - gFrameData.cameraPos.xyz);
        return float4(applyTonemap(sampleEnvSky(skyDir) * exp2(gFrameData.exposureEV)), 1.0);
#else
        discard;
#endif
    }

    float4 g0 = gGBuffer0.Sample(gGBufferSampler, uv);
    float4 g1 = gGBuffer1.Sample(gGBufferSampler, uv);
    float4 g2 = gGBuffer2.Sample(gGBufferSampler, uv);
    float4 g3 = gGBuffer3.Sample(gGBufferSampler, uv);

    // 由深度反推世界坐标：ndc 与 GBuffer pass VS 输出的 clip 经同一
    // viewProj 变换，invViewProj 精确互逆（与投影矩阵深度约定无关）。
    float4 ndc       = float4(uv * 2.0 - 1.0, depth, 1.0);
    float4 worldPos4 = mul(gFrameData.invViewProj, ndc);
    float3 worldPos  = worldPos4.xyz / worldPos4.w;

    // 重建材质参数（与 gbuffer.hlsl 的 MRT 布局一一对应）
    MaterialProperties mat;
    mat.albedo    = g0.rgb;
    mat.ao        = g0.a;
    mat.metallic  = g1.a;
    mat.roughness = g2.r;
    mat.emissive  = g3.rgb;
    mat.alpha     = 1.0;

    float3 N = normalize(g1.rgb * 2.0 - 1.0);
    float3 V = normalize(gFrameData.cameraPos.xyz - worldPos);

    // 1. Ambient：USE_IBL 时为 split-sum 环境光照（含 ao），否则常数回退
#if USE_IBL
    float3 result = evaluateIBL(mat, N, V);
#else
    float3 result = mat.albedo * mat.ao * 0.05;
#endif

    // 2. glTF BRDF for each light（punctual 衰减内建于 radiance 计算）
    for (uint i = 0; i < gFrameData.lightCount; i++)
    {
        GPULight light = gFrameData.lights[i];
        float3 L;
        float3 radiance = punctualLightRadiance(light, worldPos, L);
        float shadow = calcShadow(worldPos, light);
        result += evalPBR(mat, N, V, L) * radiance * max(dot(N, L), 0.0) * shadow;
    }

    // 3. Emissive
    result += mat.emissive;

    // 4. 曝光（EV100，线性缩放）+ tonemap（线性 HDR → [0,1]，sRGB 编码由
    //    SRGB swapchain 硬件完成）
    result *= exp2(gFrameData.exposureEV);
    result = applyTonemap(result);

    return float4(result, 1.0);
}
