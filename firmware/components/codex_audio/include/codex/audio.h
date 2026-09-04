#pragma once

#include "codex/device_reducer.h"
#include "codex/click_synth.h"
#include "esp_err.h"

namespace codex {

esp_err_t codex_audio_start();
void codex_audio_play(const SideEffect& effect, SoundProfile profile);
void codex_audio_set_enabled(bool enabled);
void codex_audio_set_volume(unsigned char volume);

}  // namespace codex
