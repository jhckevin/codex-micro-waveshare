#pragma once

#include "codex/ambient_strip_lut.h"

namespace codex {

inline constexpr unsigned int kAmbientStripTileCount = 8U;
inline constexpr unsigned int kAmbientStripMaximumPixels = 336U * 24U;

struct AmbientStripRect {
  int x;
  int y;
  int width;
  int height;
};

[[nodiscard]] constexpr unsigned int ambient_strip_object_visibility_mask() {
  return (1U << kAmbientStripTileCount) - 1U;
}

[[nodiscard]] constexpr AmbientStripRect ambient_strip_rect(
    unsigned int tile) {
  constexpr AmbientStripRect rectangles[kAmbientStripTileCount] = {
      {72, 4, 336, 24},   {408, 4, 68, 68},   {452, 72, 24, 336},
      {408, 408, 68, 68}, {72, 452, 336, 24}, {4, 408, 68, 68},
      {4, 72, 24, 336},   {4, 4, 68, 68},
  };
  return rectangles[tile & 7U];
}

[[nodiscard]] constexpr unsigned int ambient_strip_active_tiles(
    unsigned int phase) {
  return kAmbientStripActiveTiles[phase & 127U];
}

[[nodiscard]] constexpr unsigned int ambient_strip_active_pixel_count(
    unsigned int phase) {
  return kAmbientStripActivePixels[phase & 127U];
}

[[nodiscard]] constexpr unsigned int ambient_strip_pixel_count(
    unsigned int tile) {
  return kAmbientStripTileOffsets[(tile & 7U) + 1U] -
         kAmbientStripTileOffsets[tile & 7U];
}

[[nodiscard]] constexpr unsigned int ambient_strip_phase_index(float phase) {
  if (phase <= 0.0F) return 0U;
  if (phase >= 1.0F) return kAmbientStripPhaseCount - 1U;
  return static_cast<unsigned int>(
             phase * static_cast<float>(kAmbientStripPhaseCount)) &
         (kAmbientStripPhaseCount - 1U);
}

[[nodiscard]] constexpr unsigned char ambient_strip_sample(
    unsigned int tile, unsigned int phase, unsigned int pixel) {
  const unsigned int normalized_tile = tile & 7U;
  const unsigned int count = ambient_strip_pixel_count(normalized_tile);
  if (pixel >= count) return 0U;
  const unsigned int offset =
      kAmbientStripTileOffsets[normalized_tile] + pixel;
  const unsigned int path = kAmbientStripPathMap[offset];
  if (path == 0xFFFFU) return 0U;
  const unsigned char longitudinal =
      kAmbientStripLongitudinal[phase & 127U][path];
  return kAmbientStripPremultiplied[longitudinal]
                                    [kAmbientStripRadialMap[offset]];
}

inline void ambient_strip_clear(unsigned char* destination,
                                unsigned int tile) {
  const unsigned int count = ambient_strip_pixel_count(tile);
  for (unsigned int pixel = 0; pixel < count; ++pixel) {
    destination[pixel] = 0U;
  }
}

inline void ambient_strip_fill(unsigned char* destination, unsigned int tile,
                               unsigned int phase) {
  const unsigned int normalized_tile = tile & 7U;
  const unsigned int start = kAmbientStripTileOffsets[normalized_tile];
  const unsigned int end = kAmbientStripTileOffsets[normalized_tile + 1U];
  const unsigned char* longitudinal =
      kAmbientStripLongitudinal[phase & 127U];
  unsigned int output = 0U;
  for (unsigned int offset = start; offset < end; ++offset) {
    const unsigned int path = kAmbientStripPathMap[offset];
    destination[output++] =
        path == 0xFFFFU
            ? 0U
            : kAmbientStripPremultiplied[longitudinal[path]]
                                                [kAmbientStripRadialMap[offset]];
  }
}

inline void ambient_strip_fill_static(unsigned char* destination,
                                      unsigned int tile) {
  const unsigned int normalized_tile = tile & 7U;
  const unsigned int start = kAmbientStripTileOffsets[normalized_tile];
  const unsigned int end = kAmbientStripTileOffsets[normalized_tile + 1U];
  unsigned int output = 0U;
  for (unsigned int offset = start; offset < end; ++offset) {
    destination[output++] = kAmbientStripRadialMap[offset];
  }
}

}  // namespace codex
