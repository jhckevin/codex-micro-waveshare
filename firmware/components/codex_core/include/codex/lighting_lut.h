#pragma once

#include "codex/device_state.h"

namespace codex {

inline constexpr unsigned int kLightingSampleCount = 64;
// The main loop sleeps for 16 ms. A 30 ms gate therefore renders on the
// second wake (~31 fps) instead of slipping to the third wake (~21 fps).
inline constexpr unsigned int kLightingFrameIntervalMs = 30;

inline constexpr unsigned char kBreathSamples[kLightingSampleCount] = {
    4, 4, 4, 5, 5, 6, 7, 8, 9, 10, 12, 13, 15, 16, 18, 19,
    21, 22, 24, 25, 26, 27, 28, 29, 30, 30, 31, 31, 31, 31, 31, 31,
    31, 31, 31, 31, 31, 31, 30, 30, 29, 28, 27, 26, 25, 24, 22, 21,
    19, 18, 16, 15, 13, 12, 10, 9, 8, 7, 6, 5, 5, 4, 4, 4};

inline constexpr unsigned char kShallowBreathSamples[kLightingSampleCount] = {
    20, 20, 20, 20, 20, 21, 21, 22, 22, 23, 23, 24, 25, 26, 26, 27,
    27, 28, 28, 29, 29, 30, 30, 30, 31, 31, 31, 31, 31, 31, 31, 31,
    31, 31, 31, 31, 31, 31, 31, 31, 30, 30, 30, 29, 29, 28, 28, 27,
    27, 26, 26, 25, 24, 23, 23, 22, 22, 21, 21, 20, 20, 20, 20, 20};

[[nodiscard]] constexpr unsigned char snake_sample_at_head(
    unsigned int head, unsigned int segment, unsigned int segment_count) {
  if (segment_count == 0) return 0;
  const unsigned int distance =
      (head + segment_count - (segment % segment_count)) % segment_count;
  constexpr unsigned char tail[] = {31, 22, 13, 6, 2};
  return distance < sizeof(tail) ? tail[distance] : 0;
}

[[nodiscard]] constexpr unsigned int lighting_phase_index(float phase) {
  if (phase <= 0.0F) return 0;
  if (phase >= 1.0F) return kLightingSampleCount - 1U;
  return static_cast<unsigned int>(phase * kLightingSampleCount) &
         (kLightingSampleCount - 1U);
}

[[nodiscard]] constexpr unsigned char quantized_effect_sample(
    LightEffect effect, unsigned int phase_index, unsigned int segment,
    unsigned int segment_count) {
  phase_index &= kLightingSampleCount - 1U;
  switch (effect) {
    case LightEffect::Off:
      return 0;
    case LightEffect::Breath:
      return kBreathSamples[phase_index];
    case LightEffect::ShallowBreath:
      return kShallowBreathSamples[phase_index];
    case LightEffect::Snake: {
      if (segment_count == 0) return 0;
      const unsigned int scaled = phase_index * segment_count;
      const unsigned int head = scaled / kLightingSampleCount;
      const unsigned int fraction = scaled % kLightingSampleCount;
      const unsigned int current =
          snake_sample_at_head(head, segment, segment_count);
      const unsigned int next =
          snake_sample_at_head((head + 1U) % segment_count, segment,
                               segment_count);
      return static_cast<unsigned char>(
          (current * (kLightingSampleCount - fraction) + next * fraction +
           kLightingSampleCount / 2U) /
          kLightingSampleCount);
    }
    default:
      return 31;
  }
}

[[nodiscard]] constexpr bool should_render_lighting(
    unsigned int last_render_ms, unsigned int now_ms) {
  return static_cast<unsigned int>(now_ms - last_render_ms) >=
         kLightingFrameIntervalMs;
}

}  // namespace codex
