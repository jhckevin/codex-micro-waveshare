#include "codex/arcade_input.h"

namespace codex {
namespace {

constexpr float kPi = 3.14159265358979323846F;

float absolute(float value) { return value < 0.0F ? -value : value; }

float fast_sqrt(float value) {
  if (value <= 0.0F) return 0.0F;
  float estimate = value > 1.0F ? value : 1.0F;
  for (unsigned int index = 0; index < 7; ++index) {
    estimate = 0.5F * (estimate + value / estimate);
  }
  return estimate;
}

float fast_atan2(float y, float x) {
  if (x == 0.0F && y == 0.0F) return 0.0F;
  const float abs_y = absolute(y) + 0.000001F;
  float angle;
  if (x >= 0.0F) {
    const float ratio = (x - abs_y) / (x + abs_y);
    angle = 0.785398163F - 0.785398163F * ratio;
  } else {
    const float ratio = (x + abs_y) / (abs_y - x);
    angle = 2.35619449F - 0.785398163F * ratio;
  }
  return y < 0.0F ? -angle : angle;
}

float angle_for(float dx, float dy) {
  float turns = fast_atan2(dy, dx) / (2.0F * kPi);
  return turns < 0.0F ? turns + 1.0F : turns;
}

bool inside(Point point, int x, int y, int width, int height) {
  return point.x >= x && point.x < x + width &&
         point.y >= y && point.y < y + height;
}

void append(InputFrame& output, DeviceEvent event) {
  if (output.count < sizeof(output.events) / sizeof(output.events[0])) {
    output.events[output.count++] = event;
  }
}

JoystickPayload navigation_sample(const ArcadeInputState& state, Point point,
                                  bool first) {
  float center_x = 345.0F;
  float center_y = 230.0F;
  float radius = 105.0F;
  if (state.mode == ArcadeControlMode::TouchRegion) {
    center_x = static_cast<float>(state.touch_origin.x);
    center_y = static_cast<float>(state.touch_origin.y);
    radius = 70.0F;
  }
  const float dx = static_cast<float>(point.x) - center_x;
  const float dy = static_cast<float>(point.y) - center_y;
  if (first && state.mode == ArcadeControlMode::TouchRegion) {
    return {.angle = 0.0F, .distance = 0.0F};
  }
  float distance = fast_sqrt(dx * dx + dy * dy) / radius;
  if (state.mode == ArcadeControlMode::Dpad) {
    if (distance < 0.2F) return {.angle = 0.0F, .distance = 0.0F};
    return {.angle = angle_for(dx, dy), .distance = 1.0F};
  }
  if (distance > 1.0F) distance = 1.0F;
  if (distance < 0.06F) distance = 0.0F;
  return {.angle = angle_for(dx, dy), .distance = distance};
}

}  // namespace

ArcadeInputState make_arcade_input_state() { return {}; }

ArcadeInputOutput normalize_arcade_touches(ArcadeInputState& state,
                                           const RawTouchFrame& frame) {
  ArcadeInputOutput output{};
  bool agent_now = false;
  bool command_now = false;
  const RawTouchPoint* navigation = nullptr;

  for (unsigned int index = 0; index < frame.count; ++index) {
    const RawTouchPoint& touch = frame.points[index];
    if (inside(touch.point, 40, 180, 120, 120)) {
      agent_now = true;
      continue;
    }
    if (inside(touch.point, 425, 5, 55, 55)) {
      command_now = true;
      if (!state.command_latched) output.command = ArcadeUiCommand::Exit;
      continue;
    }
    if (inside(touch.point, 210, 395, 125, 85)) {
      command_now = true;
      if (!state.command_latched) {
        state.mode = state.mode == ArcadeControlMode::TouchRegion
                         ? ArcadeControlMode::Joystick
                         : ArcadeControlMode::TouchRegion;
        output.command = state.mode == ArcadeControlMode::TouchRegion
                             ? ArcadeUiCommand::UseTouchRegion
                             : ArcadeUiCommand::UseJoystick;
      }
      continue;
    }
    if (inside(touch.point, 335, 395, 145, 85)) {
      command_now = true;
      if (!state.command_latched) {
        state.mode = state.mode == ArcadeControlMode::Dpad
                         ? ArcadeControlMode::Joystick
                         : ArcadeControlMode::Dpad;
        output.command = state.mode == ArcadeControlMode::Dpad
                             ? ArcadeUiCommand::UseDpad
                             : ArcadeUiCommand::UseJoystick;
      }
      continue;
    }
    if (touch.point.x >= 190 && touch.point.y >= 70 &&
        touch.point.y < 395 && navigation == nullptr) {
      navigation = &touch;
    }
  }

  state.command_latched = command_now;
  if (agent_now != state.agent_pressed) {
    append(output.frame, agent_now ? make_key_pressed(ControlId::Agent0)
                                   : make_key_released(ControlId::Agent0));
    state.agent_pressed = agent_now;
  }

  if (navigation != nullptr) {
    const bool first = !state.navigation_active ||
                       state.navigation_track_id != navigation->track_id;
    if (first) {
      state.navigation_active = true;
      state.navigation_track_id = navigation->track_id;
      state.touch_origin = navigation->point;
    }
    const JoystickPayload sample =
        navigation_sample(state, navigation->point, first);
    append(output.frame, make_joystick_changed(sample.angle, sample.distance));
  } else if (state.navigation_active) {
    append(output.frame, make_joystick_changed(0.0F, 0.0F));
    state.navigation_active = false;
  }
  return output;
}

}  // namespace codex
