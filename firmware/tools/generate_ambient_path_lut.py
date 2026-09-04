#!/usr/bin/env python3
"""Generate a 128-point rounded-rectangle path for the ambient snake."""

from __future__ import annotations

import argparse
import math
from pathlib import Path


def line(x0: float, y0: float, x1: float, y1: float,
         include_start: bool) -> list[tuple[int, int, bool]]:
    points = []
    first = 0 if include_start else 1
    denominator = 23 if include_start else 24
    for index in range(first, 24 + (0 if include_start else 1)):
        t = index / denominator
        points.append((
            round(x0 + (x1 - x0) * t),
            round(y0 + (y1 - y0) * t),
            abs(x1 - x0) >= abs(y1 - y0),
        ))
    return points[:24]


def corner(cx: float, cy: float, start_degrees: float
           ) -> list[tuple[int, int, bool]]:
    points = []
    radius = 44.0
    for index in range(1, 9):
        angle = math.radians(start_degrees + 90.0 * index / 8.0)
        tangent_x = -math.sin(angle)
        tangent_y = math.cos(angle)
        points.append((
            round(cx + radius * math.cos(angle)),
            round(cy + radius * math.sin(angle)),
            abs(tangent_x) >= abs(tangent_y),
        ))
    return points


def generate() -> list[tuple[int, int, bool]]:
    points = []
    points += line(55, 11, 425, 11, True)
    points += corner(425, 55, -90)
    points += line(469, 55, 469, 425, False)
    points += corner(425, 425, 0)
    points += line(425, 469, 55, 469, False)
    points += corner(55, 425, 90)
    points += line(11, 425, 11, 55, False)
    points += corner(55, 55, 180)
    assert len(points) == 128
    return points


def emit(output: Path) -> None:
    points = generate()
    rows = [
        f"    {{{x}, {y}, {'true' if horizontal else 'false'}}},"
        for x, y, horizontal in points
    ]
    text = "\n".join([
        "#pragma once",
        "",
        "namespace codex {",
        "",
        "inline constexpr unsigned int kAmbientPathSampleCount = 128;",
        "inline constexpr unsigned int kAmbientSnakeMoverCount = 3;",
        "struct AmbientPathPoint {",
        "  short x;",
        "  short y;",
        "  bool horizontal;",
        "};",
        "inline constexpr AmbientPathPoint kAmbientPath[kAmbientPathSampleCount] = {",
        *rows,
        "};",
        "",
        "[[nodiscard]] constexpr AmbientPathPoint ambient_path_point(",
        "    unsigned int phase) {",
        "  return kAmbientPath[phase & (kAmbientPathSampleCount - 1U)];",
        "}",
        "",
        "[[nodiscard]] constexpr unsigned int ambient_tail_phase(",
        "    unsigned int head_phase, unsigned int tail_index) {",
        "  return (head_phase + kAmbientPathSampleCount - tail_index * 10U) &",
        "         (kAmbientPathSampleCount - 1U);",
        "}",
        "",
        "}  // namespace codex",
        "",
    ])
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    emit(args.output)


if __name__ == "__main__":
    main()
