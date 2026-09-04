#include "codex/input.h"

namespace codex {
namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kDialRotationThresholdDegrees = 8.0F;

float absolute(float value) { return value < 0.0F ? -value : value; }

float fast_sqrt(float value) {
  if (value <= 0.0F) return 0.0F;
  float estimate = value > 1.0F ? value : 1.0F;
  for (unsigned int i = 0; i < 7; ++i) {
    estimate = 0.5F * (estimate + value / estimate);
  }
  return estimate;
}

float fast_atan2(float y, float x) {
  if (x == 0.0F && y == 0.0F) return 0.0F;
  const float abs_y = absolute(y) + 0.000001F;
  float angle;
  if (x >= 0.0F) {
    const float r = (x - abs_y) / (x + abs_y);
    angle = 0.785398163F - 0.785398163F * r;
  } else {
    const float r = (x + abs_y) / (abs_y - x);
    angle = 2.35619449F - 0.785398163F * r;
  }
  return y < 0.0F ? -angle : angle;
}

float normalized_angle(Point point, const Rect& bounds) {
  const float center_x = bounds.x + bounds.width * 0.5F;
  const float center_y = bounds.y + bounds.height * 0.5F;
  float turns = fast_atan2(point.y - center_y, point.x - center_x) /
                (2.0F * kPi);
  if (turns < 0.0F) turns += 1.0F;
  return turns;
}

float circular_difference(float a, float b) {
  float difference = absolute(a - b);
  return difference > 0.5F ? 1.0F - difference : difference;
}

ControlId control_id(const ControlHit& hit) {
  if (hit.kind == ControlKind::Agent) {
    return static_cast<ControlId>(static_cast<unsigned int>(ControlId::Agent0) +
                                  hit.index);
  }
  if (hit.kind == ControlKind::Command) {
    return static_cast<ControlId>(static_cast<unsigned int>(ControlId::Command0) +
                                  hit.index);
  }
  return ControlId::Encoder;
}

void append(InputFrame& output, DeviceEvent event) {
  if (output.count < sizeof(output.events) / sizeof(output.events[0])) {
    output.events[output.count++] = event;
  }
}

void append(InputFrame& output, const InputFrame& input) {
  for (unsigned int index = 0; index < input.count; ++index) {
    append(output, input.events[index]);
  }
}

void joystick_sample(InputNormalizer& state, InputFrame& output, Point point,
                     unsigned int now_ms, bool force_release) {
  float distance = 0.0F;
  float angle = state.last_angle;
  if (!force_release) {
    const Rect& bounds = state.control.bounds;
    const float center_x = bounds.x + bounds.width * 0.5F;
    const float center_y = bounds.y + bounds.height * 0.5F;
    const float dx = point.x - center_x;
    const float dy = point.y - center_y;
    distance = fast_sqrt(dx * dx + dy * dy) / (bounds.width * 0.5F);
    if (distance < 0.08F) distance = 0.0F;
    if (distance > 1.0F) distance = 1.0F;
    angle = normalized_angle(point, bounds);
  }

  const bool due = now_ms - state.last_emit_ms >= 16U;
  const bool changed = absolute(distance - state.last_distance) >= 0.015F ||
                       circular_difference(angle, state.last_angle) >= 0.015F;
  if (force_release || state.last_emit_ms == 0 || (due && changed)) {
    append(output, make_joystick_changed(angle, distance));
    state.last_emit_ms = now_ms;
    state.last_distance = distance;
    state.last_angle = angle;
  }
}

void dial_sample(InputNormalizer& state, InputFrame& output, Point point) {
  const float angle = normalized_angle(point, state.control.bounds);
  float delta = (angle - state.dial_angle) * 360.0F;
  if (delta > 180.0F) delta -= 360.0F;
  if (delta < -180.0F) delta += 360.0F;
  state.dial_angle = angle;
  state.dial_travel_degrees += absolute(delta);
  if (!state.dial_rotated &&
      state.dial_travel_degrees >= kDialRotationThresholdDegrees) {
    // Rotation wins over click/long-press. End the press before publishing
    // encoder steps so the host never classifies a turning gesture as a hold.
    append(output, make_key_released(ControlId::Encoder));
    state.dial_rotated = true;
    state.dial_press_released = true;
  }
  state.dial_remainder_degrees += delta;
  while (state.dial_remainder_degrees >= 15.0F) {
    append(output, make_encoder_step(1));
    state.dial_remainder_degrees -= 15.0F;
  }
  while (state.dial_remainder_degrees <= -15.0F) {
    append(output, make_encoder_step(-1));
    state.dial_remainder_degrees += 15.0F;
  }
}

}  // namespace

InputNormalizer make_input_normalizer() { return {}; }

MultiTouchNormalizer make_multi_touch_normalizer() {
  MultiTouchNormalizer state{};
  return state;
}

InputFrame normalize_pointer(InputNormalizer& state,
                             const ControlLayout& layout,
                             RawPointerSample sample) {
  InputFrame output{};
  if (sample.phase == PointerPhase::Down) {
    if (state.captured || !hit_test(layout, sample.point, &state.control)) {
      return output;
    }
    state.captured = true;
    state.started_ms = sample.monotonic_ms;
    state.last_emit_ms = 0;
    state.last_distance = 0.0F;
    state.capacitive_hold_fired = false;
    if (state.control.kind == ControlKind::Agent ||
        state.control.kind == ControlKind::Command) {
      append(output, make_key_pressed(control_id(state.control)));
    } else if (state.control.kind == ControlKind::Joystick) {
      joystick_sample(state, output, sample.point, sample.monotonic_ms, false);
    } else if (state.control.kind == ControlKind::Dial) {
      state.dial_angle = normalized_angle(sample.point, state.control.bounds);
      state.dial_remainder_degrees = 0.0F;
      state.dial_travel_degrees = 0.0F;
      state.dial_rotated = false;
      state.dial_press_released = false;
      append(output, make_key_pressed(ControlId::Encoder));
    }
    return output;
  }

  if (!state.captured) return output;
  if (sample.phase == PointerPhase::Move) {
    if (state.control.kind == ControlKind::Joystick) {
      joystick_sample(state, output, sample.point, sample.monotonic_ms, false);
    } else if (state.control.kind == ControlKind::Dial) {
      dial_sample(state, output, sample.point);
    } else if (state.control.kind == ControlKind::Capacitive &&
               !state.capacitive_hold_fired &&
               sample.monotonic_ms - state.started_ms >= 3000U) {
      append(output, make_pairing_mode_requested(sample.monotonic_ms));
      state.capacitive_hold_fired = true;
    }
    return output;
  }

  if (state.control.kind == ControlKind::Agent ||
      state.control.kind == ControlKind::Command) {
    append(output, make_key_released(control_id(state.control)));
  } else if (state.control.kind == ControlKind::Dial) {
    if (!state.dial_press_released) {
      append(output, make_key_released(ControlId::Encoder));
    }
  } else if (state.control.kind == ControlKind::Joystick) {
    joystick_sample(state, output, sample.point, sample.monotonic_ms, true);
  } else if (state.control.kind == ControlKind::Capacitive &&
             !state.capacitive_hold_fired &&
             sample.phase == PointerPhase::Up) {
    append(output, make_layer_next());
  }
  state.captured = false;
  return output;
}

InputFrame normalize_touches(MultiTouchNormalizer& state,
                             const ControlLayout& layout,
                             const RawTouchFrame& frame) {
  InputFrame output{};
  for (auto& touch : state.touches) touch.seen = false;

  const unsigned int count =
      frame.count > kMaxTouchPoints ? kMaxTouchPoints : frame.count;
  for (unsigned int point_index = 0; point_index < count; ++point_index) {
    const RawTouchPoint& point = frame.points[point_index];
    TrackedTouch* tracked = nullptr;
    for (auto& candidate : state.touches) {
      if (candidate.active && candidate.track_id == point.track_id) {
        tracked = &candidate;
        break;
      }
    }
    bool newly_active = false;
    if (tracked == nullptr) {
      for (auto& candidate : state.touches) {
        if (!candidate.active) {
          tracked = &candidate;
          *tracked = {};
          tracked->active = true;
          tracked->track_id = point.track_id;
          tracked->input = make_input_normalizer();
          newly_active = true;
          break;
        }
      }
    }
    if (tracked == nullptr) continue;
    tracked->seen = true;
    tracked->last_point = point.point;
    append(output, normalize_pointer(
                       tracked->input, layout,
                       {newly_active ? PointerPhase::Down : PointerPhase::Move,
                        point.point, frame.monotonic_ms}));
  }

  for (auto& touch : state.touches) {
    if (!touch.active || touch.seen) continue;
    append(output, normalize_pointer(
                       touch.input, layout,
                       {PointerPhase::Up, touch.last_point, frame.monotonic_ms}));
    touch = {};
  }
  return output;
}

}  // namespace codex
