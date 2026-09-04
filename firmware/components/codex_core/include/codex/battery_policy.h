#pragma once

namespace codex {

struct BatteryReading {
  bool present{};
  unsigned char percent{100};
  bool charging{};
  bool external_power{};
  unsigned short voltage_mv{};
  unsigned short charge_limit_ma{};
};

[[nodiscard]] constexpr bool codex_app_charging_state(
    bool present, bool charging, bool external_power) {
  return present && (charging || external_power);
}

[[nodiscard]] constexpr unsigned char estimate_battery_percent(
    unsigned short voltage_mv) {
  if (voltage_mv <= 3300) return 1;
  if (voltage_mv >= 4200) return 100;
  return static_cast<unsigned char>(1U +
      (static_cast<unsigned int>(voltage_mv - 3300U) * 99U) / 900U);
}

[[nodiscard]] constexpr BatteryReading sanitize_battery_reading(
    BatteryReading reading) {
  if (!reading.present) {
    reading.percent = 100;
    reading.charging = false;
    reading.voltage_mv = 0;
    reading.charge_limit_ma = 0;
    return reading;
  }
  if (reading.percent == 0 && reading.voltage_mv >= 3400) {
    reading.percent = estimate_battery_percent(reading.voltage_mv);
  }
  if (reading.percent == 0) reading.percent = 1;
  if (reading.percent > 100) reading.percent = 100;
  return reading;
}

struct BatteryWarningState {
  bool initialized{};
  bool previous_present{};
  bool previous_low{};
  bool visible{};
  unsigned int expires_at_ms{};
};

[[nodiscard]] constexpr BatteryWarningState update_battery_warning(
    BatteryWarningState state, const BatteryReading& reading,
    unsigned int now_ms) {
  const bool low = reading.present && reading.percent < 10;
  if (!state.initialized) {
    state.initialized = true;
    state.previous_present = reading.present;
    state.previous_low = low;
    return state;
  }
  if ((state.previous_present && !reading.present) ||
      (!state.previous_low && low)) {
    state.visible = true;
    state.expires_at_ms = now_ms + 600000U;
  }
  state.previous_present = reading.present;
  state.previous_low = low;
  return state;
}

[[nodiscard]] constexpr BatteryWarningState tick_battery_warning(
    BatteryWarningState state, unsigned int now_ms) {
  if (state.visible &&
      static_cast<signed int>(now_ms - state.expires_at_ms) >= 0) {
    state.visible = false;
  }
  return state;
}

[[nodiscard]] constexpr BatteryWarningState dismiss_battery_warning(
    BatteryWarningState state) {
  state.visible = false;
  return state;
}

}  // namespace codex
