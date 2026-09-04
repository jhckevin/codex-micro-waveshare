#include "codex/device_reducer.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures{};
void require(bool value) { failures += value ? 0UL : 1UL; }
}  // namespace

extern "C" void mainCRTStartup() {
  using namespace codex;

  DeviceState state = make_default_state();
  state = reduce(state, make_app_session_connected(100)).state;
  RoutingConfig routing{};
  routing.layer_routing_enabled = true;
  routing.configured_layer_count = 6;
  state = reduce(state, make_routing_config_changed(routing)).state;
  state.layer = 2;

  ReduceResult pressed =
      reduce(state, make_key_pressed(ControlId::Agent1));
  require(pressed.state.agents[1].pressed);
  require(pressed.effects[0].type == SideEffectType::SendAppControl);
  require(pressed.effects[0].app_group == ControlGroup::Agent);
  require(pressed.effects[0].app_id == 7);
  require(pressed.effects[0].action == 1);

  // Release uses the press-time route even if the layer changes in between.
  pressed.state.layer = 4;
  ReduceResult released =
      reduce(pressed.state, make_key_released(ControlId::Agent1));
  require(!released.state.agents[1].pressed);
  require(released.effects[0].type == SideEffectType::SendAppControl);
  require(released.effects[0].app_id == 7);
  require(released.effects[0].action == 0);

  // A higher-layer passthrough remains byte-for-byte Codex behavior.
  state.layer = 2;
  state.routing.higher_codex_masks[0][control_group_index(
      ControlGroup::Agent)] = 1U << 1U;
  pressed = reduce(state, make_key_pressed(ControlId::Agent1));
  require(pressed.effects[0].type == SideEffectType::SendHid);
  require(pressed.effects[0].control == ControlId::Agent1);

  // Layer 1 command overrides use the configured target's flat identifier.
  state = make_default_state();
  state = reduce(state, make_app_session_connected(200)).state;
  routing.layer1_command_app_targets[4] = kLayer1CommandOverrideTarget;
  state = reduce(state, make_routing_config_changed(routing)).state;
  pressed = reduce(state, make_key_pressed(ControlId::Command4));
  require(pressed.effects[0].type == SideEffectType::SendAppControl);
  require(pressed.effects[0].app_group == ControlGroup::Command);
  require(pressed.effects[0].app_id == 40);
  released = reduce(pressed.state, make_key_released(ControlId::Command4));
  require(released.effects[0].type == SideEffectType::SendAppControl);
  require(released.effects[0].app_id == 40);
  require(released.effects[0].action == 0);

  // App absence preserves the current Codex-only behavior on every layer.
  state = make_default_state();
  state.layer = 5;
  pressed = reduce(state, make_key_pressed(ControlId::Command4));
  require(pressed.effects[0].type == SideEffectType::SendHid);
  require(pressed.effects[0].control == ControlId::Command4);

  ExitProcess(failures);
}
