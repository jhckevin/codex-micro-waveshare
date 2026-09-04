from pathlib import Path


source = Path(
    "third_party/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port_touch.c"
).read_text(encoding="utf-8")

private_start = source.index("* Private functions")
read_start = source.index("static void lvgl_port_touchpad_read(", private_start)
read_body_start = source.index("{", read_start)
read_end = source.index("static void touch_sampler_task(", read_body_start)
read_body = source[read_body_start:read_end]

assert "touch_sampler_task" in source
assert "vTaskNotifyGiveFromISR" in source
assert "esp_lcd_touch_read_data" not in read_body
assert "xSemaphoreTake(touch_ctx->sampler_stopped" in source
assert "lvgl_port_task_wake(LVGL_PORT_EVENT_TOUCH" in source

print("async touch sampling contract passed")
