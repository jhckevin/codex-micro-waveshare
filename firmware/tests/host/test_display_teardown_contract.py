from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (
    ROOT
    / "third_party/waveshare__esp32_s3_touch_lcd_4b/"
    / "esp32_s3_touch_lcd_4b.c"
).read_text(encoding="utf-8")

stop = SOURCE[SOURCE.index("static esp_err_t bsp_display_detach(bool deinit_lvgl)"):]
unregister = stop.index(
    "esp_err_t error = esp_lcd_rgb_panel_register_event_callbacks")
remove = stop.index("esp_err_t error = lvgl_port_remove_disp")
if unregister > remove:
    raise AssertionError(
        "RGB VSYNC callback must be detached before LVGL frees its semaphore")
if "vTaskDelay(pdMS_TO_TICKS(25))" not in stop[unregister:remove]:
    raise AssertionError(
        "teardown must drain an in-flight VSYNC callback before freeing context")

print("display teardown contract passed")
