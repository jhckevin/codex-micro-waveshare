#include "display_monitor_runtime.h"

#include <atomic>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace {

constexpr char kTag[] = "display_hil";
constexpr unsigned int kDisplayWidth = 480;
constexpr unsigned int kDisplayHeight = 480;
constexpr unsigned int kTileWidth =
    kDisplayWidth / codex::kDisplayMonitorTileColumns;
constexpr unsigned int kTileHeight =
    kDisplayHeight / codex::kDisplayMonitorTileRows;
constexpr unsigned int kLearningFrames = 90;
// Keep diagnostics far below the RGB DMA/LVGL PSRAM bandwidth. A complete
// 160x160 CRC every 16 ms can itself starve the panel bounce-buffer refill and
// create the corner corruption it is intended to detect.
constexpr TickType_t kMonitorPeriodTicks = pdMS_TO_TICKS(32);

std::atomic<const unsigned short*> active_buffer{};
std::atomic_uint flush_count{};
std::atomic_uint pending_vsync_count{};
std::atomic_uint latest_vsync_tick{};
std::atomic_uint previous_vsync_tick{};
std::atomic_uint latest_vsync_interval_us{};
std::atomic_uint minimum_vsync_interval_us{};
std::atomic_uint maximum_vsync_interval_us{};
std::atomic_uint vsync_timing_faults{};
std::atomic_bool expected_visible{};
portMUX_TYPE monitor_lock = portMUX_INITIALIZER_UNLOCKED;
codex::DisplayTimingState timing{};
codex::DisplayBufferObservation observation{};
unsigned int suspicious_black_blocks{};
unsigned int unstable_samples{};
unsigned int last_fault_ms{};
unsigned char last_fault_tile{};
unsigned short learned_black_permille[codex::kDisplayMonitorTileCount]{};
unsigned short learning_samples[codex::kDisplayMonitorTileCount]{};

codex::DisplayTileAnalysis sample_tile(const unsigned short* buffer,
                                       unsigned int tile) {
  codex::DisplayTileAnalysis result{};
  const unsigned int tile_x =
      (tile % codex::kDisplayMonitorTileColumns) * kTileWidth;
  const unsigned int tile_y =
      (tile / codex::kDisplayMonitorTileColumns) * kTileHeight;
  // An 8x8 spatial grid catches large rectangular corruption while reading
  // only 64 pixels per tile. Full CRC is handled separately, one tile at a
  // time, outside display callbacks.
  for (unsigned int sample_y = 0; sample_y < 8; ++sample_y) {
    const unsigned int y = tile_y + (sample_y * kTileHeight + 4U) / 8U;
    for (unsigned int sample_x = 0; sample_x < 8; ++sample_x) {
      const unsigned int x = tile_x + (sample_x * kTileWidth + 4U) / 8U;
      const unsigned short pixel = buffer[y * kDisplayWidth + x];
      const unsigned int red = (pixel >> 11U) & 0x1FU;
      const unsigned int green = (pixel >> 5U) & 0x3FU;
      const unsigned int blue = pixel & 0x1FU;
      const unsigned int luminance = red * 2U + green * 3U + blue;
      result.luminance_sum += luminance;
      if (luminance <= 8U) ++result.near_black_samples;
      result.sample_fingerprint =
          (result.sample_fingerprint ^ static_cast<unsigned int>(pixel)) *
          16777619U;
      ++result.samples;
    }
  }
  return result;
}

void monitor_task(void*) {
  unsigned int tile = 0;
  unsigned int processed_vsync = 0;
  while (true) {
    const unsigned int available_vsync =
        pending_vsync_count.load(std::memory_order_acquire);
    if (available_vsync != processed_vsync) {
      const unsigned int tick =
          latest_vsync_tick.load(std::memory_order_acquire);
      portENTER_CRITICAL(&monitor_lock);
      // Timing is measured at the ISR hook. The monitor task can be delayed or
      // coalesce several callbacks, which must not be mistaken for lost VSYNC.
      timing.vsync_count = available_vsync;
      timing.last_tick_us =
          tick * static_cast<unsigned int>(portTICK_PERIOD_MS) * 1000U;
      timing.last_interval_us =
          latest_vsync_interval_us.load(std::memory_order_acquire);
      timing.minimum_interval_us =
          minimum_vsync_interval_us.load(std::memory_order_acquire);
      timing.maximum_interval_us =
          maximum_vsync_interval_us.load(std::memory_order_acquire);
      timing.timing_faults =
          vsync_timing_faults.load(std::memory_order_acquire);
      portEXIT_CRITICAL(&monitor_lock);
      processed_vsync = available_vsync;
    }

    const unsigned short* before =
        active_buffer.load(std::memory_order_acquire);
    if (before != nullptr) {
      const codex::DisplayTileAnalysis analysis = sample_tile(before, tile);
      const unsigned int black_permille =
          analysis.samples == 0U
              ? 0U
              : analysis.near_black_samples * 1000U / analysis.samples;
      const unsigned int now_ms =
          xTaskGetTickCount() * static_cast<unsigned int>(portTICK_PERIOD_MS);

      if (learning_samples[tile] < kLearningFrames) {
        learned_black_permille[tile] = static_cast<unsigned short>(
            (static_cast<unsigned int>(learned_black_permille[tile]) *
                 learning_samples[tile] +
             black_permille) /
            (learning_samples[tile] + 1U));
        ++learning_samples[tile];
      } else if (expected_visible.load(std::memory_order_acquire) &&
                 black_permille >= 880U &&
                 black_permille >=
                     static_cast<unsigned int>(learned_black_permille[tile]) +
                         280U) {
        portENTER_CRITICAL(&monitor_lock);
        ++suspicious_black_blocks;
        last_fault_ms = now_ms;
        last_fault_tile = static_cast<unsigned char>(tile);
        portEXIT_CRITICAL(&monitor_lock);
        ESP_LOGE(kTag,
                 "black-block anomaly tile=%u current=%u baseline=%u fb=%p",
                 tile, black_permille, learned_black_permille[tile], before);
      }

      const unsigned short* after =
          active_buffer.load(std::memory_order_acquire);
      if (after == before) {
        portENTER_CRITICAL(&monitor_lock);
        codex::observe_display_buffer(observation, tile,
                                      analysis.sample_fingerprint, now_ms);
        portEXIT_CRITICAL(&monitor_lock);
      } else {
        portENTER_CRITICAL(&monitor_lock);
        ++unstable_samples;
        portEXIT_CRITICAL(&monitor_lock);
      }
      tile = (tile + 1U) % codex::kDisplayMonitorTileCount;
    }
    vTaskDelay(kMonitorPeriodTicks);
  }
}

}  // namespace

namespace codex {

void display_monitor_start() {
  xTaskCreatePinnedToCore(monitor_task, "display_hil", 4096, nullptr, 1,
                          nullptr, 0);
}

void display_monitor_set_expected_visible(bool visible) {
  expected_visible.store(visible, std::memory_order_release);
}

void display_monitor_suspend_framebuffer() {
  expected_visible.store(false, std::memory_order_release);
  active_buffer.store(nullptr, std::memory_order_release);
}

DisplayMonitorSnapshot display_monitor_snapshot() {
  DisplayMonitorSnapshot snapshot{};
  portENTER_CRITICAL(&monitor_lock);
  snapshot.timing = timing;
  snapshot.buffers = observation;
  snapshot.suspicious_black_blocks = suspicious_black_blocks;
  snapshot.unstable_samples = unstable_samples;
  snapshot.last_fault_ms = last_fault_ms;
  snapshot.last_fault_tile = last_fault_tile;
  portEXIT_CRITICAL(&monitor_lock);
  snapshot.flush_count = flush_count.load(std::memory_order_acquire);
  snapshot.active_framebuffer = static_cast<unsigned int>(
      reinterpret_cast<uintptr_t>(
          active_buffer.load(std::memory_order_acquire)));
  snapshot.expected_visible =
      expected_visible.load(std::memory_order_acquire);
  return snapshot;
}

bool display_monitor_read_pixel(unsigned short x, unsigned short y,
                                unsigned short& pixel) {
  if (x >= kDisplayWidth || y >= kDisplayHeight) return false;
  const unsigned short* before =
      active_buffer.load(std::memory_order_acquire);
  if (before == nullptr) return false;
  pixel = before[static_cast<unsigned int>(y) * kDisplayWidth + x];
  return active_buffer.load(std::memory_order_acquire) == before;
}

}  // namespace codex

extern "C" void codex_display_monitor_flush_hook(
    const unsigned char* color_map, int, int, int, int, bool last_flush) {
  if (!last_flush || color_map == nullptr) return;
  active_buffer.store(reinterpret_cast<const unsigned short*>(color_map),
                      std::memory_order_release);
  flush_count.fetch_add(1U, std::memory_order_relaxed);
}

extern "C" void codex_display_monitor_vsync_hook(unsigned int tick_count) {
  const unsigned int previous =
      previous_vsync_tick.exchange(tick_count, std::memory_order_relaxed);
  if (previous != 0U) {
    const unsigned int interval_us =
        (tick_count - previous) *
        static_cast<unsigned int>(portTICK_PERIOD_MS) * 1000U;
    latest_vsync_interval_us.store(interval_us, std::memory_order_relaxed);

    unsigned int minimum =
        minimum_vsync_interval_us.load(std::memory_order_relaxed);
    while ((minimum == 0U || interval_us < minimum) &&
           !minimum_vsync_interval_us.compare_exchange_weak(
               minimum, interval_us, std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }

    unsigned int maximum =
        maximum_vsync_interval_us.load(std::memory_order_relaxed);
    while (interval_us > maximum &&
           !maximum_vsync_interval_us.compare_exchange_weak(
               maximum, interval_us, std::memory_order_relaxed,
               std::memory_order_relaxed)) {
    }

    if (interval_us < 10000U || interval_us > 25000U) {
      vsync_timing_faults.fetch_add(1U, std::memory_order_relaxed);
    }
  }
  latest_vsync_tick.store(tick_count, std::memory_order_relaxed);
  pending_vsync_count.fetch_add(1U, std::memory_order_release);
}
