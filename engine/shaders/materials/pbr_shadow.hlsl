#include "materials/pbr.hlsl"
#include "common/vertex_io.hlsl"
struct ShadowPassData { column_major float4x4 lightViewProj; int layer; int pad0; int pad1; int pad2; };
[[vk::binding(0, 2)]] ConstantBuffer<ShadowPassData> gShadowPass;
struct ShadowVertex { float4 position : SV_Position; float2 uv : TEXCOORD0; uint layer : SV_RenderTargetArrayIndex; };
ShadowVertex vertexMain(VertexInput input) {
    ShadowVertex output;
    float4 worldPosition = mul(gObjectData.model, float4(input.position + evaluateVertexOffset(input.position,input.normal,input.uv),1));
    output.position = mul(gShadowPass.lightViewProj, worldPosition);
    output.uv = input.uv;
    output.layer = gShadowPass.layer;
    return output;
}
void fragmentMain(ShadowVertex input) {
    if (gMaterial.alphaMode == 1 && baseColorMap.Sample(baseColorMapSampler,input.uv).a * gMaterial.baseColorFactor.a < gMaterial.alphaCutoff)
        discard;
}
