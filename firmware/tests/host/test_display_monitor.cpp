#include "display_monitor.h"

#include <assert.h>

int main() {
  {
    codex::DisplayTimingState timing{};
    codex::record_display_vsync(timing, 1000000);
    codex::record_display_vsync(timing, 1016667);
    assert(timing.vsync_count == 2);
    assert(timing.last_interval_us == 16667);
    assert(timing.minimum_interval_us == 16667);
    assert(timing.maximum_interval_us == 16667);
    assert(timing.timing_faults == 0);
    codex::record_display_vsync(timing, 1060000);
    assert(timing.timing_faults == 1);
    assert(timing.maximum_interval_us == 43333);
  }
  {
    unsigned short healthy[16]{};
    for (unsigned int index = 0; index < 16; ++index) {
      healthy[index] = 0xFFFF;
    }
    const codex::DisplayTileAnalysis analysis =
        codex::analyze_rgb565_tile(healthy, 4, 4, 4);
    assert(analysis.samples == 16);
    assert(analysis.near_black_samples == 0);
    assert(analysis.sample_fingerprint != 2166136261U);
    assert(!codex::display_tile_is_suspicious(analysis, true));
  }
  {
    unsigned short black[64]{};
    const codex::DisplayTileAnalysis analysis =
        codex::analyze_rgb565_tile(black, 8, 8, 8);
    assert(analysis.samples == 64);
    assert(analysis.near_black_samples == 64);
    assert(codex::display_tile_is_suspicious(analysis, true));
    assert(!codex::display_tile_is_suspicious(analysis, false));
  }
  {
    unsigned short patterned[16]{};
    for (unsigned int index = 0; index < 16; ++index) {
      patterned[index] = static_cast<unsigned short>(index * 997U);
    }
    const unsigned int first = codex::crc32_rgb565(patterned, 4, 4, 4);
    patterned[10] ^= 0xFFFF;
    const unsigned int second = codex::crc32_rgb565(patterned, 4, 4, 4);
    assert(first != second);
  }
  {
    codex::DisplayBufferObservation observation{};
    codex::observe_display_buffer(observation, 0, 0x1234, 100);
    assert(observation.frame_sequence == 1);
    assert(observation.tile_crc[0] == 0x1234);
    codex::observe_display_buffer(observation, 0, 0x4321, 101);
    assert(observation.frame_sequence == 2);
    assert(observation.changed_tiles == 1);
    assert(observation.last_change_ms == 101);
  }

  return 0;
}
