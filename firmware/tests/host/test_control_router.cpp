#include "codex/control_router.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures{};
void require(bool value) { failures += value ? 0UL : 1UL; }
}  // namespace

extern "C" void mainCRTStartup() {
  using namespace codex;

  RoutingConfig config{};

  // An absent App session must preserve the existing Codex route on every
  // layer, even if stale persisted routing preferences are present.
  config.layer_routing_enabled = true;
  config.layer1_command_app_targets[2] = kLayer1CommandOverrideTarget;
  config.higher_codex_masks[0][control_group_index(ControlGroup::Agent)] =
      1U << 1U;
  const RoutedControl absent =
      route_control(config, 4, ControlGroup::Command, 3);
  require(absent.valid);
  require(absent.destination == RouteDestination::Codex);
  require(absent.address.group == ControlGroup::Command);
  require(absent.address.id == 3);

  // Without the companion App, every physical MIC falls back to the original
  // Codex MIC identity even when stale private routing preferences remain.
  config.layer1_command_app_targets[4] = kLayer1CommandOverrideTarget;
  for (std::uint8_t layer = 1; layer <= kMaximumLayerCount; ++layer) {
    const RoutedControl mic =
        route_control(config, layer, ControlGroup::Command, 4);
    require(mic.valid);
    require(mic.destination == RouteDestination::Codex);
    require(mic.address.group == ControlGroup::Command);
    require(mic.address.id == 4);
  }

  config.app_session_active = true;

  // Higher layers produce a flat, layerless App identifier.
  const RoutedControl layer2_agent =
      route_control(config, 2, ControlGroup::Agent, 0);
  require(layer2_agent.valid);
  require(layer2_agent.destination == RouteDestination::App);
  require(layer2_agent.address.group == ControlGroup::Agent);
  require(layer2_agent.address.id == 6);

  const RoutedControl layer6_command =
      route_control(config, 6, ControlGroup::Command, 5);
  require(layer6_command.valid);
  require(layer6_command.destination == RouteDestination::App);
  require(layer6_command.address.id == 35);

  // Layer 1 Agent keys remain protected.
  const RoutedControl layer1_agent =
      route_control(config, 1, ControlGroup::Agent, 2);
  require(layer1_agent.valid);
  require(layer1_agent.destination == RouteDestination::Codex);
  require(layer1_agent.address.id == 2);

  // A Layer 1 command override names the private target layer locally.
  const RoutedControl layer1_command =
      route_control(config, 1, ControlGroup::Command, 2);
  require(layer1_command.valid);
  require(layer1_command.destination == RouteDestination::App);
  require(layer1_command.address.id == 38);

  // The wide MIC key has one collision-free identity per physical layer.
  config.layer1_command_app_targets[4] = kLayer1CommandOverrideTarget;
  const std::uint8_t expected_mic_ids[kMaximumLayerCount] = {
      40, 10, 16, 22, 28, 34,
  };
  for (std::uint8_t layer = 1; layer <= kMaximumLayerCount; ++layer) {
    const RoutedControl mic =
        route_control(config, layer, ControlGroup::Command, 4);
    require(mic.valid);
    require(mic.destination == RouteDestination::App);
    require(mic.address.group == ControlGroup::Command);
    require(mic.address.id == expected_mic_ids[layer - 1U]);
  }
  config.layer1_command_app_targets[4] = 0;

  // The Layer 2 Agent 01 passthrough bit maps back to Codex Agent 01.
  const RoutedControl passthrough =
      route_control(config, 2, ControlGroup::Agent, 1);
  require(passthrough.valid);
  require(passthrough.destination == RouteDestination::Codex);
  require(passthrough.address.id == 1);

  const RoutedControl adjacent =
      route_control(config, 2, ControlGroup::Agent, 2);
  require(adjacent.valid);
  require(adjacent.destination == RouteDestination::App);
  require(adjacent.address.id == 8);

  // Bad persisted targets fail closed to the original Codex behavior.
  config.layer1_command_app_targets[2] = 2;
  const RoutedControl bad_target =
      route_control(config, 1, ControlGroup::Command, 2);
  require(bad_target.valid);
  require(bad_target.destination == RouteDestination::Codex);
  require(bad_target.address.id == 2);

  // Impossible physical addresses are rejected instead of aliasing AG00.
  require(!route_control(config, 0, ControlGroup::Agent, 0).valid);
  require(!route_control(config, 7, ControlGroup::Agent, 0).valid);
  require(!route_control(config, 1, ControlGroup::Agent, 6).valid);

  require(next_configured_layer(1, 2) == 2);
  require(next_configured_layer(2, 2) == 1);
  require(next_configured_layer(3, 3) == 1);
  require(next_configured_layer(6, 6) == 1);
  require(next_configured_layer(0, 3) == 1);
  require(next_configured_layer(4, 1) == 5);
  require(next_configured_layer(4, 7) == 5);

  ExitProcess(failures);
}
