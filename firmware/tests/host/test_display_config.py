#!/usr/bin/env python3
"""Keep the RGB recovery guard enabled for PSRAM-backed direct rendering."""

from pathlib import Path


DEFAULTS = (
    Path(__file__).resolve().parents[2] / "sdkconfig.defaults"
).read_text(encoding="utf-8")
ROOT = Path(__file__).resolve().parents[2]
BOARD_DISPLAY = (
    ROOT
    / "third_party/waveshare__esp32_s3_touch_lcd_4b/esp32_s3_touch_lcd_4b.c"
).read_text(encoding="utf-8")
BOARD_DISPLAY_HEADER = (
    ROOT
    / "third_party/waveshare__esp32_s3_touch_lcd_4b/include/bsp/display.h"
).read_text(encoding="utf-8")
TOUCH_HEADER = (
    ROOT / "third_party/espressif__esp_lvgl_port/include/esp_lvgl_port_touch.h"
).read_text(encoding="utf-8")
TOUCH_SOURCE = (
    ROOT
    / "third_party/espressif__esp_lvgl_port/src/lvgl9/esp_lvgl_port_touch.c"
).read_text(encoding="utf-8")
APP_MAIN = (ROOT / "main/app_main.cpp").read_text(encoding="utf-8")
BOARD_CONTROLS = (
    ROOT / "components/codex_board/board_controls.cpp"
).read_text(encoding="utf-8")
UI_SOURCE = (ROOT / "components/codex_ui/ui.cpp").read_text(encoding="utf-8")
ACTIVE_UI_SOURCE = UI_SOURCE.split(
    "#endif", 1
)[1]

assert "CONFIG_BSP_LCD_RGB_BUFFER_NUMS=2" in DEFAULTS
assert "CONFIG_BSP_DISPLAY_LVGL_DIRECT_MODE=y" in DEFAULTS
assert "CONFIG_LCD_RGB_RESTART_IN_VSYNC=y" in DEFAULTS
assert "ambient_strip_buffers[kAmbientStripTileCount][2]" in UI_SOURCE
assert "ambient_strip_images[kAmbientStripTileCount][2]" in UI_SOURCE
assert "lv_image_cache_drop(image);" in ACTIVE_UI_SOURCE
assert "commit_ambient_strip_frame(tile);" in ACTIVE_UI_SOURCE
assert (
    "ambient_strip_fill(ui.ambient_strip_buffers[tile], tile, phase)"
    not in ACTIVE_UI_SOURCE
), "the visible ambient source must never be rewritten in place"
assert (
    ".bounce_buffer_size_px = BSP_LCD_DRAW_BUFF_SIZE,"
    in BOARD_DISPLAY
), "the board-qualified RGB bounce buffer must remain enabled"
assert ".bb_mode = 1," in BOARD_DISPLAY
assert ".bb_mode = 0," not in BOARD_DISPLAY, (
    "a configured RGB bounce buffer must release LVGL framebuffers on "
    "bounce-frame completion, never on the earlier VSYNC edge"
)
assert ".timings = ST7701_480_480_PANEL_60HZ_RGB_TIMING()" not in BOARD_DISPLAY
for board_timing in (
    ".pclk_hz = BSP_LCD_PIXEL_CLOCK_HZ,",
    ".hsync_pulse_width = 8,",
    ".hsync_back_porch = 50,",
    ".hsync_front_porch = 10,",
    ".vsync_pulse_width = 8,",
    ".vsync_back_porch = 20,",
    ".vsync_front_porch = 10,",
    ".flags.pclk_active_neg = false,",
):
    assert board_timing in BOARD_DISPLAY, (
        "RGB scanout must retain the timing shipped in Waveshare's 4B demo: "
        + board_timing
    )
assert "lvgl_port_touch_set_polling_mode" in TOUCH_HEADER
assert "lv_indev_set_mode(touch, LV_INDEV_MODE_TIMER)" in TOUCH_SOURCE
assert "lvgl_port_touch_set_polling_mode(" in APP_MAIN
assert "bsp_display_panel_set_enabled(bool enabled)" in BOARD_DISPLAY_HEADER
assert "bsp_display_backlight_prepare_wake(void)" in BOARD_DISPLAY_HEADER
assert "esp_lcd_panel_disp_on_off(panel_handle, enabled)" in BOARD_DISPLAY
backlight_off = BOARD_DISPLAY[
    BOARD_DISPLAY.index("esp_err_t bsp_display_backlight_off(void)") :
    BOARD_DISPLAY.index("esp_err_t bsp_display_backlight_on(void)")
]
assert "ledc_stop(LEDC_LOW_SPEED_MODE, LCD_LEDC_CH, 1)" in backlight_off, (
    "zero-percent PWM is not a hard electrical off at 10-bit resolution; "
    "standby must stop LEDC and hold the active-low backlight GPIO high"
)
brightness_set = BOARD_DISPLAY[
    BOARD_DISPLAY.index("esp_err_t bsp_display_brightness_set(int brightness_percent)") :
    BOARD_DISPLAY.index("int bsp_display_brightness_get(void)")
]
assert "__atomic_load_n(&backlight_hard_off, __ATOMIC_ACQUIRE)" in brightness_set, (
    "late brightness publications must not release the standby hard-off latch"
)
assert "__atomic_store_n(&backlight_hard_off, true, __ATOMIC_RELEASE);" in backlight_off
prepare_wake = BOARD_DISPLAY[
    BOARD_DISPLAY.index("esp_err_t bsp_display_backlight_prepare_wake(void)") :
    BOARD_DISPLAY.index("esp_err_t bsp_display_backlight_on(void)")
]
assert "ledc_set_pin(BSP_LCD_BACKLIGHT" in prepare_wake
assert "__atomic_store_n(&backlight_hard_off, false, __ATOMIC_RELEASE);" in prepare_wake
assert ".auto_del_panel_io = 0" in BOARD_DISPLAY, (
    "the ST7701 3-wire command IO must survive initialization so standby can "
    "send DISPOFF and wake can send DISPON"
)
fade_out = APP_MAIN[
    APP_MAIN.index("void fade_display_out(") : APP_MAIN.index(
        "void fade_display_in("
    )
]
fade_in = APP_MAIN[
    APP_MAIN.index("void fade_display_in(") : APP_MAIN.index(
        "void copy_text("
    )
]
physical_brightness = APP_MAIN[
    APP_MAIN.index("unsigned char physical_brightness(") :
    APP_MAIN.index("void fade_display_out(")
]
assert "return setting;" in physical_brightness
assert "setting <= 10 ? 5" not in physical_brightness
assert fade_out.index("hard_off_display_backlight()") < fade_out.index(
    "bsp_display_panel_set_enabled(false)"
), "standby must fade the backlight out before switching the ST7701 off"
assert fade_in.index("bsp_display_backlight_prepare_wake()") < fade_in.index(
    "bsp_display_panel_set_enabled(true)"
), "only the explicit wake path may release the hard-off latch"
assert fade_in.index("bsp_display_panel_set_enabled(true)") < fade_in.index(
    "bsp_display_brightness_set("
), "wake must switch the ST7701 on before restoring the backlight"
assert "standby_polling ? 1U : 3U" in BOARD_CONTROLS
assert "standby_polling ? 40 : 20" in BOARD_CONTROLS
assert "update_button_gesture_on_press(power_gesture" in BOARD_CONTROLS
assert "__atomic_load_n(\n        &arcade_ui_command_requested" in APP_MAIN
assert "__atomic_exchange_n(\n        &arcade_ui_command_requested" not in APP_MAIN
assert "make_arcade_transition" not in UI_SOURCE
assert "lv_obj_delete_async(ui.arcade.screen)" in UI_SOURCE
assert "make_key(screen, {48, 185, 91, 91}" in UI_SOURCE
assert "lv_obj_t* dpad_keys[4]" in UI_SOURCE
assert "LV_SYMBOL_UP" in UI_SOURCE
assert "LV_SYMBOL_DOWN" in UI_SOURCE
assert "LV_SYMBOL_LEFT" in UI_SOURCE
assert "LV_SYMBOL_RIGHT" in UI_SOURCE
assert "ui.arcade.ring = lv_obj_create(screen)" in UI_SOURCE
assert "render_arcade_ambient(lighting)" in UI_SOURCE
assert "ambient_movers" not in ACTIVE_UI_SOURCE
assert "render_ambient_movers" not in ACTIVE_UI_SOURCE
assert "make_ambient_segments(ui.ambient)" not in ACTIVE_UI_SOURCE
assert "make_ambient_strips(screen)" in ACTIVE_UI_SOURCE
assert "lv_color_hex(kClassicScreenBackdropColor)" in ACTIVE_UI_SOURCE
assert "ambient_strip_fill(" in ACTIVE_UI_SOURCE
assert "heap_caps_calloc(" in ACTIVE_UI_SOURCE
assert "effect_render_color(lighting.effect, source_color," in UI_SOURCE
assert "kAmbientSnakeMoverCount = 5" in (
    ROOT / "components/codex_ui/include/codex/ambient_path_lut.h"
).read_text(encoding="utf-8")
assert "ambient_glow_horizontal" not in ACTIVE_UI_SOURCE
assert "#if 0  // Disabled opaque ring experiment" in UI_SOURCE

# Serialize whole duty writes; protect only backlight pad, never all GPIOs.
assert "xSemaphoreTake(backlight_mutex, portMAX_DELAY)" in brightness_set
assert "xSemaphoreTake(backlight_mutex, portMAX_DELAY)" in backlight_off
assert "gpio_hold_en(BSP_LCD_BACKLIGHT)" in backlight_off
assert backlight_off.index("gpio_set_level(BSP_LCD_BACKLIGHT, 1)") < backlight_off.index("gpio_hold_en(BSP_LCD_BACKLIGHT)")
assert "gpio_get_level(BSP_LCD_BACKLIGHT)" in backlight_off
assert "gpio_hold_dis(BSP_LCD_BACKLIGHT)" in prepare_wake
assert "gpio_force_hold_all" not in BOARD_DISPLAY
assert "backlight hard-off failed" in APP_MAIN
ultra_wake = APP_MAIN[APP_MAIN.index("bool exit_ultra_hardware("):APP_MAIN.index("bool exit_ultra_hardware(")+2500]
assert ultra_wake.index("bsp_display_backlight_prepare_wake()") < ultra_wake.index("bsp_display_brightness_set(")

brightness_init = BOARD_DISPLAY[BOARD_DISPLAY.index("esp_err_t bsp_display_brightness_init(void)"):BOARD_DISPLAY.index("esp_err_t bsp_display_brightness_set(int brightness_percent)")]
assert brightness_init.index("gpio_set_level(BSP_LCD_BACKLIGHT, 1)") < brightness_init.index("gpio_hold_dis(BSP_LCD_BACKLIGHT)")
assert brightness_init.index("gpio_hold_dis(BSP_LCD_BACKLIGHT)") < brightness_init.index("ledc_channel_config(&LCD_backlight_channel)")
