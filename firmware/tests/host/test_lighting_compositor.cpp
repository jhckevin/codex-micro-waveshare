#include "codex/device_reducer.h"
#include "codex/lighting_compositor.h"
#include "codex/visual_tuning.h"
#include "codex/lighting_lut.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures = 0;
void require(bool condition) { failures += condition ? 0UL : 1UL; }
}

extern "C" void mainCRTStartup() {
  using namespace codex;
  require(local_light_strength(0.5F) > 0.59F &&
          local_light_strength(0.5F) < 0.61F);
  require(local_light_strength(1.0F) > 0.94F &&
          local_light_strength(1.0F) < 0.96F);
  require(mix_ambient_white(0x304FFEU) == 0x4561FEU);
  require(mix_ambient_white(0x000000U) == 0x1A1A1AU);
  require(mix_ambient_white(0xFFFFFFU) == 0xFFFFFFU);
  require(mix_ambient_motion_white(0x304FFEU) == 0x788DFEU);
  require(ambient_segment_color(LightEffect::Snake, 0x304FFEU) ==
          0x304FFEU);
  require(ambient_segment_color(LightEffect::Solid, 0x304FFEU) ==
          0x304FFEU);
  require(ambient_background_opacity(0.0F) == 35U);
  require(ambient_background_opacity(1.0F) == 150U);
  require(ambient_effect_base_opacity(LightEffect::Solid, 1.0F) == 150U);
  require(ambient_effect_base_opacity(LightEffect::Snake, 1.0F) == 150U);
  require(ambient_effect_base_opacity(LightEffect::Snake, 0.0F) == 28U);
  require(kClassicScreenBackdropColor == 0x07100DU);
  require(ambient_diffuser_color(0x304FFEU) ==
          mix_white(0x304FFEU, 45U));
  require(ambient_diffuser_color(0x00FF4CU) ==
          mix_white(0x00FF4CU, 45U));
  require(ambient_diffuser_color(0xFF6D00U) ==
          mix_white(0xFF6D00U, 45U));
  require(ambient_diffuser_color(0xFF0033U) ==
          mix_white(0xFF0033U, 45U));
  require(ambient_emitter_color(0x304FFEU) == 0x788DFEU);
  require(ambient_emitter_color(0x00FF4CU) == 0x59FF8BU);
  require(ambient_emitter_color(0xFF6D00U) == 0xFFA059U);
  require(ambient_emitter_color(0xFF0033U) == 0xFF597AU);
  require(ambient_motion_mix_percent(LightEffect::Solid, 255U) == 0U);
  require(ambient_motion_mix_percent(LightEffect::Snake, 54U) == 0U);
  require(ambient_motion_mix_percent(LightEffect::Snake, 142U) == 9U);
  require(ambient_motion_mix_percent(LightEffect::Snake, 255U) == 18U);
  const unsigned int lifted_blue = mix_white(
      0x304FFEU,
      ambient_motion_mix_percent(LightEffect::Snake, 255U));
  require(lifted_blue == 0x556FFEU);
  require(rgb_luma(lifted_blue) * 100U >=
          rgb_luma(0x304FFEU) * 130U);
  require(moving_light_strength(1.0F) == 1.0F);
  const unsigned char agent_dim =
      agent_light_opacity(1.0F, 4.0F / 31.0F);
  const unsigned char agent_peak = agent_light_opacity(1.0F, 1.0F);
  require(agent_dim >= 54U && agent_dim <= 56U);
  require(agent_peak >= 244U);
  require(static_cast<unsigned int>(agent_peak - agent_dim) >= 188U);
  require(agent_breath_level(1.0F, 4.0F / 31.0F) == 0U);
  require(agent_breath_level(1.0F, 1.0F) == 15U);
  require(agent_cap_top_mix(0U) >= 55U);
  require(agent_cap_bottom_mix(0U) >= 80U);
  require(agent_inset_opacity(0U) >= 85U);
  require(ambient_sprite_position(9, 33) == 0);
  require(ambient_sprite_position(462, 33) == 445);
  require(ambient_sprite_position(64, 86) == 52);
  require(ambient_segment_ring_position(9) == -16);
  require(ambient_segment_ring_position(64) == 39);
  require(ambient_segment_ring_position(462) == 437);

  LightEffect effect = LightEffect::Off;
  require(light_effect_from_wire(4, &effect));
  require(effect == LightEffect::Breath);
  require(!light_effect_from_wire(7, &effect));

  DeviceState state = make_default_state();
  const CompositedLighting idle = compose_lighting(state, 100);
  require(idle.source == LightingSource::Off);
  require(idle.ambient.effect == LightEffect::Off);
  require(idle.ambient.brightness == 0.0F);
  state.ambient = {.effect = LightEffect::Snake,
                   .brightness = 1.0F,
                   .speed = 0.4F,
                   .color = 0x304FFE};
  require(compose_lighting(state, 100).source == LightingSource::CodexAmbient);
  state.animation_strength = 0;
  require(compose_lighting(state, 1000).ambient_phase == 0.0F);
  state.animation_strength = 100;

  state.agents[0].lighting = {.effect = LightEffect::Breath,
                              .brightness = 1.0F,
                              .speed = 0.4F,
                              .color = 0x304FFE};
  state.agents[3].lighting = {.effect = LightEffect::Breath,
                              .brightness = 0.8F,
                              .speed = 0.4F,
                              .color = 0xFF0033};
  const CompositedLighting synchronized = compose_lighting(state, 4000);
  require(synchronized.agent_phase[0] == synchronized.agent_phase[3]);
  require(effect_envelope(synchronized.agents[0].effect,
                          synchronized.agent_phase[0]) ==
          effect_envelope(synchronized.agents[3].effect,
                          synchronized.agent_phase[3]));
  require(synchronized.ambient_phase ==
          ambient_effect_phase(LightEffect::Snake, 0.4F, 4000));
  require(synchronized.agent_phase[0] ==
          device_effect_phase(LightEffect::Breath, 0.4F, 4000));
  require(ambient_effect_phase(LightEffect::Snake, 0.4F, 4000) ==
          device_effect_phase(LightEffect::Snake, 0.4F, 2000));
  require(synchronized.ambient_phase != synchronized.agent_phase[0]);

  state.voice = VoiceMode::Recording;
  require(compose_lighting(state, 100).source == LightingSource::Voice);
  require(compose_lighting(state, 100).ambient.color == 0x2E8B57);

  state.overlay = Overlay::Error;
  require(compose_lighting(state, 100).source == LightingSource::Error);
  state.overlay = Overlay::Pairing;
  require(compose_lighting(state, 100).source == LightingSource::Pairing);

  AppKeyLightingState app_lighting{};
  app_lighting.agent_set[7] = true;
  app_lighting.agents[7] = {.effect = LightEffect::Breath,
                            .brightness = 0.75F,
                            .speed = 0.5F,
                            .color = 0xFF0033};
  app_lighting.command_set[10] = true;
  app_lighting.commands[10] = {.effect = LightEffect::Solid,
                               .brightness = 0.5F,
                               .speed = 0.0F,
                               .color = 0x00FF4C};
  DeviceState private_state = make_default_state();
  private_state.ambient = {.effect = LightEffect::Snake,
                           .brightness = 1.0F,
                           .speed = 0.4F,
                           .color = 0x304FFE};
  private_state.agents[1].lighting = {.effect = LightEffect::Solid,
                                      .brightness = 1.0F,
                                      .color = 0xFFFFFF};
  private_state.keys_lighting = {.effect = LightEffect::Solid,
                                 .brightness = 1.0F,
                                 .color = 0xFFFFFF};
  private_state.routing.app_session_active = true;
  private_state.routing.layer_routing_enabled = true;
  private_state.layer = 2;
  const CompositedLighting private_composite =
      compose_lighting(private_state, app_lighting, 1000);
  require(private_composite.agents[1].color == 0xFF0033);
  require(private_composite.commands[4].color == 0x00FF4C);
  require(private_composite.ambient.color == 0x304FFE);

  private_state.routing.app_session_active = false;
  const CompositedLighting restored =
      compose_lighting(private_state, app_lighting, 1000);
  require(restored.agents[1].color == 0xFFFFFF);
  require(restored.commands[4].color == 0xFFFFFF);
  require(restored.ambient.color == 0x304FFE);

  const float first = effect_phase(LightEffect::Snake, 0.4F, 1000);
  const float second = effect_phase(LightEffect::Snake, 0.4F, 1000);
  require(first == second);
  require(first >= 0.0F && first < 1.0F);
  require(device_effect_phase(LightEffect::Snake, 0.4F, 1000) < 0.55F);

  require(effect_envelope(LightEffect::Solid, 0.25F) == 1.0F);
  require(effect_envelope(LightEffect::Snake, 0.25F) == 1.0F);
  require(effect_envelope(LightEffect::Breath, 0.0F) <
          effect_envelope(LightEffect::Breath, 0.5F));
  require(effect_envelope(LightEffect::Breath, 0.5F) == 1.0F);
  require(effect_envelope(LightEffect::Breath, 0.75F) < 1.0F);
  require(effect_envelope(LightEffect::ShallowBreath, 0.0F) >= 0.6F);
  require(kLightingSampleCount == 64);
  require(quantized_effect_sample(LightEffect::Breath, 0, 0, 16) < 8);
  require(quantized_effect_sample(LightEffect::Breath, 32, 0, 16) == 31);
  require(quantized_effect_sample(LightEffect::Snake, 0, 0, 16) == 31);
  require(quantized_effect_sample(LightEffect::Snake, 2, 0, 16) > 20);
  require(quantized_effect_sample(LightEffect::Snake, 2, 0, 16) < 31);
  require(quantized_effect_sample(LightEffect::Snake, 2, 1, 16) >= 15);
  require(quantized_effect_sample(LightEffect::Snake, 4, 1, 16) == 31);
  require(quantized_effect_sample(LightEffect::Snake, 0, 8, 16) == 0);
  require(!should_render_lighting(1000, 1000));
  require(!should_render_lighting(1000, 1029));
  require(should_render_lighting(1000, 1030));

  ExitProcess(failures);
}
