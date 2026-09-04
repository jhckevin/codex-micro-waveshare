#include "codex/persistent_settings.h"
#include "codex/power_policy.h"

namespace codex {
namespace {

constexpr const char* kDefaults[kCommandCount] = {
    "FAST", "APPR", "REJ", "COMPUTER", "MIC", "OAI",
};

void copy(char* destination, unsigned int capacity, const char* source) {
  unsigned int index = 0;
  while (index + 1 < capacity && source[index] != '\0') {
    destination[index] = source[index];
    ++index;
  }
  destination[index] = '\0';
}

unsigned int crc_update(unsigned int crc, unsigned char value) {
  crc ^= value;
  for (unsigned int bit = 0; bit < 8; ++bit) {
    crc = (crc >> 1U) ^ (0xEDB88320U &
          static_cast<unsigned int>(-static_cast<int>(crc & 1U)));
  }
  return crc;
}

bool valid_layer1_targets(const PersistentSettings& settings) {
  for (unsigned int index = 0; index < kPhysicalControlsPerGroup; ++index) {
    const unsigned char target = settings.layer1_command_app_targets[index];
    // Targets 2..6 were emitted by development clients before the shadow
    // range existed. Preserve the rest of NVS, while route_control fails
    // those legacy targets closed to Codex until the client writes target 7.
    if (target != 0 &&
        (target < kMinimumLayerCount ||
         target > kLayer1CommandOverrideTarget)) {
      return false;
    }
  }
  return true;
}

bool valid_passthrough_masks(const PersistentSettings& settings) {
  for (unsigned int layer = 0; layer < kMaximumLayerCount - 1; ++layer) {
    for (unsigned int group = 0; group < kControlGroupCount; ++group) {
      if ((settings.higher_codex_masks[layer][group] & 0xC0U) != 0) {
        return false;
      }
    }
  }
  return true;
}

}  // namespace

PersistentSettings make_default_settings() {
  PersistentSettings settings{};
  settings.schema_version = kSettingsSchemaVersion;
  settings.sound_profile = SoundProfile::Linear;
  settings.sound_enabled = true;
  settings.sound_volume = 100;
  settings.transport = TransportMode::Auto;
  settings.bluetooth_enabled = true;
  settings.protocol_profile = ProtocolProfile::Auto;
  settings.display_brightness = 80;
  settings.standby_timeout_seconds = kDefaultScreensaverTimeoutSeconds;
  settings.animation_strength = 100;
  settings.joystick_sensitivity_percent = 100;
  settings.super_standby_timeout_seconds =
      kDefaultAutoShutdownTimeoutSeconds;
  settings.anti_accidental_shutdown = false;
  settings.power_button_mode = PowerButtonMode::ConnectedStandby;
  settings.auto_ultra_timeout_seconds = 0;
  settings.ultra_touch_wake = false;
  settings.layer_routing_enabled = false;
  settings.configured_layer_count = kMaximumLayerCount;
  settings.layer = 1;
  settings.ble_slot = 1;
  for (unsigned int index = 0; index < kCommandCount; ++index) {
    copy(settings.keycap_ids[index], sizeof(settings.keycap_ids[index]),
         kDefaults[index]);
    copy(settings.labels[index], sizeof(settings.labels[index]), kDefaults[index]);
  }
  settings.crc32 = settings_crc32(settings);
  return settings;
}

unsigned int settings_crc32(const PersistentSettings& settings) {
  const auto* bytes = reinterpret_cast<const unsigned char*>(&settings);
  unsigned int crc = 0xFFFFFFFFU;
  for (unsigned int index = 0; index < sizeof(settings); ++index) {
    if (index >= sizeof(settings.schema_version) &&
        index < sizeof(settings.schema_version) + sizeof(settings.crc32)) {
      crc = crc_update(crc, 0);
    } else {
      crc = crc_update(crc, bytes[index]);
    }
  }
  return ~crc;
}

SettingsLoadResult validate_settings(const PersistentSettings& settings) {
  if (settings.schema_version != kSettingsSchemaVersion) {
    return SettingsLoadResult::InvalidSchema;
  }
  if (settings.crc32 != settings_crc32(settings)) {
    return SettingsLoadResult::InvalidCrc;
  }
  if (settings.layer < 1 || settings.layer > 6 ||
      settings.ble_slot < 1 || settings.ble_slot > 3 ||
      settings.sound_volume < 10 || settings.sound_volume > 100 ||
      settings.sound_volume % 10 != 0 ||
      settings.display_brightness < 10 ||
      settings.display_brightness > 100 ||
      settings.display_brightness % 10 != 0 ||
      settings.animation_strength > 100 ||
      settings.joystick_sensitivity_percent < 50 ||
      settings.joystick_sensitivity_percent > 200 ||
      settings.joystick_sensitivity_percent % 10 != 0 ||
      static_cast<unsigned char>(settings.sound_profile) > 2 ||
      static_cast<unsigned char>(settings.transport) > 3 ||
      static_cast<unsigned char>(settings.protocol_profile) > 3 ||
      static_cast<unsigned char>(settings.power_button_mode) >
          static_cast<unsigned char>(PowerButtonMode::UltraStandby) ||
      settings.configured_layer_count < kMinimumLayerCount ||
      settings.configured_layer_count > kMaximumLayerCount ||
      !valid_layer1_targets(settings) ||
      !valid_passthrough_masks(settings) ||
      !valid_auto_shutdown_timeout(
          settings.super_standby_timeout_seconds) ||
      !valid_auto_ultra_timeout(settings.auto_ultra_timeout_seconds)) {
    return SettingsLoadResult::InvalidValue;
  }
  return SettingsLoadResult::Valid;
}

}  // namespace codex
