#include "codex/battery_policy.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);
namespace { unsigned long failures{}; void require(bool v) { failures += v ? 0 : 1; } }

extern "C" void mainCRTStartup() {
  using namespace codex;
  const BatteryReading healthy =
      sanitize_battery_reading({true, 0, true, true, 4030});
  require(healthy.percent >= 80);
  const BatteryReading absent =
      sanitize_battery_reading({false, 0, true, true, 0});
  require(absent.percent == 100);
  require(!absent.charging);
  require(codex_app_charging_state(true, false, true));
  require(codex_app_charging_state(true, true, false));
  require(!codex_app_charging_state(true, false, false));
  require(!codex_app_charging_state(false, true, true));

  BatteryWarningState warning{};
  warning = update_battery_warning(warning, absent, 100);
  require(!warning.visible);
  warning = update_battery_warning(warning, {true, 55, false, false, 3800}, 200);
  require(!warning.visible);
  warning = update_battery_warning(warning, absent, 300);
  require(warning.visible);
  warning = dismiss_battery_warning(warning);
  require(!warning.visible);
  warning = update_battery_warning(warning, {true, 11, false, false, 3500}, 400);
  warning = update_battery_warning(warning, {true, 9, false, false, 3450}, 500);
  require(warning.visible);
  warning = tick_battery_warning(warning, 600500);
  require(!warning.visible);
  ExitProcess(failures);
}
