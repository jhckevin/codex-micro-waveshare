#include "codex/arcade_trigger.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures = 0;
void require(bool condition) { failures += condition ? 0UL : 1UL; }
}

extern "C" void mainCRTStartup() {
  using namespace codex;
  ArcadeTrigger trigger = start_arcade_trigger(1000);
  unsigned int emitted = 0;
  float previous = 0.0F;
  float unwrapped = 0.0F;
  float first_unwrapped = 0.0F;
  float final_distance = 0.0F;
  unsigned int completed_at = 0;

  for (unsigned int now = 1000; now < 7000; now += 10) {
    const ArcadeTriggerOutput output = advance_arcade_trigger(trigger, now);
    if (!output.ready) continue;
    require(output.distance > 0.50F && output.distance < 0.62F);
    if (emitted == 0) {
      previous = output.angle;
      unwrapped = output.angle;
      first_unwrapped = unwrapped;
    } else {
      float delta = output.angle - previous;
      if (delta < -0.5F) delta += 1.0F;
      if (delta > 0.5F) delta -= 1.0F;
      require(delta > 0.0F);
      require(delta < 0.04F);
      unwrapped += delta;
      previous = output.angle;
    }
    final_distance = output.distance;
    ++emitted;
    if (output.complete) {
      completed_at = now;
      break;
    }
  }

  require(emitted >= 96 && emitted <= 104);
  require(unwrapped - first_unwrapped > 2.0F);
  require(unwrapped - first_unwrapped < 2.1F);
  require(completed_at > 1000 && completed_at < 7000);
  require(final_distance > 0.50F);
  require(!trigger.active);
  require(!advance_arcade_trigger(trigger, completed_at + 30).ready);
  ExitProcess(failures);
}
