#include "codex/ambient_frame_lut.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures = 0;
void require(bool condition) { failures += condition ? 0UL : 1UL; }
int absolute(int value) { return value < 0 ? -value : value; }
}

extern "C" void mainCRTStartup() {
  using namespace codex;

  require(kAmbientFramePhaseCount == 64U);
  require(kAmbientFrameSegmentCount == 16U);

  require(ambient_frame_sample(LightEffect::Off, 0, 0, 1.0F) == 0U);
  require(ambient_frame_sample(LightEffect::Snake, 0, 0, 1.0F) == 255U);
  require(ambient_frame_sample(LightEffect::Snake, 0, 15, 1.0F) == 142U);
  require(ambient_frame_sample(LightEffect::Snake, 0, 14, 1.0F) == 54U);
  require(ambient_frame_sample(LightEffect::Snake, 0, 13, 1.0F) == 14U);
  require(ambient_frame_sample(LightEffect::Snake, 0, 12, 1.0F) == 0U);
  require(ambient_frame_sample(LightEffect::Snake, 0, 8, 1.0F) == 0U);
  require(ambient_frame_sample(LightEffect::Snake, 4, 1, 1.0F) == 255U);
  require(ambient_frame_sample(LightEffect::Solid, 9, 11, 1.0F) == 255U);
  require(ambient_frame_sample(LightEffect::Solid, 9, 11, 0.5F) == 128U);
  require(ambient_frame_sample(LightEffect::Solid, 9, 11, -1.0F) == 0U);
  require(ambient_frame_sample(LightEffect::Solid, 9, 11, 2.0F) == 255U);
  require(ambient_frame_global(LightEffect::Snake, 0, 1.0F) == 255U);
  require(ambient_frame_global(LightEffect::Snake, 31, 0.5F) == 128U);
  require(ambient_frame_global(LightEffect::Breath, 17, 1.0F) ==
          ambient_frame_sample(LightEffect::Breath, 17, 0, 1.0F));

  unsigned int minimum = 255U;
  unsigned int maximum = 0U;
  for (unsigned int phase = 0; phase < kAmbientFramePhaseCount; ++phase) {
    for (unsigned int segment = 0; segment < kAmbientFrameSegmentCount;
         ++segment) {
      const unsigned int sample =
          ambient_frame_sample(LightEffect::Snake, phase, segment, 1.0F);
      if (sample < minimum) minimum = sample;
      if (sample > maximum) maximum = sample;
      const unsigned int next = ambient_frame_sample(
          LightEffect::Snake, phase, segment + 1U, 1.0F);
      if (sample != 0U && next != 0U) {
        require(absolute(static_cast<int>(sample) -
                         static_cast<int>(next)) < 192);
      }
    }
  }
  require(minimum == 0U);
  require(maximum == 255U);

  for (unsigned int segment = 0; segment < kAmbientFrameSegmentCount;
       ++segment) {
    const int before = ambient_frame_sample(
        LightEffect::Snake, kAmbientFramePhaseCount - 1U, segment, 1.0F);
    const int after =
        ambient_frame_sample(LightEffect::Snake, 0U, segment, 1.0F);
    require(absolute(before - after) <= 64);
    require(ambient_frame_sample(LightEffect::Breath, 17, segment, 1.0F) ==
            ambient_frame_sample(LightEffect::Breath, 17, 0, 1.0F));
    require(
        ambient_frame_sample(LightEffect::ShallowBreath, 43, segment, 1.0F) ==
        ambient_frame_sample(LightEffect::ShallowBreath, 43, 0, 1.0F));
  }

  ExitProcess(failures);
}
