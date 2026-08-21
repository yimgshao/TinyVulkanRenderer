// common/tonemap.hlsl
//
// Tonemapping：线性 HDR → 显示范围 [0,1]。
// 顺序约定：曝光（exp2(EV)）→ applyTonemap → sRGB 编码（SRGB swapchain 由硬件完成）。
//
// 曲线为 Narkowicz 的 ACES 拟合（UE/Unity 默认档位同族近似）。
#pragma once

// 变体宏由 genericValueParams 注入（注意：参数仅支持 bool，命名避开连续大写）。
// 未注入时默认开启。
#ifndef USE_TONEMAP
#define USE_TONEMAP 1
#endif

/// ACES filmic 拟合（Narkowicz）：x*(2.51x+0.03)/(x*(2.43x+0.59)+0.14)
float3 ACESFilm(float3 x)
{
    return saturate(x * (2.51 * x + 0.03) / (x * (2.43 * x + 0.59) + 0.14));
}

/// 曝光之后的统一出口：USE_TONEMAP=0 时原样返回（便于 A/B 对比）。
float3 applyTonemap(float3 color)
{
#if USE_TONEMAP
    return ACESFilm(color);
#else
    return color;
#endif
}
