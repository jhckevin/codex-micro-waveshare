#include "display_monitor.h"

namespace codex {

void record_display_vsync(DisplayTimingState& state, unsigned int tick_us) {
  ++state.vsync_count;
  if (state.last_tick_us != 0U) {
    const unsigned int interval = tick_us - state.last_tick_us;
    state.last_interval_us = interval;
    if (state.minimum_interval_us == 0U ||
        interval < state.minimum_interval_us) {
      state.minimum_interval_us = interval;
    }
    if (interval > state.maximum_interval_us) {
      state.maximum_interval_us = interval;
    }
    // The board-qualified timing is approximately 60 Hz. Count only
    // intervals far outside a normal scheduler/jitter envelope.
    if (interval < 10000U || interval > 25000U) {
      ++state.timing_faults;
    }
  }
  state.last_tick_us = tick_us;
}

DisplayTileAnalysis analyze_rgb565_tile(const unsigned short* pixels,
                                        unsigned int stride_pixels,
                                        unsigned int width,
                                        unsigned int height) {
  DisplayTileAnalysis result{};
  if (pixels == nullptr || stride_pixels < width) return result;
  for (unsigned int y = 0; y < height; ++y) {
    const unsigned short* row = pixels + y * stride_pixels;
    for (unsigned int x = 0; x < width; ++x) {
      const unsigned short value = row[x];
      const unsigned int red = (value >> 11U) & 0x1FU;
      const unsigned int green = (value >> 5U) & 0x3FU;
      const unsigned int blue = value & 0x1FU;
      // Integer approximation sufficient for anomaly classification.
      const unsigned int luminance = red * 2U + green * 3U + blue;
      result.luminance_sum += luminance;
      if (luminance <= 8U) ++result.near_black_samples;
      result.sample_fingerprint =
          (result.sample_fingerprint ^ static_cast<unsigned int>(value)) *
          16777619U;
      ++result.samples;
    }
  }
  return result;
}

bool display_tile_is_suspicious(const DisplayTileAnalysis& analysis,
                                bool expected_visible) {
  return expected_visible && analysis.samples >= 16U &&
         analysis.near_black_samples * 100U >= analysis.samples * 88U;
}

unsigned int crc32_rgb565(const unsigned short* pixels,
                          unsigned int stride_pixels, unsigned int width,
                          unsigned int height) {
  if (pixels == nullptr || stride_pixels < width) return 0U;
  unsigned int crc = 0xFFFFFFFFU;
  for (unsigned int y = 0; y < height; ++y) {
    const unsigned short* row = pixels + y * stride_pixels;
    for (unsigned int x = 0; x < width; ++x) {
      const unsigned short value = row[x];
      const unsigned char bytes[] = {
          static_cast<unsigned char>(value & 0xFFU),
          static_cast<unsigned char>(value >> 8U),
      };
      for (unsigned char byte : bytes) {
        crc ^= byte;
        for (unsigned int bit = 0; bit < 8; ++bit) {
          crc = (crc >> 1U) ^
                (0xEDB88320U & static_cast<unsigned int>(
                                   -static_cast<signed int>(crc & 1U)));
        }
      }
    }
  }
  return ~crc;
}

void observe_display_buffer(DisplayBufferObservation& observation,
                            unsigned int tile_index, unsigned int crc,
                            unsigned int monotonic_ms) {
  if (tile_index >= kDisplayMonitorTileCount) return;
  ++observation.frame_sequence;
  if (observation.tile_crc[tile_index] != 0U &&
      observation.tile_crc[tile_index] != crc) {
    ++observation.changed_tiles;
    observation.last_change_ms = monotonic_ms;
  }
  observation.tile_crc[tile_index] = crc;
}

}  // namespace codex
