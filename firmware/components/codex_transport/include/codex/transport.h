#pragma once

#include "codex/device_reducer.h"
#include "codex/config_protocol.h"
#include "codex/protocol_trace.h"
#include "codex/update_protocol.h"
#include "codex/user_content_protocol.h"
#include "esp_err.h"

namespace codex {

struct TransportHooks {
  DeviceState (*snapshot)(void* context);
  void (*snapshot_into)(DeviceState& output, void* context);
  void (*publish)(const DeviceEvent& event, void* context);
  bool (*configure)(const ConfigReply& request, void* context);
  UpdateProtocolContext* update;
  UserContentProtocolContext* content;
  void* context;
};

struct InputTxLatencySnapshot {
  unsigned int last_us{};
  unsigned int maximum_us{};
};

esp_err_t codex_ble_start(TransportHooks hooks);
esp_err_t codex_ble_stop();
esp_err_t codex_ble_suspend();
esp_err_t codex_ble_resume();
esp_err_t codex_usb_start(TransportHooks hooks, bool recovery_gate_open);
esp_err_t codex_usb_stop();
bool codex_usb_flush(unsigned int timeout_ms);
void codex_usb_send_effect(const SideEffect& effect);
bool codex_usb_send_config_event(const char* json, unsigned int length,
                                 bool critical);
bool codex_usb_connected();
// True while a USB configuration/update request is executing, or briefly
// after the last request, so idle power policy cannot tear down its reply.
bool codex_usb_control_active();
void codex_transport_send_effect(const SideEffect& effect);
bool codex_ble_connected();
bool codex_ble_ready();
bool codex_ble_started();
bool codex_ble_flush(unsigned int timeout_ms);
bool codex_transport_flush(unsigned int timeout_ms);
esp_err_t codex_ble_clear_bonds(unsigned char slot = 0);
esp_err_t codex_ble_select_slot();
void codex_transport_set_battery(unsigned char percentage, bool present,
                                 bool charging, bool external_power);
unsigned int codex_ble_retry_count();
unsigned char codex_ble_input_high_water();
ProtocolTrace codex_ble_protocol_trace_snapshot();
void codex_transport_note_input_tx(unsigned int origin_us);
InputTxLatencySnapshot codex_transport_input_tx_latency_snapshot();
unsigned int codex_ble_rx_stack_low_water_bytes();

}  // namespace codex
