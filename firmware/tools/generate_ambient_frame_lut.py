#!/usr/bin/env python3
"""Generate 8-bit, 64-phase ambient frames for the fixed 16-sample ring."""

from __future__ import annotations

import argparse
import math
from pathlib import Path


PHASE_COUNT = 64
SEGMENT_COUNT = 16
STEPS_PER_SEGMENT = PHASE_COUNT // SEGMENT_COUNT
TAIL = (255, 142, 54, 14)


def tail_at(head: int, segment: int) -> int:
    distance = (head + SEGMENT_COUNT - segment) % SEGMENT_COUNT
    return TAIL[distance] if distance < len(TAIL) else 0


def snake_rows() -> list[list[int]]:
    rows: list[list[int]] = []
    for phase in range(PHASE_COUNT):
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


def envelope(minimum: float) -> list[int]:
    return [
        round(
            255.0
            * (
                minimum
                + (1.0 - minimum)
                * (0.5 - 0.5 * math.cos(2.0 * math.pi * phase / PHASE_COUNT))
            )
        )
        for phase in range(PHASE_COUNT)
    ]


def emit(output: Path) -> None:
    rows = snake_rows()
    breath = envelope(4.0 / 31.0)
    shallow = envelope(20.0 / 31.0)
    lines = [
        "#pragma once",
        "",
        '#include "codex/device_state.h"',
        "",
        "namespace codex {",
        "",
        f"inline constexpr unsigned int kAmbientFramePhaseCount = {PHASE_COUNT}U;",
        f"inline constexpr unsigned int kAmbientFrameSegmentCount = {SEGMENT_COUNT}U;",
        "inline constexpr unsigned char kAmbientSnakeFrames",
        "    [kAmbientFramePhaseCount][kAmbientFrameSegmentCount] = {",
    ]
    for row in rows:
        lines.append("    {" + ", ".join(f"{value}U" for value in row) + "},")
    lines += [
        "};",
        "",
        "inline constexpr unsigned char kAmbientBreathFrames",
        "    [kAmbientFramePhaseCount] = {",
        "    " + ", ".join(f"{value}U" for value in breath) + "};",
        "inline constexpr unsigned char kAmbientShallowBreathFrames",
        "    [kAmbientFramePhaseCount] = {",
        "    " + ", ".join(f"{value}U" for value in shallow) + "};",
        "",
        "[[nodiscard]] constexpr unsigned int ambient_frame_phase_index(",
        "    float phase) {",
        "  if (phase <= 0.0F) return 0U;",
        "  if (phase >= 1.0F) return kAmbientFramePhaseCount - 1U;",
        "  return static_cast<unsigned int>(phase * kAmbientFramePhaseCount) &",
        "         (kAmbientFramePhaseCount - 1U);",
        "}",
        "",
        "[[nodiscard]] constexpr unsigned char ambient_frame_sample(",
        "    LightEffect effect, unsigned int phase, unsigned int segment,",
        "    float brightness) {",
        "  if (effect == LightEffect::Off || brightness <= 0.0F) return 0U;",
        "  if (brightness > 1.0F) brightness = 1.0F;",
        "  phase &= kAmbientFramePhaseCount - 1U;",
        "  segment %= kAmbientFrameSegmentCount;",
        "  unsigned int sample = 255U;",
        "  switch (effect) {",
        "    case LightEffect::Snake:",
        "      sample = kAmbientSnakeFrames[phase][segment];",
        "      break;",
        "    case LightEffect::Breath:",
        "      sample = kAmbientBreathFrames[phase];",
        "      break;",
        "    case LightEffect::ShallowBreath:",
        "      sample = kAmbientShallowBreathFrames[phase];",
        "      break;",
        "    default:",
        "      break;",
        "  }",
        "  return static_cast<unsigned char>(",
        "      static_cast<float>(sample) * brightness + 0.5F);",
        "}",
        "",
        "[[nodiscard]] constexpr unsigned char ambient_frame_global(",
        "    LightEffect effect, unsigned int phase, float brightness) {",
        "  if (effect == LightEffect::Breath ||",
        "      effect == LightEffect::ShallowBreath) {",
        "    return ambient_frame_sample(effect, phase, 0U, brightness);",
        "  }",
        "  return ambient_frame_sample(",
        "      effect == LightEffect::Off ? LightEffect::Off",
        "                                 : LightEffect::Solid,",
        "      phase, 0U, brightness);",
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
