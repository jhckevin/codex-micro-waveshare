#!/usr/bin/env python3
"""Generate the independent Codex Micro Control application icon.

The PNG encoder uses only the Python standard library so release builds do not
depend on an image-processing package or a developer workstation.
"""

from __future__ import annotations

import math
import struct
import sys
import zlib
from pathlib import Path


SIZE = 512


def clamp(value: float, low: float = 0.0, high: float = 1.0) -> float:
    return max(low, min(high, value))


def rounded_rect_sdf(
    x: float, y: float, left: float, top: float, right: float, bottom: float, radius: float
) -> float:
    cx = (left + right) * 0.5
    cy = (top + bottom) * 0.5
    qx = abs(x - cx) - (right - left) * 0.5 + radius
    qy = abs(y - cy) - (bottom - top) * 0.5 + radius
    return min(max(qx, qy), 0.0) + math.hypot(max(qx, 0.0), max(qy, 0.0)) - radius


def coverage(distance: float, feather: float = 1.2) -> float:
    return clamp(0.5 - distance / feather)


def blend(dst: list[float], color: tuple[int, int, int], alpha: float) -> None:
    alpha = clamp(alpha)
    inv = 1.0 - alpha
    dst[0] = color[0] * alpha + dst[0] * inv
    dst[1] = color[1] * alpha + dst[1] * inv
    dst[2] = color[2] * alpha + dst[2] * inv
    dst[3] = alpha + dst[3] * inv


def chunk(kind: bytes, payload: bytes) -> bytes:
    return (
        struct.pack(">I", len(payload))
        + kind
        + payload
        + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF)
    )


def render_pixel(x: float, y: float) -> bytes:
    pixel = [0.0, 0.0, 0.0, 0.0]

    outer = rounded_rect_sdf(x, y, 34, 34, 478, 478, 112)
    shadow = clamp(1.0 - max(outer, 0.0) / 32.0) * 0.24
    if outer > 0:
        blend(pixel, (31, 88, 69), shadow)

    outer_alpha = coverage(outer)
    if outer_alpha:
        vertical = clamp((y - 34) / 444)
        housing = (
            int(24 + 9 * (1 - vertical)),
            int(37 + 23 * (1 - vertical)),
            int(50 + 17 * (1 - vertical)),
        )
        blend(pixel, housing, outer_alpha)

    rail = abs(rounded_rect_sdf(x, y, 54, 54, 458, 458, 94))
    blend(pixel, (88, 241, 174), clamp(1.0 - rail / 16.0) * 0.78 * outer_alpha)

    face = rounded_rect_sdf(x, y, 79, 79, 433, 433, 75)
    face_alpha = coverage(face)
    if face_alpha:
        blend(pixel, (235, 243, 241), face_alpha * 0.98)
        highlight = clamp(1.0 - math.hypot(x - 205, y - 165) / 310.0)
        blend(pixel, (255, 255, 255), face_alpha * highlight * 0.20)

    keys = (
        (116, 116, 205, 205, (87, 113, 255)),
        (217, 116, 306, 205, (76, 224, 150)),
        (318, 116, 397, 205, (242, 248, 246)),
        (116, 217, 205, 306, (255, 176, 96)),
        (217, 217, 306, 306, (255, 102, 139)),
        (318, 217, 397, 306, (242, 248, 246)),
    )
    for left, top, right, bottom, color in keys:
        key = rounded_rect_sdf(x, y, left, top, right, bottom, 25)
        glow = clamp(1.0 - max(key, 0.0) / 19.0)
        if color != (242, 248, 246):
            blend(pixel, color, glow * 0.22)
        key_alpha = coverage(key)
        blend(pixel, color, key_alpha * (0.78 if color != (242, 248, 246) else 0.96))
        rim = clamp(1.0 - abs(key) / 2.2)
        blend(pixel, (255, 255, 255), rim * 0.55)

    wide = rounded_rect_sdf(x, y, 168, 329, 344, 395, 27)
    blend(pixel, (252, 254, 253), coverage(wide) * 0.97)
    blend(pixel, (255, 255, 255), clamp(1.0 - abs(wide) / 2.0) * 0.7)

    for cx, cy in ((160, 160), (261, 160), (357, 160), (160, 261), (261, 261), (357, 261)):
        dot_distance = math.hypot(x - cx, y - cy) - 8
        blend(pixel, (26, 38, 49), coverage(dot_distance) * 0.48)

    # A small independent "J" signature on the wide key.
    stem = rounded_rect_sdf(x, y, 251, 346, 262, 376, 5)
    hook = math.hypot(x - 246, y - 375) - 11
    blend(pixel, (22, 34, 44), coverage(stem) * 0.92)
    if y >= 372:
        blend(pixel, (22, 34, 44), coverage(abs(hook) - 3.5) * 0.92)

    alpha = clamp(pixel[3])
    if alpha == 0:
        return b"\x00\x00\x00\x00"
    return bytes(
        (
            round(clamp(pixel[0] / (255.0 * alpha)) * 255),
            round(clamp(pixel[1] / (255.0 * alpha)) * 255),
            round(clamp(pixel[2] / (255.0 * alpha)) * 255),
            round(alpha * 255),
        )
    )


def write_png(output: Path) -> None:
    rows = []
    for y in range(SIZE):
        row = bytearray([0])
        for x in range(SIZE):
            row.extend(render_pixel(x + 0.5, y + 0.5))
        rows.append(bytes(row))
    payload = zlib.compress(b"".join(rows), level=9)
    png = (
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0))
        + chunk(b"IDAT", payload)
        + chunk(b"IEND", b"")
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(png)


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("usage: generate_app_icon.py OUTPUT.png")
    write_png(Path(sys.argv[1]))
