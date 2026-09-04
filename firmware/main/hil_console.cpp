#include "hil_console.h"

#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace codex {
namespace {

constexpr char kTag[] = "hil_console";
HilConsoleHooks runtime_hooks{};

void reply_error(const char* error) {
  printf("@hil:{\"ok\":false,\"error\":\"%s\"}\r\n",
         error == nullptr ? "unknown" : error);
  fflush(stdout);
}

void reply_snapshot() {
  const DeviceState state = runtime_hooks.snapshot(runtime_hooks.context);
  printf(
      "@hil:{\"ok\":true,\"snapshot\":{\"layer\":%u,\"power\":%u,"
      "\"overlay\":%u,\"transport\":%u,\"connected\":%s,"
      "\"codex_ready\":%s,\"ble_slot\":%u,\"mic_pressed\":%s,"
      "\"app\":{\"active\":%s,\"heartbeat_ms\":%u,"
      "\"held\":[\"%016llx\",\"%016llx\",\"%016llx\",\"%016llx\"]},"
      "\"routing\":{\"enabled\":%s,\"layers\":%u},"
      "\"input_count\":%u,\"hid\":{\"queued\":%u,\"ok\":%u,\"failed\":%u},"
      "\"battery\":{\"present\":%s,"
      "\"percent\":%u,\"charging\":%s,\"usb\":%s,\"mv\":%u}}}\r\n",
      state.layer, static_cast<unsigned int>(state.power),
      static_cast<unsigned int>(state.overlay),
      static_cast<unsigned int>(state.transport),
      state.transport_connected ? "true" : "false",
      state.codex_connected ? "true" : "false", state.ble_slot,
      state.commands[4].pressed ? "true" : "false",
      state.app_session.active ? "true" : "false",
      state.app_session.last_heartbeat_ms,
      static_cast<unsigned long long>(state.app_session.held_masks[0]),
      static_cast<unsigned long long>(state.app_session.held_masks[1]),
      static_cast<unsigned long long>(state.app_session.held_masks[2]),
      static_cast<unsigned long long>(state.app_session.held_masks[3]),
      state.routing.layer_routing_enabled ? "true" : "false",
      state.routing.configured_layer_count,
      state.input_event_count, state.hid_tx_queued_count,
      state.hid_tx_success_count, state.hid_tx_failure_count,
      state.battery_present ? "true" : "false", state.battery_percent,
      state.battery_charging ? "true" : "false",
      state.usb_power_present ? "true" : "false", state.battery_voltage_mv);
  fflush(stdout);
}

void reply_routing_snapshot() {
  const DeviceState state = runtime_hooks.snapshot(runtime_hooks.context);
  printf(
      "@hil:{\"ok\":true,\"routing\":{\"app_active\":%s,"
      "\"enabled\":%s,\"layers\":%u,\"layer1\":[",
      state.app_session.active ? "true" : "false",
      state.routing.layer_routing_enabled ? "true" : "false",
      state.routing.configured_layer_count);
  for (unsigned int index = 0; index < kPhysicalControlsPerGroup; ++index) {
    printf(index == 0U ? "%u" : ",%u",
           state.routing.layer1_command_app_targets[index]);
  }
  printf("],\"higher\":[");
  for (unsigned int layer = 0; layer < kMaximumLayerCount - 1; ++layer) {
    printf(layer == 0U ? "[" : ",[");
    for (unsigned int group = 0; group < kControlGroupCount; ++group) {
      printf(group == 0U ? "%u" : ",%u",
             state.routing.higher_codex_masks[layer][group]);
    }
    printf("]");
  }
  printf("]}}\r\n");
  fflush(stdout);
}

void reply_metrics() {
  const HilRuntimeMetrics metrics = runtime_hooks.metrics(runtime_hooks.context);
  printf(
      "@hil:{\"ok\":true,\"metrics\":{\"ms\":%u,\"drops\":%u,"
      "\"queue_high\":%u,\"input_latency_us\":{\"last\":%u,\"max\":%u},"
      "\"input_tx_latency_us\":{\"last\":%u,\"max\":%u},"
      "\"touch_gap_us\":{\"last\":%u,\"max_interval\":%u},"
      "\"slow_frames\":%u,\"internal\":%u,"
      "\"min_internal\":%u,\"psram\":%u,\"min_psram\":%u,"
      "\"ble\":{\"started\":%s,\"link\":%s,\"ready\":%s,"
      "\"retries\":%u,\"queue_high\":%u,\"rx_stack_low_water\":%u},"
      "\"usb_link\":%s,"
      "\"display\":{\"vsync\":%u,\"interval_us\":%u,\"timing_faults\":%u,"
      "\"flushes\":%u,\"black_faults\":%u,\"unstable\":%u,"
      "\"last_fault_ms\":%u,\"last_fault_tile\":%u,\"fb\":\"0x%08x\"}}}\r\n",
      metrics.monotonic_ms, metrics.event_queue_drops,
      metrics.event_queue_high_water, metrics.last_input_queue_latency_us,
      metrics.maximum_input_queue_latency_us,
      metrics.last_input_tx_latency_us, metrics.maximum_input_tx_latency_us,
      metrics.last_touch_callback_gap_us,
      metrics.maximum_touch_callback_gap_interval_us, metrics.slow_frames,
      metrics.free_internal, metrics.minimum_internal, metrics.free_psram,
      metrics.minimum_psram, metrics.ble_started ? "true" : "false",
      metrics.ble_link ? "true" : "false",
      metrics.ble_ready ? "true" : "false", metrics.ble_retries,
      metrics.ble_queue_high_water, metrics.ble_rx_stack_low_water_bytes,
      metrics.usb_link ? "true" : "false",
      metrics.display.timing.vsync_count,
      metrics.display.timing.last_interval_us,
      metrics.display.timing.timing_faults, metrics.display.flush_count,
      metrics.display.suspicious_black_blocks,
      metrics.display.unstable_samples, metrics.display.last_fault_ms,
      metrics.display.last_fault_tile, metrics.display.active_framebuffer);
  fflush(stdout);
}

void reply_trace() {
  const ProtocolTrace trace = runtime_hooks.trace(runtime_hooks.context);
  printf("@hil:{\"ok\":true,\"trace_begin\":{\"count\":%u,"
         "\"overwritten\":%u}}\r\n",
         protocol_trace_size(trace), trace.overwritten);
  for (unsigned int index = 0; index < protocol_trace_size(trace); ++index) {
    const ProtocolTraceRecord* record = protocol_trace_at(trace, index);
    if (record == nullptr) continue;
    printf("@hil:{\"trace\":{\"ms\":%u,\"type\":%u,\"report\":%u,"
           "\"detail\":%u,\"method\":\"%s\"}}\r\n",
           record->monotonic_ms, static_cast<unsigned int>(record->type),
           record->report_id, record->detail, record->method);
  }
  printf("@hil:{\"ok\":true,\"trace_end\":true}\r\n");
  fflush(stdout);
}

void reply_screen_crc() {
  const DisplayMonitorSnapshot display = display_monitor_snapshot();
  printf("@hil:{\"ok\":true,\"screen_crc\":{\"sequence\":%u,\"tiles\":[",
         display.buffers.frame_sequence);
  for (unsigned int tile = 0; tile < kDisplayMonitorTileCount; ++tile) {
    printf(tile == 0U ? "\"%08x\"" : ",\"%08x\"",
           display.buffers.tile_crc[tile]);
  }
  printf("]}}\r\n");
  fflush(stdout);
}

void reply_screen_read(const HilScreenRegion& region) {
  printf("@hil:{\"ok\":true,\"screen_begin\":{\"x\":%u,\"y\":%u,"
         "\"w\":%u,\"h\":%u,\"format\":\"rgb565\"}}\r\n",
         region.x, region.y, region.width, region.height);
  for (unsigned int row = 0; row < region.height; ++row) {
    printf("@hil:{\"screen_row\":{\"y\":%u,\"pixels\":\"",
           region.y + row);
    for (unsigned int column = 0; column < region.width; ++column) {
      unsigned short pixel{};
      if (!runtime_hooks.read_pixel(
              static_cast<unsigned short>(region.x + column),
              static_cast<unsigned short>(region.y + row), pixel,
              runtime_hooks.context)) {
        printf("????");
      } else {
        printf("%04x", pixel);
      }
    }
    printf("\"}}\r\n");
  }
  printf("@hil:{\"ok\":true,\"screen_end\":true}\r\n");
  fflush(stdout);
}

void console_task(void*) {
  HilLineAccumulator line{};
  bool ultra_wake_pending = false;
  ESP_LOGI(kTag, "development UART HIL ready");
  while (true) {
    const int value = fgetc(stdin);
    if (value == EOF) {
      // ESP-IDF's UART VFS is non-blocking in this console configuration.
      // EAGAIN/no-byte-yet is not a CH343 disconnect. A partial command stays
      // buffered until its newline arrives.
      clearerr(stdin);
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    const HilLineState line_state = line.push(static_cast<char>(value));
    if (line_state == HilLineState::Collecting) continue;
    if (line_state == HilLineState::Overflow) {
      reply_error("HIL line too long");
      continue;
    }
    const HilCommand command = parse_hil_command(line.line());
    line.reset();
    if (!command.claimed) continue;
    if (!command.valid) {
      reply_error(command.error);
      continue;
    }
    if (ultra_wake_pending &&
        runtime_hooks.snapshot(runtime_hooks.context).power !=
            PowerMode::UltraStandby) {
      ultra_wake_pending = false;
    }
    switch (command.type) {
      case HilCommandType::Ping:
        printf("@hil:{\"ok\":true,\"pong\":true,\"version\":1}\r\n");
        fflush(stdout);
        break;
      case HilCommandType::Snapshot:
        reply_snapshot();
        break;
      case HilCommandType::Metrics:
        reply_metrics();
        break;
      case HilCommandType::Trace:
        reply_trace();
        break;
      case HilCommandType::ScreenCrc:
        reply_screen_crc();
        break;
      case HilCommandType::ScreenRead:
        reply_screen_read(command.region);
        break;
      case HilCommandType::RoutingSnapshot:
        reply_routing_snapshot();
        break;
      case HilCommandType::UltraWake: {
        const DeviceState state = runtime_hooks.snapshot(runtime_hooks.context);
        if (state.power != PowerMode::UltraStandby) {
          ultra_wake_pending = false;
          printf("@hil:{\"ok\":true,\"accepted\":true}\r\n");
          fflush(stdout);
        } else if (ultra_wake_pending ||
                   runtime_hooks.submit(command, runtime_hooks.context)) {
          ultra_wake_pending = true;
          printf("@hil:{\"ok\":true,\"accepted\":true}\r\n");
          fflush(stdout);
        } else {
          reply_error("HIL command queue full");
        }
        break;
      }
      default:
        if (runtime_hooks.submit(command, runtime_hooks.context)) {
          printf("@hil:{\"ok\":true,\"accepted\":true}\r\n");
          fflush(stdout);
        } else {
          reply_error("HIL command queue full");
        }
        break;
    }
  }
}

}  // namespace

esp_err_t hil_console_start(HilConsoleHooks hooks) {
  if (hooks.submit == nullptr || hooks.snapshot == nullptr ||
      hooks.metrics == nullptr || hooks.trace == nullptr ||
      hooks.read_pixel == nullptr) {
    return ESP_ERR_INVALID_ARG;
  }
  runtime_hooks = hooks;
  return xTaskCreatePinnedToCore(console_task, "uart_hil", 6144, nullptr, 1,
                                 nullptr, 0) == pdPASS
             ? ESP_OK
             : ESP_ERR_NO_MEM;
}

}  // namespace codex
