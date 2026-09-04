from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
BSP = (
    ROOT / "third_party/waveshare__esp32_s3_touch_lcd_4b/"
    / "esp32_s3_touch_lcd_4b.c"
).read_text(encoding="utf-8")
MAIN = (ROOT / "main/app_main.cpp").read_text(encoding="utf-8")
PORT = (
    ROOT / "third_party/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port.c"
).read_text(encoding="utf-8")

if "return bsp_display_detach(false);" not in BSP:
    raise AssertionError("Ultra display suspend must retain LVGL global state")
if "bsp_display_suspend()" not in MAIN or "resume_display()" not in MAIN:
    raise AssertionError("Ultra must use the retained display lifecycle")
if "task_delay_ms = 250;" not in PORT:
    raise AssertionError("panel-less LVGL task must not poll at active frame rate")

print("display suspend contract passed")
