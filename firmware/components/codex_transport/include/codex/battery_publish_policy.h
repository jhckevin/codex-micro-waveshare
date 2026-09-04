#pragma once

namespace codex {

enum class BatteryPublishPoint : unsigned char {
  HidStarted,
  Connected,
  Authenticated,
  HostReady,
  ReadingChanged,
  Disconnected,
};

[[nodiscard]] constexpr bool should_publish_battery(
    BatteryPublishPoint point) {
  return point != BatteryPublishPoint::Disconnected;
}

[[nodiscard]] constexpr unsigned char host_battery_percentage(
    bool present, unsigned int measured_percentage) {
  if (!present) return 100U;
  if (measured_percentage == 0U) return 1U;
  return measured_percentage > 100U
             ? 100U
             : static_cast<unsigned char>(measured_percentage);
}

[[nodiscard]] constexpr bool battery_percentage_is_initialized(
    unsigned int percentage) {
  return percentage <= 100U;
}

}  // namespace codex
