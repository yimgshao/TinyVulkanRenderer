#!/usr/bin/env python3
"""IBL 离线烘焙：equirectangular 环境贴图（.hdr / .exr）→ 引擎 IBL 纹理集（.ibl）。

产出（每个环境一个输出目录）：
    env_cube.ibl    equirect→cubemap 投影（天空背景），RGBA16F
    irradiance.ibl  余弦加权半球卷积（漫反射），RGBA16F
    prefilter.ibl   GGX 重要性采样预滤波（镜面，mip=roughness），RGBA16F
    brdf_lut.ibl    BRDF 积分 LUT（scale+bias，与环境无关），RG16F
    meta.json       烘焙参数与 cubemap 方向约定
    previews/*.png  tonemap 预览图（肉眼验收）

积分核与引擎 engine/shaders/common/brdf.hlsl 对齐：
    α = roughness²，D_GGX 用 α²，Smith G1（sqrt 内 α²），Schlick (1-VdotH)^5

.ibl 容器格式（little-endian）：
    offset 0   magic      char[4] "IBL1"
             4   version    u32 = 1
             8   kind       u32   1=cubemap RGBA16F, 2=2D RG16F
             12  width      u32
             16  height     u32
             20  layers     u32   cubemap 恒 6，2D 恒 1
             24  mipLevels  u32
             28  reserved   u32 = 0
             32  逐 mip：u64 dataSize + float16 原始数据
                 （每 mip 内按 layer 顺序，layer 内 H×W×C 连续）

用法：
    python scripts/bake_ibl.py <input.hdr|input.exr> [--name NAME]
        [--out DIR] [--env-size 512] [--irr-size 32]
        [--prefilter-size 128] [--prefilter-mips 5] [--lut-size 512]
        [--samples 1024] [--skip-lut] [--force]

    默认输出到输入文件所在目录（--out 可另行指定）。
"""

import argparse
import json
import os
import struct
import sys
import time
from pathlib import Path

# 须在 import cv2 之前设置：启用 OpenEXR 解码（读取 .exr 环境贴图）
os.environ.setdefault("OPENCV_IO_ENABLE_OPENEXR", "1")

import numpy as np
import cv2

# Windows GBK 控制台：统一按 UTF-8 输出，避免中文乱码与特殊字符编码异常
for stream in (sys.stdout, sys.stderr):
    try:
        stream.reconfigure(encoding="utf-8", errors="replace")
    except Exception:
        pass

PROJECT_ROOT = Path(__file__).resolve().parent.parent

PI = np.pi
IBL_MAGIC = b"IBL1"
IBL_KIND_CUBE_RGBA16F = 1
IBL_KIND_2D_RG16F = 2


# ------------------------------------------------------------------
# HDR 读取（cv2，BGR→RGB，float32）
# ------------------------------------------------------------------

def load_hdr(path: Path) -> np.ndarray:
    img = cv2.imread(str(path), cv2.IMREAD_UNCHANGED)
    if img is None:
        sys.exit(f"[bake_ibl] 无法读取 HDR 文件: {path}")
    if img.ndim == 2:
        img = img[..., None].repeat(3, axis=2)
    img = cv2.cvtColor(img, cv2.COLOR_BGR2RGB).astype(np.float32)
    print(f"[bake_ibl] 载入 {path.name}: {img.shape[1]}x{img.shape[0]}, "
          f"动态范围 [{img.min():.4f}, {img.max():.2f}]")
    return img


# ------------------------------------------------------------------
# 公共数学
# ------------------------------------------------------------------

def cube_face_dirs(face: int, size: int) -> np.ndarray:
    """cubeConvention=1：层序 +X,-X,+Y,-Y,+Z,-Z；像素 (u,v) → 单位方向。"""
    c = (np.arange(size, dtype=np.float32) + 0.5) / size
    u, v = np.meshgrid(c, c)  # u 沿列，v 沿行
    s, t = 2.0 * u - 1.0, 2.0 * v - 1.0
    one = np.ones_like(s)
    if face == 0:    d = np.stack([one, -t, -s], -1)   # +X
    elif face == 1:  d = np.stack([-one, -t, s], -1)   # -X
    elif face == 2:  d = np.stack([s, one, t], -1)     # +Y
    elif face == 3:  d = np.stack([s, -one, -t], -1)   # -Y
    elif face == 4:  d = np.stack([s, -t, one], -1)    # +Z
    else:            d = np.stack([-s, -t, -one], -1)  # -Z
    return d / np.linalg.norm(d, axis=-1, keepdims=True)


def sample_equirect(env: np.ndarray, dirs: np.ndarray) -> np.ndarray:
    """双线性采样 equirect 图。dirs (...,3) 单位向量 → (...,3) RGB。"""
    H, W = env.shape[:2]
    shape = dirs.shape[:-1]
    d = dirs.reshape(-1, 3).astype(np.float64)
    x, y, z = d[:, 0], d[:, 1], d[:, 2]

    u = (np.arctan2(z, x) / (2 * PI) + 0.5) * W - 0.5
    v = (np.arccos(np.clip(y, -1.0, 1.0)) / PI) * H - 0.5
    u = np.mod(u, W)
    v = np.clip(v, 0.0, H - 1.0)

    x0 = np.floor(u).astype(np.int64) % W
    x1 = (x0 + 1) % W
    y0 = np.floor(v).astype(np.int64)
    y1 = np.minimum(y0 + 1, H - 1)
    fx = (u - np.floor(u))[:, None]
    fy = (v - np.floor(v))[:, None]

    c00 = env[y0, x0]
    c01 = env[y0, x1]
    c10 = env[y1, x0]
    c11 = env[y1, x1]
    out = (c00 * (1 - fx) + c01 * fx) * (1 - fy) + (c10 * (1 - fx) + c11 * fx) * fy
    return out.reshape(*shape, 3).astype(np.float32)


def radical_inverse_vdc(bits: np.ndarray) -> np.ndarray:
    """向量化 Van der Corput radical inverse（base 2）。"""
    b = bits.astype(np.uint32).copy()
    b = (b << 16) | (b >> 16)
    b = ((b & 0x55555555) << 1) | ((b & 0xAAAAAAAA) >> 1)
    b = ((b & 0x33333333) << 2) | ((b & 0xCCCCCCCC) >> 2)
    b = ((b & 0x0F0F0F0F) << 4) | ((b & 0xF0F0F0F0) >> 4)
    b = ((b & 0x00FF00FF) << 8) | ((b & 0xFF00FF00) >> 8)
    return b.astype(np.float64) * (1.0 / 4294967296.0)


def hammersley(n: int) -> np.ndarray:
    """n 个二维低差异采样点，返回 (n,2) float64。"""
    i = np.arange(n, dtype=np.uint32)
    return np.stack([i.astype(np.float64) / n, radical_inverse_vdc(i)], -1)


def importance_sample_ggx(xi: np.ndarray, roughness: float) -> np.ndarray:
    """GGX 重要性采样（切线空间半程向量 H，z 为法线）。α = roughness²。"""
    a = roughness * roughness
    a2 = a * a
    phi = 2.0 * PI * xi[..., 0]
    y = xi[..., 1]
    cos_theta = np.sqrt((1.0 - y) / (1.0 + (a2 - 1.0) * y))
    sin_theta = np.sqrt(np.clip(1.0 - cos_theta * cos_theta, 0.0, 1.0))
    return np.stack([sin_theta * np.cos(phi),
                     sin_theta * np.sin(phi),
                     cos_theta], -1)


def tangent_basis(N: np.ndarray):
    """任意法线数组 (...,3) → 切线 T、副法线 B（各 (...,3)）。"""
    up = np.zeros_like(N)
    up[..., 1] = 1.0
    mask = np.abs(N[..., 1]) >= 0.999
    up[mask] = np.array([1.0, 0.0, 0.0], dtype=np.float32)
    T = np.cross(up, N)
    T /= np.linalg.norm(T, axis=-1, keepdims=True)
    B = np.cross(N, T)
    return T, B


def tangent_to_world(H: np.ndarray, T, B, N) -> np.ndarray:
    """切线空间向量 H (...,3) → 世界空间。T/B/N 与 H 广播对齐。"""
    return (H[..., 0:1] * T + H[..., 1:2] * B + H[..., 2:3] * N)


def g1_smith(cos_x: np.ndarray, alpha2: float) -> np.ndarray:
    """引擎 brdf.hlsl 同款 Smith 单侧遮蔽：G1 = 2x/(x+sqrt(α²+(1-α²)x²))。"""
    return 2.0 * cos_x / (cos_x + np.sqrt(alpha2 + (1.0 - alpha2) * cos_x * cos_x))


# ------------------------------------------------------------------
# 烘焙：env_cube / irradiance / prefilter / brdf_lut
# ------------------------------------------------------------------

def bake_env_cube(env: np.ndarray, size: int) -> np.ndarray:
    """equirect → cubemap (6, size, size, 3)。"""
    faces = [sample_equirect(env, cube_face_dirs(f, size)) for f in range(6)]
    return np.stack(faces)


def bake_irradiance(env: np.ndarray, size: int,
                    n_theta: int = 64, n_phi: int = 128,
                    chunk: int = 256) -> np.ndarray:
    """余弦加权半球卷积。irr = π · mean(L·cosθ·sinθ)（参数空间均匀采样）。"""
    theta = (np.arange(n_theta) + 0.5) / n_theta * (PI / 2)
    phi = (np.arange(n_phi) + 0.5) / n_phi * (2 * PI)
    P, Tt = np.meshgrid(phi, theta)
    st, ct = np.sin(Tt), np.cos(Tt)
    Hdirs = np.stack([st * np.cos(P), st * np.sin(P), ct], -1)  # (nt,np,3)
    Hflat = Hdirs.reshape(-1, 3)
    w = (ct * st).reshape(-1)                                    # cosθ·sinθ

    out = np.zeros((6, size, size, 3), dtype=np.float32)
    for f in range(6):
        N = cube_face_dirs(f, size).reshape(-1, 3)
        for lo in range(0, N.shape[0], chunk):
            Nc = N[lo:lo + chunk]
            T, B = tangent_basis(Nc)
            # (P,S,3)
            L = tangent_to_world(Hflat[None, :, :],
                                 T[:, None, :], B[:, None, :], Nc[:, None, :])
            Le = sample_equirect(env, L.reshape(-1, 3)).reshape(L.shape)
            irr = PI * (Le * w[None, :, None]).mean(axis=1)
            out[f].reshape(-1, 3)[lo:lo + chunk] = irr
        print(f"  [irradiance] face {f} done")
    return out


def bake_prefilter(env: np.ndarray, env_cube: np.ndarray,
                   base_size: int, mips: int, samples: int,
                   chunk: int = 2048) -> list:
    """GGX 预滤波。返回逐 mip 的 (6, size_m, size_m, 3) 列表。mip0 直接重采样 env_cube。"""
    xi = hammersley(samples)
    out_mips = []
    for m in range(mips):
        sz = max(base_size >> m, 1)
        roughness = m / (mips - 1) if mips > 1 else 0.0
        t0 = time.time()

        if roughness == 0.0:
            mip = np.stack([cv2.resize(env_cube[f], (sz, sz),
                                       interpolation=cv2.INTER_AREA)
                            for f in range(6)]).astype(np.float32)
        else:
            Ht = importance_sample_ggx(xi, roughness)  # (S,3) 切线空间
            mip = np.zeros((6, sz, sz, 3), dtype=np.float32)
            for f in range(6):
                N = cube_face_dirs(f, sz).reshape(-1, 3)
                for lo in range(0, N.shape[0], chunk):
                    Nc = N[lo:lo + chunk]
                    T, B = tangent_basis(Nc)
                    L = tangent_to_world(Ht[None, :, :],
                                         T[:, None, :], B[:, None, :],
                                         Nc[:, None, :])
                    NdotL = np.clip((L * Nc[:, None, :]).sum(-1), 0.0, None)
                    Le = sample_equirect(env, L.reshape(-1, 3)).reshape(L.shape)
                    num = (Le * NdotL[..., None]).sum(axis=1)
                    den = NdotL.sum(axis=1)
                    mip[f].reshape(-1, 3)[lo:lo + chunk] = num / np.maximum(den, 1e-6)[:, None]
        print(f"  [prefilter] mip {m} ({sz}x{sz}, roughness={roughness:.2f}) "
              f"{time.time() - t0:.1f}s")
        out_mips.append(mip)
    return out_mips


def bake_brdf_lut(lut_size: int, samples: int, chunk: int = 4096) -> np.ndarray:
    """BRDF 积分 LUT：UV = (NdotV, roughness) → (A, B)，与环境无关。

    积分核对齐引擎 brdf.hlsl：Schlick 幂 5、G1_Smith(α²)。
    specular = prefiltered · (F0·A + B)
    """
    xi = hammersley(samples)
    out = np.zeros((lut_size, lut_size, 2), dtype=np.float32)

    for m in range(lut_size):
        roughness = (m + 0.5) / lut_size
        a = roughness * roughness
        a2 = a * a
        Ht = importance_sample_ggx(xi, roughness)          # (S,3)

        for lo in range(0, lut_size, chunk):
            hi = min(lo + chunk, lut_size)
            ndv = (np.arange(lo, hi) + 0.5) / lut_size     # (P,)
            sin_v = np.sqrt(1.0 - ndv * ndv)
            # 切线空间固定 N=(0,0,1)，V=(sinV, 0, cosV)
            V = np.stack([sin_v, np.zeros_like(ndv), ndv], -1)      # (P,3)
            H = np.broadcast_to(Ht[None, :, :],
                                (hi - lo, samples, 3))              # (P,S,3)
            VdotH = (V[:, None, :] * H).sum(-1)                     # (P,S)
            # L = 2·(V·H)·H - V
            L = 2.0 * VdotH[..., None] * H - V[:, None, :]
            NdotL = L[..., 2]
            NdotH = H[..., 2]

            valid = NdotL > 0.0
            NdotLc = np.clip(NdotL, 1e-6, None)
            G = g1_smith(NdotLc, a2) * g1_smith(ndv[:, None], a2)
            Gvis = G * VdotH / (np.clip(NdotH, 1e-6, None) * ndv[:, None])
            Gvis = np.where(valid, Gvis, 0.0)
            Fc = (1.0 - VdotH) ** 5
            A = ((1.0 - Fc) * Gvis).sum(axis=1) / samples
            B = (Fc * Gvis).sum(axis=1) / samples
            out[m, lo:hi, 0] = A
            out[m, lo:hi, 1] = B
        if m % 64 == 0:
            print(f"  [brdf_lut] row {m}/{lut_size}")
    return out


# ------------------------------------------------------------------
# .ibl 容器写出 + meta.json + 预览
# ------------------------------------------------------------------

def write_ibl(path: Path, kind: int, width: int, height: int,
              layers: int, mip_data: list):
    """mip_data: 逐 mip 的 float32/16 数组，cube 为 (6,h,w,3)，2D 为 (h,w,2)。
    cube 写出时补 alpha=1.0 凑齐 RGBA16F（4 通道对齐，引擎侧直接当
    R16G16B16A16_SFLOAT 上传）。"""
    payload = struct.pack("<4s7I", IBL_MAGIC, 1, kind, width, height,
                          layers, len(mip_data), 0)
    for data in mip_data:
        if kind == IBL_KIND_CUBE_RGBA16F and data.shape[-1] == 3:
            alpha = np.ones(data.shape[:-1] + (1,), dtype=data.dtype)
            data = np.concatenate([data, alpha], axis=-1)
        raw = np.ascontiguousarray(data, dtype=np.float16).tobytes()
        payload += struct.pack("<Q", len(raw)) + raw
    path.write_bytes(payload)
    print(f"[bake_ibl] 写出 {path} ({len(payload) / 1024:.0f} KiB)")


def tonemap_u8(rgb: np.ndarray) -> np.ndarray:
    """Reinhard + gamma → uint8 BGR（供 cv2.imwrite）。"""
    c = rgb / (1.0 + rgb)
    c = np.power(np.clip(c, 0.0, 1.0), 1.0 / 2.2)
    return cv2.cvtColor((c * 255).astype(np.uint8), cv2.COLOR_RGB2BGR)


def cube_cross(cube: np.ndarray) -> np.ndarray:
    """6 面十字展开图：    [+Y]
    [-X][+Z][+X][-Z]
        [-Y]"""
    _, N, _, C = cube.shape
    grid = np.zeros((3 * N, 4 * N, C), dtype=cube.dtype)
    # (row, col) → face
    layout = {(0, 1): 2, (1, 0): 1, (1, 1): 4, (1, 2): 0, (1, 3): 5, (2, 1): 3}
    for (r, c), f in layout.items():
        grid[r * N:(r + 1) * N, c * N:(c + 1) * N] = cube[f]
    return grid


def write_previews(out_dir: Path, env_cube, irradiance,
                   prefilter_mips, brdf_lut):
    prev = out_dir / "previews"
    prev.mkdir(parents=True, exist_ok=True)
    cv2.imwrite(str(prev / "env_cross.png"), tonemap_u8(cube_cross(env_cube)))
    cv2.imwrite(str(prev / "irradiance_cross.png"),
                tonemap_u8(cube_cross(irradiance)))
    for i, mip in enumerate(prefilter_mips):
        cv2.imwrite(str(prev / f"prefilter_mip{i}.png"),
                    tonemap_u8(cube_cross(mip)))
    lut_rgb = np.concatenate([brdf_lut, np.zeros_like(brdf_lut[..., :1])], -1)
    cv2.imwrite(str(prev / "brdf_lut.png"), tonemap_u8(lut_rgb))
    print(f"[bake_ibl] 预览图 → {prev}")


# ------------------------------------------------------------------
# main
# ------------------------------------------------------------------

def main():
    ap = argparse.ArgumentParser(description="IBL 离线烘焙（.hdr → .ibl）")
    ap.add_argument("input", help="equirectangular 环境贴图路径（.hdr / .exr）")
    ap.add_argument("--name", default=None, help="环境名（默认取输入文件名）")
    ap.add_argument("--out", default=None,
                    help="输出目录（默认与输入文件同目录）")
    ap.add_argument("--env-size", type=int, default=512)
    ap.add_argument("--irr-size", type=int, default=32)
    ap.add_argument("--prefilter-size", type=int, default=128)
    ap.add_argument("--prefilter-mips", type=int, default=5)
    ap.add_argument("--lut-size", type=int, default=512)
    ap.add_argument("--samples", type=int, default=1024)
    ap.add_argument("--skip-lut", action="store_true",
                    help="跳过 brdf_lut（若输出目录已有则保留）")
    ap.add_argument("--force", action="store_true", help="忽略已有 brdf_lut 重烘")
    args = ap.parse_args()

    input_path = Path(args.input)
    if not input_path.is_absolute():
        input_path = (PROJECT_ROOT / input_path).resolve()
    name = args.name or input_path.stem
    out_dir = Path(args.out) if args.out else input_path.parent
    out_dir.mkdir(parents=True, exist_ok=True)

    env = load_hdr(input_path)

    # 1. env_cube
    print(f"[bake_ibl] env_cube {args.env_size}^2 ...")
    env_cube = bake_env_cube(env, args.env_size)
    write_ibl(out_dir / "env_cube.ibl", IBL_KIND_CUBE_RGBA16F,
              args.env_size, args.env_size, 6, [env_cube])

    # 2. irradiance
    print(f"[bake_ibl] irradiance {args.irr_size}^2 ...")
    irradiance = bake_irradiance(env, args.irr_size)
    write_ibl(out_dir / "irradiance.ibl", IBL_KIND_CUBE_RGBA16F,
              args.irr_size, args.irr_size, 6, [irradiance])

    # 3. prefilter
    print(f"[bake_ibl] prefilter {args.prefilter_size}^2 "
          f"x{args.prefilter_mips} mips, {args.samples} samples ...")
    prefilter_mips = bake_prefilter(env, env_cube, args.prefilter_size,
                                    args.prefilter_mips, args.samples)
    write_ibl(out_dir / "prefilter.ibl", IBL_KIND_CUBE_RGBA16F,
              args.prefilter_size, args.prefilter_size, 6, prefilter_mips)

    # 4. brdf_lut（环境无关，已有且未 --force 时跳过）
    lut_path = out_dir / "brdf_lut.ibl"
    if args.skip_lut or (lut_path.exists() and not args.force):
        print(f"[bake_ibl] 跳过 brdf_lut（{lut_path.name} 已存在或 --skip-lut）")
        brdf_lut = None
    else:
        print(f"[bake_ibl] brdf_lut {args.lut_size}^2, {args.samples} samples ...")
        brdf_lut = bake_brdf_lut(args.lut_size, args.samples)
        write_ibl(lut_path, IBL_KIND_2D_RG16F,
                  args.lut_size, args.lut_size, 1, [brdf_lut])

    # 预览
    write_previews(out_dir, env_cube, irradiance, prefilter_mips,
                   brdf_lut if brdf_lut is not None
                   else np.zeros((args.lut_size, args.lut_size, 2), np.float32))

    # meta.json
    meta = {
        "name": name,
        "source": str(input_path),
        "envSize": args.env_size,
        "irradianceSize": args.irr_size,
        "prefilterSize": args.prefilter_size,
        "prefilterMips": args.prefilter_mips,
        "lutSize": args.lut_size,
        "samples": args.samples,
        "cubeConvention": 1,  # 层序 +X,-X,+Y,-Y,+Z,-Z，公式见 bake_ibl.py cube_face_dirs
        "bakedAt": time.strftime("%Y-%m-%d %H:%M:%S"),
    }
    (out_dir / "meta.json").write_text(
        json.dumps(meta, indent=4, ensure_ascii=False), encoding="utf-8")

    # 数值自检
    print("---- 数值自检 ----")
    env_mean = env.reshape(-1, 3).mean(axis=0)
    irr_mean = irradiance.reshape(-1, 3).mean(axis=0)
    print(f"env 均值 {env_mean} vs irradiance 均值 {irr_mean}"
          "（应同量级，能量守恒粗查）")
    if brdf_lut is not None:
        r0 = brdf_lut[0]  # roughness≈0 行
        print(f"LUT 值域 A[{brdf_lut[..., 0].min():.3f}, {brdf_lut[..., 0].max():.3f}] "
              f"B[{brdf_lut[..., 1].min():.3f}, {brdf_lut[..., 1].max():.3f}]（应在 [0,1] 内）")
        print(f"LUT r=0 行 A+B 范围 [{(r0[..., 0] + r0[..., 1]).min():.4f}, "
              f"{(r0[..., 0] + r0[..., 1]).max():.4f}]（应 ≈ 1）")
    pf0 = prefilter_mips[0]
    pf0_resized = np.stack([cv2.resize(pf0[f], (args.env_size, args.env_size),
                                       interpolation=cv2.INTER_AREA)
                            for f in range(6)])
    diff = np.abs(pf0_resized - env_cube).mean()
    print(f"prefilter mip0 vs env_cube 平均绝对差 {diff:.5f}（应接近 0）")

    print(f"[bake_ibl] 完成 → {out_dir}")


if __name__ == "__main__":
    main()
