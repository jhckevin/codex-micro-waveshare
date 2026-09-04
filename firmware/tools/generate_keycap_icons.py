#!/usr/bin/env python3
"""Build a compact LVGL A8 icon atlas from Codex Micro SVG sources.

Run `prepare`, render the resulting HTML at the reported viewport with a
Chromium screenshot, then run `finalize`. This keeps SVG tooling out of the
firmware build and preserves deterministic generated C sources.
"""

from __future__ import annotations

import argparse
import io
import re
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

CELL = 48
COLUMNS = 8
ALIASES = {
    "FORK": "COMPUTER",
    "MAGIC": "FAVOURITE",
    "FAVORITE": "FAVOURITE",
}


def names(source: Path) -> list[str]:
    return sorted((path.stem for path in source.glob("*.svg")),
                  key=keycap_id) + ["YOLO", "YEET"]


def keycap_id(name: str) -> str:
    """Return the stable configuration ID exposed over CDC/BLE."""
    if name.upper() == "MIND-":
        return "MIND-"
    return re.sub(r"[^A-Z0-9+]+", "_", name.upper()).strip("_")


def prepare(source: Path, html: Path) -> None:
    cells = []
    for name in names(source):
        if name in ("YOLO", "YEET"):
            cells.append(f'<div><span>{name.lower()}</span></div>')
        else:
            svg = (source / f"{name}.svg").read_text(encoding="utf-8")
            svg = re.sub(r"<\?xml[^>]*>\s*", "", svg)
            cells.append(f"<div>{svg}</div>")
    width = COLUMNS * CELL
    rows = (len(cells) + COLUMNS - 1) // COLUMNS
    height = rows * CELL
    document = f"""<!doctype html><style>
html,body{{margin:0;padding:0;width:{width}px;height:{height}px;background:transparent;}}
body{{display:grid;grid-template-columns:repeat({COLUMNS},{CELL}px);}}
div{{width:{CELL}px;height:{CELL}px;display:flex;align-items:center;justify-content:center;color:#000;}}
svg{{width:36px!important;height:36px!important;display:block;}}
span{{font:italic 700 15px Georgia,serif;letter-spacing:-1px;}}
</style>{''.join(cells)}"""
    html.parent.mkdir(parents=True, exist_ok=True)
    html.write_text(document, encoding="utf-8")
    print(f"{width}x{height}")


def render(source: Path, screenshot: Path) -> None:
    """Render the atlas without a browser for reproducible remote builds."""
    try:
        import cairosvg
        rasterize = lambda svg: cairosvg.svg2png(
            bytestring=svg, output_width=36, output_height=36)
    except (ImportError, OSError):
        import resvg_py
        rasterize = lambda svg: resvg_py.svg_to_bytes(
            svg_string=svg.decode("utf-8"), width=36, height=36)

    icon_names = names(source)
    rows = (len(icon_names) + COLUMNS - 1) // COLUMNS
    atlas = Image.new("RGBA", (COLUMNS * CELL, rows * CELL), (0, 0, 0, 0))
    for index, name in enumerate(icon_names):
        left = (index % COLUMNS) * CELL
        top = (index // COLUMNS) * CELL
        if name in ("YOLO", "YEET"):
            cell = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
            draw = ImageDraw.Draw(cell)
            try:
                font = ImageFont.truetype("DejaVuSerif-BoldItalic.ttf", 15)
            except OSError:
                font = ImageFont.load_default()
            text = name.lower()
            bounds = draw.textbbox((0, 0), text, font=font)
            x = (CELL - (bounds[2] - bounds[0])) // 2
            y = (CELL - (bounds[3] - bounds[1])) // 2 - bounds[1]
            draw.text((x, y), text, fill=(0, 0, 0, 255), font=font)
            atlas.alpha_composite(cell, (left, top))
            continue
        svg = (source / f"{name}.svg").read_bytes()
        png = rasterize(svg)
        icon = Image.open(io.BytesIO(png)).convert("RGBA")
        atlas.alpha_composite(icon, (left + 6, top + 6))
    screenshot.parent.mkdir(parents=True, exist_ok=True)
    atlas.save(screenshot)
    print(f"{atlas.width}x{atlas.height}")


def symbol(name: str) -> str:
    # Preserve semantically meaningful suffixes before sanitising.  In
    # particular MIND+ and MIND- otherwise collide as the same C symbol.
    safe = keycap_id(name).lower().replace("+", "_plus").replace("-", "_minus")
    return "icon_" + re.sub(r"[^a-z0-9]+", "_", safe).strip("_")


def finalize(source: Path, screenshot: Path, output: Path, header: Path) -> None:
    icon_names = names(source)
    image = Image.open(screenshot).convert("RGBA")
    expected_height = ((len(icon_names) + COLUMNS - 1) // COLUMNS) * CELL
    if image.size != (COLUMNS * CELL, expected_height):
        raise SystemExit(f"unexpected screenshot size {image.size}")
    output.parent.mkdir(parents=True, exist_ok=True)
    declarations = ["#pragma once", "", '#include "lvgl.h"', "", "namespace codex {",
                    "const lv_image_dsc_t* icon_for_keycap(const char* id);", "", "}  // namespace codex", ""]
    header.write_text("\n".join(declarations), encoding="utf-8")
    lines = ['#include "keycap_icons.h"', "", "namespace codex {", "namespace {", ""]
    for index, name in enumerate(icon_names):
        left = (index % COLUMNS) * CELL
        top = (index // COLUMNS) * CELL
        alpha = image.crop((left, top, left + CELL, top + CELL)).getchannel("A")
        values = list(alpha.getdata())
        sym = symbol(name)
        lines.append(f"const LV_ATTRIBUTE_MEM_ALIGN unsigned char {sym}_map[] = {{")
        for offset in range(0, len(values), 24):
            lines.append("    " + ", ".join(f"0x{value:02x}" for value in values[offset:offset + 24]) + ",")
        # Positional aggregate initialization works in both C++17 and C.  GCC's
        # nested designators (`.header.cf`) do not compile when this generated
        # file is built as C++ alongside the UI implementation.
        lines += ["};", f"const lv_image_dsc_t {sym} = {{",
                  f"    {{LV_IMAGE_HEADER_MAGIC, LV_COLOR_FORMAT_A8, 0, {CELL}, {CELL}, {CELL}, 0}},",
                  f"    sizeof({sym}_map),", f"    {sym}_map,", "    nullptr,", "};", ""]
    lines += ["bool same(const char* a, const char* b) {",
              "  unsigned int i = 0; while (a[i] && b[i] && a[i] == b[i]) ++i;",
              "  return a[i] == b[i];", "}", "", "}  // namespace", "",
              "const lv_image_dsc_t* icon_for_keycap(const char* id) {"]
    for name in icon_names:
        lines.append(
            f'  if (same(id, "{keycap_id(name)}")) return &{symbol(name)};')
    symbols = {keycap_id(name): symbol(name) for name in icon_names}
    for alias, target in ALIASES.items():
        lines.append(f'  if (same(id, "{alias}")) return &{symbols[target]};')
    lines += ["  return nullptr;", "}", "", "}  // namespace codex", ""]
    output.write_text("\n".join(lines), encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=("prepare", "render", "finalize"))
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--html", type=Path)
    parser.add_argument("--screenshot", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--header", type=Path)
    args = parser.parse_args()
    if args.mode == "prepare":
        prepare(args.source, args.html)
    elif args.mode == "render":
        render(args.source, args.screenshot)
    else:
        finalize(args.source, args.screenshot, args.output, args.header)


if __name__ == "__main__":
    main()
