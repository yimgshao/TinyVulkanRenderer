#include "common/types.hlsl"
#include "common/vertex_io.hlsl"

struct AnimatedColorMaterial
{
    float4 colorA;
    float4 colorB;
    float speed;
};

[[vk::binding(0, 1)]]
ConstantBuffer<AnimatedColorMaterial> material;

struct Varyings
{
    float4 position : SV_Position;
    float3 normal   : TEXCOORD0;
};

Varyings vertexMain(VertexInput input)
{
    Varyings output;
    float4 worldPosition = mul(gObjectData.model, float4(input.position, 1.0));
    output.position = mul(gFrameData.proj, mul(gFrameData.view, worldPosition));
    output.normal = normalize(mul((float3x3)gObjectData.model, input.normal));
    return output;
}

struct GBufferOutput
{
    float4 g0 : SV_Target0;
    float4 g1 : SV_Target1;
    float4 g2 : SV_Target2;
    float4 g3 : SV_Target3;
};

GBufferOutput fragmentMain(Varyings input)
{
    float phase = 0.5 + 0.5 * sin(gFrameData.timeSeconds * material.speed);
    float3 color = lerp(material.colorA.rgb, material.colorB.rgb, phase);

    GBufferOutput output;
    output.g0 = float4(0.0, 0.0, 0.0, 1.0);
    output.g1 = float4(normalize(input.normal) * 0.5 + 0.5, 0.0);
    output.g2 = float4(1.0, 0.0, 0.0, 0.0);
    output.g3 = float4(color, 0.0);
    return output;
}
