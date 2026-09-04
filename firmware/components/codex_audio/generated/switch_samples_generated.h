#pragma once

namespace codex {

struct SwitchSample {
  const short* samples;
  unsigned short count;
};

constexpr unsigned int kSwitchSampleRate = 22050;
constexpr unsigned int kSwitchSampleVariations = 4;

[[nodiscard]] SwitchSample switch_sample(bool tactile, bool down,
                                         unsigned int variation);

}  // namespace codex
