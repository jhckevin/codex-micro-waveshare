#pragma once

namespace codex {

enum class RuntimeMemory : unsigned char {
  InternalRam,
  ExternalRam,
};

constexpr unsigned short kBluedroidGattAppIdMax = 0x7FFF;
constexpr unsigned short kConfigGattAppId = 0x40DE;
constexpr unsigned int kConfigGattChunkCapacity = 512;
constexpr unsigned int kConfigGattRxQueueDepth = 8;
constexpr unsigned int kConfigGattWorkerStackBytes = 6144;
constexpr unsigned int kBleTxQueueDepth = 16;
constexpr unsigned int kBleTxCriticalAttemptLimit = 4;
constexpr unsigned int kBleTxCriticalEnqueueWaitMs = 2;
constexpr RuntimeMemory kConfigGattQueueMemory = RuntimeMemory::ExternalRam;
constexpr RuntimeMemory kBleTxQueueMemory = RuntimeMemory::ExternalRam;
constexpr RuntimeMemory kBleTxStackMemory = RuntimeMemory::ExternalRam;
static_assert(kConfigGattAppId <= kBluedroidGattAppIdMax);

constexpr bool config_gatt_should_start(bool hid_started,
                                        bool config_gatt_started,
                                        bool stopping) {
  return hid_started && !config_gatt_started && !stopping;
}

}  // namespace codex
