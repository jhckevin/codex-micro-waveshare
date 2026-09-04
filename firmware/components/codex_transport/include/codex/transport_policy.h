#pragma once

#include "codex/device_event.h"

namespace codex {

enum class ActiveTransport : unsigned char {
  None,
  Usb,
  Ble,
};

constexpr unsigned int kAutoTransportStableMs = 1500U;

struct AutoTransportDebounce {
  bool initialized{false};
  bool observed_usb{false};
  unsigned int changed_at_ms{0};
};

// USB mount notifications can briefly flap while Windows rebuilds a composite
// HID path.  Do not tear down BLE (or recreate it) until the observed USB
// state has remained unchanged for a full settling window.
[[nodiscard]] constexpr bool auto_transport_observation_stable(
    AutoTransportDebounce& state, bool usb_mounted, unsigned int now_ms) {
  if (!state.initialized) {
    state.initialized = true;
    state.observed_usb = usb_mounted;
    state.changed_at_ms = now_ms;
    return false;
  }
  if (state.observed_usb != usb_mounted) {
    state.observed_usb = usb_mounted;
    state.changed_at_ms = now_ms;
    return false;
  }
  return static_cast<unsigned int>(now_ms - state.changed_at_ms) >=
         kAutoTransportStableMs;
}

[[nodiscard]] constexpr ActiveTransport choose_active_transport(
    TransportMode mode, bool usb_connected, bool ble_connected) {
  if ((mode == TransportMode::Auto || mode == TransportMode::Usb ||
       mode == TransportMode::Mixed) &&
      usb_connected) {
    return ActiveTransport::Usb;
  }
  if ((mode == TransportMode::Auto || mode == TransportMode::Ble ||
       mode == TransportMode::Mixed) &&
      ble_connected) {
    return ActiveTransport::Ble;
  }
  return ActiveTransport::None;
}

[[nodiscard]] constexpr bool keep_ble_running(TransportMode mode,
                                               bool usb_connected,
                                               bool bluetooth_enabled) {
  return bluetooth_enabled &&
         (mode == TransportMode::Ble || mode == TransportMode::Mixed ||
          (mode == TransportMode::Auto && !usb_connected));
}

// A dark display is not a transport-selection event. Freezing Auto handoff
// prevents USB mount jitter from repeatedly rebuilding the BLE session.
[[nodiscard]] constexpr bool allow_auto_transport_handoff(PowerMode power) {
  return power == PowerMode::Active;
}

[[nodiscard]] constexpr bool transport_event_requires_order(EventType type) {
  return type == EventType::TransportConnected ||
         type == EventType::TransportDisconnected ||
         type == EventType::BlePeerBonded ||
         type == EventType::BleBondsCleared ||
         type == EventType::AppSessionConnected ||
         type == EventType::AppSessionHeartbeat ||
         type == EventType::AppSessionDisconnected ||
         type == EventType::RoutingConfigChanged;
}

// A failed HID notification after the link is otherwise ready means the host
// has not enabled (or has removed) the input CCC. Retrying in a tight loop
// cannot make that state change and starves UI/input work.
[[nodiscard]] constexpr bool quarantine_ble_input_channel(
    bool sent, bool link_open, bool host_ready) {
  return !sent && link_open && host_ready;
}

}  // namespace codex
