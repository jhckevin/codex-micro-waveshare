#include "codex/display_transition_policy.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures{};
void require(bool value) { failures += value ? 0UL : 1UL; }
}  // namespace

extern "C" void mainCRTStartup() {
  using namespace codex;

  require(kDisplayFadeStepCount >= 4U);
  require(kDisplayFadeStepCount <= 6U);
  unsigned char previous = 100;
  for (unsigned int step = 0; step < kDisplayFadeStepCount; ++step) {
    const unsigned char current = display_fade_level(100, step);
    require(current <= previous);
    previous = current;
  }
  require(display_fade_level(100, kDisplayFadeStepCount - 1U) == 0U);
  require(display_fade_level(40, 0) == 40U);
  require(display_fade_level(40, kDisplayFadeStepCount - 1U) == 0U);
  require(kDisplayFadeStepDelayMs <= 10U);

  ExitProcess(failures);
}
