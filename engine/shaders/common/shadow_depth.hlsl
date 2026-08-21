// common/shadow_depth.hlsl
//
// Depth-only vertex shader for shadow map generation.
// Uses SV_RenderTargetArrayIndex to route draws to atlas layers.
// CPU pre-multiplies lightViewProj * model into a single mvp matrix.

#include "vertex_io.hlsl"

struct ShadowPushConstants
{
    column_major float4x4 mvp;   // lightViewProj * model (pre-multiplied by CPU)
    int  layerIndex;             // target atlas layer
    int3 _pad;
};

[[vk::push_constant]] ConstantBuffer<ShadowPushConstants> gShadowPush;

struct VSOutput
{
    float4 position : SV_Position;
    uint   layer    : SV_RenderTargetArrayIndex;
};

VSOutput vertexMain(VertexInput input)
{
    VSOutput output;
    output.position = mul(gShadowPush.mvp, float4(input.position, 1.0));
    output.layer    = gShadowPush.layerIndex;
    return output;
}
