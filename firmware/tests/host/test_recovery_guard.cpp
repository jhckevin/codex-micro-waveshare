#include "recovery_guard.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

extern "C" void mainCRTStartup() {
  unsigned long failures = 0;
  {
    const RecoveryGuard guard({.recovery_proven = false, .gpio0_low = false});
    failures += guard.allows_codex_usb() ? 1UL : 0UL;
  }
  {
    const RecoveryGuard guard({.recovery_proven = true, .gpio0_low = true});
    failures += guard.mode() == RecoveryBootMode::Maintenance ? 0UL : 1UL;
    failures += guard.allows_codex_usb() ? 1UL : 0UL;
  }
  {
    const RecoveryGuard guard({.recovery_proven = true, .gpio0_low = false});
    failures += guard.mode() == RecoveryBootMode::Normal ? 0UL : 1UL;
    failures += guard.allows_codex_usb() ? 0UL : 1UL;
  }
  ExitProcess(failures);
}
