#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ingo Ruhnke <grumbel@gmail.com>
"""Rasterize data/icons/xanatoo.svg into the committed app icons.

  data/icons/hicolor/<N>x<N>/apps/xanatoo.png   (Linux, window icon)
  mk/android/app/res/mipmap-*/ic_launcher.png   (Android launcher)

Needs rsvg-convert (librsvg) or inkscape:
  nix shell nixpkgs#librsvg -c tools/make_icons.py
"""
from __future__ import annotations

import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "data" / "icons" / "xanatoo.svg"

HICOLOR_SIZES = (16, 32, 48, 64, 128, 256)
ANDROID_SIZES = {
    "mdpi": 48,
    "hdpi": 72,
    "xhdpi": 96,
    "xxhdpi": 144,
    "xxxhdpi": 192,
}


def rasterize(svg: Path, png: Path, size: int) -> None:
    png.parent.mkdir(parents=True, exist_ok=True)
    if shutil.which("rsvg-convert"):
        cmd = ["rsvg-convert", "-w", str(size), "-h", str(size),
               "-o", str(png), str(svg)]
    elif shutil.which("inkscape"):
        cmd = ["inkscape", "-w", str(size), "-h", str(size),
               "-o", str(png), str(svg)]
    else:
        sys.exit("make_icons.py: needs rsvg-convert or inkscape")
    subprocess.run(cmd, check=True)


def main() -> None:
    for size in HICOLOR_SIZES:
        rasterize(SOURCE, ROOT / "data" / "icons" / "hicolor" /
                  f"{size}x{size}" / "apps" / "xanatoo.png", size)
    for density, size in ANDROID_SIZES.items():
        rasterize(SOURCE, ROOT / "mk" / "android" / "app" / "res" /
                  f"mipmap-{density}" / "ic_launcher.png", size)


if __name__ == "__main__":
    main()
