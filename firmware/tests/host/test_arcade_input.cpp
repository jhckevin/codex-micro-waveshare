#include "codex/arcade_input.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures = 0;
void require(bool condition) { failures += condition ? 0UL : 1UL; }
codex::RawTouchFrame frame(unsigned int now, unsigned char id, int x, int y) {
  codex::RawTouchFrame value{};
  value.monotonic_ms = now;
  value.count = 1;
  value.points[0] = {id, {x, y}};
  return value;
}
}

extern "C" void mainCRTStartup() {
  using namespace codex;
  ArcadeInputState state = make_arcade_input_state();
  require(state.mode == ArcadeControlMode::Joystick);

  ArcadeInputOutput out =
      normalize_arcade_touches(state, frame(100, 1, 95, 235));
  require(out.frame.count == 1);
  require(out.frame.events[0].type == EventType::KeyPressed);
  require(out.frame.events[0].payload.key.control == ControlId::Agent0);
  RawTouchFrame empty{};
  empty.monotonic_ms = 120;
  out = normalize_arcade_touches(state, empty);
  require(out.frame.count == 1 &&
          out.frame.events[0].type == EventType::KeyReleased);

  out = normalize_arcade_touches(state, frame(200, 2, 445, 230));
  require(out.frame.count == 1);
  require(out.frame.events[0].type == EventType::JoystickChanged);
  require(out.frame.events[0].payload.joystick.distance > 0.8F);
  require(out.frame.events[0].payload.joystick.angle < 0.05F ||
          out.frame.events[0].payload.joystick.angle > 0.95F);
  empty.monotonic_ms = 230;
  out = normalize_arcade_touches(state, empty);
  require(out.frame.count == 1 &&
          out.frame.events[0].payload.joystick.distance == 0.0F);

  out = normalize_arcade_touches(state, frame(300, 3, 400, 430));
  require(out.command == ArcadeUiCommand::UseDpad);
  require(state.mode == ArcadeControlMode::Dpad);
  empty.monotonic_ms = 310;
  out = normalize_arcade_touches(state, empty);
  out = normalize_arcade_touches(state, frame(340, 4, 285, 170));
  require(out.frame.count == 1);
  require(out.frame.events[0].payload.joystick.distance == 1.0F);
  require(out.frame.events[0].payload.joystick.angle > 0.60F &&
          out.frame.events[0].payload.joystick.angle < 0.65F);

  empty.monotonic_ms = 360;
  out = normalize_arcade_touches(state, empty);
  out = normalize_arcade_touches(state, frame(400, 5, 270, 430));
  require(out.command == ArcadeUiCommand::UseTouchRegion);
  require(state.mode == ArcadeControlMode::TouchRegion);
  empty.monotonic_ms = 410;
  out = normalize_arcade_touches(state, empty);
  out = normalize_arcade_touches(state, frame(450, 6, 300, 210));
  require(out.frame.count == 1);
  require(out.frame.events[0].payload.joystick.distance == 0.0F);
  out = normalize_arcade_touches(state, frame(470, 6, 345, 255));
  require(out.frame.count == 1);
  require(out.frame.events[0].payload.joystick.distance > 0.6F);

  empty.monotonic_ms = 500;
  out = normalize_arcade_touches(state, empty);
  out = normalize_arcade_touches(state, frame(530, 7, 451, 29));
  require(out.command == ArcadeUiCommand::Exit);
  ExitProcess(failures);
}
