#!/usr/bin/env python3
"""Rasterize icons/svg/*.svg → PNG. Prefers ImageMagick; falls back to drawn icons."""
from __future__ import annotations

import struct
import subprocess
import sys
import zlib
from pathlib import Path


def write_png(path: Path, w: int, h: int, rgba: bytes) -> None:
    def chunk(tag: bytes, data: bytes) -> bytes:
        return struct.pack(">I", len(data)) + tag + data + struct.pack(
            ">I", zlib.crc32(tag + data) & 0xFFFFFFFF
        )

    raw = b""
    stride = w * 4
    for y in range(h):
        raw += b"\x00" + rgba[y * stride : (y + 1) * stride]
    ihdr = struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)
    data = b"\x89PNG\r\n\x1a\n"
    data += chunk(b"IHDR", ihdr)
    data += chunk(b"IDAT", zlib.compress(raw, 9))
    data += chunk(b"IEND", b"")
    path.write_bytes(data)


def try_convert(svg: Path, png: Path, size: int) -> bool:
    for cmd in (
        ["convert", "-background", "none", "-density", "192",
         str(svg), "-resize", f"{size}x{size}", str(png)],
        ["rsvg-convert", "-w", str(size), "-h", str(size), "-o", str(png), str(svg)],
    ):
        try:
            r = subprocess.run(cmd, capture_output=True, timeout=30)
            if r.returncode == 0 and png.exists() and png.stat().st_size > 0:
                return True
        except (FileNotFoundError, subprocess.TimeoutExpired):
            pass
    return False


def px(buf, w, h, x, y, r, g, b, a=255):
    if 0 <= x < w and 0 <= y < h:
        i = (y * w + x) * 4
        buf[i : i + 4] = bytes((r, g, b, a))


def line(buf, w, h, x0, y0, x1, y1, col, t=1):
    x0, y0, x1, y1 = map(int, (x0, y0, x1, y1))
    dx, dy = abs(x1 - x0), abs(y1 - y0)
    sx = 1 if x0 < x1 else -1
    sy = 1 if y0 < y1 else -1
    err = dx - dy
    while True:
        for ox in range(-t + 1, t):
            for oy in range(-t + 1, t):
                px(buf, w, h, x0 + ox, y0 + oy, *col)
        if x0 == x1 and y0 == y1:
            break
        e2 = 2 * err
        if e2 > -dy:
            err -= dy
            x0 += sx
        if e2 < dx:
            err += dx
            y0 += sy


def rect(buf, w, h, x, y, rw, rh, col, fill=None):
    if fill:
        for yy in range(int(y), int(y + rh)):
            for xx in range(int(x), int(x + rw)):
                px(buf, w, h, xx, yy, *fill)
    line(buf, w, h, x, y, x + rw, y, col)
    line(buf, w, h, x + rw, y, x + rw, y + rh, col)
    line(buf, w, h, x + rw, y + rh, x, y + rh, col)
    line(buf, w, h, x, y + rh, x, y, col)


def circle(buf, w, h, cx, cy, r, col, fill=None):
    r = int(r)
    for yy in range(-r - 1, r + 2):
        for xx in range(-r - 1, r + 2):
            d2 = xx * xx + yy * yy
            if fill and d2 <= r * r:
                px(buf, w, h, cx + xx, cy + yy, *fill)
            if abs(d2 - r * r) <= r:
                px(buf, w, h, cx + xx, cy + yy, *col)


INK = (0x2E, 0x34, 0x36, 255)
ACCENT = (0x4A, 0x90, 0xD9, 255)
PAPER = (0xF5, 0xF5, 0xF0, 255)
SOFT = (0xDC, 0xE8, 0xF7, 255)


def draw_icon(name: str, size: int = 20) -> bytes:
    buf = bytearray([0] * size * size * 4)
    s = size / 20.0

    def S(v):
        return int(v * s)

    if name == "save":
        rect(buf, size, size, S(4), S(3), S(12), S(14), INK, SOFT)
        rect(buf, size, size, S(7), S(3), S(6), S(4), INK, PAPER)
        rect(buf, size, size, S(6), S(11), S(8), S(5), INK, PAPER)
    elif name == "undo":
        line(buf, size, size, S(6), S(10), S(11), S(5), INK, 1)
        line(buf, size, size, S(11), S(5), S(15), S(10), INK, 1)
        line(buf, size, size, S(4), S(10), S(7), S(7), INK, 1)
        line(buf, size, size, S(4), S(10), S(7), S(13), INK, 1)
    elif name == "redo":
        line(buf, size, size, S(14), S(10), S(9), S(5), INK, 1)
        line(buf, size, size, S(9), S(5), S(5), S(10), INK, 1)
        line(buf, size, size, S(16), S(10), S(13), S(7), INK, 1)
        line(buf, size, size, S(16), S(10), S(13), S(13), INK, 1)
    elif name == "bold":
        rect(buf, size, size, S(6), S(4), S(7), S(5), INK, INK)
        rect(buf, size, size, S(6), S(10), S(8), S(6), INK, INK)
        line(buf, size, size, S(6), S(4), S(6), S(16), INK, 1)
    elif name == "italic":
        line(buf, size, size, S(12), S(4), S(8), S(16), INK, 1)
        line(buf, size, size, S(7), S(4), S(15), S(4), INK, 1)
        line(buf, size, size, S(5), S(16), S(13), S(16), INK, 1)
    elif name == "code":
        line(buf, size, size, S(8), S(5), S(4), S(10), INK, 1)
        line(buf, size, size, S(4), S(10), S(8), S(15), INK, 1)
        line(buf, size, size, S(12), S(5), S(16), S(10), INK, 1)
        line(buf, size, size, S(16), S(10), S(12), S(15), INK, 1)
        line(buf, size, size, S(11), S(4), S(9), S(16), INK, 1)
    elif name == "link":
        circle(buf, size, size, S(7), S(10), S(3), INK)
        circle(buf, size, size, S(13), S(10), S(3), INK)
        line(buf, size, size, S(9), S(10), S(11), S(10), INK, 1)
    elif name == "list":
        for yy in (5, 10, 15):
            line(buf, size, size, S(8), S(yy), S(16), S(yy), INK, 1)
            circle(buf, size, size, S(5), S(yy), S(1), INK, INK)
    elif name == "olist":
        for i, yy in enumerate((5, 10, 15)):
            line(buf, size, size, S(8), S(yy), S(16), S(yy), INK, 1)
            px(buf, size, size, S(4), S(yy), *INK)
            px(buf, size, size, S(5), S(yy - 1), *INK)
    elif name == "quote":
        line(buf, size, size, S(6), S(6), S(6), S(12), INK, 1)
        line(buf, size, size, S(6), S(6), S(9), S(6), INK, 1)
        line(buf, size, size, S(12), S(6), S(12), S(12), INK, 1)
        line(buf, size, size, S(12), S(6), S(15), S(6), INK, 1)
    elif name == "image":
        rect(buf, size, size, S(3), S(4), S(14), S(12), INK, (0xE8, 0xF0, 0xE0, 255))
        circle(buf, size, size, S(7), S(8), S(2), INK, (0xC4, 0xA0, 0x00, 255))
        line(buf, size, size, S(4), S(14), S(8), S(10), INK, 1)
        line(buf, size, size, S(8), S(10), S(11), S(12), INK, 1)
        line(buf, size, size, S(11), S(12), S(16), S(8), INK, 1)
    elif name == "split":
        rect(buf, size, size, S(3), S(3), S(6), S(14), INK, SOFT)
        rect(buf, size, size, S(11), S(3), S(6), S(14), INK, SOFT)
        line(buf, size, size, S(10), S(3), S(10), S(17), INK, 1)
    elif name == "find":
        circle(buf, size, size, S(9), S(9), S(4), INK)
        line(buf, size, size, S(12), S(12), S(16), S(16), INK, 1)
    elif name == "help":
        circle(buf, size, size, S(10), S(10), S(7), INK, SOFT)
        line(buf, size, size, S(10), S(7), S(10), S(11), INK, 1)
        circle(buf, size, size, S(10), S(14), S(1), INK, INK)
    elif name == "cut":
        circle(buf, size, size, S(6), S(14), S(2), INK)
        circle(buf, size, size, S(14), S(14), S(2), INK)
        line(buf, size, size, S(8), S(12), S(14), S(4), INK, 1)
        line(buf, size, size, S(12), S(12), S(6), S(4), INK, 1)
    elif name == "strike":
        line(buf, size, size, S(5), S(6), S(5), S(14), INK, 2)
        line(buf, size, size, S(5), S(6), S(10), S(6), INK, 1)
        line(buf, size, size, S(5), S(14), S(10), S(14), INK, 1)
        line(buf, size, size, S(4), S(10), S(16), S(10), ACCENT, 2)
    elif name == "cite":
        line(buf, size, size, S(5), S(6), S(5), S(12), INK, 1)
        line(buf, size, size, S(5), S(6), S(8), S(6), INK, 1)
        line(buf, size, size, S(11), S(6), S(11), S(12), INK, 1)
        line(buf, size, size, S(11), S(6), S(14), S(6), INK, 1)
    elif name in ("h1", "h2", "h3"):
        rect(buf, size, size, S(3), S(3), S(14), S(14), INK, SOFT)
        line(buf, size, size, S(6), S(6), S(6), S(14), INK, 2)
        line(buf, size, size, S(6), S(10), S(11), S(10), INK, 1)
        line(buf, size, size, S(11), S(6), S(11), S(14), INK, 2)
    elif name == "para":
        for yy, x1 in ((6, 16), (10, 16), (14, 12)):
            line(buf, size, size, S(4), S(yy), S(x1), S(yy), INK, 1)
    elif name == "task":
        rect(buf, size, size, S(4), S(4), S(12), S(12), INK, SOFT)
        line(buf, size, size, S(6), S(10), S(9), S(13), ACCENT, 2)
        line(buf, size, size, S(9), S(13), S(14), S(6), ACCENT, 2)
    elif name == "table":
        rect(buf, size, size, S(3), S(3), S(14), S(14), INK, SOFT)
        line(buf, size, size, S(3), S(8), S(17), S(8), INK, 1)
        line(buf, size, size, S(3), S(13), S(17), S(13), INK, 1)
        line(buf, size, size, S(10), S(3), S(10), S(17), INK, 1)
    elif name == "figure":
        rect(buf, size, size, S(3), S(3), S(14), S(10), INK, SOFT)
        circle(buf, size, size, S(7), S(7), S(1), ACCENT, ACCENT)
        line(buf, size, size, S(5), S(16), S(15), S(16), INK, 1)
    elif name == "note":
        rect(buf, size, size, S(4), S(3), S(12), S(14), INK, (0xFF, 0xF8, 0xD0, 255))
        line(buf, size, size, S(6), S(9), S(14), S(9), ACCENT, 1)
        line(buf, size, size, S(6), S(12), S(12), S(12), ACCENT, 1)
    elif name == "replace":
        line(buf, size, size, S(5), S(7), S(13), S(7), INK, 1)
        line(buf, size, size, S(13), S(7), S(11), S(5), INK, 1)
        line(buf, size, size, S(13), S(7), S(11), S(9), INK, 1)
        line(buf, size, size, S(15), S(13), S(7), S(13), ACCENT, 1)
        line(buf, size, size, S(7), S(13), S(9), S(11), ACCENT, 1)
        line(buf, size, size, S(7), S(13), S(9), S(15), ACCENT, 1)
    elif name == "outline":
        line(buf, size, size, S(4), S(5), S(16), S(5), INK, 2)
        line(buf, size, size, S(6), S(10), S(16), S(10), INK, 1)
        line(buf, size, size, S(6), S(14), S(13), S(14), INK, 1)
    elif name == "paste":
        rect(buf, size, size, S(5), S(5), S(11), S(12), INK, SOFT)
        rect(buf, size, size, S(7), S(2), S(6), S(4), INK, ACCENT)
    elif name == "copy":
        rect(buf, size, size, S(6), S(5), S(10), S(12), INK, SOFT)
        rect(buf, size, size, S(3), S(2), S(10), S(12), INK, (0xF8, 0xF6, 0xF0, 255))
    elif name == "new":
        rect(buf, size, size, S(4), S(2), S(12), S(16), INK, SOFT)
        line(buf, size, size, S(10), S(8), S(10), S(14), ACCENT, 2)
        line(buf, size, size, S(7), S(11), S(13), S(11), ACCENT, 2)
    elif name == "open":
        rect(buf, size, size, S(3), S(6), S(14), S(10), INK, ACCENT)
        line(buf, size, size, S(3), S(6), S(8), S(6), INK, 1)
        line(buf, size, size, S(8), S(6), S(10), S(4), INK, 1)
        line(buf, size, size, S(10), S(4), S(17), S(4), INK, 1)
    else:
        rect(buf, size, size, S(4), S(4), S(12), S(12), INK, ACCENT)
    return bytes(buf)


def main() -> int:
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <svg_dir> <out_dir> [size]", file=sys.stderr)
        return 2
    svg_dir = Path(sys.argv[1])
    out_dir = Path(sys.argv[2])
    size = int(sys.argv[3]) if len(sys.argv) > 3 else 20
    out_dir.mkdir(parents=True, exist_ok=True)
    svgs = sorted(svg_dir.glob("*.svg"))
    if not svgs:
        print(f"No SVGs in {svg_dir}", file=sys.stderr)
        return 1
    for svg in svgs:
        png = out_dir / (svg.stem + ".png")
        if try_convert(svg, png, size):
            print(f"convert {svg.name} → {png.name}")
        else:
            write_png(png, size, size, draw_icon(svg.stem, size))
            print(f"fallback {svg.name} → {png.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
