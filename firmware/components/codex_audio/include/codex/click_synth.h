#pragma once

#include "codex/device_state.h"

namespace codex {

constexpr unsigned int kClickSampleRate = 22050;
constexpr unsigned int kClickSampleCount = 768;
constexpr unsigned char kCapacitiveGainPercent = 60;

void synthesize_click(SoundProfile profile, bool key_down,
                      short* samples, unsigned int count);
void synthesize_capacitive_swipe(short* samples, unsigned int count);

}  // namespace codex
