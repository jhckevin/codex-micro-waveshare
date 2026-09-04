from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
TRANSPORT_HEADER = (
    ROOT / "components/codex_transport/include/codex/transport.h"
).read_text(encoding="utf-8")
BLE_SOURCE = (
    ROOT / "components/codex_transport/ble_transport.cpp"
).read_text(encoding="utf-8")
MAIN_SOURCE = (ROOT / "main/app_main.cpp").read_text(encoding="utf-8")


def require(fragment: str, source: str, description: str) -> None:
    if fragment not in source:
        raise AssertionError(f"missing {description}: {fragment}")


require("esp_err_t codex_ble_suspend();", TRANSPORT_HEADER,
        "public BLE ultra suspend API")
require("esp_err_t codex_ble_resume();", TRANSPORT_HEADER,
        "public BLE ultra resume API")
require("std::atomic_bool suspended", BLE_SOURCE,
        "transport-level suspended state")
require("esp_ble_gap_stop_advertising()", BLE_SOURCE,
        "advertising stop without stack teardown")
require("if (suspended.load", BLE_SOURCE,
        "advertising guard while suspended")
require("codex::codex_ble_suspend()", MAIN_SOURCE,
        "Ultra entry uses non-destructive BLE suspension")
require("codex::codex_ble_resume()", MAIN_SOURCE,
        "Ultra exit resumes the retained BLE stack")
require("if (ble_link_active)", MAIN_SOURCE,
        "connected BLE standby branch")
require("vTaskDelay(pdMS_TO_TICKS(50))", MAIN_SOURCE,
        "modem-sleep scheduling window without CPU light sleep")

enter_start = MAIN_SOURCE.index("bool enter_ultra_hardware")
enter_end = MAIN_SOURCE.index("bool exit_ultra_hardware")
enter_body = MAIN_SOURCE[enter_start:enter_end]
if "codex::codex_ble_stop()" in enter_body:
    raise AssertionError("Ultra entry must not deinitialize the BLE stack")

suspend_start = BLE_SOURCE.index("esp_err_t codex_ble_suspend()")
suspend_end = BLE_SOURCE.index("esp_err_t codex_ble_resume()")
if "esp_ble_gap_disconnect" in BLE_SOURCE[suspend_start:suspend_end]:
    raise AssertionError(
        "Ultra must retain an authenticated BLE link to avoid host reconnect leaks")

print("ble ultra suspend contract passed")
