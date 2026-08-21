#!/usr/bin/env python3
"""一键从 RenderDoc 启动 TinyVulkanRenderer 抓帧。

用法:
    python scripts/capture.py              # Release 构建，手动抓帧（窗口内按 F12/PrintScreen）
    python scripts/capture.py --debug      # Debug 构建
    python scripts/capture.py --frame 60   # 第 60 帧自动抓取并退出
    python scripts/capture.py --name sun   # 抓帧文件名前缀（默认 capture）

可执行文件路径与 run.ps1 的构建输出对应（build/<Config>/TinyVulkanRenderer.exe）。
RenderDoc 安装目录、抓帧存储目录从 configs/renderdoc.json 读取。
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path


PROJECT_ROOT = Path(__file__).resolve().parent.parent
CONFIG_PATH = PROJECT_ROOT / "configs" / "renderdoc.json"

DEFAULT_CONFIG = {
    "renderdocDir": "C:/Program Files/RenderDoc",
    "captureDir": "captures",  # 相对项目根目录，也可填绝对路径
}


def load_config():
    cfg = dict(DEFAULT_CONFIG)
    if CONFIG_PATH.exists():
        try:
            cfg.update(json.loads(CONFIG_PATH.read_text(encoding="utf-8")))
        except json.JSONDecodeError as e:
            sys.exit(f"[capture] 配置文件解析失败 {CONFIG_PATH}: {e}")
    return cfg


def main():
    ap = argparse.ArgumentParser(description="从 RenderDoc 启动渲染器抓帧")
    ap.add_argument("--debug", action="store_true",
                    help="启动 Debug 构建（默认 Release）")
    ap.add_argument("--frame", type=int, default=None, metavar="N",
                    help="第 N 帧自动抓取并退出（默认手动按 F12）")
    ap.add_argument("--name", default="capture",
                    help="抓帧文件名前缀（默认 capture，帧号自动追加）")
    args = ap.parse_args()

    cfg = load_config()
    renderdoccmd = Path(cfg["renderdocDir"]) / "renderdoccmd.exe"
    exe = (PROJECT_ROOT / "build" / ("Debug" if args.debug else "Release")
           / "TinyVulkanRenderer.exe")

    capture_dir = Path(cfg["captureDir"])
    if not capture_dir.is_absolute():
        capture_dir = PROJECT_ROOT / capture_dir
    capture_dir.mkdir(parents=True, exist_ok=True)

    for path, what in [(renderdoccmd, "renderdoccmd.exe"), (exe, "渲染器可执行文件")]:
        if not path.exists():
            sys.exit(f"[capture] 找不到{what}: {path}")

    cmd = [str(renderdoccmd), "capture",
           "-c", str(capture_dir / f"{args.name}.rdc"),
           "-w"]
    if args.frame is not None:
        cmd += ["--capture-frame", str(args.frame)]
    cmd.append(str(exe))

    print("[capture]", " ".join(cmd))
    print("[capture] 手动抓帧: 在程序窗口按 F12 或 Print Screen")
    return subprocess.call(cmd, cwd=PROJECT_ROOT)


if __name__ == "__main__":
    sys.exit(main())
