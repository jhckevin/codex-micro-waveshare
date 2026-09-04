#!/usr/bin/env python3
from pathlib import Path


root = Path(__file__).resolve().parents[2]
touch_port = (
    root
    / "third_party"
    / "espressif__esp_lvgl_port"
    / "src"
    / "lvgl9"
    / "esp_lvgl_port_touch.c"
).read_text(encoding="utf-8")
app = (root / "main" / "app_main.cpp").read_text(encoding="utf-8")

function = touch_port.split(
    "esp_err_t lvgl_port_touch_set_polling_mode", 1
)[1].split("esp_err_t lvgl_port_remove_touch", 1)[0]

assert "if (!lvgl_port_lock(100))" in function
assert "return ESP_ERR_TIMEOUT;" in function
assert function.index("lvgl_port_lock(100)") < function.index(
    "lv_indev_set_mode"
)
assert "lvgl_port_lock(0);" not in function
assert "ESP_ERROR_CHECK(lvgl_port_touch_set_polling_mode" not in app
assert "standby-touch-cancel" in app
assert app.index(
    "__atomic_store_n(&board_touch_enabled, false, __ATOMIC_RELEASE);",
    app.index("SideEffectType::ExitStandby"),
) < app.index("fade_display_in", app.index("SideEffectType::ExitStandby"))

print("touch polling lock contract ok")
