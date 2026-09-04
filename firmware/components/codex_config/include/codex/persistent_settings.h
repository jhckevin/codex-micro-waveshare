#pragma once

#include "codex/device_state.h"

namespace codex {

constexpr unsigned int kSettingsSchemaVersion = 13;

#pragma pack(push, 1)
struct PersistentSettings {
  unsigned int schema_version{0};
  unsigned int crc32{0};
  char keycap_ids[kCommandCount][kKeycapIdCapacity]{};
  char labels[kCommandCount][kCommandLabelCapacity]{};
  SoundProfile sound_profile{SoundProfile::Linear};
  bool sound_enabled{true};
  unsigned char sound_volume{100};
  TransportMode transport{TransportMode::Auto};
  bool bluetooth_enabled{true};
  ProtocolProfile protocol_profile{ProtocolProfile::Auto};
  unsigned char display_brightness{80};
  unsigned short standby_timeout_seconds{180};
  unsigned char animation_strength{100};
  unsigned char joystick_sensitivity_percent{100};
  unsigned int super_standby_timeout_seconds{7200};
  bool anti_accidental_shutdown{false};
  PowerButtonMode power_button_mode{PowerButtonMode::ConnectedStandby};
  unsigned int auto_ultra_timeout_seconds{0};
  bool ultra_touch_wake{false};
  bool layer_routing_enabled{false};
  unsigned char configured_layer_count{kMaximumLayerCount};
  unsigned char
      layer1_command_app_targets[kPhysicalControlsPerGroup]{};
  unsigned char
      higher_codex_masks[kMaximumLayerCount - 1][kControlGroupCount]{};
  unsigned char layer{1};
  unsigned char ble_slot{1};
  BlePeerState ble_peers[3]{};
};
#pragma pack(pop)

enum class SettingsLoadResult : unsigned char {
  Valid,
  InvalidSchema,
  InvalidCrc,
  InvalidValue,
};

[[nodiscard]] PersistentSettings make_default_settings();
[[nodiscard]] unsigned int settings_crc32(const PersistentSettings& settings);
[[nodiscard]] SettingsLoadResult validate_settings(
    const PersistentSettings& settings);

}  // namespace codex
