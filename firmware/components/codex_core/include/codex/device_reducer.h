#pragma once

#include "codex/device_event.h"

namespace codex {

enum class SideEffectType : unsigned char {
  None,
  SendHid,
  SendJoystick,
  SendAppControl,
  PlayKeyDownSound,
  PlayKeyUpSound,
  PlayCapacitiveSound,
  ReleaseAllInputs,
  ReleaseAppInputs,
  EnterStandby,
  ExitStandby,
  EnterUltraStandby,
  ExitUltraStandby,
  PowerOff,
  ClearBonds,
};

struct SideEffect {
  SideEffectType type{SideEffectType::None};
  ControlId control{ControlId::Agent0};
  float angle{0.0F};
  float distance{0.0F};
  signed char direction{0};
  unsigned char action{0};
  unsigned short release_mask{0};
  unsigned long long app_release_masks[kControlGroupCount]{};
  ControlGroup app_group{ControlGroup::Agent};
  unsigned char app_id{0};
  // Development telemetry origin. It is never serialized to either host.
  unsigned int origin_us{0};
};

struct ReduceResult {
  DeviceState state{};
  SideEffect effects[3]{};
  unsigned char effect_count{0};
};

[[nodiscard]] ReduceResult reduce(const DeviceState& state,
                                  const DeviceEvent& event);

}  // namespace codex
