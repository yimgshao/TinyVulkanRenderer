#!/usr/bin/env python3
"""一键用 RenderDoc 打开抓帧文件。

用法:
    python scripts/view_capture.py captures/capture_frame60.rdc   # 打开指定抓帧
    python scripts/view_capture.py                                # 打开抓帧目录里最新的 .rdc

抓帧目录与 RenderDoc 安装目录从 configs/renderdoc.json 读取（与 capture.py 共用）。
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
    "captureDir": "captures",
}


def load_config():
    cfg = dict(DEFAULT_CONFIG)
    if CONFIG_PATH.exists():
        try:
            cfg.update(json.loads(CONFIG_PATH.read_text(encoding="utf-8")))
        except json.JSONDecodeError as e:
            sys.exit(f"[view] 配置文件解析失败 {CONFIG_PATH}: {e}")
    return cfg


def main():
    ap = argparse.ArgumentParser(description="用 RenderDoc 打开抓帧文件")
    ap.add_argument("capture", nargs="?", default=None,
                    help="抓帧文件路径（缺省打开抓帧目录里最新的 .rdc）")
    args = ap.parse_args()

    cfg = load_config()
    qrenderdoc = Path(cfg["renderdocDir"]) / "qrenderdoc.exe"
    if not qrenderdoc.exists():
        sys.exit(f"[view] 找不到 qrenderdoc.exe: {qrenderdoc}")

    if args.capture:
        rdc = Path(args.capture).resolve()
    else:
        capture_dir = Path(cfg["captureDir"])
        if not capture_dir.is_absolute():
            capture_dir = PROJECT_ROOT / capture_dir
        captures = sorted(capture_dir.glob("*.rdc"),
                          key=lambda p: p.stat().st_mtime)
        if not captures:
            sys.exit(f"[view] 抓帧目录为空: {capture_dir}（先用 scripts/capture.py 抓一帧）")
        rdc = captures[-1]

    if not rdc.exists():
        sys.exit(f"[view] 抓帧文件不存在: {rdc}")

    print("[view] 打开:", rdc)
    subprocess.Popen([str(qrenderdoc), str(rdc)])  # 不阻塞终端
    return 0


if __name__ == "__main__":
    sys.exit(main())
