#include "codex/device_reducer.h"
#include "codex/icon_id.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures{};
void require(bool value) { failures += value ? 0UL : 1UL; }
}  // namespace

extern "C" void mainCRTStartup() {
  using namespace codex;

  DeviceState state = make_default_state();
  state.codex_connected = true;
  state = reduce(state, make_app_session_connected(100)).state;
  require(state.app_session.active);
  require(state.routing.app_session_active);
  require(state.app_session.last_heartbeat_ms == 100);
  require(state.codex_connected);

  const unsigned int layer2_commands[kPhysicalControlsPerGroup] = {
      icon_id_hash("FAST"), icon_id_hash("APPR"), icon_id_hash("REJ"),
      icon_id_hash("COMPUTER"), icon_id_hash("custom-mic"),
      icon_id_hash("__blank")};
  state = reduce(state, make_app_icons_changed(
                            2, ControlGroup::Command, layer2_commands)).state;
  require(state.app_icon_hashes[1][1][4] == icon_id_hash("custom-mic"));
  const unsigned int first_icon_revision = state.app_icon_revisions[1][1];
  state = reduce(state, make_app_icons_changed(
                            2, ControlGroup::Command, layer2_commands)).state;
  require(state.app_icon_hashes[1][1][4] == icon_id_hash("custom-mic"));
  require(state.app_icon_revisions[1][1] == first_icon_revision + 1U);
  require(state.app_icon_hashes[1][1][5] == icon_id_hash("__blank"));

  state = reduce(state, make_app_session_heartbeat(500)).state;
  require(state.app_session.last_heartbeat_ms == 500);
  state = reduce(state,
                 make_app_control_changed(ControlGroup::Agent, 7, true)).state;
  state = reduce(state,
                 make_app_control_changed(ControlGroup::Command, 14, true)).state;
  state = reduce(state,
                 make_app_control_changed(ControlGroup::Command, 40, true)).state;
  require((state.app_session.held_masks[control_group_index(
               ControlGroup::Agent)] &
           (1ULL << 7U)) != 0);
  require((state.app_session.held_masks[control_group_index(
               ControlGroup::Command)] &
           (1ULL << 14U)) != 0);
  require((state.app_session.held_masks[control_group_index(
               ControlGroup::Command)] &
           (1ULL << 40U)) != 0);

  const ReduceResult before_timeout = reduce(state, make_tick(1999));
  require(before_timeout.state.app_session.active);
  require(before_timeout.effect_count == 0);

  const ReduceResult expired = reduce(before_timeout.state, make_tick(2000));
  require(!expired.state.app_session.active);
  require(!expired.state.routing.app_session_active);
  require(expired.state.codex_connected);
  require(expired.state.app_icon_hashes[1][1][4] == 0);
  require(expired.effect_count == 1);
  require(expired.effects[0].type == SideEffectType::ReleaseAppInputs);
  require((expired.effects[0].app_release_masks[control_group_index(
               ControlGroup::Agent)] &
           (1ULL << 7U)) != 0);
  require((expired.effects[0].app_release_masks[control_group_index(
               ControlGroup::Command)] &
           (1ULL << 14U)) != 0);
  require((expired.effects[0].app_release_masks[control_group_index(
               ControlGroup::Command)] &
           (1ULL << 40U)) != 0);

  // Heartbeats cannot reopen a closed session.
  state = reduce(expired.state, make_app_session_heartbeat(2500)).state;
  require(!state.app_session.active);

  // Routing preferences only mutate inside an active App session.
  RoutingConfig requested{};
  requested.layer_routing_enabled = true;
  requested.configured_layer_count = 3;
  requested.layer1_command_app_targets[4] =
      kLayer1CommandOverrideTarget;
  requested.higher_codex_masks[1][control_group_index(ControlGroup::Encoder)] =
      0xFF;
  state = reduce(state, make_routing_config_changed(requested)).state;
  require(!state.routing.layer_routing_enabled);

  state = reduce(state, make_app_session_connected(3000)).state;
  state = reduce(state, make_routing_config_changed(requested)).state;
  require(state.routing.layer_routing_enabled);
  require(state.routing.configured_layer_count == 3);
  require(state.routing.layer1_command_app_targets[4] ==
          kLayer1CommandOverrideTarget);
  require(state.routing.higher_codex_masks[1][control_group_index(
              ControlGroup::Encoder)] == 0x3F);
  require(state.routing.app_session_active);

  state.layer = 3;
  state = reduce(state, make_layer_next()).state;
  require(state.layer == 1);

  // Disconnect releases held private controls but does not disturb Codex.
  state.codex_connected = true;
  state = reduce(state,
                 make_app_control_changed(ControlGroup::Joystick, 6, true)).state;
  const ReduceResult disconnected =
      reduce(state, make_app_session_disconnected());
  require(!disconnected.state.app_session.active);
  require(disconnected.state.codex_connected);
  require(disconnected.effect_count == 1);
  require(disconnected.effects[0].type == SideEffectType::ReleaseAppInputs);

  // Once the App is absent, the physical layer key returns to six-layer legacy
  // cycling regardless of the persisted preference.
  state = disconnected.state;
  state.layer = 3;
  state = reduce(state, make_layer_next()).state;
  require(state.layer == 4);

  // Malformed runtime configuration fails closed.
  state = reduce(state, make_app_session_connected(4000)).state;
  RoutingConfig malformed = requested;
  malformed.configured_layer_count = 1;
  state = reduce(state, make_routing_config_changed(malformed)).state;
  require(state.routing.configured_layer_count == 3);

  ExitProcess(failures);
}
