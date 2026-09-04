#pragma once

#include "codex/device_state.h"

namespace codex {

constexpr unsigned int kClassicScreenBackdropColor = 0x07100DU;

inline constexpr unsigned int kAmbientSegmentCount = 16;
inline constexpr int kAmbientBarLength = 62;
inline constexpr int kAmbientBarThickness = 9;
inline constexpr int kDisplayExtent = 480;
inline constexpr int kGlowPadding = 12;
inline constexpr int kDisplayDmaSafeInset = 2;
inline constexpr int kAmbientRingOrigin = 7;
inline constexpr int kAmbientSegmentPadding = 18;

[[nodiscard]] constexpr unsigned int compose_rgb(unsigned int foreground,
                                                 unsigned int background,
                                                 unsigned char opacity) {
  const auto compose_channel = [opacity](unsigned int foreground_channel,
                                         unsigned int background_channel) {
    return (foreground_channel * opacity +
            background_channel * (255U - opacity) + 127U) /
           255U;
  };
  return (compose_channel((foreground >> 16U) & 0xFFU,
                          (background >> 16U) & 0xFFU)
          << 16U) |
         (compose_channel((foreground >> 8U) & 0xFFU,
                          (background >> 8U) & 0xFFU)
          << 8U) |
         compose_channel(foreground & 0xFFU, background & 0xFFU);
}

[[nodiscard]] constexpr unsigned int rgb_luma(unsigned int color) {
  return (54U * ((color >> 16U) & 0xFFU) +
          183U * ((color >> 8U) & 0xFFU) +
          19U * (color & 0xFFU) + 128U) /
         256U;
}

[[nodiscard]] constexpr unsigned int ambient_layer_color(
    unsigned int background, unsigned int glow, unsigned char strength,
    unsigned int maximum_lift_percent) {
  if (strength == 0U || maximum_lift_percent == 0U) return background;
  const unsigned int background_luma = rgb_luma(background);
  const unsigned int glow_luma = rgb_luma(glow);
  if (glow_luma <= background_luma) return background;
  const unsigned int denominator = 100U * 255U;
  const unsigned int lift =
      (background_luma * maximum_lift_percent * strength +
       denominator / 2U) /
      denominator;
  if (lift == 0U) return background;
  const unsigned int available = glow_luma - background_luma;
  unsigned int opacity =
      (lift * 255U + available / 2U) / available;
  if (opacity > 255U) opacity = 255U;
  return compose_rgb(glow, background,
                     static_cast<unsigned char>(opacity));
}

[[nodiscard]] constexpr bool ambient_phase_wrapped(unsigned int previous,
                                                   unsigned int current) {
  return current < previous;
}

[[nodiscard]] constexpr unsigned int mix_white(unsigned int color,
                                               unsigned int percent) {
  const auto mix_channel = [percent](unsigned int channel) {
    return (channel * (100U - percent) + 255U * percent + 50U) / 100U;
  };
  return (mix_channel((color >> 16U) & 0xFFU) << 16U) |
         (mix_channel((color >> 8U) & 0xFFU) << 8U) |
         mix_channel(color & 0xFFU);
}

[[nodiscard]] constexpr float local_light_strength(float strength) {
  if (strength <= 0.0F) return 0.0F;
  const float boosted = strength * 1.20F;
  return boosted < 0.95F ? boosted : 0.95F;
}

[[nodiscard]] constexpr unsigned int mix_ambient_white(unsigned int color) {
  return mix_white(color, 10U);
}

[[nodiscard]] constexpr unsigned int mix_ambient_motion_white(
    unsigned int color) {
  return mix_white(color, 35U);
}

[[nodiscard]] constexpr unsigned int ambient_segment_color(
    LightEffect effect, unsigned int color) {
  (void)effect;
  return color;
}

[[nodiscard]] constexpr unsigned int effect_render_color(
    LightEffect effect, unsigned int color, unsigned int phase_index) {
  if (effect == LightEffect::Rainbow) {
    constexpr unsigned int kRainbow[] = {
        0xFF375FU, 0xFF9F0AU, 0xFFD60AU, 0x32D74BU,
        0x64D2FFU, 0x0A84FFU, 0x5E5CE6U, 0xBF5AF2U};
    const unsigned int step = (phase_index >> 3U) & 7U;
    const unsigned int fraction = (phase_index & 7U) * 32U;
    return compose_rgb(kRainbow[(step + 1U) & 7U], kRainbow[step],
                       static_cast<unsigned char>(fraction));
  }
  if (effect == LightEffect::Gradient) {
    const unsigned int phase = phase_index & 63U;
    const unsigned int triangle = phase <= 32U ? phase : 64U - phase;
    return mix_white(color, triangle * 30U / 32U);
  }
  return color;
}

[[nodiscard]] constexpr unsigned char ambient_background_opacity(
    float brightness) {
  if (brightness <= 0.0F) return 35U;
  if (brightness > 1.0F) brightness = 1.0F;
  return static_cast<unsigned char>(35.0F + brightness * 115.0F);
}

[[nodiscard]] constexpr unsigned char ambient_effect_base_opacity(
    LightEffect effect, float brightness) {
  if (effect != LightEffect::Snake) {
    return ambient_background_opacity(brightness);
  }
  if (brightness <= 0.0F) return 28U;
  if (brightness > 1.0F) brightness = 1.0F;
  return static_cast<unsigned char>(28.0F + brightness * 122.0F);
}

[[nodiscard]] constexpr unsigned int ambient_diffuser_color(
    unsigned int color) {
  return mix_white(color, 45U);
}

[[nodiscard]] constexpr unsigned int ambient_emitter_color(
    unsigned int color) {
  return mix_white(color, 35U);
}

[[nodiscard]] constexpr unsigned int ambient_strip_motion_color(
    unsigned int color) {
  return ambient_emitter_color(color);
}

[[nodiscard]] constexpr unsigned int ambient_motion_mix_percent(
    LightEffect effect, unsigned char sample) {
  if (effect != LightEffect::Snake) return 0U;
  if (sample >= 224U) return 18U;
  if (sample >= 96U) return 9U;
  return 0U;
}

[[nodiscard]] constexpr float moving_light_strength(float strength) {
  if (strength <= 0.0F) return 0.0F;
  const float boosted = strength * 1.35F;
  return boosted < 1.0F ? boosted : 1.0F;
}

[[nodiscard]] constexpr float agent_light_level(float brightness,
                                                float envelope) {
  float value = brightness * envelope;
  if (value <= 0.12F) return 0.0F;
  if (value >= 1.0F) return 1.0F;
  return (value - 0.12F) / 0.88F;
}

[[nodiscard]] constexpr unsigned char agent_light_opacity(
    float brightness, float envelope) {
  return static_cast<unsigned char>(
      55.0F + agent_light_level(brightness, envelope) * 190.0F);
}

[[nodiscard]] constexpr unsigned char agent_breath_level(
    float brightness, float envelope) {
  return static_cast<unsigned char>(
      agent_light_level(brightness, envelope) * 15.0F);
}

// Six simultaneously lit keys must not invalidate six large A8 glow sprites
// on every animation sample. Keep the expensive outer diffuser static and
// animate only the small inset using eight LUT-backed levels.
[[nodiscard]] constexpr unsigned char agent_breath_frame(
    unsigned char breath_level) {
  return static_cast<unsigned char>((breath_level > 15U ? 15U : breath_level) >>
                                    1U);
}

[[nodiscard]] constexpr unsigned char agent_static_glow_opacity(
    float brightness) {
  if (brightness <= 0.0F) return 0U;
  if (brightness >= 1.0F) return 180U;
  return static_cast<unsigned char>(80.0F + brightness * 100.0F);
}

[[nodiscard]] constexpr unsigned char agent_optimized_inset_opacity(
    unsigned char breath_frame) {
  const unsigned int frame = breath_frame > 7U ? 7U : breath_frame;
  return static_cast<unsigned char>(125U + frame * 14U);
}

[[nodiscard]] constexpr unsigned int agent_cap_top_mix(
    unsigned char breath_level) {
  return 60U + static_cast<unsigned int>(breath_level) * 4U;
}

[[nodiscard]] constexpr unsigned int agent_cap_bottom_mix(
    unsigned char breath_level) {
  return 85U + static_cast<unsigned int>(breath_level) * 6U;
}

[[nodiscard]] constexpr unsigned char agent_inset_opacity(
    unsigned char breath_level) {
  return static_cast<unsigned char>(
      90U + static_cast<unsigned int>(breath_level) * 5U);
}

[[nodiscard]] constexpr int ambient_sprite_position(int core_position,
                                                    int sprite_extent) {
  int position = core_position - kGlowPadding;
  if (position < 0) return 0;
  // RGB direct-mode partial refreshes that end on the physical last column or
  // row can expose an incompletely synchronized DMA edge. Keep the transparent
  // A8 padding two pixels inside the right/bottom framebuffer boundary.
  const int maximum =
      kDisplayExtent - sprite_extent - kDisplayDmaSafeInset;
  return position > maximum ? maximum : position;
}

[[nodiscard]] constexpr int ambient_mover_position(int center,
                                                   int sprite_extent) {
  int position = center - sprite_extent / 2;
  if (position < 0) return 0;
  const int maximum =
      kDisplayExtent - sprite_extent - kDisplayDmaSafeInset;
  return position > maximum ? maximum : position;
}

// Movers are children of the clipped 466x466 ring. Negative and overflowing
// child coordinates are intentional: LVGL clips them to the ring before the
// RGB framebuffer edge, so old/new invalidation rectangles never expose the
// physical right or bottom scanout boundary.
[[nodiscard]] constexpr int ambient_mover_ring_position(int center,
                                                        int sprite_extent) {
  return center - kAmbientRingOrigin - sprite_extent / 2;
}

[[nodiscard]] constexpr int ambient_segment_ring_position(
    int core_position) {
  return core_position - kAmbientRingOrigin - kAmbientSegmentPadding;
}

// 0=up, 1=right, 2=down, 3=left, 4=neutral.
[[nodiscard]] constexpr unsigned char arcade_dpad_direction(
    float angle, float distance) {
  if (distance < 0.20F) return 4U;
  while (angle < 0.0F) angle += 1.0F;
  while (angle >= 1.0F) angle -= 1.0F;
  if (angle >= 0.875F || angle < 0.125F) return 1U;
  if (angle < 0.375F) return 2U;
  if (angle < 0.625F) return 3U;
  return 0U;
}

}  // namespace codex
