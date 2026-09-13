#include "common/types.hlsl"
#include "common/tonemap.hlsl"
[[vk::combinedImageSampler]] [[vk::binding(0,1)]] Texture2D SceneColor;
[[vk::combinedImageSampler]] [[vk::binding(0,1)]] SamplerState SceneColorSampler;
struct Varyings { float4 position : SV_Position; float2 uv : TEXCOORD0; };
Varyings vertexMain(uint id : SV_VertexID) {
    Varyings result;
    result.uv = float2((id << 1) & 2, id & 2);
    result.position = float4(result.uv * 2 - 1, 0, 1);
    return result;
}
float4 fragmentMain(Varyings input) : SV_Target0 {
    float3 hdr = SceneColor.Sample(SceneColorSampler, input.uv).rgb;
    return float4(applyTonemap(hdr * exp2(gFrameData.exposureEV)), 1);
}
