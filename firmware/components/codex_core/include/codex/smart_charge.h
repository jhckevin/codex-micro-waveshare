#pragma once
namespace codex {
constexpr unsigned short kSmartChargeMaximumMa = 1200;
constexpr unsigned short charge_current_ma(unsigned char code) {
  code &= 31U;
  return code <= 8 ? code * 25U : code <= 21 ? 200U + (code - 8U) * 100U : 0U;
}
constexpr unsigned char charge_current_code(unsigned short ma) {
  if (ma > kSmartChargeMaximumMa) ma = kSmartChargeMaximumMa;
  return ma <= 200 ? ma / 25U : 8U + (ma - 200U) / 100U;
}
struct ChargeSample {
  bool valid{}, present{}, vbus_good{}, constrained{};
  unsigned short battery_mv{}, vbus_mv{}, input_limit_ma{};
  unsigned char phase{}, cv_code{};
};
struct SmartCharge {
  unsigned short current_ma{200};
  unsigned int changed_ms{};
  bool started{}, recovering{};
};
// Fast 1.2 A policy: the PMIC owns CC/CV taper and charge termination.
// Software only limits low-voltage precharge, input headroom and fault recovery.
inline unsigned short smart_charge_next(SmartCharge& state, const ChargeSample& s,
                                         unsigned int now) {
  const bool normal_phase = s.phase == 2 || s.phase == 3;
  const bool usable = s.valid && s.present && s.vbus_good &&
      s.battery_mv >= 2500 && s.battery_mv <= 4250 &&
      s.vbus_mv >= 4750 && s.vbus_mv <= 5500 &&
      s.cv_code == 3 && normal_phase;
  unsigned short target = 200;
  if (usable) {
    target = s.battery_mv < 3000 ? 200 : s.battery_mv < 3300 ? 400 : 1200;
    // Preserve 300 mA of the configured USB input budget for the running board.
    const unsigned short budget = s.input_limit_ma > 300 ? s.input_limit_ma - 300 : 0;
    if (target > budget) target = budget;
  }
  if (s.constrained) {
    const unsigned short reduced = state.current_ma > 400 ? state.current_ma - 200 : 200;
    if (target > reduced) target = reduced;
    state.recovering = true;
  }
  target = charge_current_ma(charge_current_code(target));
  if (!state.started) {
    state.started = true;
    state.current_ma = usable && target > 600 ? 600 : target;
    state.changed_ms = now;
    return state.current_ma;
  }
  if (target < state.current_ma) {
    state.current_ma = target;
    state.changed_ms = now;
  } else if (target > state.current_ma && !s.constrained) {
    const unsigned int delay = state.recovering ? 30000U : 10000U;
    if (static_cast<unsigned int>(now - state.changed_ms) >= delay) {
      unsigned short next = state.current_ma < 600 ? 600 : state.current_ma + 200;
      if (next > target) next = target;
      state.current_ma = charge_current_ma(charge_current_code(next));
      state.changed_ms = now;
      state.recovering = false;
    }
  }
  if (s.constrained) state.changed_ms = now;
  return state.current_ma;
}
}
