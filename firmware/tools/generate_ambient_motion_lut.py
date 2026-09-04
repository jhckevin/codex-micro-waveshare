#!/usr/bin/env python3
"""Generate a smooth 128-step ambient snake table for zero-cost interpolation."""

from __future__ import annotations

import argparse
from pathlib import Path


SAMPLE_COUNT = 128
SEGMENT_COUNT = 16
STEPS_PER_SEGMENT = SAMPLE_COUNT // SEGMENT_COUNT
TAIL = (255, 184, 112, 56, 20)


def tail_at(head: int, segment: int) -> int:
    distance = (head + SEGMENT_COUNT - segment) % SEGMENT_COUNT
    return TAIL[distance] if distance < len(TAIL) else 0


def generate() -> list[list[int]]:
    rows: list[list[int]] = []
    for phase in range(SAMPLE_COUNT):
        head = phase // STEPS_PER_SEGMENT
        fraction = phase % STEPS_PER_SEGMENT
        next_head = (head + 1) % SEGMENT_COUNT
        rows.append([
            (
                tail_at(head, segment) * (STEPS_PER_SEGMENT - fraction)
                + tail_at(next_head, segment) * fraction
                + STEPS_PER_SEGMENT // 2
            )
            // STEPS_PER_SEGMENT
            for segment in range(SEGMENT_COUNT)
        ])
    return rows


def emit(output: Path) -> None:
    rows = generate()
    lines = [
        "#pragma once",
        "",
        "namespace codex {",
        "",
        f"inline constexpr unsigned int kAmbientMotionSampleCount = {SAMPLE_COUNT};",
        f"inline constexpr unsigned int kAmbientMotionSegmentCount = {SEGMENT_COUNT};",
        "inline constexpr unsigned char kAmbientMotionSamples",
        "    [kAmbientMotionSampleCount][kAmbientMotionSegmentCount] = {",
    ]
    for row in rows:
        lines.append("    {" + ", ".join(str(value) for value in row) + "},")
    lines += [
        "};",
        "",
        "[[nodiscard]] constexpr unsigned int ambient_motion_phase_index(",
        "    float phase) {",
        "  if (phase <= 0.0F) return 0;",
        "  if (phase >= 1.0F) return kAmbientMotionSampleCount - 1U;",
        "  return static_cast<unsigned int>(phase * kAmbientMotionSampleCount) &",
        "         (kAmbientMotionSampleCount - 1U);",
        "}",
        "",
        "[[nodiscard]] constexpr unsigned char ambient_motion_sample(",
        "    unsigned int phase, unsigned int segment) {",
        "  return kAmbientMotionSamples[phase & (kAmbientMotionSampleCount - 1U)]",
        "                              [segment % kAmbientMotionSegmentCount];",
        "}",
        "",
        "}  // namespace codex",
        "",
    ]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    emit(args.output)


if __name__ == "__main__":
    main()
