from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (ROOT / "main" / "app_main.cpp").read_text(encoding="utf-8")


def require(fragment: str) -> None:
    if fragment not in SOURCE:
        raise AssertionError(f"missing critical-input wake contract: {fragment}")


require("TaskHandle_t reducer_task_handle")
require("xTaskNotifyGive(reducer_task_handle)")
require("ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(16))")
