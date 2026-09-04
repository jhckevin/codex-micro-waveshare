#!/usr/bin/env python3
"""Generate reusable A8 glow masks so LVGL does no runtime shadow blur."""

from __future__ import annotations

import argparse
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter


def fade_sprite_edges(image: Image.Image, width: int = 8) -> Image.Image:
    """Fade a blurred mask to exact transparency without enlarging its sprite."""
    faded = image.copy()
    pixels = faded.load()
    image_width, image_height = faded.size
    for y in range(image_height):
        for x in range(image_width):
            distance = min(x, y, image_width - 1 - x, image_height - 1 - y)
            if distance >= width:
                continue
            t = distance / width
            smooth = t * t * (3.0 - 2.0 * t)
            pixels[x, y] = round(pixels[x, y] * smooth)
    return faded


def glow_mask(content_width: int, content_height: int, radius: int,
              padding: int, blur: int, fade_edges: bool = False,
              fade_width: int = 8) -> Image.Image:
    size = (content_width + padding * 2, content_height + padding * 2)
    core = Image.new("L", size, 0)
    draw = ImageDraw.Draw(core)
    draw.rounded_rectangle(
        (padding, padding, padding + content_width - 1,
         padding + content_height - 1),
        radius=radius, fill=255)
    blurred = core.filter(ImageFilter.GaussianBlur(blur))
    return fade_sprite_edges(blurred, fade_width) if fade_edges else blurred


def luminous_glow_mask(content_width: int, content_height: int, radius: int,
                       padding: int = 18, blur: int = 9) -> Image.Image:
    """Bake the early renderer's solid rounded core and wide soft shadow."""
    size = (content_width + padding * 2, content_height + padding * 2)
    core = Image.new("L", size, 0)
    draw = ImageDraw.Draw(core)
    draw.rounded_rectangle(
        (padding, padding, padding + content_width - 1,
         padding + content_height - 1),
        radius=radius, fill=255)
    halo = core.filter(ImageFilter.GaussianBlur(blur))
    combined = ImageChops.lighter(core, halo)
    return fade_sprite_edges(combined, width=6)


def emit(output: Path, header: Path) -> None:
    images = {
        "key_glow_small": glow_mask(
            91, 86, 17, 18, 8, fade_edges=True, fade_width=12),
        "key_glow_wide": glow_mask(
            189, 86, 17, 18, 8, fade_edges=True, fade_width=12),
        "ambient_glow_horizontal": luminous_glow_mask(62, 9, 5),
        "ambient_glow_vertical": luminous_glow_mask(9, 62, 5),
    }
    header.write_text(
        '#pragma once\n\n#include "lvgl.h"\n\nnamespace codex {\n'
        + "".join(f"extern const lv_image_dsc_t {name};\n"
                  for name in images)
        + "\n}  // namespace codex\n",
        encoding="utf-8")
    lines = ['#include "glow_images.h"', "", "namespace codex {", ""]
    for name, image in images.items():
        values = list(image.get_flattened_data())
        lines.append(
            f"const LV_ATTRIBUTE_MEM_ALIGN unsigned char {name}_map[] = {{")
        for offset in range(0, len(values), 32):
            lines.append("    " + ", ".join(
                f"0x{value:02x}" for value in values[offset:offset + 32]) + ",")
        width, height = image.size
        lines += [
            "};",
            f"const lv_image_dsc_t {name} = {{",
            f"    {{LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_A8, 0, "
            f"{width}, {height}, {width}, 0}},",
            f"    sizeof({name}_map),",
            f"    {name}_map,",
            "    nullptr,",
            "};",
            "",
        ]
    lines += ["}  // namespace codex", ""]
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.header.parent.mkdir(parents=True, exist_ok=True)
    emit(args.output, args.header)


if __name__ == "__main__":
    main()
