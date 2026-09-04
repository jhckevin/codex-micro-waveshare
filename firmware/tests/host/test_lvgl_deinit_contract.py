from pathlib import Path


source = Path("third_party/lvgl_init_patch/lv_init.c").read_text(
    encoding="utf-8"
)
start = source.index("void lv_deinit(void)")
end = source.index("lv_initialized = false;", start)
body = source[start:end]

assert body.count("lv_draw_sw_deinit();") == 1, (
    "LVGL 9.2 must deinitialize the software renderer exactly once; "
    "the upstream duplicate double-frees the FreeRTOS mask mutex"
)

assert "third_party/lvgl_init_patch/lv_init.c" in Path("CMakeLists.txt").read_text()
