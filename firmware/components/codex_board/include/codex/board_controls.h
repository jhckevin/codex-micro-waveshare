#pragma once

#include "codex/device_event.h"
#include "esp_err.h"

namespace codex {

using BoardEventCallback = void (*)(const DeviceEvent& event, void* context);
esp_err_t board_controls_start(BoardEventCallback callback, void* context);
void board_controls_set_standby(bool standby);
esp_err_t board_controls_start_ultra_touch();
void board_controls_stop_ultra_touch();
[[nodiscard]] bool board_controls_ultra_touch_pressed();

}  // namespace codex
