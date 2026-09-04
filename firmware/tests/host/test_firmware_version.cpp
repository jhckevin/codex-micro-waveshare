#include "codex/firmware_version.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
bool same(const char* left, const char* right) {
  unsigned int index = 0;
  while (left[index] && right[index] && left[index] == right[index]) ++index;
  return left[index] == right[index];
}
}

extern "C" void mainCRTStartup() {
  using namespace codex;
  unsigned long failures = 0;
  failures += same(kPublicFirmwareVersion, "1.0.1.1") ? 0UL : 1UL;
  failures += same(kFirmwareSettingsLabel, "Firmware 1.0.1.1") ? 0UL : 1UL;
  ExitProcess(failures);
}
