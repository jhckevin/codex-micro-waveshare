#pragma once

namespace codex {

struct ProtectedUnlockState {
  unsigned int first_tap_ms{};
  unsigned int visible_until_ms{};
  bool visible{};
};

struct ProtectedUnlockResult {
  bool show_unlock{};
  bool return_to_black{};
  bool unlock{};
};

enum class ProtectedUnlockInput : unsigned char {
  Tap,
  SideButton,
  Tick,
  Slide,
};

[[nodiscard]] constexpr ProtectedUnlockResult update_protected_unlock(
    ProtectedUnlockState& state, ProtectedUnlockInput input,
    unsigned int now_ms, unsigned char slide_percent = 0) {
  ProtectedUnlockResult result{};
  if (input == ProtectedUnlockInput::SideButton) {
    state.visible = true;
    state.visible_until_ms = now_ms + 3000U;
    state.first_tap_ms = 0;
    result.show_unlock = true;
  } else if (input == ProtectedUnlockInput::Tap && !state.visible) {
    if (state.first_tap_ms != 0 &&
        static_cast<unsigned int>(now_ms - state.first_tap_ms) <= 1500U) {
      state.visible = true;
      state.visible_until_ms = now_ms + 3000U;
      state.first_tap_ms = 0;
      result.show_unlock = true;
    } else {
      state.first_tap_ms = now_ms;
    }
  } else if (input == ProtectedUnlockInput::Slide && state.visible &&
             slide_percent >= 98U) {
    state = {};
    result.unlock = true;
  } else if (input == ProtectedUnlockInput::Tick && state.visible &&
             static_cast<signed int>(now_ms - state.visible_until_ms) >= 0) {
    state.visible = false;
    result.return_to_black = true;
  }
  return result;
}

}  // namespace codex
