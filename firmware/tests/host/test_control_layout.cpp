#include "codex/control_layout.h"
#include "codex/settings_layout.h"
#include "codex/layer_led_pattern.h"
#include "codex/visual_tuning.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures = 0;
void require(bool condition) { failures += condition ? 0UL : 1UL; }
bool overlaps(const codex::Rect& a, const codex::Rect& b) {
  return a.x < b.x + b.width && a.x + a.width > b.x &&
         a.y < b.y + b.height && a.y + a.height > b.y;
}
}

extern "C" void mainCRTStartup() {
  using namespace codex;
  require(kSettingsPageCount == 2);
  require(auto_shutdown_option(1) == 7200);
  require(auto_shutdown_option(3) == 18000);
  require(layer_led_mask(0) == 0b000);
  require(layer_led_mask(1) == 0b001);
  require(layer_led_mask(2) == 0b010);
  require(layer_led_mask(3) == 0b100);
  require(layer_led_mask(4) == 0b011);
  require(layer_led_mask(5) == 0b110);
  require(layer_led_mask(6) == 0b111);
  require(layer_led_mask(7) == 0b000);
  require(layer_led_y_offset(0) == 29);
  require(layer_led_y_offset(1) == 39);
  require(layer_led_y_offset(2) == 49);
  require(local_light_strength(0.50F) > 0.599F);
  require(local_light_strength(0.50F) < 0.601F);
  require(local_light_strength(0.95F) == 0.95F);
  require(kAmbientSegmentCount == 16);
  require(kSurfaceWidth == 480 && kSurfaceHeight == 480);
  require(kControlCount == 15);

  const ControlLayout layout = make_control_layout();
  for (unsigned int i = 0; i < kControlCount; ++i) {
    require(layout.controls[i].bounds.x >= 0);
    require(layout.controls[i].bounds.y >= 0);
    require(layout.controls[i].bounds.x + layout.controls[i].bounds.width <= 480);
    require(layout.controls[i].bounds.y + layout.controls[i].bounds.height <= 480);
    for (unsigned int j = i + 1; j < kControlCount; ++j) {
      require(!overlaps(layout.controls[i].bounds, layout.controls[j].bounds));
    }
  }
  require(layout.controls[13].bounds.width == 189);

  ControlHit hit{};
  require(hit_test(layout, {.x = 188, .y = 384}, &hit));
  require(hit.kind == ControlKind::Command && hit.index == 4);
  require(!hit_test(layout, {.x = 10, .y = 10}, &hit));
  ExitProcess(failures);
}
