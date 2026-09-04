#!/usr/bin/env python3
"""Convert the supplied Codex dial reference into an LVGL ARGB8888 asset."""

from __future__ import annotations

import argparse
import math
from pathlib import Path

from PIL import Image

OUTPUT_SIZE = 91
FRAME_COUNT = 24


def prepare_reference(path: Path) -> Image.Image:
    source = Image.open(path).convert("RGBA")
    width, height = source.size
    side = min(width, height) - 16
    left = (width - side) // 2
    top = (height - side) // 2 + 1
    dial = source.crop((left, top, left + side, top + side))
    dial = dial.resize((OUTPUT_SIZE, OUTPUT_SIZE), Image.Resampling.LANCZOS)

    # The reference is flattened over white. A radial mask retains the real
    # photographed rim, diagonal face and bevel while removing only the
    # surrounding screenshot background and stray edge pixels.
    pixels = dial.load()
    center = (OUTPUT_SIZE - 1) / 2.0
    inner = OUTPUT_SIZE * 0.47
    outer = OUTPUT_SIZE * 0.505
    for y in range(OUTPUT_SIZE):
        for x in range(OUTPUT_SIZE):
            distance = math.hypot(x - center, y - center)
            if distance <= inner:
                alpha = 255
            elif distance >= outer:
                alpha = 0
            else:
                alpha = round(255 * (outer - distance) / (outer - inner))
            red, green, blue, source_alpha = pixels[x, y]
            pixels[x, y] = red, green, blue, min(alpha, source_alpha)
    return dial


def generate(reference: Path, output: Path, header: Path) -> None:
    image = prepare_reference(reference)
    frames = [
        image.rotate(-index * (360.0 / FRAME_COUNT),
                     resample=Image.Resampling.BICUBIC)
        for index in range(FRAME_COUNT)
    ]

    header.parent.mkdir(parents=True, exist_ok=True)
    header.write_text(
        '#pragma once\n\n#include "codex/dial_frame_policy.h"\n'
        '#include "lvgl.h"\n\nnamespace codex {\n'
        "extern const lv_image_dsc_t dial_frames[kDialFrameCount];\n"
        "\n}  // namespace codex\n",
        encoding="utf-8")

    lines = ['#include "dial_images.h"', "", "namespace codex {", ""]
    for frame_index, frame in enumerate(frames):
        values = []
        for red, green, blue, alpha in frame.getdata():
            values.extend((blue, green, red, alpha))
        lines.append(
            "const LV_ATTRIBUTE_MEM_ALIGN unsigned char "
            f"dial_image_map_{frame_index}[] = {{")
        for offset in range(0, len(values), 24):
            lines.append("    " + ", ".join(
                f"0x{value:02x}" for value in values[offset:offset + 24]) + ",")
        lines += ["};", ""]
    lines.append("const lv_image_dsc_t dial_frames[kDialFrameCount] = {")
    for frame_index in range(FRAME_COUNT):
        lines += [
            "  {",
            f"    {{LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_ARGB8888, 0, "
            f"{OUTPUT_SIZE}, {OUTPUT_SIZE}, {OUTPUT_SIZE * 4}, 0}},",
            f"    sizeof(dial_image_map_{frame_index}),",
            f"    dial_image_map_{frame_index},",
            "    nullptr,",
            "  },",
        ]
    lines += ["};", "", "}  // namespace codex", ""]
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--reference", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    args = parser.parse_args()
    generate(args.reference, args.output, args.header)


if __name__ == "__main__":
    main()
