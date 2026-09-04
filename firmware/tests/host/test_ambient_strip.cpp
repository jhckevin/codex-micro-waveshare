#include "codex/ambient_strip.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures = 0;
void require(bool condition) { failures += condition ? 0UL : 1UL; }

unsigned int bit_count(unsigned int value) {
  unsigned int count = 0;
  while (value != 0U) {
    count += value & 1U;
    value >>= 1U;
  }
  return count;
}

unsigned int pixel_at(const codex::AmbientStripRect& rect, int x, int y) {
  return static_cast<unsigned int>((y - rect.y) * rect.width + (x - rect.x));
}
}

extern "C" void mainCRTStartup() {
  using namespace codex;

  constexpr AmbientStripRect expected[kAmbientStripTileCount] = {
      {72, 4, 336, 24},  {408, 4, 68, 68},   {452, 72, 24, 336},
      {408, 408, 68, 68}, {72, 452, 336, 24}, {4, 408, 68, 68},
      {4, 72, 24, 336},  {4, 4, 68, 68},
  };
  require(kAmbientStripTileCount == 8U);
  require(kAmbientStripPhaseCount == 128U);
  require(kAmbientStripTailExtent >= 400U);
  require(kAmbientStripMaximumPixels == 8064U);
  require(ambient_strip_object_visibility_mask() == 0xFFU);

  for (unsigned int tile = 0; tile < kAmbientStripTileCount; ++tile) {
    const AmbientStripRect rect = ambient_strip_rect(tile);
    require(rect.x == expected[tile].x);
    require(rect.y == expected[tile].y);
    require(rect.width == expected[tile].width);
    require(rect.height == expected[tile].height);
    require(rect.x >= 4);
    require(rect.y >= 4);
    require(rect.x + rect.width - 1 <= 475);
    require(rect.y + rect.height - 1 <= 475);
  }

  static unsigned char buffer[kAmbientStripMaximumPixels + 2U]{};
  const AmbientStripRect top_left = ambient_strip_rect(7U);
  buffer[0] = 0xA5U;
  buffer[kAmbientStripMaximumPixels + 1U] = 0x5AU;
  ambient_strip_fill_static(buffer + 1U, 7U);
  require(buffer[0] == 0xA5U);
  require(buffer[kAmbientStripMaximumPixels + 1U] == 0x5AU);
  require(buffer[1U + pixel_at(top_left, 60, 16)] == 255U);
  require(buffer[1U + pixel_at(top_left, 29, 29)] >= 250U);
  require(buffer[1U + pixel_at(top_left, 16, 60)] == 255U);
  require(buffer[1U + pixel_at(top_left, 4, 4)] == 0U);
  require(buffer[1U + pixel_at(top_left, 60, 60)] == 0U);
  require(buffer[1U + pixel_at(top_left, 4, 60)] == 0U);

  for (unsigned int phase = 0; phase < kAmbientStripPhaseCount; ++phase) {
    const unsigned int active = ambient_strip_active_tiles(phase);
    require(active != 0U);
    require((active & ~0xFFU) == 0U);
    require(bit_count(active) <= 4U);
    require(ambient_strip_active_pixel_count(phase) <= 28816U);

    const unsigned int next = (phase + 1U) & 127U;
    bool differs = false;
    for (unsigned int tile = 0; tile < kAmbientStripTileCount && !differs;
         ++tile) {
      const unsigned int count = ambient_strip_pixel_count(tile);
      for (unsigned int pixel = 0; pixel < count; pixel += 37U) {
        if (ambient_strip_sample(tile, phase, pixel) !=
            ambient_strip_sample(tile, next, pixel)) {
          differs = true;
          break;
        }
      }
    }
    require(differs);
  }

  ambient_strip_fill(buffer + 1U, 0U, 0U);
  require(buffer[0] == 0xA5U);
  require(buffer[kAmbientStripMaximumPixels + 1U] == 0x5AU);

  ExitProcess(failures);
}
