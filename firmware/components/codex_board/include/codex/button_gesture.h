#pragma once

namespace codex {

constexpr unsigned int kButtonLongPressMs = 3000;

enum class ButtonGestureEvent : unsigned char {
  None,
  ShortPress,
  LongPress,
};

struct ButtonGesture {
  bool pressed{false};
  bool armed{true};
  bool long_emitted{false};
  unsigned int pressed_at_ms{0};
};

[[nodiscard]] ButtonGesture make_button_gesture(bool initially_pressed,
                                                unsigned int now_ms);
[[nodiscard]] ButtonGestureEvent update_button_gesture(ButtonGesture& gesture,
                                                       bool pressed,
                                                       unsigned int now_ms);
[[nodiscard]] ButtonGestureEvent update_button_gesture_on_press(
    ButtonGesture& gesture, bool pressed, unsigned int now_ms);

}  // namespace codex
