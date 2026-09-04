#pragma once

#include "display_monitor.h"

namespace codex {

struct DisplayMonitorSnapshot {
  DisplayTimingState timing{};
  DisplayBufferObservation buffers{};
  unsigned int flush_count{};
  unsigned int suspicious_black_blocks{};
  unsigned int unstable_samples{};
  unsigned int last_fault_ms{};
  unsigned char last_fault_tile{};
  unsigned int active_framebuffer{};
  bool expected_visible{};
};

void display_monitor_start();
void display_monitor_set_expected_visible(bool visible);
void display_monitor_suspend_framebuffer();
[[nodiscard]] DisplayMonitorSnapshot display_monitor_snapshot();
[[nodiscard]] bool display_monitor_read_pixel(unsigned short x,
                                              unsigned short y,
                                              unsigned short& pixel);

}  // namespace codex

extern "C" void codex_display_monitor_flush_hook(
    const unsigned char* color_map, int x1, int y1, int x2, int y2,
    bool last_flush);
extern "C" void codex_display_monitor_vsync_hook(unsigned int tick_count);
