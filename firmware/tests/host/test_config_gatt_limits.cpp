#include "codex/config_gatt_limits.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

extern "C" void mainCRTStartup() {
  const bool valid =
      codex::kConfigGattAppId <= codex::kBluedroidGattAppIdMax &&
      codex::kConfigGattRxQueueDepth >= 4 &&
      codex::kConfigGattWorkerStackBytes >= 6144 &&
      codex::kBleTxQueueDepth >= 16 &&
      codex::kBleTxCriticalAttemptLimit <= 4 &&
      codex::kBleTxCriticalEnqueueWaitMs <= 2 &&
      codex::kConfigGattQueueMemory == codex::RuntimeMemory::ExternalRam &&
      codex::kBleTxQueueMemory == codex::RuntimeMemory::ExternalRam &&
      codex::kBleTxStackMemory == codex::RuntimeMemory::ExternalRam &&
      codex::config_gatt_should_start(true, false, false) &&
      !codex::config_gatt_should_start(false, false, false) &&
      !codex::config_gatt_should_start(true, true, false) &&
      !codex::config_gatt_should_start(true, false, true);
  ExitProcess(valid ? 0UL : 1UL);
}
