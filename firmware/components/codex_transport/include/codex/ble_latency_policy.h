#pragma once

namespace codex {

struct BleConnectionParameters {
  unsigned short minimum_interval_units{};
  unsigned short maximum_interval_units{};
  unsigned short peripheral_latency{};
  unsigned short supervision_timeout_units{};
};

[[nodiscard]] constexpr BleConnectionParameters
preferred_ble_connection_parameters() {
  // Bluetooth LE interval units are 1.25 ms. Keep the range acceptable to
  // Windows and macOS while requesting the lowest practical HID latency.
  return {
      .minimum_interval_units = 6U,
      .maximum_interval_units = 12U,
      .peripheral_latency = 0U,
      .supervision_timeout_units = 200U,
  };
}

[[nodiscard]] constexpr unsigned int ble_interval_microseconds(
    unsigned short interval_units) {
  return static_cast<unsigned int>(interval_units) * 1250U;
}

}  // namespace codex
