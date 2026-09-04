#pragma once

#include "codex/control_layout.h"
#include "codex/device_event.h"

namespace codex {

enum class PointerPhase : unsigned char { Down, Move, Up, Cancel };

struct RawPointerSample {
  PointerPhase phase{PointerPhase::Move};
  Point point{};
  unsigned int monotonic_ms{0};
};

struct InputFrame {
  DeviceEvent events[24]{};
  unsigned char count{0};
};

struct InputNormalizer {
  bool captured{false};
  ControlHit control{};
  unsigned int started_ms{0};
  unsigned int last_emit_ms{0};
  float last_distance{0.0F};
  float last_angle{0.0F};
  float dial_angle{0.0F};
  float dial_remainder_degrees{0.0F};
  float dial_travel_degrees{0.0F};
  bool dial_rotated{false};
  bool dial_press_released{false};
  bool capacitive_hold_fired{false};
};

constexpr unsigned int kMaxTouchPoints = 5;

struct RawTouchPoint {
  unsigned char track_id{0};
  Point point{};
};

struct RawTouchFrame {
  RawTouchPoint points[kMaxTouchPoints]{};
  unsigned char count{0};
  unsigned int monotonic_ms{0};
};

struct TrackedTouch {
  bool active{false};
  bool seen{false};
  unsigned char track_id{0};
  Point last_point{};
  InputNormalizer input{};
};

struct MultiTouchNormalizer {
  TrackedTouch touches[kMaxTouchPoints]{};
};

[[nodiscard]] InputNormalizer make_input_normalizer();
[[nodiscard]] InputFrame normalize_pointer(InputNormalizer& state,
                                           const ControlLayout& layout,
                                           RawPointerSample sample);
[[nodiscard]] MultiTouchNormalizer make_multi_touch_normalizer();
[[nodiscard]] InputFrame normalize_touches(MultiTouchNormalizer& state,
                                           const ControlLayout& layout,
                                           const RawTouchFrame& frame);

}  // namespace codex
