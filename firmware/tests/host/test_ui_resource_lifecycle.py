from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
UI = (ROOT / "components/codex_ui/ui.cpp").read_text(encoding="utf-8")
MAIN = (ROOT / "main/app_main.cpp").read_text(encoding="utf-8")

deinit_start = UI.index("void ui_deinit()")
release_start = UI.index("void ui_release_resources()", deinit_start)
end = UI.index("void ui_set_pointer_callback", release_start)
deinit = UI[deinit_start:release_start]
release = UI[release_start:end]

if "ui.ready = false" not in deinit:
    raise AssertionError("UI must reject updates before display removal")
if "lv_obj_clean" in deinit:
    raise AssertionError("UI teardown must not recursively clean objects under lock")
if "heap_caps_free(ui.ambient_strip_buffers[tile][bank])" not in release:
    raise AssertionError("UI teardown must release ambient A8 strip buffers")
reset = "ui = UiState{}"
if reset not in release:
    raise AssertionError("UI cached pointers must be reset explicitly")
if release.index("heap_caps_free(ui.ambient_strip_buffers[tile][bank])") > release.index(
    reset
):
    raise AssertionError("owned buffers must be freed before cached pointers reset")
if MAIN.index("bsp_display_suspend()") > MAIN.index("codex::ui_release_resources()"):
    raise AssertionError("A8 resources must remain alive until display removal completes")

print("UI resource lifecycle contract passed")
