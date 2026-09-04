#include "codex/ble_latency_policy.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

unsigned long failures{};

void require(bool value) {
  if (!value) ++failures;
}

extern "C" void mainCRTStartup() {
  constexpr codex::BleConnectionParameters parameters =
      codex::preferred_ble_connection_parameters();

  require(parameters.minimum_interval_units == 6U);
  require(parameters.maximum_interval_units == 12U);
  require(parameters.peripheral_latency == 0U);
  require(parameters.supervision_timeout_units == 200U);
  require(codex::ble_interval_microseconds(
              parameters.minimum_interval_units) == 7500U);
  require(codex::ble_interval_microseconds(
              parameters.maximum_interval_units) == 15000U);

  ExitProcess(failures);
}
