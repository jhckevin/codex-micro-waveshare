#include "codex/device_reducer.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {

unsigned long failures = 0;

void require(bool condition) {
  failures += condition ? 0UL : 1UL;
}

}  // namespace

extern "C" void mainCRTStartup() {
  using namespace codex;

  {
    const DeviceState initial = make_default_state();
    require(initial.layer == 1);
    require(initial.ambient.effect == LightEffect::Off);
    require(initial.agents[0].lighting.effect == LightEffect::Off);
    require(initial.commands[3].keycap_id[0] == 'C');
    require(initial.commands[3].keycap_id[7] == 'R');
  }

  {
    DeviceState state = reduce(
        make_default_state(), make_transport_connected(TransportLink::Usb)).state;
    state = reduce(state, make_codex_rpc_received(
                              100U, false, TransportLink::Usb)).state;
    require(state.usb_connected && state.usb_codex_ready);
    require(state.codex_connected);

    state = reduce(
        state, make_transport_mode_changed(TransportMode::Mixed)).state;
    require(state.usb_connected && state.usb_codex_ready);
    require(state.transport_connected && state.codex_connected);

    state = reduce(
        state, make_transport_connected(TransportLink::Ble)).state;
    require(state.ble_connected && !state.ble_codex_ready);
    require(state.codex_connected);
    state = reduce(state, make_transport_disconnected(TransportLink::Ble)).state;
    require(!state.ble_connected && state.usb_connected);
    require(state.codex_connected);

    state = reduce(state, make_transport_disconnected(TransportLink::Usb)).state;
    require(!state.transport_connected);
    require(!state.codex_connected);
  }

  {
    const Lighting working{
        .effect = LightEffect::Breath,
        .brightness = 1.4F,
        .speed = 0.4F,
        .magic = 0,
        .color = 0x304FFE,
    };
    const ReduceResult result = reduce(
        make_default_state(), make_agent_lighting_changed(0, working));
    require(result.state.agents[0].lighting.effect == LightEffect::Breath);
    require(result.state.agents[0].lighting.color == 0x304FFE);
    require(result.state.agents[0].lighting.brightness == 1.0F);
    DeviceState retained = result.state;
    const Lighting invalid{.effect = static_cast<LightEffect>(7),
                           .brightness = 0.5F, .color = 0xFF0000};
    retained = reduce(retained, make_agent_lighting_changed(0, invalid)).state;
    require(retained.agents[0].lighting.effect == LightEffect::Breath);
    require(retained.agents[0].lighting.color == 0x304FFE);
  }

  {
    DeviceState state = reduce(make_default_state(),
                               make_key_pressed(ControlId::Agent0)).state;
    state = reduce(state, make_key_pressed(ControlId::Command0)).state;
    state = reduce(state, make_joystick_changed(0.75F, 0.8F)).state;
    const ReduceResult disconnected = reduce(state, make_transport_disconnected());
    require(!disconnected.state.agents[0].pressed);
    require(!disconnected.state.commands[0].pressed);
    require(disconnected.state.joystick.distance == 0.0F);
    require(disconnected.effects[0].release_mask ==
            ((1U << 0U) | (1U << 6U) | (1U << 13U)));
    state = reduce(make_default_state(),
                   make_key_pressed(ControlId::Command4)).state;
    const ReduceResult watchdog =
        reduce(state, make_input_release_requested());
    require(!watchdog.state.commands[4].pressed);
    require(watchdog.effects[0].type == SideEffectType::ReleaseAllInputs);
    require((watchdog.effects[0].release_mask & (1U << 10U)) != 0);
  }

  {
    DeviceState state = make_default_state();
    for (unsigned int i = 0; i < 6; ++i) {
      state = reduce(state, make_layer_next()).state;
    }
    require(state.layer == 1);
  }

  {
    const ReduceResult pressed = reduce(make_default_state(),
                                        make_key_pressed(ControlId::Agent0));
    require(pressed.state.input_event_count == 1);
    require(pressed.effect_count == 2);
    require(pressed.effects[0].type == SideEffectType::SendHid);
    require(pressed.effects[1].type == SideEffectType::PlayKeyDownSound);
    const ReduceResult released =
        reduce(pressed.state, make_key_released(ControlId::Agent0));
    require(released.state.input_event_count == 2);
    require(released.effect_count == 2);
    require(released.effects[0].type == SideEffectType::SendHid);
    require(released.effects[1].type == SideEffectType::PlayKeyUpSound);
  }

  {
    DeviceState state = reduce(make_default_state(), make_hid_tx_queued()).state;
    require(state.hid_tx_queued_count == 1);
    state = reduce(state, make_hid_tx_completed(true)).state;
    require(state.hid_tx_success_count == 1);
    require(state.hid_tx_failure_count == 0);
    state = reduce(state, make_hid_tx_completed(false)).state;
    require(state.hid_tx_success_count == 1);
    require(state.hid_tx_failure_count == 1);
  }

  {
    ReduceResult power = reduce(make_default_state(), make_power_button_pressed());
    require(power.state.power == PowerMode::Screensaver);
    require(power.effects[1].type == SideEffectType::EnterStandby);
    require(power.effects[1].direction == 1);
    power = reduce(power.state, make_power_button_pressed());
    require(power.state.power == PowerMode::Active);
    require(power.effects[0].type == SideEffectType::ExitStandby);
    power = reduce(power.state, make_protected_standby_requested());
    require(power.state.power == PowerMode::ProtectedBlack);
    power = reduce(power.state, make_protected_unlock_shown(1000));
    require(power.state.power == PowerMode::ProtectedUnlock);
    require(power.state.protected_unlock_until_ms == 4000);
    power = reduce(power.state, make_protected_unlock_completed());
    require(power.state.power == PowerMode::Active);
    power = reduce(power.state, make_super_standby_requested());
    require(power.state.power == PowerMode::PowerOffPending);
    require(power.effects[1].type == SideEffectType::PowerOff);
  }

  {
    DeviceState state = reduce(
        make_default_state(),
        make_power_settings_changed(PowerButtonMode::UltraStandby, 10800,
                                    true)).state;
    require(state.power_button_mode == PowerButtonMode::UltraStandby);
    require(state.auto_ultra_timeout_seconds == 10800);
    require(state.ultra_touch_wake);
    state.transport_connected = true;
    state.ble_connected = true;
    state.codex_connected = true;
    ReduceResult power = reduce(state, make_power_button_pressed());
    require(power.state.power == PowerMode::UltraStandby);
    require(power.state.transport_connected);
    require(power.state.ble_connected);
    require(power.state.codex_connected);
    require(power.effects[1].type == SideEffectType::EnterUltraStandby);
    power = reduce(power.state, make_power_button_pressed());
    require(power.state.power == PowerMode::Active);
    require(power.effects[0].type == SideEffectType::ExitUltraStandby);
    power = reduce(state, make_enter_ultra_standby_requested());
    require(power.state.power == PowerMode::UltraStandby);
    require(power.effects[1].type == SideEffectType::EnterUltraStandby);
  }

  {
    DeviceState state = make_default_state();
    require(state.bluetooth_enabled);
    state = reduce(state, make_bluetooth_enabled_changed(false)).state;
    require(!state.bluetooth_enabled);
    require(!state.ble_connected);
    state = reduce(state, make_bluetooth_enabled_changed(true)).state;
    require(state.bluetooth_enabled);
  }

  {
    ReduceResult mode = reduce(make_default_state(),
                               make_transport_mode_changed(TransportMode::Ble));
    require(mode.state.transport == TransportMode::Ble);
    ReduceResult clear = reduce(mode.state, make_clear_bonds_requested());
    require(clear.effect_count == 1);
    require(clear.effects[0].type == SideEffectType::ClearBonds);
    mode.state.ble_peers[0].bonded = true;
    mode.state.ble_peers[1].bonded = true;
    DeviceState cleared = reduce(mode.state, make_ble_bonds_cleared(1)).state;
    require(!cleared.ble_peers[0].bonded && cleared.ble_peers[1].bonded);
    cleared = reduce(cleared, make_ble_bonds_cleared()).state;
    require(!cleared.ble_peers[1].bonded);
  }

  {
    DeviceState connected = reduce(make_default_state(),
                                   make_transport_connected()).state;
    require(connected.transport_connected);
    require(!connected.codex_connected);
    connected = reduce(connected, make_codex_rpc_received(1234, true)).state;
    require(connected.codex_connected);
    require(connected.codex_rpc_count == 1);
    require(connected.lighting_rpc_count == 1);
    require(connected.last_codex_rpc_ms == 1234);
    DeviceState switched = reduce(
        connected, make_transport_mode_changed(TransportMode::Ble)).state;
    require(!switched.transport_connected);
    require(!switched.codex_connected);
    connected = reduce(switched, make_transport_connected()).state;
    DeviceState slot_changed = reduce(connected, make_ble_slot_changed(2)).state;
    require(!slot_changed.transport_connected);
    connected = reduce(slot_changed, make_transport_connected()).state;
    DeviceState standby = reduce(connected, make_enter_standby_requested()).state;
    require(standby.transport_connected);
    ReduceResult cancelled = reduce(
        standby, make_standby_transition_cancelled(true));
    require(cancelled.state.power == PowerMode::Active);
    require(cancelled.state.transport_connected);
    require(cancelled.effect_count == 0);
    cancelled = reduce(
        slot_changed,
        make_selection_transition_cancelled(TransportMode::Auto, 1, true));
    require(cancelled.state.transport == TransportMode::Auto);
    require(cancelled.state.ble_slot == 1);
    require(cancelled.state.transport_connected);
    require(cancelled.effect_count == 0);
  }

  {
    DeviceState state = reduce(make_default_state(), make_encoder_step(1)).state;
    require(state.encoder.accumulated_degrees == 15.0F);
    state = reduce(state, make_encoder_step(-1)).state;
    require(state.encoder.accumulated_degrees == 0.0F);
    ReduceResult pressed =
        reduce(state, make_key_pressed(ControlId::Encoder));
    require(pressed.state.encoder.pressed);
    require(pressed.effects[0].type == SideEffectType::SendHid);
    require(pressed.effects[0].control == ControlId::Encoder);
    require(pressed.effects[0].action == 1);
    ReduceResult released =
        reduce(pressed.state, make_key_released(ControlId::Encoder));
    require(!released.state.encoder.pressed);
    require(released.effects[0].action == 0);
  }

  {
    DeviceState state = reduce(make_default_state(),
                               make_pairing_mode_requested(1000)).state;
    require(state.overlay == Overlay::Pairing);
    require(state.overlay_until_ms == 21000U);
    state = reduce(state, make_layer_next()).state;
    require(state.ble_slot == 2);
    require(state.transport == TransportMode::Ble);
    state.ble_slot = 3;
    state = reduce(state, make_layer_next()).state;
    require(state.transport == TransportMode::Usb);
    state = reduce(state, make_tick(21000)).state;
    require(state.overlay == Overlay::None);
  }

  {
    DeviceState state = make_default_state();
    state.ble_slot = 2;
    const ReduceResult repair = reduce(
        state, make_current_ble_slot_repair_requested(2000));
    require(repair.state.overlay == Overlay::Pairing);
    require(repair.state.overlay_until_ms == 22000U);
    require(repair.effect_count == 1);
    require(repair.effects[0].type == SideEffectType::ClearBonds);
    require(repair.effects[0].direction == 2);
  }

  {
    const unsigned char address[6] = {1, 2, 3, 4, 5, 6};
    DeviceState state = reduce(make_default_state(),
                               make_ble_peer_bonded(2, address, 1)).state;
    require(state.ble_peers[1].bonded);
    require(state.ble_peers[1].address[0] == 1);
    require(state.ble_peers[1].address[5] == 6);
    require(state.ble_peers[1].address_type == 1);
  }

  {
    DeviceState state = reduce(make_default_state(),
                               make_command_keycap_changed(1, "YOLO")).state;
    require(state.commands[1].keycap_id[0] == 'Y');
    state = reduce(
        state,
        make_command_keycap_changed(1, "GIT_PULL_REQUEST_CREATE_ARROW")).state;
    require(state.commands[1].keycap_id[28] == 'W');
    require(state.commands[1].keycap_id[29] == '\0');
    state = reduce(state, make_command_label_changed(1, "Ship it")).state;
    require(state.commands[1].label[5] == 'i');
    state = reduce(state,
                   make_sound_settings_changed(SoundProfile::Clicky, false,
                                               70)).state;
    require(state.sound_profile == SoundProfile::Clicky);
    require(!state.sound_enabled);
    require(state.sound_volume == 70);
    state = reduce(state, make_display_settings_changed(70, 300, 45)).state;
    require(state.smart_screensaver_enabled);
    state = reduce(state, make_display_settings_changed(
        70, 300, 45, 7200, false, false)).state;
    require(!state.smart_screensaver_enabled);
    require(state.display_brightness == 70);
    require(state.standby_timeout_seconds == 300);
    require(state.animation_strength == 45);
    state = reduce(state, make_display_settings_changed(73, 500, 22)).state;
    require(state.display_brightness == 70);
    require(state.standby_timeout_seconds == 300);
    state = reduce(state, make_joystick_sensitivity_changed(150)).state;
    require(state.joystick_sensitivity_percent == 150);
    state = reduce(state, make_joystick_sensitivity_changed(155)).state;
    require(state.joystick_sensitivity_percent == 150);
  state = reduce(state,
                 make_diagnostics_changed(4, 19, 2, 123456, 7, false)).state;
    require(state.event_queue_drops == 4);
    require(state.event_queue_high_water == 19);
  require(state.free_heap_bytes == 123456);
    require(state.reset_reason == 7);
    state = reduce(state, make_rpc_error_recorded()).state;
    require(state.rpc_errors == 3);
  }

  {
    DeviceState state = reduce(
        make_default_state(),
        make_battery_changed(true, 63, true, true, 3912)).state;
    require(state.battery_present);
    require(state.battery_percent == 63);
    require(state.battery_charging);
    require(state.usb_power_present);
    require(state.battery_voltage_mv == 3912);
    state = reduce(state,
                   make_battery_changed(false, 17, true, false, 0)).state;
    require(!state.battery_present);
    require(state.battery_percent == 100);
    require(!state.battery_charging);
    require(state.battery_warning_visible);
    state = reduce(state, make_battery_warning_acknowledged()).state;
    require(!state.battery_warning_visible);
  }

  DeviceState ble_indicator = make_default_state();
  ble_indicator = reduce(ble_indicator, make_ble_slot_changed(2)).state;
  require(ble_indicator.ble_indicator == BleIndicator::Pairing);
  ble_indicator = reduce(ble_indicator, make_tick(100)).state;
  require(ble_indicator.ble_indicator_until_ms == 20100U);
  ble_indicator = reduce(ble_indicator, make_tick(20100)).state;
  require(ble_indicator.ble_indicator == BleIndicator::Off);
  ble_indicator = reduce(ble_indicator,
      make_transport_connected(TransportLink::Ble)).state;
  require(ble_indicator.ble_indicator == BleIndicator::Connected);
  ble_indicator = reduce(ble_indicator, make_tick(30000)).state;
  require(ble_indicator.ble_indicator_until_ms == 40000U);
  ble_indicator = reduce(ble_indicator, make_tick(40000)).state;
  require(ble_indicator.ble_indicator == BleIndicator::Off);

  ExitProcess(failures);
}
