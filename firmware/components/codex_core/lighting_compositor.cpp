#include "codex/lighting_compositor.h"
#include "codex/lighting_lut.h"

namespace codex {
namespace {

constexpr Lighting kPairingLighting{LightEffect::Snake, 1.0F, 0.55F, 0, 0x304FFE};
constexpr Lighting kErrorLighting{LightEffect::Breath, 1.0F, 0.45F, 0, 0xFF0033};
constexpr Lighting kVoiceRecording{LightEffect::Snake, 1.0F, 0.45F, 0, 0x2E8B57};
constexpr Lighting kVoiceProcessing{LightEffect::Snake, 0.85F, 0.45F, 0, 0xFFFFFF};
constexpr Lighting kVoiceComplete{LightEffect::Solid, 0.75F, 0.0F, 0, 0xFFFFFF};

bool is_visible(const Lighting& lighting) {
  return lighting.effect != LightEffect::Off && lighting.brightness > 0.0F;
}

void set_ambient(CompositedLighting& output, LightingSource source,
                 Lighting lighting) {
  output.source = source;
  output.ambient = lighting;
}

}  // namespace

bool light_effect_from_wire(unsigned char value, LightEffect* output) {
  if (output == nullptr || value > 6) return false;
  *output = static_cast<LightEffect>(value);
  return true;
}

float effect_phase(LightEffect effect, float speed, unsigned int monotonic_ms) {
  if (effect == LightEffect::Off || effect == LightEffect::Solid) return 0.0F;
  if (speed < 0.0F) speed = 0.0F;
  if (speed > 1.0F) speed = 1.0F;
  const unsigned int period_ms = 2500U - static_cast<unsigned int>(speed * 2000.0F);
  return static_cast<float>(monotonic_ms % period_ms) / static_cast<float>(period_ms);
}

float device_effect_phase(LightEffect effect, float speed,
                          unsigned int monotonic_ms) {
  constexpr float kDeviceAnimationSpeedScale = 0.72F;
  return effect_phase(effect, speed * kDeviceAnimationSpeedScale, monotonic_ms);
}

float ambient_effect_phase(LightEffect effect, float speed,
                           unsigned int monotonic_ms) {
  return device_effect_phase(effect, speed, monotonic_ms / 2U);
}

float effect_envelope(LightEffect effect, float phase) {
  if (effect == LightEffect::Off) return 0.0F;
  return static_cast<float>(quantized_effect_sample(
             effect, lighting_phase_index(phase), 0, 1)) /
         31.0F;
}

CompositedLighting compose_lighting(const DeviceState& state,
                                    const AppKeyLightingState& app_lighting,
                                    unsigned int monotonic_ms) {
  CompositedLighting output{};
  output.keys = state.keys_lighting;
  for (unsigned int index = 0; index < kAgentCount; ++index) {
    const RoutedControl route =
        route_control(state.routing, state.layer, ControlGroup::Agent,
                      static_cast<unsigned char>(index));
    output.agents[index] =
        route.valid && route.destination == RouteDestination::App &&
                app_lighting.agent_set[route.address.id]
            ? app_lighting.agents[route.address.id]
            : state.agents[index].lighting;
  }
  for (unsigned int index = 0; index < kCommandCount; ++index) {
    const RoutedControl route =
        route_control(state.routing, state.layer, ControlGroup::Command,
                      static_cast<unsigned char>(index));
    output.commands[index] =
        route.valid && route.destination == RouteDestination::App &&
                app_lighting.command_set[route.address.id]
            ? app_lighting.commands[route.address.id]
            : state.keys_lighting;
  }

  // Lowest to highest priority: off, Codex, temporary, voice, error, pairing.
  // Idle is deliberately dark: Codex owns the normal ambient and key lighting.
  if (state.active_agent < kAgentCount &&
      is_visible(state.agents[state.active_agent].lighting)) {
    set_ambient(output, LightingSource::CodexAgent,
                state.agents[state.active_agent].lighting);
  }
  if (is_visible(state.ambient)) {
    set_ambient(output, LightingSource::CodexAmbient, state.ambient);
  }
  if (state.temporary_lighting_until_ms > monotonic_ms &&
      is_visible(state.temporary_lighting)) {
    set_ambient(output, LightingSource::TemporarySelection,
                state.temporary_lighting);
  }
  switch (state.voice) {
    case VoiceMode::Recording:
      set_ambient(output, LightingSource::Voice, kVoiceRecording);
      break;
    case VoiceMode::Processing:
      set_ambient(output, LightingSource::Voice, kVoiceProcessing);
      break;
    case VoiceMode::Complete:
      set_ambient(output, LightingSource::Voice, kVoiceComplete);
      break;
    case VoiceMode::Idle:
      break;
  }
  if (state.overlay == Overlay::Error) {
    set_ambient(output, LightingSource::Error, kErrorLighting);
  }
  if (state.overlay == Overlay::Pairing) {
    set_ambient(output, LightingSource::Pairing, kPairingLighting);
  }

  const float animation_scale =
      static_cast<float>(state.animation_strength) / 100.0F;
  if (animation_scale <= 0.0F) return output;

  output.ambient_phase = ambient_effect_phase(
      output.ambient.effect, output.ambient.speed * animation_scale,
      monotonic_ms);
  output.keys_phase = device_effect_phase(
      output.keys.effect, output.keys.speed * animation_scale, monotonic_ms);
  for (unsigned int index = 0; index < kAgentCount; ++index) {
    output.agent_phase[index] = device_effect_phase(
        output.agents[index].effect,
        output.agents[index].speed * animation_scale, monotonic_ms);
  }
  for (unsigned int index = 0; index < kCommandCount; ++index) {
    output.command_phase[index] = device_effect_phase(
        output.commands[index].effect,
        output.commands[index].speed * animation_scale, monotonic_ms);
  }
  return output;
}

CompositedLighting compose_lighting(const DeviceState& state,
                                    unsigned int monotonic_ms) {
  return compose_lighting(state, AppKeyLightingState{}, monotonic_ms);
}

}  // namespace codex
