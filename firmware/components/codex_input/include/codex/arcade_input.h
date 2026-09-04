#pragma once

#include "codex/input.h"

namespace codex {

enum class ArcadeControlMode : unsigned char {
  Joystick,
  Dpad,
  TouchRegion,
};

enum class ArcadeUiCommand : unsigned char {
  None,
  Exit,
  UseJoystick,
  UseDpad,
  UseTouchRegion,
};

struct ArcadeInputState {
  ArcadeControlMode mode{ArcadeControlMode::Joystick};
  bool agent_pressed{false};
  bool navigation_active{false};
  bool command_latched{false};
  unsigned char navigation_track_id{0};
  Point touch_origin{};
};

struct ArcadeInputOutput {
  InputFrame frame{};
  ArcadeUiCommand command{ArcadeUiCommand::None};
};

[[nodiscard]] ArcadeInputState make_arcade_input_state();
[[nodiscard]] ArcadeInputOutput normalize_arcade_touches(
    ArcadeInputState& state, const RawTouchFrame& frame);

}  // namespace codex
