#pragma once

#include "codex/device_event.h"

namespace codex {

constexpr unsigned int kMaxCriticalEventsPerFrame = 16;

[[nodiscard]] constexpr bool touch_frame_is_user_activity(
    unsigned int point_count) {
  return point_count != 0;
}

[[nodiscard]] constexpr bool input_event_is_latency_critical(
    EventType type) {
  return type == EventType::KeyPressed ||
         type == EventType::KeyReleased ||
         type == EventType::JoystickChanged ||
         type == EventType::EncoderStep ||
         type == EventType::LayerNext ||
         type == EventType::PairingModeRequested;
}

}  // namespace codex
