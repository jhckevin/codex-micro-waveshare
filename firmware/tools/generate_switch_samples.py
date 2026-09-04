#!/usr/bin/env python3
"""Generate compact PCM switch samples from MechvibesDX config-v2 packs.

Run only on the remote build server with miniaudio installed. The generated
source is checked in so the ESP32 build itself has no audio decoder dependency.
"""

from __future__ import annotations

import argparse
import json
from array import array
from pathlib import Path

import miniaudio


SAMPLE_RATE = 22_050
KEYS = ("KeyA", "KeyS", "KeyD", "KeyF")
PACKS = (
    ("linear", "cherrymx-red-abs"),
    ("tactile", "cherrymx-brown-abs"),
)


def clean_clip(samples: array, start_ms: float, end_ms: float) -> list[int]:
    start = round(start_ms * SAMPLE_RATE / 1000.0)
    end = round(end_ms * SAMPLE_RATE / 1000.0)
    values = [int(value) for value in samples[start:end]]
    if not values:
        raise ValueError("empty audio segment")
    mean = sum(values) / len(values)
    values = [round(value - mean) for value in values]
    peak = max(abs(value) for value in values)
    if peak == 0:
        raise ValueError("silent audio segment")
    gain = 27_000.0 / peak
    values = [max(-32768, min(32767, round(value * gain))) for value in values]
    fade = min(32, len(values) // 5)
    for index in range(fade):
        values[index] = round(values[index] * index / fade)
        values[-1 - index] = round(values[-1 - index] * index / fade)
    return values


def emit_array(name: str, values: list[int]) -> str:
    lines = [f"alignas(4) const short {name}[] = {{"]
    for offset in range(0, len(values), 16):
        chunk = ", ".join(str(value) for value in values[offset : offset + 16])
        lines.append(f"    {chunk},")
    lines.append("};")
    return "\n".join(lines)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--mechvibes-dx", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    output = args.output_dir
    output.mkdir(parents=True, exist_ok=True)

    arrays: list[tuple[str, list[int]]] = []
    table: dict[tuple[str, str], list[str]] = {}
    for profile, pack_name in PACKS:
        pack = (
            args.mechvibes_dx / "soundpacks" / "keyboard" / pack_name
        )
        config = json.loads((pack / "config.json").read_text())
        if str(config.get("config_version")) != "2":
            raise ValueError(f"{pack_name} is not config-v2")
        decoded = miniaudio.decode_file(
            str(pack / config["audio_file"]),
            output_format=miniaudio.SampleFormat.SIGNED16,
            nchannels=1,
            sample_rate=SAMPLE_RATE,
        )
        for action_index, action in enumerate(("down", "up")):
            names: list[str] = []
            for variation, key in enumerate(KEYS):
                timing = config["definitions"][key]["timing"][action_index]
                name = f"k_{profile}_{action}_{variation}"
                arrays.append((name, clean_clip(decoded.samples, *timing)))
                names.append(name)
            table[(profile, action)] = names

    header = """#pragma once

namespace codex {

struct SwitchSample {
  const short* samples;
  unsigned short count;
};

constexpr unsigned int kSwitchSampleRate = 22050;
constexpr unsigned int kSwitchSampleVariations = 4;

[[nodiscard]] SwitchSample switch_sample(bool tactile, bool down,
                                         unsigned int variation);

}  // namespace codex
"""
    (output / "switch_samples_generated.h").write_text(header)

    source = [
        '#include "switch_samples_generated.h"',
        "",
        "namespace codex {",
        "namespace {",
        "",
    ]
    source.extend(emit_array(name, values) + "\n" for name, values in arrays)
    source.extend(
        [
            "constexpr SwitchSample kSamples[2][2][4] = {",
            "  {",
            "    {"
            + ", ".join(
                f"{{{name}, static_cast<unsigned short>(sizeof({name}) / sizeof(short))}}"
                for name in table[("linear", "down")]
            )
            + "},",
            "    {"
            + ", ".join(
                f"{{{name}, static_cast<unsigned short>(sizeof({name}) / sizeof(short))}}"
                for name in table[("linear", "up")]
            )
            + "},",
            "  },",
            "  {",
            "    {"
            + ", ".join(
                f"{{{name}, static_cast<unsigned short>(sizeof({name}) / sizeof(short))}}"
                for name in table[("tactile", "down")]
            )
            + "},",
            "    {"
            + ", ".join(
                f"{{{name}, static_cast<unsigned short>(sizeof({name}) / sizeof(short))}}"
                for name in table[("tactile", "up")]
            )
            + "},",
            "  },",
            "};",
            "",
            "}  // namespace",
            "",
            "SwitchSample switch_sample(bool tactile, bool down,",
            "                           unsigned int variation) {",
            "  return kSamples[tactile ? 1U : 0U][down ? 0U : 1U]",
            "                 [variation % kSwitchSampleVariations];",
            "}",
            "",
            "}  // namespace codex",
            "",
        ]
    )
    (output / "switch_samples_generated.cpp").write_text("\n".join(source))


if __name__ == "__main__":
    main()
