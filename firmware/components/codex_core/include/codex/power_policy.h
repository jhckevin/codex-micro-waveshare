#pragma once

namespace codex {

constexpr unsigned int kDefaultScreensaverTimeoutSeconds = 180;
constexpr unsigned int kDefaultAutoShutdownTimeoutSeconds = 7200;
constexpr unsigned int kUltraStandbyGraceMs = 300000;

struct UltraStandbyGuard {
  bool was_blocked{};
  unsigned int grace_until_ms{};
};

struct AutoShutdownInputs {
  unsigned int timeout_ms{};
  unsigned int disconnected_ms{};
  unsigned int inactive_ms{};
  bool any_transport_connected{};
  bool anti_accidental{};
};

enum class PowerDeadline : unsigned char {
  None,
  Screensaver,
  UltraStandby,
  AutoShutdown,
};

struct PowerDeadlineInputs {
  unsigned int screensaver_timeout_ms{};
  unsigned int auto_shutdown_timeout_ms{};
  unsigned int inactive_ms{};
  unsigned int disconnected_ms{};
  bool transport_ready{};
  bool anti_accidental{};
  bool screensaver_eligible{};
  unsigned int auto_ultra_timeout_ms{};
  bool ultra_eligible{};
  bool ultra_grace_elapsed{};
};

[[nodiscard]] constexpr bool valid_auto_shutdown_timeout(
    unsigned int seconds) {
  return seconds == 0 || seconds == 3600 || seconds == 7200 ||
         seconds == 10800 || seconds == 18000;
}

[[nodiscard]] constexpr bool valid_auto_ultra_timeout(
    unsigned int seconds) {
  return seconds == 0 || seconds == 3600 || seconds == 10800 ||
         seconds == 18000 || seconds == 28800 || seconds == 43200;
}

[[nodiscard]] constexpr bool deadline_reached(unsigned int now_ms,
                                              unsigned int deadline_ms) {
  return static_cast<signed int>(now_ms - deadline_ms) >= 0;
}

[[nodiscard]] constexpr bool update_ultra_standby_guard(
    UltraStandbyGuard& guard, bool blocked, unsigned int now_ms) {
  if (blocked) {
    guard.was_blocked = true;
    guard.grace_until_ms = 0;
    return false;
  }
  if (guard.was_blocked) {
    guard.was_blocked = false;
    guard.grace_until_ms = now_ms + kUltraStandbyGraceMs;
  }
  return guard.grace_until_ms == 0 ||
         deadline_reached(now_ms, guard.grace_until_ms);
}

[[nodiscard]] constexpr bool should_auto_shutdown(
    const AutoShutdownInputs& input) {
  if (input.timeout_ms == 0 || input.any_transport_connected) return false;
  const unsigned int elapsed =
      input.anti_accidental ? input.disconnected_ms : input.inactive_ms;
  return elapsed >= input.timeout_ms;
}

[[nodiscard]] constexpr PowerDeadline evaluate_power_deadline(
    const PowerDeadlineInputs& input) {
  if (should_auto_shutdown({
          input.auto_shutdown_timeout_ms,
          input.disconnected_ms,
          input.inactive_ms,
          input.transport_ready,
          input.anti_accidental,
      })) {
    return PowerDeadline::AutoShutdown;
  }
  if (input.auto_ultra_timeout_ms != 0 &&
      input.inactive_ms >= input.auto_ultra_timeout_ms &&
      input.ultra_eligible && input.ultra_grace_elapsed) {
    return PowerDeadline::UltraStandby;
  }
  if (input.screensaver_eligible &&
      input.screensaver_timeout_ms != 0 &&
      input.inactive_ms >= input.screensaver_timeout_ms) {
    return PowerDeadline::Screensaver;
  }
  return PowerDeadline::None;
}

}  // namespace codex
