#include "codex/click_synth.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace { unsigned long failures = 0; void require(bool v) { failures += v ? 0UL : 1UL; } }

extern "C" void mainCRTStartup() {
  using namespace codex;
  require(kCapacitiveGainPercent == 60);
  short linear[768]{};
  short tactile[768]{};
  short clicky[768]{};
  short swipe[768]{};
  synthesize_click(SoundProfile::Linear, true, linear, 768);
  synthesize_click(SoundProfile::Tactile, true, tactile, 768);
  synthesize_click(SoundProfile::Clicky, true, clicky, 768);
  synthesize_capacitive_swipe(swipe, 768);
  unsigned int different = 0;
  int early = 0;
  int late = 0;
  for (unsigned int i = 0; i < 768; ++i) {
    if (linear[i] != tactile[i] || tactile[i] != clicky[i]) ++different;
    const int magnitude = linear[i] < 0 ? -linear[i] : linear[i];
    if (i < 96) early += magnitude;
    if (i >= 672) late += magnitude;
    require(linear[i] <= 16000 && linear[i] >= -16000);
  }
  require(different > 500);
  require(early > late);
  require(clicky[700] == 0);
  unsigned int swipe_different = 0;
  int swipe_early = 0;
  int swipe_late = 0;
  for (unsigned int i = 0; i < 768; ++i) {
    if (swipe[i] != tactile[i]) ++swipe_different;
    const int magnitude = swipe[i] < 0 ? -swipe[i] : swipe[i];
    if (i < 160) swipe_early += magnitude;
    if (i >= 608) swipe_late += magnitude;
  }
  require(swipe_different > 500);
  require(swipe_early > swipe_late);
  int keyboard_peak = 0;
  int swipe_peak = 0;
  for (unsigned int i = 0; i < 768; ++i) {
    const int key = linear[i] < 0 ? -linear[i] : linear[i];
    const int cap = swipe[i] < 0 ? -swipe[i] : swipe[i];
    if (key > keyboard_peak) keyboard_peak = key;
    if (cap > swipe_peak) swipe_peak = cap;
  }
  require(swipe_peak > keyboard_peak);
  ExitProcess(failures);
}
