#pragma once

#include <climits>

namespace codex {

enum class TouchSampleKind : unsigned char {
  Contacts,
  Empty,
  ReadFailure,
};

struct TouchContactState {
  bool pressed{};
  unsigned char consecutive_read_failures{};
};

struct TouchContactUpdate {
  TouchContactState state{};
  bool emit_release{};
  bool valid_frame{};
};

[[nodiscard]] constexpr TouchContactUpdate update_touch_contact(
    TouchContactState state, TouchSampleKind sample) {
  switch (sample) {
    case TouchSampleKind::Contacts:
      state.pressed = true;
      state.consecutive_read_failures = 0;
      return {state, false, true};
    case TouchSampleKind::Empty: {
      const bool release = state.pressed;
      state.pressed = false;
      state.consecutive_read_failures = 0;
      return {state, release, true};
    }
    case TouchSampleKind::ReadFailure:
    default:
      if (state.consecutive_read_failures < UCHAR_MAX) {
        ++state.consecutive_read_failures;
      }
      return {state, false, false};
  }
}

}  // namespace codex
