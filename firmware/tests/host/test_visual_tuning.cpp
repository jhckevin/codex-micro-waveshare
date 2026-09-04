#include "codex/visual_tuning.h"
#include "codex/ambient_motion_lut.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures = 0;
void require(bool condition) { failures += condition ? 0UL : 1UL; }
}

extern "C" void mainCRTStartup() {
  using namespace codex;

  require(kAmbientBarLength == 62);
  require(kAmbientBarThickness == 9);
  require(compose_rgb(0x304FFE, 0x07100D, 0) == 0x07100D);
  require(compose_rgb(0x304FFE, 0x07100D, 255) == 0x304FFE);
  require(compose_rgb(0xFFFFFF, 0x000000, 128) == 0x808080);
  require(ambient_layer_color(0x1C2A4F, 0x647BFE, 0, 30) ==
          0x1C2A4F);
  require(ambient_layer_color(0x1C2A4F, 0x647BFE, 255, 30) ==
          0x27366A);
  require(ambient_layer_color(0x1C2A4F, 0x647BFE, 255, 8) ==
          0x1F2D55);
  require(ambient_layer_color(0x1C2A4F, 0x647BFE, 255, 45) ==
          0x2C3C75);
  require(ambient_motion_sample(0, 0) == 255);
  require(ambient_motion_sample(0, 15) == 184);
  require(ambient_motion_sample(4, 0) == 220);
  require(ambient_motion_sample(4, 1) == 128);
  require(ambient_motion_sample(8, 1) == 255);
  require(ambient_motion_sample(8, 0) == 184);
  require(!ambient_phase_wrapped(10, 11));
  require(ambient_phase_wrapped(127, 0));
  require(arcade_dpad_direction(0.0F, 0.1F) == 4U);
  require(arcade_dpad_direction(0.0F, 0.8F) == 1U);
  require(arcade_dpad_direction(0.25F, 0.8F) == 2U);
  require(arcade_dpad_direction(0.50F, 0.8F) == 3U);
  require(arcade_dpad_direction(0.75F, 0.8F) == 0U);
  require(effect_render_color(LightEffect::Solid, 0x304FFE, 17U) ==
          0x304FFEU);
  require(effect_render_color(LightEffect::Breath, 0x304FFE, 17U) ==
          0x304FFEU);
  require(effect_render_color(LightEffect::Rainbow, 0x304FFE, 0U) ==
          0xFF375FU);
  require(effect_render_color(LightEffect::Rainbow, 0x304FFE, 8U) ==
          0xFF9F0AU);
  require(effect_render_color(LightEffect::Gradient, 0x304FFE, 0U) ==
          0x304FFEU);
  require(effect_render_color(LightEffect::Gradient, 0x304FFE, 32U) ==
          mix_white(0x304FFE, 30U));
  require(ambient_diffuser_color(0x304FFE) == mix_white(0x304FFE, 45U));
  require(ambient_effect_base_opacity(LightEffect::Snake, 1.0F) >= 145U);
  require(agent_breath_frame(0U) == 0U);
  require(agent_breath_frame(1U) == 0U);
  require(agent_breath_frame(14U) == 7U);
  require(agent_breath_frame(15U) == 7U);
  require(agent_static_glow_opacity(0.0F) == 0U);
  require(agent_static_glow_opacity(1.0F) == 180U);
  require(agent_optimized_inset_opacity(0U) == 125U);
  require(agent_optimized_inset_opacity(7U) == 223U);

  ExitProcess(failures);
}
