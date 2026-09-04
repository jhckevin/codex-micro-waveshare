#pragma once

#include "codex/device_state.h"

namespace codex {

enum class LightingSource : unsigned char {
  Off,
  LocalDefault,
  CodexAgent,
  CodexAmbient,
  TemporarySelection,
  Voice,
  Error,
  Pairing,
};

inline constexpr unsigned int kPrivateLightingCount =
    kPrivateControlIdCount;

struct AppKeyLightingState {
  Lighting agents[kPrivateLightingCount]{};
  Lighting commands[kPrivateLightingCount]{};
  bool agent_set[kPrivateLightingCount]{};
  bool command_set[kPrivateLightingCount]{};
};

struct CompositedLighting {
  LightingSource source{LightingSource::Off};
  Lighting ambient{};
  Lighting keys{};
  Lighting agents[kAgentCount]{};
  Lighting commands[kCommandCount]{};
  float ambient_phase{0.0F};
  float keys_phase{0.0F};
  float agent_phase[kAgentCount]{};
  float command_phase[kCommandCount]{};
};

[[nodiscard]] bool light_effect_from_wire(unsigned char value,
                                          LightEffect* output);
[[nodiscard]] float effect_phase(LightEffect effect, float speed,
                                 unsigned int monotonic_ms);
[[nodiscard]] float device_effect_phase(LightEffect effect, float speed,
                                        unsigned int monotonic_ms);
[[nodiscard]] float ambient_effect_phase(LightEffect effect, float speed,
                                         unsigned int monotonic_ms);
[[nodiscard]] float effect_envelope(LightEffect effect, float phase);
[[nodiscard]] CompositedLighting compose_lighting(
    const DeviceState& state, unsigned int monotonic_ms);
[[nodiscard]] CompositedLighting compose_lighting(
    const DeviceState& state, const AppKeyLightingState& app_lighting,
    unsigned int monotonic_ms);

}  // namespace codex
