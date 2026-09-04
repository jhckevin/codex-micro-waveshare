#pragma once

#include "codex/control_router.h"

namespace codex {

constexpr unsigned int kAgentCount = 6;
constexpr unsigned int kCommandCount = 6;
constexpr unsigned int kKeycapIdCapacity = 32;
constexpr unsigned int kCommandLabelCapacity = 17;

enum class LightEffect : unsigned char {
  Off = 0,
  Solid = 1,
  Snake = 2,
  Rainbow = 3,
  Breath = 4,
  Gradient = 5,
  ShallowBreath = 6,
};

struct Lighting {
  LightEffect effect{LightEffect::Off};
  float brightness{0.0F};
  float speed{0.0F};
  float magic{0.0F};
  unsigned int color{0};
};

struct AgentState {
  Lighting lighting{};
  bool pressed{false};
};

struct CommandState {
  char keycap_id[kKeycapIdCapacity]{};
  char label[kCommandLabelCapacity]{};
  bool pressed{false};
};

struct JoystickState {
  float angle{0.0F};
  float distance{0.0F};
  bool captured{false};
};

struct EncoderState {
  bool pressed{false};
  float accumulated_degrees{0.0F};
  signed char last_step{0};
};

struct BlePeerState {
  unsigned char address[6]{};
  unsigned char address_type{0};
  bool bonded{false};
};

struct AppSessionState {
  bool active{false};
  unsigned int last_heartbeat_ms{0};
  unsigned long long held_masks[kControlGroupCount]{};
};

struct PhysicalRouteLatch {
  bool active{false};
  RoutedControl route{};
};

struct InputRouteState {
  PhysicalRouteLatch agents[kAgentCount]{};
  PhysicalRouteLatch commands[kCommandCount]{};
  PhysicalRouteLatch encoder{};
  PhysicalRouteLatch joystick{};
};

enum class TransportMode : unsigned char {
  Auto,
  Usb,
  Ble,
  Mixed,
};

enum class ProtocolProfile : unsigned char {
  Auto,
  Current,
  Legacy,
  Compatibility,
};

enum class TransportLink : unsigned char {
  Usb,
  Ble,
};

enum class PowerMode : unsigned char {
  Active,
  Screensaver,
  UltraStandby,
  ProtectedBlack,
  ProtectedUnlock,
  PowerOffPending,
};

enum class PowerButtonMode : unsigned char {
  ConnectedStandby,
  UltraStandby,
};

enum class SoundProfile : unsigned char {
  Linear,
  Tactile,
  Clicky,
};

enum class Overlay : unsigned char {
  None,
  Connection,
  Pairing,
  Error,
  Diagnostics,
};

enum class VoiceMode : unsigned char {
  Idle,
  Recording,
  Processing,
  Complete,
};

enum class BleIndicator : unsigned char { Off, Pairing, Connected };

struct DeviceState {
  AgentState agents[kAgentCount]{};
  CommandState commands[kCommandCount]{};
  JoystickState joystick{};
  EncoderState encoder{};
  unsigned char layer{1};
  Lighting ambient{};
  Lighting keys_lighting{};
  TransportMode transport{TransportMode::Auto};
  ProtocolProfile protocol_profile{ProtocolProfile::Auto};
  bool bluetooth_enabled{true};
  bool transport_connected{false};
  bool usb_connected{false};
  bool ble_connected{false};
  bool usb_codex_ready{false};
  bool ble_codex_ready{false};
  bool codex_connected{false};
  RoutingConfig routing{};
  AppSessionState app_session{};
  // [layer][agent-or-command][physical key]. Zero means Classical fallback.
  // Hashes are resolved only when a layer or mapping changes.
  unsigned int
      app_icon_hashes[kMaximumLayerCount][2][kPhysicalControlsPerGroup]{};
  // Changes even when an updated content pack reuses the same stable icon ID.
  unsigned int app_icon_revisions[kMaximumLayerCount][2]{};
  InputRouteState input_routes{};
  unsigned int codex_rpc_count{0};
  unsigned int lighting_rpc_count{0};
  unsigned int last_codex_rpc_ms{0};
  unsigned int input_event_count{0};
  unsigned int hid_tx_queued_count{0};
  unsigned int hid_tx_success_count{0};
  unsigned int hid_tx_failure_count{0};
  unsigned char ble_slot{1};
  BlePeerState ble_peers[3]{};
  unsigned int overlay_until_ms{0};
  PowerMode power{PowerMode::Active};
  bool sound_enabled{true};
  SoundProfile sound_profile{SoundProfile::Linear};
  unsigned char sound_volume{100};
  unsigned char display_brightness{80};
  unsigned short standby_timeout_seconds{180};
  bool smart_screensaver_enabled{true};
  BleIndicator ble_indicator{BleIndicator::Off};
  unsigned int ble_indicator_until_ms{0};
  unsigned char animation_strength{100};
  unsigned char joystick_sensitivity_percent{100};
  unsigned int super_standby_timeout_seconds{7200};
  bool anti_accidental_shutdown{false};
  PowerButtonMode power_button_mode{PowerButtonMode::ConnectedStandby};
  unsigned int auto_ultra_timeout_seconds{0};
  bool ultra_touch_wake{false};
  unsigned int protected_unlock_until_ms{0};
  bool battery_present{false};
  unsigned char battery_percent{100};
  bool battery_charging{false};
  bool usb_power_present{false};
  unsigned short battery_voltage_mv{0};
  unsigned short battery_charge_limit_ma{0};
  bool battery_initialized{false};
  bool battery_warning_visible{false};
  unsigned int battery_warning_until_ms{0};
  unsigned int event_queue_drops{0};
  unsigned char event_queue_high_water{0};
  unsigned int rpc_errors{0};
  unsigned int free_heap_bytes{0};
  unsigned char reset_reason{0};
  bool recovery_gate_open{false};
  Overlay overlay{Overlay::None};
  VoiceMode voice{VoiceMode::Idle};
  Lighting temporary_lighting{};
  unsigned int temporary_lighting_until_ms{0};
  unsigned char active_agent{0xFF};
};

[[nodiscard]] DeviceState make_default_state();

}  // namespace codex
