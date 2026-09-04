#pragma once

#include "codex/device_state.h"

namespace codex {

enum class AmbientOverlay : unsigned char {
  None,
  Segments,
};

struct AmbientRenderPolicy {
  bool colored_base;
  bool animated_base;
  AmbientOverlay overlay;
};

[[nodiscard]] constexpr AmbientRenderPolicy ambient_render_policy(
    LightEffect effect) {
  switch (effect) {
    case LightEffect::Solid:
      return {true, false, AmbientOverlay::Segments};
    case LightEffect::Snake:
      return {true, false, AmbientOverlay::Segments};
    case LightEffect::Rainbow:
      return {true, false, AmbientOverlay::Segments};
    case LightEffect::Breath:
      return {true, true, AmbientOverlay::Segments};
    case LightEffect::Gradient:
      return {true, false, AmbientOverlay::Segments};
    case LightEffect::ShallowBreath:
      return {true, true, AmbientOverlay::Segments};
    case LightEffect::Off:
    default:
      return {false, false, AmbientOverlay::None};
  }
}

[[nodiscard]] constexpr unsigned char ambient_fixed_opacity(
    unsigned char sample, unsigned char global_level) {
  if (global_level == 0U) return 0U;
  const unsigned int floor =
      14U + (26U * static_cast<unsigned int>(global_level) + 127U) / 255U;
  return sample > floor ? sample : static_cast<unsigned char>(floor);
}

}  // namespace codex
