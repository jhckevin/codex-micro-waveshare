#pragma once

#include "display_monitor_runtime.h"
#include "hil_protocol.h"
#include "codex/device_state.h"
#include "codex/protocol_trace.h"

#include "esp_err.h"

namespace codex {

struct HilRuntimeMetrics {
  unsigned int monotonic_ms{};
  unsigned int event_queue_drops{};
  unsigned int event_queue_high_water{};
  unsigned int last_input_queue_latency_us{};
  unsigned int maximum_input_queue_latency_us{};
  unsigned int last_input_tx_latency_us{};
  unsigned int maximum_input_tx_latency_us{};
  unsigned int last_touch_callback_gap_us{};
  unsigned int maximum_touch_callback_gap_interval_us{};
  unsigned int slow_frames{};
  unsigned int free_internal{};
  unsigned int minimum_internal{};
  unsigned int free_psram{};
  unsigned int minimum_psram{};
  unsigned int ble_retries{};
  unsigned int ble_queue_high_water{};
  unsigned int ble_rx_stack_low_water_bytes{};
  bool ble_started{};
  bool ble_link{};
  bool ble_ready{};
  bool usb_link{};
  DisplayMonitorSnapshot display{};
};

struct HilConsoleHooks {
  bool (*submit)(const HilCommand& command, void* context){};
  DeviceState (*snapshot)(void* context){};
  HilRuntimeMetrics (*metrics)(void* context){};
  ProtocolTrace (*trace)(void* context){};
  bool (*read_pixel)(unsigned short x, unsigned short y,
                     unsigned short& pixel, void* context){};
  void* context{};
};

esp_err_t hil_console_start(HilConsoleHooks hooks);

}  // namespace codex
