#include "codex/battery_display.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures{};
void require(bool value) { failures += value ? 0UL : 1UL; }
}  // namespace

extern "C" void mainCRTStartup() {
  using namespace codex;

  BatteryDisplayModel absent =
      make_battery_display(false, 41, false, true, 0, 200);
  require(!absent.present);
  require(absent.percent == 100);
  require(absent.charge_limit_mw == 0);
  require(absent.external_power);

  BatteryDisplayModel charging =
      make_battery_display(true, 63, true, true, 3912, 200);
  require(charging.present);
  require(charging.percent == 63);
  require(charging.charging);
  require(charging.voltage_mv == 3912);
  require(charging.charge_limit_ma == 200);
  require(charging.charge_limit_mw == 820);

  BatteryDisplayModel discharging =
      make_battery_display(true, 24, false, false, 3710, 0);
  require(discharging.present);
  require(!discharging.charging);
  require(discharging.charge_limit_mw == 0);

  BatteryDisplayModel full_on_usb =
      make_battery_display(true, 100, false, true, 4177, 200);
  require(full_on_usb.present);
  require(full_on_usb.percent == 100);
  require(!full_on_usb.charging);
  require(full_on_usb.external_power);
  require(full_on_usb.charge_limit_ma == 200);
  require(full_on_usb.charge_limit_mw == 0);

  ExitProcess(failures);
}
