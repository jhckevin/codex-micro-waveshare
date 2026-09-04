#include "codex/device_reducer.h"
#include "codex/battery_policy.h"
#include "codex/power_policy.h"

#include <cstring>

namespace codex {
namespace {

constexpr unsigned int kAppLeaseTimeoutMs = 1500U;
constexpr const char* kDefaultKeycaps[kCommandCount] = {
    "FAST", "APPR", "REJ", "COMPUTER", "MIC", "OAI",
};

void copy_text(char* destination, unsigned int capacity, const char* source) {
  unsigned int index = 0;
  while (index + 1 < capacity && source[index] != '\0') {
    destination[index] = source[index];
    ++index;
  }
  destination[index] = '\0';
}

float clamp_unit(float value) {
  if (value < 0.0F) return 0.0F;
  if (value > 1.0F) return 1.0F;
  return value;
}

float normalize_angle(float value) {
  while (value < 0.0F) value += 1.0F;
  while (value >= 1.0F) value -= 1.0F;
  return value;
}

Lighting sanitize_lighting(Lighting lighting) {
  const unsigned char effect = static_cast<unsigned char>(lighting.effect);
  if (effect > static_cast<unsigned char>(LightEffect::ShallowBreath)) {
    lighting.effect = LightEffect::Off;
  }
  lighting.brightness = clamp_unit(lighting.brightness);
  lighting.speed = clamp_unit(lighting.speed);
  lighting.color &= 0x00FFFFFFU;
  return lighting;
}

bool valid_lighting(const Lighting& lighting) {
  return static_cast<unsigned char>(lighting.effect) <=
         static_cast<unsigned char>(LightEffect::ShallowBreath);
}

bool is_agent(ControlId control) {
  return control >= ControlId::Agent0 && control <= ControlId::Agent5;
}

bool is_command(ControlId control) {
  return control >= ControlId::Command0 && control <= ControlId::Command5;
}

bool* pressed_slot(DeviceState& state, ControlId control) {
  if (is_agent(control)) {
    const unsigned int index = static_cast<unsigned int>(control) -
                               static_cast<unsigned int>(ControlId::Agent0);
    return &state.agents[index].pressed;
  }
  if (is_command(control)) {
    const unsigned int index = static_cast<unsigned int>(control) -
                               static_cast<unsigned int>(ControlId::Command0);
    return &state.commands[index].pressed;
  }
  if (control == ControlId::Encoder) return &state.encoder.pressed;
  return nullptr;
}

bool control_route_address(ControlId control, ControlGroup& group,
                           unsigned char& physical_index) {
  if (is_agent(control)) {
    group = ControlGroup::Agent;
    physical_index = static_cast<unsigned char>(
        static_cast<unsigned int>(control) -
        static_cast<unsigned int>(ControlId::Agent0));
    return true;
  }
  if (is_command(control)) {
    group = ControlGroup::Command;
    physical_index = static_cast<unsigned char>(
        static_cast<unsigned int>(control) -
        static_cast<unsigned int>(ControlId::Command0));
    return true;
  }
  if (control == ControlId::Encoder) {
    group = ControlGroup::Encoder;
    physical_index = 0;
    return true;
  }
  return false;
}

PhysicalRouteLatch* route_latch(DeviceState& state, ControlId control) {
  if (is_agent(control)) {
    const unsigned int index = static_cast<unsigned int>(control) -
                               static_cast<unsigned int>(ControlId::Agent0);
    return &state.input_routes.agents[index];
  }
  if (is_command(control)) {
    const unsigned int index = static_cast<unsigned int>(control) -
                               static_cast<unsigned int>(ControlId::Command0);
    return &state.input_routes.commands[index];
  }
  if (control == ControlId::Encoder) return &state.input_routes.encoder;
  return nullptr;
}

void set_app_held(DeviceState& state, const RoutedControl& route, bool held) {
  if (route.destination != RouteDestination::App ||
      !control_group_is_valid(route.address.group) ||
      route.address.id >= kPrivateControlIdCount) {
    return;
  }
  unsigned long long& mask =
      state.app_session.held_masks[control_group_index(route.address.group)];
  const unsigned long long bit = 1ULL << route.address.id;
  if (held) {
    mask |= bit;
  } else {
    mask &= ~bit;
  }
}

SideEffect routed_key_effect(const RoutedControl& route, ControlId control,
                             unsigned char action,
                             signed char direction = 0,
                             unsigned int origin_us = 0) {
  if (route.destination == RouteDestination::App) {
    return {
        .type = SideEffectType::SendAppControl,
        .control = control,
        .direction = direction,
        .action = action,
        .app_group = route.address.group,
        .app_id = route.address.id,
        .origin_us = origin_us,
    };
  }
  return {
      .type = SideEffectType::SendHid,
      .control = control,
      .direction = direction,
      .action = action,
      .origin_us = origin_us,
  };
}

void append_effect(ReduceResult& result, SideEffect effect) {
  if (result.effect_count >= 3) return;
  result.effects[result.effect_count++] = effect;
}

SideEffect release_all_effect(const DeviceState& state) {
  SideEffect effect{.type = SideEffectType::ReleaseAllInputs};
  for (unsigned int index = 0; index < kAgentCount; ++index) {
    const PhysicalRouteLatch& latch = state.input_routes.agents[index];
    if (state.agents[index].pressed &&
        (!latch.active ||
         latch.route.destination == RouteDestination::Codex)) {
      effect.release_mask |= 1U << index;
    }
  }
  for (unsigned int index = 0; index < kCommandCount; ++index) {
    const PhysicalRouteLatch& latch = state.input_routes.commands[index];
    if (state.commands[index].pressed &&
        (!latch.active ||
         latch.route.destination == RouteDestination::Codex)) {
      effect.release_mask |= 1U << (kAgentCount + index);
    }
  }
  if (state.encoder.pressed &&
      (!state.input_routes.encoder.active ||
       state.input_routes.encoder.route.destination ==
           RouteDestination::Codex)) {
    effect.release_mask |= 1U << 12U;
  }
  if (state.joystick.captured || state.joystick.distance > 0.0F) {
    if (!state.input_routes.joystick.active ||
        state.input_routes.joystick.route.destination ==
            RouteDestination::Codex) {
      effect.release_mask |= 1U << 13U;
    }
  }
  return effect;
}

bool app_controls_held(const AppSessionState& session) {
  for (unsigned int group = 0; group < kControlGroupCount; ++group) {
    if (session.held_masks[group] != 0) return true;
  }
  return false;
}

SideEffect release_app_effect(const AppSessionState& session) {
  SideEffect effect{.type = SideEffectType::ReleaseAppInputs};
  for (unsigned int group = 0; group < kControlGroupCount; ++group) {
    effect.app_release_masks[group] = session.held_masks[group];
  }
  return effect;
}

void append_all_input_releases(ReduceResult& result) {
  if (app_controls_held(result.state.app_session)) {
    append_effect(result, release_app_effect(result.state.app_session));
  }
  append_effect(result, release_all_effect(result.state));
}

void clear_all_physical_inputs(DeviceState& state) {
  for (unsigned int index = 0; index < kAgentCount; ++index) {
    state.agents[index].pressed = false;
  }
  for (unsigned int index = 0; index < kCommandCount; ++index) {
    state.commands[index].pressed = false;
  }
  state.encoder.pressed = false;
  state.joystick.distance = 0.0F;
  state.joystick.captured = false;
  state.input_routes = InputRouteState{};
  for (unsigned int group = 0; group < kControlGroupCount; ++group) {
    state.app_session.held_masks[group] = 0;
  }
}

void close_app_session(ReduceResult& result) {
  if (app_controls_held(result.state.app_session)) {
    append_effect(result, release_app_effect(result.state.app_session));
  }
  for (unsigned int index = 0; index < kAgentCount; ++index) {
    PhysicalRouteLatch& latch = result.state.input_routes.agents[index];
    if (latch.active &&
        latch.route.destination == RouteDestination::App) {
      latch = PhysicalRouteLatch{};
      result.state.agents[index].pressed = false;
    }
  }
  for (unsigned int index = 0; index < kCommandCount; ++index) {
    PhysicalRouteLatch& latch = result.state.input_routes.commands[index];
    if (latch.active &&
        latch.route.destination == RouteDestination::App) {
      latch = PhysicalRouteLatch{};
      result.state.commands[index].pressed = false;
    }
  }
  if (result.state.input_routes.encoder.active &&
      result.state.input_routes.encoder.route.destination ==
          RouteDestination::App) {
    result.state.input_routes.encoder = {};
    result.state.encoder.pressed = false;
  }
  if (result.state.input_routes.joystick.active &&
      result.state.input_routes.joystick.route.destination ==
          RouteDestination::App) {
    result.state.input_routes.joystick = PhysicalRouteLatch{};
    result.state.joystick = JoystickState{};
  }
  result.state.app_session = {};
  result.state.routing.app_session_active = false;
  memset(result.state.app_icon_hashes, 0, sizeof(result.state.app_icon_hashes));
}

bool valid_routing_preferences(const RoutingConfig& config) {
  if (config.configured_layer_count < kMinimumLayerCount ||
      config.configured_layer_count > kMaximumLayerCount) {
    return false;
  }
  for (unsigned int index = 0; index < kPhysicalControlsPerGroup; ++index) {
    const unsigned char target = config.layer1_command_app_targets[index];
    if (target != 0 && target != kLayer1CommandOverrideTarget) {
      return false;
    }
  }
  return true;
}

RoutingConfig sanitize_routing_preferences(const RoutingConfig& requested,
                                           bool app_session_active) {
  RoutingConfig sanitized = requested;
  sanitized.app_session_active = app_session_active;
  for (unsigned int layer = 0; layer < kMaximumLayerCount - 1; ++layer) {
    for (unsigned int group = 0; group < kControlGroupCount; ++group) {
      sanitized.higher_codex_masks[layer][group] &= 0x3FU;
    }
  }
  return sanitized;
}

DeviceEvent make_simple(EventType type) {
  DeviceEvent event{};
  event.type = type;
  return event;
}

}  // namespace

DeviceState make_default_state() {
  DeviceState state{};
  for (unsigned int index = 0; index < kCommandCount; ++index) {
    copy_text(state.commands[index].keycap_id,
              sizeof(state.commands[index].keycap_id), kDefaultKeycaps[index]);
    copy_text(state.commands[index].label,
              sizeof(state.commands[index].label), kDefaultKeycaps[index]);
  }
  return state;
}

DeviceEvent make_key_pressed(ControlId control) {
  DeviceEvent event = make_simple(EventType::KeyPressed);
  event.payload.key = {.control = control};
  return event;
}

DeviceEvent make_key_released(ControlId control) {
  DeviceEvent event = make_simple(EventType::KeyReleased);
  event.payload.key = {.control = control};
  return event;
}

DeviceEvent make_joystick_changed(float angle, float distance) {
  DeviceEvent event = make_simple(EventType::JoystickChanged);
  event.payload.joystick = {.angle = angle, .distance = distance};
  return event;
}

DeviceEvent make_encoder_step(signed char direction) {
  DeviceEvent event = make_simple(EventType::EncoderStep);
  event.payload.encoder_step = direction < 0 ? -1 : 1;
  return event;
}

DeviceEvent make_agent_lighting_changed(unsigned char index,
                                        Lighting lighting) {
  DeviceEvent event = make_simple(EventType::AgentLightingChanged);
  event.payload.agent_lighting = {.index = index, .lighting = lighting};
  return event;
}

DeviceEvent make_app_key_lighting_changed(ControlGroup group, unsigned char id,
                                          Lighting lighting) {
  DeviceEvent event = make_simple(EventType::AppKeyLightingChanged);
  event.payload.app_key_lighting = {
      .group = group,
      .id = id,
      .lighting = lighting,
  };
  return event;
}

DeviceEvent make_app_icons_changed(
    unsigned char layer, ControlGroup group,
    const unsigned int hashes[kPhysicalControlsPerGroup]) {
  DeviceEvent event = make_simple(EventType::AppIconsChanged);
  event.payload.app_icons.layer = layer;
  event.payload.app_icons.group = group;
  if (hashes != nullptr) {
    for (unsigned int index = 0; index < kPhysicalControlsPerGroup; ++index) {
      event.payload.app_icons.hashes[index] = hashes[index];
    }
  }
  return event;
}

DeviceEvent make_ambient_lighting_changed(Lighting lighting) {
  DeviceEvent event = make_simple(EventType::AmbientLightingChanged);
  event.payload.lighting = lighting;
  return event;
}

DeviceEvent make_keys_lighting_changed(Lighting lighting) {
  DeviceEvent event = make_simple(EventType::KeysLightingChanged);
  event.payload.lighting = lighting;
  return event;
}

DeviceEvent make_temporary_lighting_changed(Lighting lighting,
                                            unsigned int until_ms) {
  DeviceEvent event = make_simple(EventType::TemporaryLightingChanged);
  event.payload.temporary_lighting = {.lighting = lighting, .until_ms = until_ms};
  return event;
}

DeviceEvent make_layer_next() {
  return make_simple(EventType::LayerNext);
}

DeviceEvent make_pairing_mode_requested(unsigned int monotonic_ms) {
  DeviceEvent event = make_simple(EventType::PairingModeRequested);
  event.payload.tick_ms = monotonic_ms;
  return event;
}

DeviceEvent make_current_ble_slot_repair_requested(unsigned int monotonic_ms) {
  DeviceEvent event = make_simple(EventType::CurrentBleSlotRepairRequested);
  event.payload.tick_ms = monotonic_ms;
  return event;
}

DeviceEvent make_ble_slot_changed(unsigned char slot) {
  DeviceEvent event = make_simple(EventType::BleSlotChanged);
  event.payload.ble_slot = slot;
  return event;
}

DeviceEvent make_ble_peer_bonded(unsigned char slot,
                                 const unsigned char address[6],
                                 unsigned char address_type) {
  DeviceEvent event = make_simple(EventType::BlePeerBonded);
  event.payload.ble_peer.slot = slot;
  event.payload.ble_peer.address_type = address_type;
  for (unsigned int index = 0; index < 6; ++index) {
    event.payload.ble_peer.address[index] = address[index];
  }
  return event;
}

DeviceEvent make_connection_overlay_requested() {
  return make_simple(EventType::ConnectionOverlayRequested);
}

DeviceEvent make_transport_connected(TransportLink link) {
  DeviceEvent event = make_simple(EventType::TransportConnected);
  event.payload.transport_link = link;
  return event;
}

DeviceEvent make_transport_disconnected(TransportLink link) {
  DeviceEvent event = make_simple(EventType::TransportDisconnected);
  event.payload.transport_link = link;
  return event;
}

DeviceEvent make_codex_rpc_received(unsigned int monotonic_ms, bool lighting,
                                    TransportLink link) {
  DeviceEvent event = make_simple(EventType::CodexRpcReceived);
  event.payload.codex_rpc = {
      .monotonic_ms = monotonic_ms,
      .lighting = lighting,
      .link = link,
  };
  return event;
}

DeviceEvent make_app_session_connected(unsigned int monotonic_ms) {
  DeviceEvent event = make_simple(EventType::AppSessionConnected);
  event.payload.tick_ms = monotonic_ms;
  return event;
}

DeviceEvent make_app_session_heartbeat(unsigned int monotonic_ms) {
  DeviceEvent event = make_simple(EventType::AppSessionHeartbeat);
  event.payload.tick_ms = monotonic_ms;
  return event;
}

DeviceEvent make_app_session_disconnected() {
  return make_simple(EventType::AppSessionDisconnected);
}

DeviceEvent make_routing_config_changed(const RoutingConfig& config) {
  DeviceEvent event = make_simple(EventType::RoutingConfigChanged);
  event.payload.routing_config = config;
  return event;
}

DeviceEvent make_app_control_changed(ControlGroup group, unsigned char id,
                                     bool pressed) {
  DeviceEvent event = make_simple(EventType::AppControlChanged);
  event.payload.app_control = {.group = group, .id = id, .pressed = pressed};
  return event;
}

DeviceEvent make_hid_tx_queued() {
  return make_simple(EventType::HidTxQueued);
}

DeviceEvent make_hid_tx_completed(bool success) {
  DeviceEvent event = make_simple(EventType::HidTxCompleted);
  event.payload.success = success;
  return event;
}

DeviceEvent make_input_release_requested() {
  return make_simple(EventType::InputReleaseRequested);
}

DeviceEvent make_enter_standby_requested() {
  return make_simple(EventType::EnterStandbyRequested);
}

DeviceEvent make_exit_standby_requested() {
  return make_simple(EventType::ExitStandbyRequested);
}

DeviceEvent make_enter_ultra_standby_requested() {
  return make_simple(EventType::EnterUltraStandbyRequested);
}

DeviceEvent make_protected_standby_requested() {
  return make_simple(EventType::ProtectedStandbyRequested);
}

DeviceEvent make_protected_unlock_shown(unsigned int monotonic_ms) {
  DeviceEvent event = make_simple(EventType::ProtectedUnlockShown);
  event.payload.tick_ms = monotonic_ms;
  return event;
}

DeviceEvent make_protected_unlock_completed() {
  return make_simple(EventType::ProtectedUnlockCompleted);
}

DeviceEvent make_super_standby_requested() {
  return make_simple(EventType::SuperStandbyRequested);
}

DeviceEvent make_standby_transition_cancelled(bool transport_connected) {
  DeviceEvent event = make_simple(EventType::StandbyTransitionCancelled);
  event.payload.selection_restore.transport_connected = transport_connected;
  return event;
}

DeviceEvent make_power_button_pressed() {
  return make_simple(EventType::PowerButtonPressed);
}

DeviceEvent make_transport_mode_changed(TransportMode mode) {
  DeviceEvent event = make_simple(EventType::TransportModeChanged);
  event.payload.transport_mode = mode;
  return event;
}

DeviceEvent make_bluetooth_enabled_changed(bool enabled) {
  DeviceEvent event = make_simple(EventType::BluetoothEnabledChanged);
  event.payload.success = enabled;
  return event;
}

DeviceEvent make_protocol_profile_changed(ProtocolProfile profile) {
  DeviceEvent event = make_simple(EventType::ProtocolProfileChanged);
  event.payload.protocol_profile = profile;
  return event;
}

DeviceEvent make_selection_transition_cancelled(
    TransportMode transport, unsigned char ble_slot,
    bool transport_connected) {
  DeviceEvent event = make_simple(EventType::SelectionTransitionCancelled);
  event.payload.selection_restore = {
      .transport = transport,
      .ble_slot = ble_slot,
      .transport_connected = transport_connected,
  };
  return event;
}

DeviceEvent make_command_keycap_changed(unsigned char index, const char* text) {
  DeviceEvent event = make_simple(EventType::CommandKeycapChanged);
  event.payload.command_text.index = index;
  copy_text(event.payload.command_text.text,
            sizeof(event.payload.command_text.text), text);
  return event;
}

DeviceEvent make_command_label_changed(unsigned char index, const char* text) {
  DeviceEvent event = make_simple(EventType::CommandLabelChanged);
  event.payload.command_text.index = index;
  copy_text(event.payload.command_text.text,
            sizeof(event.payload.command_text.text), text);
  return event;
}

DeviceEvent make_sound_settings_changed(SoundProfile profile, bool enabled,
                                        unsigned char volume) {
  DeviceEvent event = make_simple(EventType::SoundSettingsChanged);
  event.payload.sound_settings = {
      .profile = profile,
      .enabled = enabled,
      .volume = volume,
  };
  return event;
}

DeviceEvent make_display_settings_changed(unsigned char brightness,
                                           unsigned short standby_timeout_seconds,
                                           unsigned char animation_strength,
                                           unsigned int super_standby_timeout_seconds,
                                           bool anti_accidental_shutdown,
                                           bool smart_screensaver_enabled) {
  DeviceEvent event = make_simple(EventType::DisplaySettingsChanged);
  event.payload.display_settings = {
      .brightness = brightness,
      .standby_timeout_seconds = standby_timeout_seconds,
      .animation_strength = animation_strength,
      .super_standby_timeout_seconds = super_standby_timeout_seconds,
      .anti_accidental_shutdown = anti_accidental_shutdown,
      .smart_screensaver_enabled = smart_screensaver_enabled,
  };
  return event;
}

DeviceEvent make_power_settings_changed(
    PowerButtonMode button_mode, unsigned int auto_ultra_timeout_seconds,
    bool ultra_touch_wake) {
  DeviceEvent event = make_simple(EventType::PowerSettingsChanged);
  event.payload.power_settings = {
      .button_mode = button_mode,
      .auto_ultra_timeout_seconds = auto_ultra_timeout_seconds,
      .ultra_touch_wake = ultra_touch_wake,
  };
  return event;
}

DeviceEvent make_joystick_sensitivity_changed(
    unsigned char sensitivity_percent) {
  DeviceEvent event = make_simple(EventType::JoystickSensitivityChanged);
  event.payload.joystick_sensitivity_percent = sensitivity_percent;
  return event;
}

DeviceEvent make_battery_changed(bool present, unsigned char percent,
                                 bool charging, bool usb_power_present,
                                 unsigned short voltage_mv,
                                 unsigned short charge_limit_ma,
                                 unsigned int monotonic_ms) {
  DeviceEvent event = make_simple(EventType::BatteryChanged);
  event.payload.battery = {
      .present = present,
      .percent = percent,
      .charging = charging,
      .usb_power_present = usb_power_present,
      .voltage_mv = voltage_mv,
      .charge_limit_ma = charge_limit_ma,
      .monotonic_ms = monotonic_ms,
  };
  return event;
}

DeviceEvent make_battery_warning_acknowledged() {
  return make_simple(EventType::BatteryWarningAcknowledged);
}

DeviceEvent make_diagnostics_changed(unsigned int event_queue_drops,
                                     unsigned char high_water,
                                     unsigned int rpc_errors,
                                     unsigned int free_heap_bytes,
                                     unsigned char reset_reason,
                                     bool recovery_gate_open) {
  DeviceEvent event = make_simple(EventType::DiagnosticsChanged);
  event.payload.diagnostics = {
      .event_queue_drops = event_queue_drops,
      .event_queue_high_water = high_water,
      .rpc_errors = rpc_errors,
      .free_heap_bytes = free_heap_bytes,
      .reset_reason = reset_reason,
      .recovery_gate_open = recovery_gate_open,
  };
  return event;
}

DeviceEvent make_rpc_error_recorded() {
  return make_simple(EventType::RpcErrorRecorded);
}

DeviceEvent make_clear_bonds_requested(unsigned char slot) {
  DeviceEvent event = make_simple(EventType::ClearBondsRequested);
  event.payload.ble_slot = slot;
  return event;
}

DeviceEvent make_ble_bonds_cleared(unsigned char slot) {
  DeviceEvent event = make_simple(EventType::BleBondsCleared);
  event.payload.ble_slot = slot;
  return event;
}

DeviceEvent make_tick(unsigned int monotonic_ms) {
  DeviceEvent event = make_simple(EventType::Tick);
  event.payload.tick_ms = monotonic_ms;
  return event;
}

ReduceResult reduce(const DeviceState& state, const DeviceEvent& event) {
  ReduceResult result{};
  result.state = state;

  switch (event.type) {
    case EventType::KeyPressed: {
      bool* pressed = pressed_slot(result.state, event.payload.key.control);
      if (pressed == nullptr || *pressed) break;
      ControlGroup group = ControlGroup::Agent;
      unsigned char physical_index = 0;
      PhysicalRouteLatch* latch =
          route_latch(result.state, event.payload.key.control);
      if (latch == nullptr ||
          !control_route_address(event.payload.key.control, group,
                                 physical_index)) {
        break;
      }
      const RoutedControl route = route_control(
          result.state.routing, result.state.layer, group, physical_index);
      if (!route.valid) break;
      *latch = {.active = true, .route = route};
      *pressed = true;
      ++result.state.input_event_count;
      set_app_held(result.state, route, true);
      append_effect(result,
                    routed_key_effect(route, event.payload.key.control, 1, 0,
                                      event.enqueued_at_us));
      if (is_agent(event.payload.key.control) ||
          is_command(event.payload.key.control)) {
        append_effect(result, {.type = SideEffectType::PlayKeyDownSound,
                               .control = event.payload.key.control});
      }
      break;
    }
    case EventType::KeyReleased: {
      bool* pressed = pressed_slot(result.state, event.payload.key.control);
      if (pressed == nullptr || !*pressed) break;
      PhysicalRouteLatch* latch =
          route_latch(result.state, event.payload.key.control);
      if (latch == nullptr || !latch->active || !latch->route.valid) break;
      const RoutedControl route = latch->route;
      *pressed = false;
      *latch = PhysicalRouteLatch{};
      ++result.state.input_event_count;
      set_app_held(result.state, route, false);
      append_effect(result,
                    routed_key_effect(route, event.payload.key.control, 0, 0,
                                      event.enqueued_at_us));
      if (is_agent(event.payload.key.control) ||
          is_command(event.payload.key.control)) {
        append_effect(result, {.type = SideEffectType::PlayKeyUpSound,
                               .control = event.payload.key.control});
      }
      break;
    }
    case EventType::JoystickChanged: {
      ++result.state.input_event_count;
      result.state.joystick.angle = normalize_angle(event.payload.joystick.angle);
      result.state.joystick.distance =
          clamp_unit(event.payload.joystick.distance);
      result.state.joystick.captured = result.state.joystick.distance > 0.0F;
      PhysicalRouteLatch& latch = result.state.input_routes.joystick;
      if (!latch.active && result.state.joystick.captured) {
        const RoutedControl route =
            route_control(result.state.routing, result.state.layer,
                          ControlGroup::Joystick, 0);
        if (!route.valid) break;
        latch = {.active = true, .route = route};
        set_app_held(result.state, route, true);
      }
      const RoutedControl route =
          latch.active
              ? latch.route
              : route_control(result.state.routing, result.state.layer,
                              ControlGroup::Joystick, 0);
      if (!route.valid) break;
      if (route.destination == RouteDestination::App) {
        append_effect(result, {
            .type = SideEffectType::SendAppControl,
            .angle = result.state.joystick.angle,
            .distance = result.state.joystick.distance,
            .action = 3,
            .app_group = route.address.group,
            .app_id = route.address.id,
        });
      } else {
        append_effect(result, {.type = SideEffectType::SendJoystick,
                               .angle = result.state.joystick.angle,
                               .distance = result.state.joystick.distance});
      }
      if (!result.state.joystick.captured && latch.active) {
        set_app_held(result.state, latch.route, false);
        latch = PhysicalRouteLatch{};
      }
      break;
    }
    case EventType::EncoderStep: {
      ++result.state.input_event_count;
      result.state.encoder.last_step = event.payload.encoder_step;
      result.state.encoder.accumulated_degrees +=
          static_cast<float>(event.payload.encoder_step) * 15.0F;
      while (result.state.encoder.accumulated_degrees >= 360.0F) {
        result.state.encoder.accumulated_degrees -= 360.0F;
      }
      while (result.state.encoder.accumulated_degrees < 0.0F) {
        result.state.encoder.accumulated_degrees += 360.0F;
      }
      const RoutedControl route =
          route_control(result.state.routing, result.state.layer,
                        ControlGroup::Encoder, 0);
      if (route.valid) {
        append_effect(result, routed_key_effect(
                                  route, ControlId::Encoder, 2,
                                  event.payload.encoder_step,
                                  event.enqueued_at_us));
      }
      break;
    }
    case EventType::AgentLightingChanged:
      if (event.payload.agent_lighting.index < kAgentCount &&
          valid_lighting(event.payload.agent_lighting.lighting)) {
        result.state.agents[event.payload.agent_lighting.index].lighting =
            sanitize_lighting(event.payload.agent_lighting.lighting);
      }
      break;
    case EventType::AppKeyLightingChanged:
      // Stored in the dedicated App lighting cache owned by the main state
      // task; keeping it out of DeviceState avoids copying a multi-kilobyte
      // table for every touch and transport event.
      break;
    case EventType::AmbientLightingChanged:
      if (valid_lighting(event.payload.lighting)) {
        result.state.ambient = sanitize_lighting(event.payload.lighting);
      }
      break;
    case EventType::KeysLightingChanged:
      if (valid_lighting(event.payload.lighting)) {
        result.state.keys_lighting = sanitize_lighting(event.payload.lighting);
      }
      break;
    case EventType::TemporaryLightingChanged:
      if (valid_lighting(event.payload.temporary_lighting.lighting)) {
        result.state.temporary_lighting =
            sanitize_lighting(event.payload.temporary_lighting.lighting);
        result.state.temporary_lighting_until_ms =
            event.payload.temporary_lighting.until_ms;
      }
      break;
    case EventType::LayerNext:
      if (result.state.overlay == Overlay::Pairing) {
        if (result.state.transport == TransportMode::Usb) {
          result.state.transport = TransportMode::Ble;
          result.state.ble_slot = 1;
        } else if (result.state.ble_slot < 3) {
          result.state.transport = TransportMode::Ble;
          ++result.state.ble_slot;
        } else {
          result.state.transport = TransportMode::Usb;
        }
        append_effect(result, {.type = SideEffectType::PlayCapacitiveSound});
        break;
      }
      result.state.layer =
          result.state.app_session.active &&
                  result.state.routing.layer_routing_enabled
              ? next_configured_layer(
                    result.state.layer,
                    result.state.routing.configured_layer_count)
              : next_configured_layer(result.state.layer,
                                      kMaximumLayerCount);
      append_effect(result, {.type = SideEffectType::PlayCapacitiveSound});
      break;
    case EventType::PairingModeRequested:
      if (result.state.overlay == Overlay::Pairing) {
        append_effect(result, {.type = SideEffectType::ClearBonds,
                               .direction = static_cast<signed char>(result.state.ble_slot)});
      } else {
        result.state.overlay = Overlay::Pairing;
      }
      result.state.overlay_until_ms = event.payload.tick_ms + 20000U;
      result.state.ble_indicator = BleIndicator::Pairing;
      result.state.ble_indicator_until_ms = event.payload.tick_ms + 20000U;
      break;
    case EventType::CurrentBleSlotRepairRequested:
      result.state.overlay = Overlay::Pairing;
      result.state.overlay_until_ms = event.payload.tick_ms + 20000U;
      result.state.ble_indicator = BleIndicator::Pairing;
      result.state.ble_indicator_until_ms = event.payload.tick_ms + 20000U;
      append_effect(result, {
          .type = SideEffectType::ClearBonds,
          .direction = static_cast<signed char>(result.state.ble_slot),
      });
      break;
    case EventType::BleSlotChanged:
      if (event.payload.ble_slot >= 1 && event.payload.ble_slot <= 3) {
        if (result.state.ble_slot != event.payload.ble_slot ||
            result.state.transport != TransportMode::Ble) {
          append_all_input_releases(result);
          clear_all_physical_inputs(result.state);
        }
        result.state.ble_slot = event.payload.ble_slot;
        result.state.transport = TransportMode::Ble;
        result.state.usb_connected = false;
        result.state.ble_connected = false;
        result.state.usb_codex_ready = false;
        result.state.ble_codex_ready = false;
        result.state.transport_connected = false;
        result.state.codex_connected = false;
        result.state.ble_indicator = BleIndicator::Pairing;
        result.state.ble_indicator_until_ms = 0;
      }
      break;
    case EventType::BlePeerBonded:
      if (event.payload.ble_peer.slot >= 1 && event.payload.ble_peer.slot <= 3) {
        BlePeerState& peer = result.state.ble_peers[event.payload.ble_peer.slot - 1];
        for (unsigned int index = 0; index < 6; ++index) {
          peer.address[index] = event.payload.ble_peer.address[index];
        }
        peer.address_type = event.payload.ble_peer.address_type;
        peer.bonded = true;
        result.state.ble_indicator = BleIndicator::Connected;
        result.state.ble_indicator_until_ms = 0;
      }
      break;
    case EventType::ConnectionOverlayRequested:
      result.state.overlay = result.state.overlay == Overlay::Connection
                                 ? Overlay::None
                                 : Overlay::Connection;
      break;
    case EventType::TransportConnected:
      if (event.payload.transport_link == TransportLink::Usb) {
        result.state.usb_connected = true;
      } else {
        result.state.ble_connected = true;
      }
      result.state.transport_connected =
          result.state.usb_connected || result.state.ble_connected;
      if (event.payload.transport_link == TransportLink::Ble) {
        result.state.ble_indicator = BleIndicator::Connected;
        result.state.ble_indicator_until_ms = 0;
      }
      break;
    case EventType::TransportDisconnected:
      append_all_input_releases(result);
      if (event.payload.transport_link == TransportLink::Usb) {
        result.state.usb_connected = false;
        result.state.usb_codex_ready = false;
      } else {
        result.state.ble_connected = false;
        result.state.ble_codex_ready = false;
      }
      result.state.transport_connected =
          result.state.usb_connected || result.state.ble_connected;
      result.state.codex_connected =
          result.state.usb_codex_ready || result.state.ble_codex_ready;
      clear_all_physical_inputs(result.state);
      break;
    case EventType::CodexRpcReceived:
      if (event.payload.codex_rpc.link == TransportLink::Usb) {
        result.state.usb_codex_ready = true;
      } else {
        result.state.ble_codex_ready = true;
      }
      result.state.codex_connected = true;
      result.state.last_codex_rpc_ms = event.payload.codex_rpc.monotonic_ms;
      ++result.state.codex_rpc_count;
      if (event.payload.codex_rpc.lighting) {
        ++result.state.lighting_rpc_count;
      }
      break;
    case EventType::AppIconsChanged:
      if (result.state.app_session.active &&
          event.payload.app_icons.layer >= 1 &&
          event.payload.app_icons.layer <= kMaximumLayerCount &&
          (event.payload.app_icons.group == ControlGroup::Agent ||
           event.payload.app_icons.group == ControlGroup::Command)) {
        const unsigned int group =
            event.payload.app_icons.group == ControlGroup::Command ? 1U : 0U;
        for (unsigned int index = 0; index < kPhysicalControlsPerGroup;
             ++index) {
          result.state.app_icon_hashes[event.payload.app_icons.layer - 1U]
                                      [group][index] =
              event.payload.app_icons.hashes[index];
        }
        ++result.state.app_icon_revisions[event.payload.app_icons.layer - 1U]
                                                [group];
      }
      break;
    case EventType::AppSessionConnected:
      if (!result.state.app_session.active) {
        result.state.app_session = {};
        memset(result.state.app_icon_hashes, 0,
               sizeof(result.state.app_icon_hashes));
      }
      result.state.app_session.active = true;
      result.state.app_session.last_heartbeat_ms = event.payload.tick_ms;
      result.state.routing.app_session_active = true;
      break;
    case EventType::AppSessionHeartbeat:
      if (result.state.app_session.active) {
        result.state.app_session.last_heartbeat_ms = event.payload.tick_ms;
      }
      break;
    case EventType::AppSessionDisconnected:
      close_app_session(result);
      break;
    case EventType::RoutingConfigChanged:
      if (result.state.app_session.active &&
          valid_routing_preferences(event.payload.routing_config)) {
        append_all_input_releases(result);
        clear_all_physical_inputs(result.state);
        result.state.routing = sanitize_routing_preferences(
            event.payload.routing_config, true);
        if (result.state.layer > result.state.routing.configured_layer_count) {
          result.state.layer = 1;
        }
      }
      break;
    case EventType::AppControlChanged:
      if (result.state.app_session.active &&
          control_group_is_valid(event.payload.app_control.group) &&
          event.payload.app_control.id < kPrivateControlIdCount) {
        unsigned long long& held =
            result.state.app_session
                .held_masks[control_group_index(
                    event.payload.app_control.group)];
        const unsigned long long bit =
            1ULL << event.payload.app_control.id;
        if (event.payload.app_control.pressed) {
          held |= bit;
        } else {
          held &= ~bit;
        }
      }
      break;
    case EventType::HidTxQueued:
      ++result.state.hid_tx_queued_count;
      break;
    case EventType::HidTxCompleted:
      if (event.payload.success) {
        ++result.state.hid_tx_success_count;
      } else {
        ++result.state.hid_tx_failure_count;
      }
      break;
    case EventType::InputReleaseRequested:
      append_all_input_releases(result);
      clear_all_physical_inputs(result.state);
      break;
    case EventType::EnterStandbyRequested:
      append_all_input_releases(result);
      result.state.power = PowerMode::Screensaver;
      clear_all_physical_inputs(result.state);
      append_effect(result, {.type = SideEffectType::EnterStandby});
      break;
    case EventType::ExitStandbyRequested:
      if (result.state.power == PowerMode::Screensaver) {
        result.state.power = PowerMode::Active;
        append_effect(result, {.type = SideEffectType::ExitStandby});
      }
      break;
    case EventType::EnterUltraStandbyRequested:
      if (result.state.power == PowerMode::Active ||
          result.state.power == PowerMode::Screensaver) {
        append_all_input_releases(result);
        result.state.power = PowerMode::UltraStandby;
        clear_all_physical_inputs(result.state);
        // Ultra retains an authenticated BLE session. Real transport
        // disconnect events remain authoritative for these flags; changing
        // the local power mode must not fabricate a host disconnect.
        append_effect(result, {.type = SideEffectType::EnterUltraStandby});
      }
      break;
    case EventType::ProtectedStandbyRequested:
      append_all_input_releases(result);
      clear_all_physical_inputs(result.state);
      result.state.power = PowerMode::ProtectedBlack;
      result.state.overlay = Overlay::None;
      result.state.protected_unlock_until_ms = 0;
      append_effect(result, {.type = SideEffectType::EnterStandby});
      break;
    case EventType::ProtectedUnlockShown:
      if (result.state.power == PowerMode::ProtectedBlack) {
        result.state.power = PowerMode::ProtectedUnlock;
        result.state.protected_unlock_until_ms =
            event.payload.tick_ms + 3000U;
        append_effect(result, {.type = SideEffectType::ExitStandby});
      }
      break;
    case EventType::ProtectedUnlockCompleted:
      if (result.state.power == PowerMode::ProtectedUnlock) {
        result.state.power = PowerMode::Active;
        result.state.overlay = Overlay::None;
        result.state.protected_unlock_until_ms = 0;
      }
      break;
    case EventType::SuperStandbyRequested:
      append_all_input_releases(result);
      result.state.power = PowerMode::PowerOffPending;
      clear_all_physical_inputs(result.state);
      append_effect(result, {.type = SideEffectType::PowerOff});
      break;
    case EventType::StandbyTransitionCancelled:
      result.state.power = PowerMode::Active;
      result.state.transport_connected =
          event.payload.selection_restore.transport_connected;
      result.state.usb_connected =
          result.state.transport_connected &&
          result.state.transport != TransportMode::Ble;
      result.state.ble_connected =
          result.state.transport_connected &&
          result.state.transport == TransportMode::Ble;
      break;
    case EventType::PowerButtonPressed:
      if (result.state.power == PowerMode::Active) {
        append_all_input_releases(result);
        const bool ultra =
            result.state.power_button_mode == PowerButtonMode::UltraStandby;
        result.state.power =
            ultra ? PowerMode::UltraStandby : PowerMode::Screensaver;
        clear_all_physical_inputs(result.state);
        append_effect(result,
                      {.type = ultra ? SideEffectType::EnterUltraStandby
                                    : SideEffectType::EnterStandby,
                       .direction = 1});
      } else if (result.state.power == PowerMode::Screensaver) {
        result.state.power = PowerMode::Active;
        append_effect(result, {.type = SideEffectType::ExitStandby});
      } else if (result.state.power == PowerMode::UltraStandby) {
        result.state.power = PowerMode::Active;
        append_effect(result, {.type = SideEffectType::ExitUltraStandby});
      } else if (result.state.power == PowerMode::ProtectedBlack) {
        result.state.power = PowerMode::ProtectedUnlock;
        result.state.protected_unlock_until_ms = 3000U;
        append_effect(result, {.type = SideEffectType::ExitStandby});
      }
      break;
    case EventType::TransportModeChanged:
      if (result.state.transport != event.payload.transport_mode) {
        append_all_input_releases(result);
        clear_all_physical_inputs(result.state);
        result.state.transport = event.payload.transport_mode;
        if (result.state.transport == TransportMode::Usb) {
          result.state.ble_connected = false;
          result.state.ble_codex_ready = false;
        } else if (result.state.transport == TransportMode::Ble) {
          result.state.usb_connected = false;
          result.state.usb_codex_ready = false;
        }
        result.state.transport_connected =
            result.state.usb_connected || result.state.ble_connected;
        result.state.codex_connected =
            result.state.usb_codex_ready || result.state.ble_codex_ready;
      }
      break;
    case EventType::BluetoothEnabledChanged:
      if (result.state.bluetooth_enabled != event.payload.success) {
        result.state.bluetooth_enabled = event.payload.success;
        if (!result.state.bluetooth_enabled) {
          result.state.ble_connected = false;
          result.state.ble_codex_ready = false;
          if (result.state.transport == TransportMode::Ble) {
            result.state.transport_connected = false;
          } else {
            result.state.transport_connected = result.state.usb_connected;
          }
          result.state.codex_connected = result.state.usb_codex_ready;
        }
      }
      break;
    case EventType::ProtocolProfileChanged:
      if (static_cast<unsigned char>(event.payload.protocol_profile) <=
          static_cast<unsigned char>(ProtocolProfile::Compatibility)) {
        result.state.protocol_profile = event.payload.protocol_profile;
      }
      break;
    case EventType::SelectionTransitionCancelled:
      result.state.transport = event.payload.selection_restore.transport;
      result.state.ble_slot = event.payload.selection_restore.ble_slot;
      result.state.transport_connected =
          event.payload.selection_restore.transport_connected;
      result.state.usb_connected =
          result.state.transport_connected &&
          result.state.transport != TransportMode::Ble;
      result.state.ble_connected =
          result.state.transport_connected &&
          result.state.transport == TransportMode::Ble;
      result.state.codex_connected = false;
      break;
    case EventType::CommandKeycapChanged:
      if (event.payload.command_text.index < kCommandCount) {
        copy_text(result.state.commands[event.payload.command_text.index].keycap_id,
                  sizeof(result.state.commands[0].keycap_id),
                  event.payload.command_text.text);
      }
      break;
    case EventType::CommandLabelChanged:
      if (event.payload.command_text.index < kCommandCount) {
        copy_text(result.state.commands[event.payload.command_text.index].label,
                  sizeof(result.state.commands[0].label),
                  event.payload.command_text.text);
      }
      break;
    case EventType::SoundSettingsChanged:
      if (static_cast<unsigned char>(event.payload.sound_settings.profile) <= 2 &&
          event.payload.sound_settings.volume >= 10 &&
          event.payload.sound_settings.volume <= 100 &&
          event.payload.sound_settings.volume % 10 == 0) {
        result.state.sound_profile = event.payload.sound_settings.profile;
        result.state.sound_enabled = event.payload.sound_settings.enabled;
        result.state.sound_volume = event.payload.sound_settings.volume;
      }
      break;
    case EventType::DisplaySettingsChanged:
      if (event.payload.display_settings.brightness >= 10 &&
          event.payload.display_settings.brightness <= 100 &&
          event.payload.display_settings.brightness % 10 == 0 &&
          event.payload.display_settings.animation_strength <= 100) {
        result.state.display_brightness =
            event.payload.display_settings.brightness;
        result.state.standby_timeout_seconds =
            event.payload.display_settings.standby_timeout_seconds;
        result.state.animation_strength =
            event.payload.display_settings.animation_strength;
        const unsigned int super_timeout =
            event.payload.display_settings.super_standby_timeout_seconds;
        if (valid_auto_shutdown_timeout(super_timeout)) {
          result.state.super_standby_timeout_seconds = super_timeout;
          result.state.anti_accidental_shutdown =
              event.payload.display_settings.anti_accidental_shutdown;
          result.state.smart_screensaver_enabled =
              event.payload.display_settings.smart_screensaver_enabled;
        }
      }
      break;
    case EventType::PowerSettingsChanged:
      if (static_cast<unsigned char>(
              event.payload.power_settings.button_mode) <=
              static_cast<unsigned char>(PowerButtonMode::UltraStandby) &&
          valid_auto_ultra_timeout(
              event.payload.power_settings.auto_ultra_timeout_seconds)) {
        result.state.power_button_mode =
            event.payload.power_settings.button_mode;
        result.state.auto_ultra_timeout_seconds =
            event.payload.power_settings.auto_ultra_timeout_seconds;
        result.state.ultra_touch_wake =
            event.payload.power_settings.ultra_touch_wake;
      }
      break;
    case EventType::JoystickSensitivityChanged:
      if (event.payload.joystick_sensitivity_percent >= 50 &&
          event.payload.joystick_sensitivity_percent <= 200 &&
          event.payload.joystick_sensitivity_percent % 10 == 0) {
        result.state.joystick_sensitivity_percent =
            event.payload.joystick_sensitivity_percent;
      }
      break;
    case EventType::BatteryChanged:
      {
        const BatteryReading reading = sanitize_battery_reading({
            event.payload.battery.present,
            event.payload.battery.percent,
            event.payload.battery.charging,
            event.payload.battery.usb_power_present,
            event.payload.battery.voltage_mv,
            event.payload.battery.charge_limit_ma,
        });
        if (result.state.battery_initialized &&
            ((result.state.battery_present && !reading.present) ||
             (result.state.battery_present &&
              result.state.battery_percent >= 10 &&
              reading.present && reading.percent < 10))) {
          result.state.battery_warning_visible = true;
          result.state.battery_warning_until_ms =
              event.payload.battery.monotonic_ms + 600000U;
        }
        result.state.battery_initialized = true;
        result.state.battery_present = reading.present;
        result.state.battery_percent = reading.percent;
        result.state.battery_charging = reading.charging;
        result.state.usb_power_present = reading.external_power;
        result.state.battery_voltage_mv = reading.voltage_mv;
        result.state.battery_charge_limit_ma = reading.charge_limit_ma;
      }
      break;
    case EventType::BatteryWarningAcknowledged:
      result.state.battery_warning_visible = false;
      break;
    case EventType::DiagnosticsChanged:
      result.state.event_queue_drops =
          event.payload.diagnostics.event_queue_drops;
      result.state.event_queue_high_water =
          event.payload.diagnostics.event_queue_high_water;
      result.state.rpc_errors = event.payload.diagnostics.rpc_errors;
      result.state.free_heap_bytes =
          event.payload.diagnostics.free_heap_bytes;
      result.state.reset_reason = event.payload.diagnostics.reset_reason;
      result.state.recovery_gate_open =
          event.payload.diagnostics.recovery_gate_open;
      break;
    case EventType::RpcErrorRecorded:
      ++result.state.rpc_errors;
      break;
    case EventType::ClearBondsRequested:
      append_effect(result, {.type = SideEffectType::ClearBonds,
                             .direction = static_cast<signed char>(event.payload.ble_slot)});
      break;
    case EventType::BleBondsCleared:
      if (event.payload.ble_slot >= 1 && event.payload.ble_slot <= 3) {
        result.state.ble_peers[event.payload.ble_slot - 1] = {};
        result.state.ble_indicator = BleIndicator::Pairing;
        result.state.ble_indicator_until_ms = 0;
      } else {
        for (unsigned int slot = 0; slot < 3; ++slot) {
          result.state.ble_peers[slot] = {};
        }
      }
      break;
    case EventType::Tick:
      if (result.state.ble_indicator != BleIndicator::Off &&
          result.state.ble_indicator_until_ms == 0) {
        result.state.ble_indicator_until_ms = event.payload.tick_ms +
            (result.state.ble_indicator == BleIndicator::Connected ? 10000U : 20000U);
      } else if (result.state.ble_indicator_until_ms != 0 &&
                 static_cast<signed int>(event.payload.tick_ms -
                                         result.state.ble_indicator_until_ms) >= 0) {
        result.state.ble_indicator = BleIndicator::Off;
        result.state.ble_indicator_until_ms = 0;
      }
      if (result.state.app_session.active &&
          static_cast<signed int>(
              event.payload.tick_ms -
              result.state.app_session.last_heartbeat_ms) >=
              static_cast<signed int>(kAppLeaseTimeoutMs)) {
        close_app_session(result);
      }
      if (result.state.battery_warning_visible &&
          static_cast<signed int>(
              event.payload.tick_ms - result.state.battery_warning_until_ms) >=
              0) {
        result.state.battery_warning_visible = false;
      }
      if (result.state.power == PowerMode::ProtectedUnlock &&
          result.state.protected_unlock_until_ms != 0 &&
          static_cast<signed int>(
              event.payload.tick_ms -
              result.state.protected_unlock_until_ms) >= 0) {
        result.state.power = PowerMode::ProtectedBlack;
        result.state.protected_unlock_until_ms = 0;
        append_effect(result, {.type = SideEffectType::EnterStandby});
      }
      if (result.state.overlay == Overlay::Pairing &&
          result.state.overlay_until_ms != 0 &&
          static_cast<signed int>(event.payload.tick_ms -
                                  result.state.overlay_until_ms) >= 0) {
        result.state.overlay = Overlay::None;
        result.state.overlay_until_ms = 0;
      }
      break;
  }

  return result;
}

}  // namespace codex
