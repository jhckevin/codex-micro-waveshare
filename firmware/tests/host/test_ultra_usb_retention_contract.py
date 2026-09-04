from pathlib import Path


SOURCE = (
    Path(__file__).resolve().parents[2] / "main/app_main.cpp"
).read_text(encoding="utf-8")
enter = SOURCE[
    SOURCE.index("bool enter_ultra_hardware"):
    SOURCE.index("bool exit_ultra_hardware")
]
if "ultra_usb_retained = !codex::codex_usb_connected();" not in enter:
    raise AssertionError("unmounted native USB must be retained across Ultra")
if "ultra_usb_retained ? ESP_OK : codex::codex_usb_stop()" not in enter:
    raise AssertionError("mounted native USB must still disconnect cleanly")

print("ultra USB retention contract passed")
