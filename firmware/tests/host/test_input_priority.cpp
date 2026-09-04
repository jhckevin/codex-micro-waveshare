#include "codex/input_priority.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);
namespace { unsigned long failures{}; void require(bool v) { failures += v ? 0 : 1; } }

extern "C" void mainCRTStartup() {
  using namespace codex;
  require(input_event_is_latency_critical(EventType::KeyPressed));
  require(input_event_is_latency_critical(EventType::KeyReleased));
  require(input_event_is_latency_critical(EventType::EncoderStep));
  require(input_event_is_latency_critical(EventType::LayerNext));
  require(input_event_is_latency_critical(EventType::PairingModeRequested));
  require(!input_event_is_latency_critical(EventType::AmbientLightingChanged));
  require(touch_frame_is_user_activity(1));
  require(touch_frame_is_user_activity(5));
  require(!touch_frame_is_user_activity(0));
  require(kMaxCriticalEventsPerFrame >= 12);
  require(kMaxCriticalEventsPerFrame <= 16);
  ExitProcess(failures);
}
