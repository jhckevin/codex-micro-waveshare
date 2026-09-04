#include "codex/ambient_render_policy.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures = 0;
void require(bool condition) { failures += condition ? 0UL : 1UL; }
}

extern "C" void mainCRTStartup() {
  using namespace codex;

  const AmbientRenderPolicy off = ambient_render_policy(LightEffect::Off);
  require(!off.colored_base);
  require(off.overlay == AmbientOverlay::None);

  const AmbientRenderPolicy solid =
      ambient_render_policy(LightEffect::Solid);
  require(solid.colored_base && !solid.animated_base);
  require(solid.overlay == AmbientOverlay::Segments);

  const AmbientRenderPolicy snake =
      ambient_render_policy(LightEffect::Snake);
  require(snake.colored_base && !snake.animated_base);
  require(snake.overlay == AmbientOverlay::Segments);

  const AmbientRenderPolicy rainbow =
      ambient_render_policy(LightEffect::Rainbow);
  require(rainbow.colored_base);
  require(rainbow.overlay == AmbientOverlay::Segments);

  const AmbientRenderPolicy breath =
      ambient_render_policy(LightEffect::Breath);
  require(breath.colored_base && breath.animated_base);
  require(breath.overlay == AmbientOverlay::Segments);

  const AmbientRenderPolicy gradient =
      ambient_render_policy(LightEffect::Gradient);
  require(gradient.colored_base);
  require(gradient.overlay == AmbientOverlay::Segments);

  const AmbientRenderPolicy shallow =
      ambient_render_policy(LightEffect::ShallowBreath);
  require(shallow.colored_base && shallow.animated_base);
  require(shallow.overlay == AmbientOverlay::Segments);

  require(ambient_fixed_opacity(0U, 0U) == 0U);
  require(ambient_fixed_opacity(0U, 255U) == 40U);
  require(ambient_fixed_opacity(16U, 255U) == 40U);
  require(ambient_fixed_opacity(128U, 255U) == 128U);
  require(ambient_fixed_opacity(255U, 255U) == 255U);
  require(ambient_fixed_opacity(0U, 128U) == 27U);

  ExitProcess(failures);
}
