#pragma once

#include "codex/device_state.h"

namespace codex {

inline constexpr unsigned int kAmbientFramePhaseCount = 64U;
inline constexpr unsigned int kAmbientFrameSegmentCount = 16U;
inline constexpr unsigned char kAmbientSnakeFrames
    [kAmbientFramePhaseCount][kAmbientFrameSegmentCount] = {
    {255U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 14U, 54U, 142U},
    {227U, 64U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 11U, 44U, 120U},
    {199U, 128U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 7U, 34U, 98U},
    {170U, 191U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 4U, 24U, 76U},
    {142U, 255U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 14U, 54U},
    {120U, 227U, 64U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 11U, 44U},
    {98U, 199U, 128U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 7U, 34U},
    {76U, 170U, 191U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 4U, 24U},
    {54U, 142U, 255U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 14U},
    {44U, 120U, 227U, 64U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 11U},
    {34U, 98U, 199U, 128U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 7U},
    {24U, 76U, 170U, 191U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 4U},
    {14U, 54U, 142U, 255U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {11U, 44U, 120U, 227U, 64U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {7U, 34U, 98U, 199U, 128U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {4U, 24U, 76U, 170U, 191U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 14U, 54U, 142U, 255U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 11U, 44U, 120U, 227U, 64U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 7U, 34U, 98U, 199U, 128U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 4U, 24U, 76U, 170U, 191U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 14U, 54U, 142U, 255U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 11U, 44U, 120U, 227U, 64U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 7U, 34U, 98U, 199U, 128U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 4U, 24U, 76U, 170U, 191U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 14U, 54U, 142U, 255U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 11U, 44U, 120U, 227U, 64U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 7U, 34U, 98U, 199U, 128U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 4U, 24U, 76U, 170U, 191U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 14U, 54U, 142U, 255U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 11U, 44U, 120U, 227U, 64U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 7U, 34U, 98U, 199U, 128U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 4U, 24U, 76U, 170U, 191U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 14U, 54U, 142U, 255U, 0U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 11U, 44U, 120U, 227U, 64U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 7U, 34U, 98U, 199U, 128U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 4U, 24U, 76U, 170U, 191U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 14U, 54U, 142U, 255U, 0U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 11U, 44U, 120U, 227U, 64U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 7U, 34U, 98U, 199U, 128U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 4U, 24U, 76U, 170U, 191U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 14U, 54U, 142U, 255U, 0U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 11U, 44U, 120U, 227U, 64U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 7U, 34U, 98U, 199U, 128U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 4U, 24U, 76U, 170U, 191U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 14U, 54U, 142U, 255U, 0U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 11U, 44U, 120U, 227U, 64U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 7U, 34U, 98U, 199U, 128U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 4U, 24U, 76U, 170U, 191U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 14U, 54U, 142U, 255U, 0U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 11U, 44U, 120U, 227U, 64U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 7U, 34U, 98U, 199U, 128U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 4U, 24U, 76U, 170U, 191U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 14U, 54U, 142U, 255U, 0U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 11U, 44U, 120U, 227U, 64U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 7U, 34U, 98U, 199U, 128U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 4U, 24U, 76U, 170U, 191U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 14U, 54U, 142U, 255U, 0U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 11U, 44U, 120U, 227U, 64U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 7U, 34U, 98U, 199U, 128U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 4U, 24U, 76U, 170U, 191U},
    {0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 14U, 54U, 142U, 255U},
    {64U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 11U, 44U, 120U, 227U},
    {128U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 7U, 34U, 98U, 199U},
    {191U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 0U, 4U, 24U, 76U, 170U},
};

inline constexpr unsigned char kAmbientBreathFrames
    [kAmbientFramePhaseCount] = {
    33U, 33U, 35U, 38U, 41U, 46U, 52U, 58U, 65U, 74U, 82U, 92U, 101U, 112U, 122U, 133U, 144U, 155U, 166U, 176U, 186U, 196U, 206U, 214U, 222U, 230U, 236U, 242U, 247U, 250U, 253U, 254U, 255U, 254U, 253U, 250U, 247U, 242U, 236U, 230U, 222U, 214U, 206U, 196U, 186U, 176U, 166U, 155U, 144U, 133U, 122U, 112U, 101U, 92U, 82U, 74U, 65U, 58U, 52U, 46U, 41U, 38U, 35U, 33U};
inline constexpr unsigned char kAmbientShallowBreathFrames
    [kAmbientFramePhaseCount] = {
    165U, 165U, 165U, 166U, 168U, 170U, 172U, 175U, 178U, 181U, 185U, 188U, 192U, 197U, 201U, 205U, 210U, 214U, 219U, 223U, 227U, 231U, 235U, 238U, 242U, 245U, 247U, 250U, 252U, 253U, 254U, 255U, 255U, 255U, 254U, 253U, 252U, 250U, 247U, 245U, 242U, 238U, 235U, 231U, 227U, 223U, 219U, 214U, 210U, 205U, 201U, 197U, 192U, 188U, 185U, 181U, 178U, 175U, 172U, 170U, 168U, 166U, 165U, 165U};

[[nodiscard]] constexpr unsigned int ambient_frame_phase_index(
    float phase) {
  if (phase <= 0.0F) return 0U;
  if (phase >= 1.0F) return kAmbientFramePhaseCount - 1U;
  return static_cast<unsigned int>(phase * kAmbientFramePhaseCount) &
         (kAmbientFramePhaseCount - 1U);
}

[[nodiscard]] constexpr unsigned char ambient_frame_sample(
    LightEffect effect, unsigned int phase, unsigned int segment,
    float brightness) {
  if (effect == LightEffect::Off || brightness <= 0.0F) return 0U;
  if (brightness > 1.0F) brightness = 1.0F;
  phase &= kAmbientFramePhaseCount - 1U;
  segment %= kAmbientFrameSegmentCount;
  unsigned int sample = 255U;
  switch (effect) {
    case LightEffect::Snake:
      sample = kAmbientSnakeFrames[phase][segment];
      break;
    case LightEffect::Breath:
      sample = kAmbientBreathFrames[phase];
      break;
    case LightEffect::ShallowBreath:
      sample = kAmbientShallowBreathFrames[phase];
      break;
    default:
      break;
  }
  return static_cast<unsigned char>(
      static_cast<float>(sample) * brightness + 0.5F);
}

[[nodiscard]] constexpr unsigned char ambient_frame_global(
    LightEffect effect, unsigned int phase, float brightness) {
  if (effect == LightEffect::Breath ||
      effect == LightEffect::ShallowBreath) {
    return ambient_frame_sample(effect, phase, 0U, brightness);
  }
  return ambient_frame_sample(
      effect == LightEffect::Off ? LightEffect::Off
                                 : LightEffect::Solid,
      phase, 0U, brightness);
}

}  // namespace codex
