#include "codex/button_gesture.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures = 0;
void require(bool condition) { failures += condition ? 0UL : 1UL; }
}

extern "C" void mainCRTStartup() {
  using namespace codex;

  ButtonGesture short_press = make_button_gesture(false, 0);
  require(update_button_gesture(short_press, true, 100) == ButtonGestureEvent::None);
  require(update_button_gesture(short_press, false, 700) == ButtonGestureEvent::ShortPress);

  ButtonGesture immediate_power = make_button_gesture(false, 0);
  require(update_button_gesture_on_press(immediate_power, true, 100) ==
          ButtonGestureEvent::ShortPress);
  require(update_button_gesture_on_press(immediate_power, false, 140) ==
          ButtonGestureEvent::None);

  ButtonGesture long_press = make_button_gesture(false, 0);
  require(update_button_gesture(long_press, true, 1000) == ButtonGestureEvent::None);
  require(update_button_gesture(long_press, true, 3999) == ButtonGestureEvent::None);
  require(update_button_gesture(long_press, true, 4000) == ButtonGestureEvent::LongPress);
  require(update_button_gesture(long_press, true, 4500) == ButtonGestureEvent::None);
  require(update_button_gesture(long_press, false, 4600) == ButtonGestureEvent::None);

  // A button already held when the application starts belongs to the ROM boot
  // path and must not become an application short/long press on release.
  ButtonGesture held_at_boot = make_button_gesture(true, 0);
  require(update_button_gesture(held_at_boot, true, 5000) == ButtonGestureEvent::None);
  require(update_button_gesture(held_at_boot, false, 5100) == ButtonGestureEvent::None);
  require(update_button_gesture(held_at_boot, true, 5200) == ButtonGestureEvent::None);
  require(update_button_gesture(held_at_boot, false, 5300) == ButtonGestureEvent::ShortPress);

  ExitProcess(failures);
}
