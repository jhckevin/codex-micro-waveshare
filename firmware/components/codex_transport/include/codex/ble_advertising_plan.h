#pragma once

namespace codex {

constexpr unsigned int kLegacyAdvertisingLimit = 31;
constexpr unsigned int kAdvertisingReserveTarget = 6;
inline constexpr char kBleDeviceName[] = "Codex Micro";

struct BleAdvertisingPacketLayout {
  bool flags;
  unsigned int complete_name_bytes;
  bool tx_power;
  bool connection_interval;
  bool appearance;
  unsigned int service_uuid_bytes;
};

constexpr unsigned int advertising_structure_size(unsigned int payload_bytes) {
  return payload_bytes == 0 ? 0 : payload_bytes + 2;
}

constexpr unsigned int advertising_packet_size(BleAdvertisingPacketLayout layout) {
  return (layout.flags ? advertising_structure_size(1) : 0) +
         advertising_structure_size(layout.complete_name_bytes) +
         (layout.tx_power ? advertising_structure_size(1) : 0) +
         (layout.connection_interval ? advertising_structure_size(4) : 0) +
         (layout.appearance ? advertising_structure_size(2) : 0) +
         advertising_structure_size(layout.service_uuid_bytes);
}

constexpr unsigned int advertising_packet_margin(BleAdvertisingPacketLayout layout) {
  const unsigned int size = advertising_packet_size(layout);
  return size <= kLegacyAdvertisingLimit ? kLegacyAdvertisingLimit - size : 0;
}

constexpr bool advertising_service_uuid_width_valid(
    BleAdvertisingPacketLayout layout) {
  return (layout.service_uuid_bytes % 16) == 0;
}

inline constexpr BleAdvertisingPacketLayout kCodexPrimaryAdvertisement{
    .flags = true,
    .complete_name_bytes = sizeof(kBleDeviceName) - 1,
    .tx_power = false,
    .connection_interval = false,
    .appearance = true,
    .service_uuid_bytes = 0,
};

inline constexpr BleAdvertisingPacketLayout kCodexScanResponse{
    .flags = false,
    .complete_name_bytes = 0,
    .tx_power = false,
    .connection_interval = false,
    .appearance = false,
    .service_uuid_bytes = 16,
};

}  // namespace codex
