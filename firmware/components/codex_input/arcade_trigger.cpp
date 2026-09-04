#include "codex/arcade_trigger.h"

namespace codex {
namespace {

constexpr unsigned int kSampleIntervalMs = 30;
constexpr unsigned int kSampleCount = 100;
constexpr float kStartAngle = 0.47F;
constexpr float kTurns = 2.05F;
constexpr signed char kAngularJitter[] = {0, 1, -1, 2, 0, -2, 1, 0};
constexpr signed char kRadialJitter[] = {0, 1, -1, 2, -2, 1, 0, -1, 2, 0};

float normalized(float value) {
  while (value >= 1.0F) value -= 1.0F;
  while (value < 0.0F) value += 1.0F;
  return value;
}

}  // namespace

ArcadeTrigger start_arcade_trigger(unsigned int now_ms) {
  return {.active = true,
          .sample_index = 0,
          .started_ms = now_ms,
          .next_sample_ms = now_ms};
}

ArcadeTriggerOutput advance_arcade_trigger(ArcadeTrigger& trigger,
                                           unsigned int now_ms) {
  if (!trigger.active ||
      static_cast<int>(now_ms - trigger.next_sample_ms) < 0) {
    return {};
  }
  const unsigned int index = trigger.sample_index;
  const float progress =
      static_cast<float>(index) / static_cast<float>(kSampleCount - 1U);
  const float angle_jitter =
      static_cast<float>(
          kAngularJitter[index % (sizeof(kAngularJitter) /
                                  sizeof(kAngularJitter[0]))]) *
      0.0007F;
  const float radius_jitter =
      static_cast<float>(
          kRadialJitter[index % (sizeof(kRadialJitter) /
                                 sizeof(kRadialJitter[0]))]) *
      0.008F;
  ArcadeTriggerOutput output{
      .ready = true,
      .complete = index + 1U == kSampleCount,
      .angle = normalized(kStartAngle + kTurns * progress + angle_jitter),
      .distance = 0.56F + radius_jitter,
  };
  ++trigger.sample_index;
  trigger.next_sample_ms += kSampleIntervalMs;
  if (output.complete) trigger.active = false;
  return output;
}

}  // namespace codex
