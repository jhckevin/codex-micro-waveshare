#include "esp_hid_battery_status.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned int);

namespace {
int failures = 0;
void require(bool condition) {
  if (!condition) ++failures;
}
}  // namespace

extern "C" void mainCRTStartup() {
  const auto charging =
      esp_hid_make_battery_level_status(true, 68, true, true);
  require(charging.bytes[0] == 0x02);
  require(charging.bytes[1] == 0xA3);
  require(charging.bytes[2] == 0x00);
  require(charging.bytes[3] == 68);

  const auto discharging =
      esp_hid_make_battery_level_status(true, 8, false, false);
  require(discharging.bytes[0] == 0x02);
  require(discharging.bytes[1] == 0x41);
  require(discharging.bytes[2] == 0x01);
  require(discharging.bytes[3] == 8);

  const auto full_on_usb =
      esp_hid_make_battery_level_status(true, 100, false, true);
  require(full_on_usb.bytes[1] == 0xE3);
  require(full_on_usb.bytes[2] == 0x00);

  const auto absent =
      esp_hid_make_battery_level_status(false, 100, false, true);
  require(absent.bytes[0] == 0x00);
  require(absent.bytes[1] == 0x00);
  require(absent.bytes[2] == 0x00);
  require(absent.length == 3);

  ExitProcess(static_cast<unsigned int>(failures));
}
