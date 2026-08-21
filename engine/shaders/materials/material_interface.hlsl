// materials/material_interface.hlsl
//
// 材质接口反射载体——仅用于反射材质资源接口（set 1），不参与渲染。
// 材质实现由 DxcCompiler 注入到本模块之前（见 MaterialTemplate::init）。
#include "materials/material_common.hlsl"

float4 fragmentMain(float2 uv : TEXCOORD0) : SV_Target
{
    MaterialInput mi;
    mi.uv       = uv;
    mi.worldPos = float3(0.0, 0.0, 0.0);
    mi.normal   = float3(0.0, 1.0, 0.0);
    mi.viewDir  = float3(0.0, 0.0, 1.0);
    mi.clipPos  = float4(0.0, 0.0, 0.0, 1.0);

    MaterialProperties m = evaluateMaterial(mi);
    // 必须消费全部字段，否则 dxc 会消除未用资源，
    // 导致反射出的材质 set 布局缺 binding
    return float4(m.albedo + m.emissive + m.ao + m.roughness + m.metallic, m.alpha);
}
