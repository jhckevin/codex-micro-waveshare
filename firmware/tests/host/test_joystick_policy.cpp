#include "codex/joystick_policy.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures = 0;
void require(bool condition) { failures += condition ? 0UL : 1UL; }
}

extern "C" void mainCRTStartup() {
  using namespace codex;
  require(scale_classic_joystick(0.6F, 50) > 0.299F);
  require(scale_classic_joystick(0.6F, 50) < 0.301F);
  require(scale_classic_joystick(0.6F, 100) > 0.599F);
  require(scale_classic_joystick(0.6F, 100) < 0.601F);
  require(scale_classic_joystick(0.6F, 200) == 1.0F);
  require(scale_classic_joystick(-0.2F, 100) == 0.0F);
  require(scale_classic_joystick(0.4F, 20) > 0.199F);
  require(scale_classic_joystick(0.4F, 250) == 0.8F);
  ExitProcess(failures);
}
