#pragma once

#include "codex/device_state.h"

namespace codex {

enum class ControlId : unsigned char {
  Agent0,
  Agent1,
  Agent2,
  Agent3,
  Agent4,
  Agent5,
  Command0,
  Command1,
  Command2,
  Command3,
  Command4,
  Command5,
  Encoder,
};

enum class EventType : unsigned char {
  KeyPressed,
  KeyReleased,
  JoystickChanged,
  EncoderStep,
  AgentLightingChanged,
  AppKeyLightingChanged,
  AppIconsChanged,
  AmbientLightingChanged,
  KeysLightingChanged,
  TemporaryLightingChanged,
  LayerNext,
  PairingModeRequested,
  CurrentBleSlotRepairRequested,
  BleSlotChanged,
  BlePeerBonded,
  ConnectionOverlayRequested,
  TransportConnected,
  TransportDisconnected,
  CodexRpcReceived,
  AppSessionConnected,
  AppSessionHeartbeat,
  AppSessionDisconnected,
  RoutingConfigChanged,
  AppControlChanged,
  HidTxQueued,
  HidTxCompleted,
  InputReleaseRequested,
  EnterStandbyRequested,
  ExitStandbyRequested,
  EnterUltraStandbyRequested,
  ProtectedStandbyRequested,
  ProtectedUnlockShown,
  ProtectedUnlockCompleted,
  SuperStandbyRequested,
  StandbyTransitionCancelled,
  PowerButtonPressed,
  TransportModeChanged,
  BluetoothEnabledChanged,
  ProtocolProfileChanged,
  SelectionTransitionCancelled,
  CommandKeycapChanged,
  CommandLabelChanged,
  SoundSettingsChanged,
  DisplaySettingsChanged,
  PowerSettingsChanged,
  JoystickSensitivityChanged,
  BatteryChanged,
  BatteryWarningAcknowledged,
  DiagnosticsChanged,
  RpcErrorRecorded,
  ClearBondsRequested,
  BleBondsCleared,
  Tick,
};

struct KeyPayload {
  ControlId control{ControlId::Agent0};
};

struct JoystickPayload {
  float angle{0.0F};
  float distance{0.0F};
};

struct AgentLightingPayload {
  unsigned char index{0};
  Lighting lighting{};
};

struct AppKeyLightingPayload {
  ControlGroup group{ControlGroup::Agent};
  unsigned char id{0};
  Lighting lighting{};
};

struct AppIconsPayload {
  unsigned char layer{1};
  ControlGroup group{ControlGroup::Agent};
  unsigned int hashes[kPhysicalControlsPerGroup]{};
};

struct TemporaryLightingPayload {
  Lighting lighting{};
  unsigned int until_ms{0};
};

struct BlePeerPayload {
  unsigned char slot{1};
  unsigned char address[6]{};
  unsigned char address_type{0};
};

struct CommandTextPayload {
  unsigned char index{0};
  char text[kKeycapIdCapacity]{};
};

struct SoundSettingsPayload {
  SoundProfile profile{SoundProfile::Linear};
  bool enabled{true};
  unsigned char volume{100};
};

struct DisplaySettingsPayload {
  unsigned char brightness{80};
  unsigned short standby_timeout_seconds{180};
  unsigned char animation_strength{100};
  unsigned int super_standby_timeout_seconds{7200};
  bool anti_accidental_shutdown{false};
  bool smart_screensaver_enabled{true};
};

struct PowerSettingsPayload {
  PowerButtonMode button_mode{PowerButtonMode::ConnectedStandby};
  unsigned int auto_ultra_timeout_seconds{0};
  bool ultra_touch_wake{false};
};

struct BatteryPayload {
  bool present{false};
  unsigned char percent{100};
  bool charging{false};
  bool usb_power_present{false};
  unsigned short voltage_mv{0};
  unsigned short charge_limit_ma{0};
  unsigned int monotonic_ms{0};
};

struct DiagnosticsPayload {
  unsigned int event_queue_drops{0};
  unsigned char event_queue_high_water{0};
  unsigned int rpc_errors{0};
  unsigned int free_heap_bytes{0};
  unsigned char reset_reason{0};
  bool recovery_gate_open{false};
};

struct SelectionRestorePayload {
  TransportMode transport{TransportMode::Ble};
  unsigned char ble_slot{1};
  bool transport_connected{false};
};

struct CodexRpcPayload {
  unsigned int monotonic_ms{0};
  bool lighting{false};
  TransportLink link{TransportLink::Usb};
};

struct AppControlPayload {
  ControlGroup group{ControlGroup::Agent};
  unsigned char id{0};
  bool pressed{false};
};

union EventPayload {
  constexpr EventPayload() : tick_ms(0) {}
  KeyPayload key;
  JoystickPayload joystick;
  signed char encoder_step;
  AgentLightingPayload agent_lighting;
  AppKeyLightingPayload app_key_lighting;
  AppIconsPayload app_icons;
  TemporaryLightingPayload temporary_lighting;
  Lighting lighting;
  unsigned int tick_ms;
  TransportMode transport_mode;
  ProtocolProfile protocol_profile;
  TransportLink transport_link;
  unsigned char ble_slot;
  BlePeerPayload ble_peer;
  CommandTextPayload command_text;
  SoundSettingsPayload sound_settings;
  DisplaySettingsPayload display_settings;
  PowerSettingsPayload power_settings;
  unsigned char joystick_sensitivity_percent;
  BatteryPayload battery;
  DiagnosticsPayload diagnostics;
  SelectionRestorePayload selection_restore;
  CodexRpcPayload codex_rpc;
  RoutingConfig routing_config;
  AppControlPayload app_control;
  bool success;
};

struct DeviceEvent {
  EventType type{EventType::Tick};
  EventPayload payload{};
  // Local-only diagnostic timestamp. Never serialized onto Codex/App links.
  unsigned int enqueued_at_us{0};
};

[[nodiscard]] DeviceEvent make_key_pressed(ControlId control);
[[nodiscard]] DeviceEvent make_key_released(ControlId control);
[[nodiscard]] DeviceEvent make_joystick_changed(float angle, float distance);
[[nodiscard]] DeviceEvent make_encoder_step(signed char direction);
[[nodiscard]] DeviceEvent make_agent_lighting_changed(unsigned char index,
                                                       Lighting lighting);
[[nodiscard]] DeviceEvent make_app_key_lighting_changed(ControlGroup group,
                                                        unsigned char id,
                                                        Lighting lighting);
[[nodiscard]] DeviceEvent make_app_icons_changed(
    unsigned char layer, ControlGroup group,
    const unsigned int hashes[kPhysicalControlsPerGroup]);
[[nodiscard]] DeviceEvent make_ambient_lighting_changed(Lighting lighting);
[[nodiscard]] DeviceEvent make_keys_lighting_changed(Lighting lighting);
[[nodiscard]] DeviceEvent make_temporary_lighting_changed(Lighting lighting,
                                                          unsigned int until_ms);
[[nodiscard]] DeviceEvent make_layer_next();
[[nodiscard]] DeviceEvent make_pairing_mode_requested(unsigned int monotonic_ms);
[[nodiscard]] DeviceEvent make_current_ble_slot_repair_requested(
    unsigned int monotonic_ms);
[[nodiscard]] DeviceEvent make_ble_slot_changed(unsigned char slot);
[[nodiscard]] DeviceEvent make_ble_peer_bonded(unsigned char slot,
                                               const unsigned char address[6],
                                               unsigned char address_type);
[[nodiscard]] DeviceEvent make_connection_overlay_requested();
[[nodiscard]] DeviceEvent make_transport_connected(
    TransportLink link = TransportLink::Usb);
[[nodiscard]] DeviceEvent make_transport_disconnected(
    TransportLink link = TransportLink::Usb);
[[nodiscard]] DeviceEvent make_codex_rpc_received(unsigned int monotonic_ms,
                                                   bool lighting,
                                                   TransportLink link =
                                                       TransportLink::Usb);
[[nodiscard]] DeviceEvent make_app_session_connected(
    unsigned int monotonic_ms);
[[nodiscard]] DeviceEvent make_app_session_heartbeat(
    unsigned int monotonic_ms);
[[nodiscard]] DeviceEvent make_app_session_disconnected();
[[nodiscard]] DeviceEvent make_routing_config_changed(
    const RoutingConfig& config);
[[nodiscard]] DeviceEvent make_app_control_changed(ControlGroup group,
                                                   unsigned char id,
                                                   bool pressed);
[[nodiscard]] DeviceEvent make_hid_tx_queued();
[[nodiscard]] DeviceEvent make_hid_tx_completed(bool success);
[[nodiscard]] DeviceEvent make_input_release_requested();
[[nodiscard]] DeviceEvent make_enter_standby_requested();
[[nodiscard]] DeviceEvent make_exit_standby_requested();
[[nodiscard]] DeviceEvent make_enter_ultra_standby_requested();
[[nodiscard]] DeviceEvent make_protected_standby_requested();
[[nodiscard]] DeviceEvent make_protected_unlock_shown(unsigned int monotonic_ms);
[[nodiscard]] DeviceEvent make_protected_unlock_completed();
[[nodiscard]] DeviceEvent make_super_standby_requested();
[[nodiscard]] DeviceEvent make_standby_transition_cancelled(
    bool transport_connected);
[[nodiscard]] DeviceEvent make_power_button_pressed();
[[nodiscard]] DeviceEvent make_transport_mode_changed(TransportMode mode);
[[nodiscard]] DeviceEvent make_bluetooth_enabled_changed(bool enabled);
[[nodiscard]] DeviceEvent make_protocol_profile_changed(
    ProtocolProfile profile);
[[nodiscard]] DeviceEvent make_selection_transition_cancelled(
    TransportMode transport, unsigned char ble_slot,
    bool transport_connected);
[[nodiscard]] DeviceEvent make_command_keycap_changed(unsigned char index,
                                                       const char* text);
[[nodiscard]] DeviceEvent make_command_label_changed(unsigned char index,
                                                      const char* text);
[[nodiscard]] DeviceEvent make_sound_settings_changed(SoundProfile profile,
                                                       bool enabled,
                                                       unsigned char volume);
[[nodiscard]] DeviceEvent make_display_settings_changed(
    unsigned char brightness, unsigned short standby_timeout_seconds,
    unsigned char animation_strength,
    unsigned int super_standby_timeout_seconds = 7200,
    bool anti_accidental_shutdown = false,
    bool smart_screensaver_enabled = true);
[[nodiscard]] DeviceEvent make_power_settings_changed(
    PowerButtonMode button_mode, unsigned int auto_ultra_timeout_seconds,
    bool ultra_touch_wake);
[[nodiscard]] DeviceEvent make_joystick_sensitivity_changed(
    unsigned char sensitivity_percent);
[[nodiscard]] DeviceEvent make_battery_changed(bool present,
                                                unsigned char percent,
                                                bool charging,
                                                bool usb_power_present,
                                                unsigned short voltage_mv,
                                                unsigned short charge_limit_ma = 0,
                                                unsigned int monotonic_ms = 0);
[[nodiscard]] DeviceEvent make_battery_warning_acknowledged();
[[nodiscard]] DeviceEvent make_diagnostics_changed(unsigned int event_queue_drops,
                                                    unsigned char high_water,
                                                    unsigned int rpc_errors,
                                                    unsigned int free_heap_bytes,
                                                    unsigned char reset_reason,
                                                    bool recovery_gate_open);
[[nodiscard]] DeviceEvent make_rpc_error_recorded();
[[nodiscard]] DeviceEvent make_clear_bonds_requested(unsigned char slot = 0);
[[nodiscard]] DeviceEvent make_ble_bonds_cleared(unsigned char slot = 0);
[[nodiscard]] DeviceEvent make_tick(unsigned int monotonic_ms);

}  // namespace codex
