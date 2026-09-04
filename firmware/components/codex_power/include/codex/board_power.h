#pragma once

#include "codex/device_event.h"
#include "esp_err.h"

namespace codex {

using PowerEventCallback = void (*)(const DeviceEvent& event, void* context);

esp_err_t board_power_start(PowerEventCallback callback, void* context);
esp_err_t board_power_sample_now();
esp_err_t board_power_shutdown();

}  // namespace codex
