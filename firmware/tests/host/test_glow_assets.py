#!/usr/bin/env python3
"""Verify pre-generated key glows fade fully before their sprite boundary."""

from pathlib import Path
import re


SOURCE = (
    Path(__file__).resolve().parents[2]
    / "components"
    / "codex_ui"
    / "glow_images_generated.cpp"
)


def read_mask(name: str) -> tuple[list[int], int, int]:
    text = SOURCE.read_text(encoding="utf-8")
    data_match = re.search(
        rf"unsigned char {name}_map\[\] = \{{(.*?)\n\}};",
        text,
        re.DOTALL,
    )
    descriptor_match = re.search(
        rf"const lv_image_dsc_t {name} = \{{\s*"
        rf"\{{[^,]+,\s*[^,]+,\s*0,\s*(\d+),\s*(\d+),",
        text,
        re.DOTALL,
    )
    assert data_match is not None
    assert descriptor_match is not None
    values = [int(value, 16) for value in re.findall(r"0x([0-9a-f]{2})", data_match.group(1))]
    width, height = map(int, descriptor_match.groups())
    assert len(values) == width * height
    return values, width, height


def assert_transparent_border(name: str, expected_size: tuple[int, int]) -> None:
    values, width, height = read_mask(name)
    assert (width, height) == expected_size
    border = (
        values[:width]
        + values[-width:]
        + values[0::width]
        + values[width - 1::width]
    )
    assert max(border) == 0


def assert_ambient_profile(
    name: str, expected_size: tuple[int, int], horizontal: bool
) -> None:
    values, width, height = read_mask(name)
    assert (width, height) == expected_size
    center_x = width // 2
    center_y = height // 2
    assert values[center_y * width + center_x] == 255
    padding = 18
    if horizontal:
        profile = [
            values[y * width + center_x]
            for y in range(padding + 1)
        ]
    else:
        profile = [
            values[center_y * width + x]
            for x in range(padding + 1)
        ]
    assert profile[0] == 0
    assert profile[-1] == 255
    assert 0 < profile[-2] < 255
    assert all(before <= after for before, after in zip(profile, profile[1:]))


assert_transparent_border("key_glow_small", (127, 122))
assert_transparent_border("key_glow_wide", (225, 122))
assert_transparent_border("ambient_glow_horizontal", (98, 45))
assert_transparent_border("ambient_glow_vertical", (45, 98))
assert_ambient_profile("ambient_glow_horizontal", (98, 45), True)
assert_ambient_profile("ambient_glow_vertical", (45, 98), False)
