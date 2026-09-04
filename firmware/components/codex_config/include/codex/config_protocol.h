#pragma once

#include "codex/device_state.h"
#include "codex/icon_id.h"

namespace codex {

constexpr unsigned int kConfigMaxMessage = 4096;

enum class ConfigAction : unsigned char {
  None,
  SetKeycap,
  SetLabel,
  SetSound,
  SetTransport,
  SetBluetooth,
  SetProtocolProfile,
  SetDisplay,
  SetPower,
  SetInput,
  SetBleSlot,
  ClearBond,
  Reboot,
  AppHello,
  AppHeartbeat,
  AppDisconnect,
  SetRouting,
  SetAppKeyLighting,
  SetAppIcons,
};

struct ConfigReply {
  bool ok{false};
  char json[1024]{};
  unsigned short length{0};
  ConfigAction action{ConfigAction::None};
  unsigned char index{0};
  unsigned int value{0};
  unsigned int secondary_value{0};
  unsigned int tertiary_value{0};
  unsigned int quaternary_value{0};
  bool enabled{true};
  bool secondary_enabled{true};
  char text[64]{};
  RoutingConfig routing_config{};
  ControlGroup control_group{ControlGroup::Agent};
  Lighting lighting{};
  unsigned int icon_hashes[kPhysicalControlsPerGroup]{};
};

[[nodiscard]] ConfigReply handle_config_line(const char* data,
                                             unsigned int length,
                                             const DeviceState& state);

}  // namespace codex
