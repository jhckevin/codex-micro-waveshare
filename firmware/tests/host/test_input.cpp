#include "codex/input.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures = 0;
void require(bool condition) { failures += condition ? 0UL : 1UL; }
}

extern "C" void mainCRTStartup() {
  using namespace codex;
  const ControlLayout layout = make_control_layout();
  InputNormalizer input = make_input_normalizer();

  InputFrame out = normalize_pointer(input, layout, {PointerPhase::Down, {180, 85}, 0});
  require(out.count == 1 && out.events[0].type == EventType::KeyPressed);
  out = normalize_pointer(input, layout, {PointerPhase::Up, {180, 85}, 20});
  require(out.count == 1 && out.events[0].type == EventType::KeyReleased);

  out = normalize_pointer(input, layout, {PointerPhase::Down, {80, 80}, 40});
  require(out.count == 1 && out.events[0].type == EventType::KeyPressed);
  require(out.events[0].payload.key.control == ControlId::Encoder);
  out = normalize_pointer(input, layout, {PointerPhase::Move, {80, 80}, 3100});
  require(out.count == 0);
  out = normalize_pointer(input, layout, {PointerPhase::Up, {80, 80}, 3300});
  require(out.count == 1 && out.events[0].type == EventType::KeyReleased);
  require(out.events[0].payload.key.control == ControlId::Encoder);

  input = make_input_normalizer();
  out = normalize_pointer(input, layout, {PointerPhase::Down, {125, 90}, 3400});
  require(out.count == 1 && out.events[0].type == EventType::KeyPressed);
  out = normalize_pointer(input, layout, {PointerPhase::Move, {124, 87}, 3420});
  require(out.count == 0);  // Small touch jitter remains a press gesture.
  out = normalize_pointer(input, layout, {PointerPhase::Up, {124, 87}, 3500});
  require(out.count == 1 && out.events[0].type == EventType::KeyReleased);

  input = make_input_normalizer();
  out = normalize_pointer(input, layout, {PointerPhase::Down, {125, 90}, 3600});
  require(out.count == 1 && out.events[0].type == EventType::KeyPressed);
  out = normalize_pointer(input, layout, {PointerPhase::Move, {120, 70}, 3620});
  require(out.count >= 2);
  require(out.events[0].type == EventType::KeyReleased);
  require(out.events[0].payload.key.control == ControlId::Encoder);
  require(out.events[1].type == EventType::EncoderStep);
  out = normalize_pointer(input, layout, {PointerPhase::Up, {120, 70}, 3650});
  require(out.count == 0);  // Rotation already cancelled the press gesture.

  out = normalize_pointer(input, layout, {PointerPhase::Down, {384, 90}, 100});
  require(out.count == 1 && out.events[0].type == EventType::JoystickChanged);
  require(out.events[0].payload.joystick.distance < 0.08F);
  out = normalize_pointer(input, layout, {PointerPhase::Move, {425, 90}, 120});
  require(out.count == 1 && out.events[0].payload.joystick.distance > 0.5F);
  out = normalize_pointer(input, layout, {PointerPhase::Move, {426, 90}, 125});
  require(out.count == 0);  // 60 Hz and delta threshold gate.
  out = normalize_pointer(input, layout, {PointerPhase::Up, {426, 90}, 145});
  require(out.count == 1 && out.events[0].payload.joystick.distance == 0.0F);

  out = normalize_pointer(input, layout, {PointerPhase::Down, {80, 380}, 1000});
  out = normalize_pointer(input, layout, {PointerPhase::Up, {80, 380}, 1200});
  require(out.count == 1 && out.events[0].type == EventType::LayerNext);

  out = normalize_pointer(input, layout, {PointerPhase::Down, {80, 380}, 2000});
  out = normalize_pointer(input, layout, {PointerPhase::Move, {80, 380}, 5001});
  require(out.count == 1 && out.events[0].type == EventType::PairingModeRequested);
  out = normalize_pointer(input, layout, {PointerPhase::Up, {80, 380}, 5100});
  require(out.count == 0);

  MultiTouchNormalizer multi = make_multi_touch_normalizer();
  RawTouchFrame touches{};
  touches.monotonic_ms = 6000;
  touches.count = 2;
  touches.points[0] = {3, {180, 85}};
  touches.points[1] = {7, {280, 180}};
  out = normalize_touches(multi, layout, touches);
  require(out.count == 2);
  require(out.events[0].type == EventType::KeyPressed);
  require(out.events[1].type == EventType::KeyPressed);

  touches.monotonic_ms = 6010;
  touches.count = 1;
  touches.points[0] = {7, {280, 180}};
  out = normalize_touches(multi, layout, touches);
  require(out.count == 1);
  require(out.events[0].type == EventType::KeyReleased);

  touches.monotonic_ms = 6020;
  touches.count = 0;
  out = normalize_touches(multi, layout, touches);
  require(out.count == 1);
  require(out.events[0].type == EventType::KeyReleased);

  input = make_input_normalizer();
  out = normalize_pointer(input, layout,
                          {PointerPhase::Down, {220, 380}, 7000});
  require(out.count == 1 && out.events[0].type == EventType::KeyPressed);
  require(out.events[0].payload.key.control == ControlId::Command4);
  out = normalize_pointer(input, layout,
                          {PointerPhase::Cancel, {220, 380}, 7100});
  require(out.count == 1 && out.events[0].type == EventType::KeyReleased);
  require(out.events[0].payload.key.control == ControlId::Command4);

  ExitProcess(failures);
}
