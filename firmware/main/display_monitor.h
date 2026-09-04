#pragma once

namespace codex {

constexpr unsigned int kDisplayMonitorTileColumns = 6;
constexpr unsigned int kDisplayMonitorTileRows = 6;
constexpr unsigned int kDisplayMonitorTileCount =
    kDisplayMonitorTileColumns * kDisplayMonitorTileRows;

struct DisplayTimingState {
  unsigned int vsync_count{};
  unsigned int last_tick_us{};
  unsigned int last_interval_us{};
  unsigned int minimum_interval_us{};
  unsigned int maximum_interval_us{};
  unsigned int timing_faults{};
};

struct DisplayTileAnalysis {
  unsigned int samples{};
  unsigned int near_black_samples{};
  unsigned int luminance_sum{};
  unsigned int sample_fingerprint{2166136261U};
};

struct DisplayBufferObservation {
  unsigned int frame_sequence{};
  unsigned int tile_crc[kDisplayMonitorTileCount]{};
  unsigned int changed_tiles{};
  unsigned int last_change_ms{};
};

void record_display_vsync(DisplayTimingState& state, unsigned int tick_us);
[[nodiscard]] DisplayTileAnalysis analyze_rgb565_tile(
    const unsigned short* pixels, unsigned int stride_pixels,
    unsigned int width, unsigned int height);
[[nodiscard]] bool display_tile_is_suspicious(
    const DisplayTileAnalysis& analysis, bool expected_visible);
[[nodiscard]] unsigned int crc32_rgb565(const unsigned short* pixels,
                                        unsigned int stride_pixels,
                                        unsigned int width,
                                        unsigned int height);
void observe_display_buffer(DisplayBufferObservation& observation,
                            unsigned int tile_index, unsigned int crc,
                            unsigned int monotonic_ms);

}  // namespace codex
