#include "codex/button_gesture.h"

namespace codex {

ButtonGesture make_button_gesture(bool initially_pressed,
                                  unsigned int now_ms) {
  return {
      .pressed = initially_pressed,
      .armed = !initially_pressed,
      .long_emitted = false,
      .pressed_at_ms = now_ms,
  };
}

ButtonGestureEvent update_button_gesture(ButtonGesture& gesture,
                                         bool pressed,
                                         unsigned int now_ms) {
  if (pressed != gesture.pressed) {
    gesture.pressed = pressed;
    if (pressed) {
      gesture.pressed_at_ms = now_ms;
      gesture.long_emitted = false;
      return ButtonGestureEvent::None;
    }
    if (!gesture.armed) {
      gesture.armed = true;
      gesture.long_emitted = false;
      return ButtonGestureEvent::None;
    }
    const bool was_long = gesture.long_emitted;
    gesture.long_emitted = false;
    return was_long ? ButtonGestureEvent::None
                    : ButtonGestureEvent::ShortPress;
  }

  if (pressed && gesture.armed && !gesture.long_emitted &&
      static_cast<unsigned int>(now_ms - gesture.pressed_at_ms) >=
          kButtonLongPressMs) {
    gesture.long_emitted = true;
    return ButtonGestureEvent::LongPress;
  }
  return ButtonGestureEvent::None;
}

ButtonGestureEvent update_button_gesture_on_press(ButtonGesture& gesture,
                                                  bool pressed,
                                                  unsigned int now_ms) {
  if (pressed != gesture.pressed) {
    gesture.pressed = pressed;
    if (pressed) {
      gesture.pressed_at_ms = now_ms;
      gesture.long_emitted = false;
      return gesture.armed ? ButtonGestureEvent::ShortPress
                           : ButtonGestureEvent::None;
    }
    gesture.armed = true;
    gesture.long_emitted = false;
    return ButtonGestureEvent::None;
  }
  if (pressed && gesture.armed && !gesture.long_emitted &&
      static_cast<unsigned int>(now_ms - gesture.pressed_at_ms) >=
          kButtonLongPressMs) {
    gesture.long_emitted = true;
    return ButtonGestureEvent::LongPress;
  }
  return ButtonGestureEvent::None;
}

}  // namespace codex
