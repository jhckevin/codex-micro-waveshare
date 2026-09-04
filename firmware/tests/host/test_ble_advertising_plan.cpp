#include "codex/ble_advertising_plan.h"

namespace {

unsigned long failures{};

void check(bool condition) {
  if (!condition) ++failures;
}

}  // namespace

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

extern "C" void mainCRTStartup() {
  check(codex::advertising_service_uuid_width_valid(
      codex::kCodexPrimaryAdvertisement));
  check(codex::advertising_packet_size(codex::kCodexPrimaryAdvertisement) <=
        codex::kLegacyAdvertisingLimit);
  check(codex::advertising_packet_margin(codex::kCodexPrimaryAdvertisement) >=
        codex::kAdvertisingReserveTarget);
  check(codex::advertising_packet_size(codex::kCodexScanResponse) <=
        codex::kLegacyAdvertisingLimit);
  check(codex::advertising_packet_margin(codex::kCodexScanResponse) >=
        codex::kAdvertisingReserveTarget);
  check(codex::advertising_service_uuid_width_valid(
      codex::kCodexScanResponse));
  ExitProcess(failures);
}
