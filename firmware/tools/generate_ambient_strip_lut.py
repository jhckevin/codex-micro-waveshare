#!/usr/bin/env python3
"""Generate rounded, tile-local ambient-ring lookup tables."""

from __future__ import annotations

import math
from pathlib import Path


PHASES = 128
PERIMETER = 1716
LEADING = 24
BODY = 40
TAIL_END = 410
RADIUS = 44.0
STRAIGHT = 360.0
QUARTER = math.pi * RADIUS / 2.0
TOTAL_PATH = STRAIGHT * 4.0 + QUARTER * 4.0
INVALID_PATH = 0xFFFF

TILES = (
    (72, 4, 336, 24),
    (408, 4, 68, 68),
    (452, 72, 24, 336),
    (408, 408, 68, 68),
    (72, 452, 336, 24),
    (4, 408, 68, 68),
    (4, 72, 24, 336),
    (4, 4, 68, 68),
)


def clamp(value: float, low: float, high: float) -> float:
    return max(low, min(high, value))


def clamp_byte(value: float) -> int:
    return max(0, min(255, round(value)))


def smoothstep(value: float) -> float:
    value = clamp(value, 0.0, 1.0)
    return value * value * (3.0 - 2.0 * value)


def closest_track_point(x: float, y: float) -> tuple[float, float]:
    """Return Euclidean distance and clockwise path coordinate."""
    candidates: list[tuple[float, float]] = []

    def add(px: float, py: float, path: float) -> None:
        candidates.append((math.hypot(x - px, y - py), path))

    top_x = clamp(x, 60.0, 420.0)
    add(top_x, 16.0, top_x - 60.0)

    right_y = clamp(y, 60.0, 420.0)
    add(464.0, right_y, STRAIGHT + QUARTER + right_y - 60.0)

    bottom_x = clamp(x, 60.0, 420.0)
    add(
        bottom_x,
        464.0,
        STRAIGHT * 2.0 + QUARTER * 2.0 + 420.0 - bottom_x,
    )

    left_y = clamp(y, 60.0, 420.0)
    add(
        16.0,
        left_y,
        STRAIGHT * 3.0 + QUARTER * 3.0 + 420.0 - left_y,
    )

    arcs = (
        (420.0, 60.0, 1.5 * math.pi, 2.0 * math.pi, STRAIGHT),
        (
            420.0,
            420.0,
            0.0,
            0.5 * math.pi,
            STRAIGHT * 2.0 + QUARTER,
        ),
        (
            60.0,
            420.0,
            0.5 * math.pi,
            math.pi,
            STRAIGHT * 3.0 + QUARTER * 2.0,
        ),
        (
            60.0,
            60.0,
            math.pi,
            1.5 * math.pi,
            STRAIGHT * 4.0 + QUARTER * 3.0,
        ),
    )
    for center_x, center_y, start, end, path_start in arcs:
        angle = math.atan2(y - center_y, x - center_x)
        if angle < 0.0:
            angle += 2.0 * math.pi
        if start > math.pi and angle < math.pi:
            angle += 2.0 * math.pi
        angle = clamp(angle, start, end)
        px = center_x + math.cos(angle) * RADIUS
        py = center_y + math.sin(angle) * RADIUS
        add(px, py, path_start + (angle - start) * RADIUS)

    distance, path = min(candidates, key=lambda candidate: candidate[0])
    normalized = round(path * PERIMETER / TOTAL_PATH) % PERIMETER
    return distance, float(normalized)


def radial_alpha(distance: float) -> int:
    if distance >= 12.0:
        return 0
    if distance <= 4.0:
        return 255
    return clamp_byte(255.0 * smoothstep((12.0 - distance) / 8.0))


def longitudinal_alpha(head: int, position: int) -> int:
    behind = (head - position) % PERIMETER
    if behind <= BODY:
        return 255
    if behind <= TAIL_END:
        progress = (behind - BODY) / (TAIL_END - BODY)
        return clamp_byte(
            255.0 * math.cos(progress * math.pi / 2.0) ** 0.75
        )
    forward = PERIMETER - behind
    if 0 < forward < LEADING:
        return clamp_byte(
            255.0 * smoothstep((LEADING - forward) / LEADING)
        )
    return 0


def format_rows(
    values: list[int], indent: str = "    ", width: int = 20, suffix: str = "U"
) -> str:
    rows = []
    for offset in range(0, len(values), width):
        chunk = ", ".join(
            f"{value}{suffix}" for value in values[offset : offset + width]
        )
        rows.append(f"{indent}{chunk},")
    return "\n".join(rows)


def generate(output: Path) -> None:
    offsets = [0]
    path_map: list[int] = []
    radial_map: list[int] = []
    for origin_x, origin_y, width, height in TILES:
        for local_y in range(height):
            for local_x in range(width):
                distance, path = closest_track_point(
                    origin_x + local_x, origin_y + local_y
                )
                alpha = radial_alpha(distance)
                path_map.append(int(path) if alpha != 0 else INVALID_PATH)
                radial_map.append(alpha)
        offsets.append(len(path_map))

    longitudinal: list[list[int]] = []
    active_masks: list[int] = []
    active_pixels: list[int] = []
    tile_areas = [width * height for _, _, width, height in TILES]
    for phase in range(PHASES):
        head = (phase * PERIMETER + PHASES // 2) // PHASES
        head %= PERIMETER
        row = [
            longitudinal_alpha(head, position)
            for position in range(PERIMETER)
        ]
        longitudinal.append(row)
        mask = 0
        for tile in range(len(TILES)):
            start, end = offsets[tile], offsets[tile + 1]
            if any(
                path != INVALID_PATH and row[path] != 0
                for path in path_map[start:end]
            ):
                mask |= 1 << tile
        pixels = sum(
            tile_areas[tile]
            for tile in range(len(TILES))
            if mask & (1 << tile)
        )
        if mask.bit_count() > 4 or pixels > 28816:
            raise RuntimeError(
                f"phase {phase} exceeds render budget: mask={mask:#x} "
                f"pixels={pixels}"
            )
        active_masks.append(mask)
        active_pixels.append(pixels)

    premultiplied = [
        [round(level * radial / 255) for radial in range(256)]
        for level in range(256)
    ]

    lines = [
        "#pragma once",
        "",
        "#include <stdint.h>",
        "",
        "namespace codex {",
        "",
        f"inline constexpr unsigned int kAmbientStripPhaseCount = {PHASES}U;",
        f"inline constexpr unsigned int kAmbientStripPerimeter = {PERIMETER}U;",
        f"inline constexpr unsigned int kAmbientStripTailExtent = {TAIL_END}U;",
        f"inline constexpr unsigned int kAmbientStripTotalPixels = {len(path_map)}U;",
        "",
        "inline constexpr uint32_t kAmbientStripTileOffsets[9] = {",
        format_rows(offsets),
        "};",
        "",
        "inline constexpr uint16_t kAmbientStripPathMap"
        "[kAmbientStripTotalPixels] = {",
        format_rows(path_map),
        "};",
        "",
        "inline constexpr uint8_t kAmbientStripRadialMap"
        "[kAmbientStripTotalPixels] = {",
        format_rows(radial_map),
        "};",
        "",
        "inline constexpr uint8_t kAmbientStripPremultiplied[256][256] = {",
    ]
    for row in premultiplied:
        lines.extend(("  {", format_rows(row, "    "), "  },"))
    lines.extend(
        (
            "};",
            "",
            "inline constexpr uint8_t kAmbientStripLongitudinal"
            "[kAmbientStripPhaseCount][kAmbientStripPerimeter] = {",
        )
    )
    for row in longitudinal:
        lines.extend(("  {", format_rows(row, "    "), "  },"))
    lines.extend(
        (
            "};",
            "",
            "inline constexpr uint8_t kAmbientStripActiveTiles"
            "[kAmbientStripPhaseCount] = {",
            format_rows(active_masks),
            "};",
            "",
            "inline constexpr uint16_t kAmbientStripActivePixels"
            "[kAmbientStripPhaseCount] = {",
            format_rows(active_pixels),
            "};",
            "",
            "}  // namespace codex",
            "",
        )
    )
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8", newline="\n")


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[1]
    generate(
        root
        / "components"
        / "codex_ui"
        / "include"
        / "codex"
        / "ambient_strip_lut.h"
    )
