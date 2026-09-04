#include "codex/protected_unlock.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);
namespace { unsigned long failures{}; void require(bool v) { failures += v ? 0 : 1; } }

extern "C" void mainCRTStartup() {
  using namespace codex;
  ProtectedUnlockState state{};
  require(!update_protected_unlock(
      state, ProtectedUnlockInput::Tap, 1000).show_unlock);
  require(update_protected_unlock(
      state, ProtectedUnlockInput::Tap, 2400).show_unlock);
  require(!update_protected_unlock(
      state, ProtectedUnlockInput::Slide, 2500, 97).unlock);
  require(update_protected_unlock(
      state, ProtectedUnlockInput::Slide, 2600, 100).unlock);
  require(update_protected_unlock(
      state, ProtectedUnlockInput::SideButton, 3000).show_unlock);
  require(update_protected_unlock(
      state, ProtectedUnlockInput::Tick, 6001).return_to_black);
  ExitProcess(failures);
}
