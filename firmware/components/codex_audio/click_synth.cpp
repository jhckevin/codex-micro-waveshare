#include "codex/click_synth.h"

namespace codex {

void synthesize_click(SoundProfile profile, bool key_down,
                      short* samples, unsigned int count) {
  if (samples == nullptr) return;
  unsigned int random = key_down ? 0x31A7U : 0xB41DU;
  int low = 0;
  const unsigned int profile_value = static_cast<unsigned int>(profile);
  const unsigned int duration = profile == SoundProfile::Clicky ? 430U :
                                profile == SoundProfile::Tactile ? 620U : 760U;
  for (unsigned int index = 0; index < count; ++index) {
    random = random * 1664525U + 1013904223U;
    const int noise = static_cast<int>((random >> 18U) & 0x3FFFU) - 8192;
    low += (noise - low) >> (profile == SoundProfile::Linear ? 4 : 2);
    const unsigned int remaining = index < duration ? duration - index : 0U;
    int value = 0;
    if (remaining != 0) {
      const int body = profile == SoundProfile::Clicky ? noise : low;
      const int scale = key_down ? 13500 : 8500;
      value = (body * static_cast<int>(remaining) * scale) /
              (8192 * static_cast<int>(duration));
      if (profile_value >= 1U && index > 90U && index < 145U) {
        const int tactile = static_cast<int>(145U - index) *
                            (profile == SoundProfile::Clicky ? 170 : 70);
        value += (index & 4U) ? tactile : -tactile;
      }
    }
    if (value > 16000) value = 16000;
    if (value < -16000) value = -16000;
    samples[index] = static_cast<short>(value);
  }
}

void synthesize_capacitive_swipe(short* samples, unsigned int count) {
  if (samples == nullptr) return;
  unsigned int random = 0xC0D3A11U;
  int brushed = 0;
  for (unsigned int index = 0; index < count; ++index) {
    random = random * 1664525U + 1013904223U;
    const int noise = static_cast<int>((random >> 17U) & 0x7FFFU) - 16384;
    brushed += (noise - brushed) >> 3;
    const unsigned int duration = 560U;
    const unsigned int remaining = index < duration ? duration - index : 0U;
    int value = remaining == 0
                    ? 0
                    : static_cast<int>(
                          (static_cast<long long>(brushed) * remaining *
                           120000LL) /
                          (16384LL * duration));
    if (index < 70U) {
      value = value * static_cast<int>(index) / 70;
    }
    if (value > 30000) value = 30000;
    if (value < -30000) value = -30000;
    samples[index] = static_cast<short>(value);
  }
}

}  // namespace codex
