#pragma once

namespace codex {

constexpr unsigned int kBatteryChargeTargetMv = 4100U;

struct BatteryDisplayModel {
  bool present{};
  unsigned char percent{100};
  bool charging{};
  bool external_power{};
  unsigned short voltage_mv{};
  unsigned short charge_limit_ma{};
  unsigned int charge_limit_mw{};
};

[[nodiscard]] constexpr BatteryDisplayModel make_battery_display(
    bool present, unsigned int percent, bool charging, bool external_power,
    unsigned short voltage_mv, unsigned short charge_limit_ma) {
  if (!present) {
    return {false, 100U, false, external_power, 0U, 0U, 0U};
  }
  const unsigned char sanitized_percent =
      percent == 0U ? 1U
                    : percent > 100U ? 100U
                                     : static_cast<unsigned char>(percent);
  return {
      true,
      sanitized_percent,
      charging,
      external_power,
      voltage_mv,
      charge_limit_ma,
      charging ? kBatteryChargeTargetMv * charge_limit_ma / 1000U : 0U,
  };
}

}  // namespace codex
