#pragma once

namespace codex {

struct ArcadeTrigger {
  bool active{false};
  unsigned char sample_index{0};
  unsigned int started_ms{0};
  unsigned int next_sample_ms{0};
};

struct ArcadeTriggerOutput {
  bool ready{false};
  bool complete{false};
  float angle{0.0F};
  float distance{0.0F};
};

[[nodiscard]] ArcadeTrigger start_arcade_trigger(unsigned int now_ms);
[[nodiscard]] ArcadeTriggerOutput advance_arcade_trigger(
    ArcadeTrigger& trigger, unsigned int now_ms);

}  // namespace codex
