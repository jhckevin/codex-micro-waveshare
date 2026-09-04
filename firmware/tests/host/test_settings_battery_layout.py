from pathlib import Path
import re


root = Path(__file__).resolve().parents[2]
source = (root / "components/codex_ui/ui.cpp").read_text(encoding="utf-8")

assert "lv_obj_t* battery_card = lv_obj_create(page_one);" in source

size = re.search(
    r"lv_obj_t\* battery_card = lv_obj_create\(page_one\);\s*"
    r"lv_obj_set_size\(battery_card,\s*(\d+),\s*(\d+)\);",
    source,
)
assert size, "battery card size must be explicit"
assert int(size.group(1)) <= 190, "battery card must not consume the page width"
assert int(size.group(2)) <= 44, "battery card must remain compact"

status_block = source[
    source.index("const BatteryDisplayModel battery")
    : source.index("const char* super_label")
]
assert "路" not in status_block
assert "·" not in status_block
assert " | " in status_block

page_two_start = source.index("lv_obj_t* page_two")
firmware_start = source.index("lv_obj_t* firmware_version")
assert firmware_start > page_two_start

print("PASS settings_battery_layout")
