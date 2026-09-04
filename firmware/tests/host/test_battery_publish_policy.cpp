#include "codex/battery_publish_policy.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);
namespace { unsigned long failures{}; void require(bool v) { failures += v ? 0 : 1; } }

extern "C" void mainCRTStartup() {
  using namespace codex;
  require(should_publish_battery(BatteryPublishPoint::HidStarted));
  require(should_publish_battery(BatteryPublishPoint::Connected));
  require(should_publish_battery(BatteryPublishPoint::Authenticated));
  require(should_publish_battery(BatteryPublishPoint::HostReady));
  require(should_publish_battery(BatteryPublishPoint::ReadingChanged));
  require(!should_publish_battery(BatteryPublishPoint::Disconnected));
  require(host_battery_percentage(true, 37) == 37);
  require(host_battery_percentage(true, 0) == 1);
  require(host_battery_percentage(true, 120) == 100);
  require(host_battery_percentage(false, 37) == 100);
  require(battery_percentage_is_initialized(0));
  require(battery_percentage_is_initialized(100));
  require(!battery_percentage_is_initialized(0xFF));
  ExitProcess(failures);
}
