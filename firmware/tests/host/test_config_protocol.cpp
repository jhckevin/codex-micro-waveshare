#include "codex/config_protocol.h"
#include "codex/device_reducer.h"
#include "codex/keycap_catalog.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures = 0;
void require(bool value) { failures += value ? 0UL : 1UL; }
bool contains(const char* text, const char* needle) {
  for (unsigned int i = 0; text[i]; ++i) {
    unsigned int j = 0;
    while (needle[j] && text[i + j] == needle[j]) ++j;
    if (!needle[j]) return true;
  }
  return false;
}
unsigned int length(const char* text) { unsigned int n = 0; while (text[n]) ++n; return n; }
}

extern "C" void mainCRTStartup() {
  using namespace codex;
  DeviceState state = make_default_state();
  state.battery_present = true;
  state.battery_percent = 64;
  state.battery_charging = true;
  state.usb_power_present = true;
  state.battery_voltage_mv = 3920;
  state.battery_charge_limit_ma = 200;
  const char hello[] = "{\"id\":1,\"method\":\"cfg.hello\"}";
  ConfigReply reply = handle_config_line(hello, length(hello), state);
  require(reply.ok);
  require(contains(reply.json, "\"protocol\":1"));
  require(contains(reply.json, "\"public_version\":\"1.0.1.1\""));
  require(contains(reply.json, "\"max_message\":4096"));
  const char get_state[] = "{\"method\":\"cfg.get_state\"}";
  reply = handle_config_line(get_state, length(get_state), state);
  require(reply.ok && contains(reply.json, "\"commands\":["));
  require(contains(reply.json, "\"brightness\":80"));
  require(contains(reply.json, "\"volume\":100"));
  require(contains(reply.json, "\"connected\":false"));
  require(contains(reply.json, "\"transport_connected\":false"));
  require(contains(reply.json, "\"ble_slot\":1"));
  require(contains(reply.json, "\"animation_strength\":100"));
  require(contains(reply.json, "\"joystick_sensitivity_percent\":100"));
  require(contains(reply.json, "\"battery_present\":true"));
  require(contains(reply.json, "\"battery_percent\":64"));
  require(contains(reply.json, "\"battery_charging\":true"));
  require(contains(reply.json, "\"battery_external_power\":true"));
  require(contains(reply.json, "\"battery_voltage_mv\":3920"));
  require(contains(reply.json, "\"battery_charge_limit_ma\":200"));
  require(contains(reply.json, "\"super_standby_timeout_seconds\":7200"));
  require(contains(reply.json, "\"screensaver_timeout_seconds\":180"));
  require(contains(reply.json, "\"auto_shutdown_timeout_seconds\":7200"));
  require(contains(reply.json, "\"anti_accidental_shutdown\":false"));
  require(contains(reply.json, "\"power_button_mode\":\"connected_standby\""));
  require(contains(reply.json, "\"auto_ultra_timeout_seconds\":0"));
  require(contains(reply.json, "\"ultra_touch_wake\":false"));
  require(contains(reply.json, "\"protocol_profile\":\"auto\""));

  const char app_hello[] =
      "{\"method\":\"app.hello\",\"protocol\":1,\"ignored\":\"future\"}";
  reply = handle_config_line(app_hello, length(app_hello), state);
  require(reply.ok && reply.action == ConfigAction::AppHello);
  require(contains(reply.json, "\"protocol\":1"));
  require(contains(reply.json, "\"app_protocol\":1"));
  require(contains(reply.json, "\"heartbeat_ms\":500"));
  require(contains(reply.json, "\"lease_ms\":1500"));
  const char bad_app_hello[] =
      "{\"method\":\"app.hello\",\"protocol\":2}";
  reply = handle_config_line(bad_app_hello, length(bad_app_hello), state);
  require(!reply.ok && contains(reply.json, "unsupported_protocol"));

  const char heartbeat[] = "{\"method\":\"app.heartbeat\"}";
  reply = handle_config_line(heartbeat, length(heartbeat), state);
  require(reply.ok && reply.action == ConfigAction::AppHeartbeat);
  const char goodbye[] = "{\"method\":\"app.goodbye\"}";
  reply = handle_config_line(goodbye, length(goodbye), state);
  require(reply.ok && reply.action == ConfigAction::AppDisconnect);

  DeviceState routed_state = state;
  routed_state.routing.layer_routing_enabled = true;
  routed_state.routing.configured_layer_count = 3;
  routed_state.routing.layer1_command_app_targets[4] =
      kLayer1CommandOverrideTarget;
  routed_state.routing.higher_codex_masks[0][0] = 3;
  const char routing_get[] = "{\"method\":\"routing.get\"}";
  reply =
      handle_config_line(routing_get, length(routing_get), routed_state);
  require(reply.ok);
  require(contains(reply.json, "\"enabled\":true"));
  require(contains(reply.json, "\"layer_count\":3"));
  require(contains(reply.json, "\"layer1_command_targets\":[0,0,0,0,7,0]"));
  require(contains(reply.json, "\"higher_codex_masks\":[[3,0,0,0]"));

  const char routing_set[] =
      "{\"method\":\"routing.set\",\"enabled\":true,\"layer_count\":3,"
      "\"layer1_command_targets\":[0,0,0,0,7,0],"
      "\"higher_codex_masks\":[[1,2,3,4],[5,6,7,8],[9,10,11,12],"
      "[13,14,15,16],[17,18,19,20]],\"future\":17}";
  reply = handle_config_line(routing_set, length(routing_set), state);
  require(reply.ok && reply.action == ConfigAction::SetRouting);
  require(reply.routing_config.layer_routing_enabled);
  require(reply.routing_config.configured_layer_count == 3);
  require(reply.routing_config.layer1_command_app_targets[4] ==
          kLayer1CommandOverrideTarget);
  require(reply.routing_config.higher_codex_masks[4][3] == 20);

  const char icon_set[] =
      "{\"method\":\"icons.set\",\"layer\":2,\"group\":\"command\","
      "\"ids\":[\"FAST\",\"APPR\",\"REJ\",\"COMPUTER\","
      "\"custom-mic\",\"__blank\"]}";
  reply = handle_config_line(icon_set, length(icon_set), state);
  require(reply.ok && reply.action == ConfigAction::SetAppIcons);
  require(reply.secondary_value == 2);
  require(reply.control_group == ControlGroup::Command);
  require(reply.icon_hashes[0] == icon_id_hash("FAST"));
  require(reply.icon_hashes[4] == icon_id_hash("custom-mic"));
  require(reply.icon_hashes[5] == icon_id_hash("__blank"));
  const char extended_icon_set[] =
      "{\"method\":\"icons.set\",\"layer\":1,\"group\":\"agent\","
      "\"ids\":[\"A\",\"B\",\"C\",\"D\",\"E\",\"F\"],\"future\":true}";
  reply = handle_config_line(
      extended_icon_set, length(extended_icon_set), state);
  require(reply.ok && reply.action == ConfigAction::SetAppIcons);
  const char bad_icon_layer[] =
      "{\"method\":\"icons.set\",\"layer\":7,\"group\":\"agent\","
      "\"ids\":[\"a\",\"b\",\"c\",\"d\",\"e\",\"f\"]}";
  reply = handle_config_line(
      bad_icon_layer, length(bad_icon_layer), state);
  require(!reply.ok && contains(reply.json, "invalid_value"));
  const char bad_icon_count[] =
      "{\"method\":\"icons.set\",\"layer\":1,\"group\":\"agent\","
      "\"ids\":[\"a\",\"b\",\"c\",\"d\",\"e\"]}";
  reply = handle_config_line(
      bad_icon_count, length(bad_icon_count), state);
  require(!reply.ok && contains(reply.json, "invalid_value"));

  const char bad_routing_target[] =
      "{\"method\":\"routing.set\",\"enabled\":true,\"layer_count\":3,"
      "\"layer1_command_targets\":[1,0,0,0,0,0],"
      "\"higher_codex_masks\":[[0,0,0,0],[0,0,0,0],[0,0,0,0],"
      "[0,0,0,0],[0,0,0,0]]}";
  reply = handle_config_line(
      bad_routing_target, length(bad_routing_target), state);
  require(!reply.ok && contains(reply.json, "invalid_value"));

  const char bad_routing_mask[] =
      "{\"method\":\"routing.set\",\"enabled\":true,\"layer_count\":3,"
      "\"layer1_command_targets\":[0,0,0,0,0,0],"
      "\"higher_codex_masks\":[[64,0,0,0],[0,0,0,0],[0,0,0,0],"
      "[0,0,0,0],[0,0,0,0]]}";
  reply =
      handle_config_line(bad_routing_mask, length(bad_routing_mask), state);
  require(!reply.ok && contains(reply.json, "invalid_value"));

  const char key_light[] =
      "{\"method\":\"lighting.key.set\",\"group\":\"agent\",\"id\":7,"
      "\"e\":4,\"b\":0,\"s\":1,\"m\":0,\"c\":3166206}";
  reply = handle_config_line(key_light, length(key_light), state);
  require(reply.ok && reply.action == ConfigAction::SetAppKeyLighting);
  require(reply.control_group == ControlGroup::Agent && reply.index == 7);
  require(reply.lighting.effect == LightEffect::Breath);
  require(reply.lighting.brightness == 0.0F);
  require(reply.lighting.speed == 1.0F);
  require(reply.lighting.color == 3166206);
  const char shadow_key_light[] =
      "{\"method\":\"lighting.key.set\",\"group\":\"command\",\"id\":40,"
      "\"e\":1,\"b\":1,\"s\":0,\"m\":0,\"c\":16777215}";
  reply = handle_config_line(
      shadow_key_light, length(shadow_key_light), state);
  require(reply.ok && reply.action == ConfigAction::SetAppKeyLighting);
  require(reply.control_group == ControlGroup::Command && reply.index == 40);
  const char invalid_shadow_key_light[] =
      "{\"method\":\"lighting.key.set\",\"group\":\"command\",\"id\":42,"
      "\"e\":1,\"b\":1,\"s\":0,\"m\":0,\"c\":16777215}";
  reply = handle_config_line(
      invalid_shadow_key_light, length(invalid_shadow_key_light), state);
  require(!reply.ok && contains(reply.json, "invalid_value"));
  const char ambient_write[] =
      "{\"method\":\"lighting.key.set\",\"group\":\"ambient\",\"id\":0,"
      "\"e\":2,\"b\":1,\"s\":1,\"m\":0,\"c\":3166206}";
  reply = handle_config_line(ambient_write, length(ambient_write), state);
  require(!reply.ok && contains(reply.json, "invalid_group"));
  const char get_keycaps[] = "{\"method\":\"cfg.get_keycaps\"}";
  reply = handle_config_line(get_keycaps, length(get_keycaps), state);
  require(reply.ok);
  require(contains(reply.json, "\"COMPUTER\""));
  require(contains(reply.json, "\"next\":10"));
  const char get_keycaps_second[] =
      "{\"method\":\"cfg.get_keycaps\",\"offset\":10}";
  reply = handle_config_line(
      get_keycaps_second, length(get_keycaps_second), state);
  require(reply.ok);
  require(contains(reply.json, "\"FAVOURITE\""));
  require(contains(reply.json, "\"GIT_FORK\""));
  const char get_keycaps_third[] =
      "{\"method\":\"cfg.get_keycaps\",\"offset\":20}";
  reply = handle_config_line(
      get_keycaps_third, length(get_keycaps_third), state);
  require(reply.ok);
  require(contains(reply.json, "\"GIT_PULL_REQUEST_CREATE_ARROW\""));
  bool found_message_icon = false;
  for (unsigned int index = 0; index < keycap_catalog_size(); ++index) {
    if (contains(keycap_catalog_id(index), "MESSAGE_CIRCLE_PLUS")) {
      found_message_icon = true;
    }
  }
  require(found_message_icon);

  const char keycap[] =
      "{\"method\":\"cfg.set_keycap\",\"index\":4,\"value\":\"yolo\"}";
  reply = handle_config_line(keycap, length(keycap), state);
  require(reply.ok && reply.action == ConfigAction::SetKeycap);
  require(reply.index == 4 && reply.text[0] == 'y' && reply.text[3] == 'o');
  const char long_keycap[] =
      "{\"method\":\"cfg.set_keycap\",\"index\":2,"
      "\"value\":\"GIT_PULL_REQUEST_CREATE_ARROW\"}";
  reply = handle_config_line(long_keycap, length(long_keycap), state);
  require(reply.ok && reply.action == ConfigAction::SetKeycap);
  require(reply.text[28] == 'W' && reply.text[29] == '\0');

  const char sound[] =
      "{\"method\":\"cfg.set_sound\",\"value\":\"clicky\"}";
  reply = handle_config_line(sound, length(sound), state);
  require(reply.ok && reply.action == ConfigAction::SetSound && reply.value == 2);
  const char muted[] =
      "{\"method\":\"cfg.set_sound\",\"enabled\":false}";
  reply = handle_config_line(muted, length(muted), state);
  require(reply.ok && reply.action == ConfigAction::SetSound && !reply.enabled);
  const char volume[] =
      "{\"method\":\"cfg.set_sound\",\"value\":\"tactile\",\"volume\":70}";
  reply = handle_config_line(volume, length(volume), state);
  require(reply.ok && reply.value == 1 && reply.secondary_value == 70);
  const char invalid_volume[] =
      "{\"method\":\"cfg.set_sound\",\"volume\":75}";
  reply = handle_config_line(invalid_volume, length(invalid_volume), state);
  require(!reply.ok && contains(reply.json, "invalid_value"));

  const char display[] =
      "{\"method\":\"cfg.set_display\",\"value\":73}";
  reply = handle_config_line(display, length(display), state);
  require(!reply.ok && contains(reply.json, "invalid_value"));
  const char display_full[] =
      "{\"method\":\"cfg.set_display\",\"brightness\":70,"
      "\"standby_timeout_seconds\":300,\"animation_strength\":55,"
      "\"super_standby_timeout_seconds\":18000,"
      "\"anti_accidental_shutdown\":true}";
  reply = handle_config_line(display_full, length(display_full), state);
  require(reply.ok && reply.value == 70 && reply.secondary_value == 300 &&
          reply.tertiary_value == 55 &&
          reply.quaternary_value == 18000 && reply.enabled);
  const char input[] =
      "{\"method\":\"cfg.set_input\",\"joystick_sensitivity_percent\":150}";
  reply = handle_config_line(input, length(input), state);
  require(reply.ok && reply.action == ConfigAction::SetInput &&
          reply.value == 150);
  const char invalid_input[] =
      "{\"method\":\"cfg.set_input\",\"joystick_sensitivity_percent\":155}";
  reply = handle_config_line(invalid_input, length(invalid_input), state);
  require(!reply.ok && contains(reply.json, "invalid_value"));
  const char power[] =
      "{\"method\":\"cfg.set_power\","
      "\"power_button_mode\":\"ultra_standby\","
      "\"auto_ultra_timeout_seconds\":10800,"
      "\"ultra_touch_wake\":true}";
  reply = handle_config_line(power, length(power), state);
  require(reply.ok && reply.action == ConfigAction::SetPower);
  require(reply.value ==
          static_cast<unsigned int>(PowerButtonMode::UltraStandby));
  require(reply.secondary_value == 10800 && reply.enabled);
  const char invalid_ultra[] =
      "{\"method\":\"cfg.set_power\","
      "\"auto_ultra_timeout_seconds\":7200}";
  reply = handle_config_line(invalid_ultra, length(invalid_ultra), state);
  require(!reply.ok && contains(reply.json, "invalid_value"));

  DeviceState diagnostics_state = state;
  diagnostics_state.event_queue_drops = 9;
  diagnostics_state.free_heap_bytes = 4242;
  diagnostics_state.codex_connected = true;
  diagnostics_state.codex_rpc_count = 12;
  diagnostics_state.lighting_rpc_count = 4;
  diagnostics_state.ambient.effect = LightEffect::Snake;
  diagnostics_state.ambient.color = 0x304FFE;
  const char diagnostics[] = "{\"method\":\"cfg.get_diagnostics\"}";
  reply = handle_config_line(diagnostics, length(diagnostics), diagnostics_state);
  require(reply.ok && contains(reply.json, "\"event_queue_drops\":9"));
  require(contains(reply.json, "\"free_heap_bytes\":4242"));
  require(contains(reply.json, "\"settings_schema\":13"));
  require(contains(reply.json, "\"codex_connected\":true"));
  require(contains(reply.json, "\"codex_rpc_count\":12"));
  require(contains(reply.json, "\"lighting_rpc_count\":4"));
  require(contains(reply.json, "\"ambient_effect\":2"));
  require(contains(reply.json, "\"ambient_color\":3166206"));

  const char slot[] = "{\"method\":\"cfg.set_ble_slot\",\"value\":3}";
  reply = handle_config_line(slot, length(slot), state);
  require(reply.ok && reply.action == ConfigAction::SetBleSlot && reply.value == 3);

  const char invalid_index[] =
      "{\"method\":\"cfg.set_label\",\"index\":6,\"value\":\"bad\"}";
  reply = handle_config_line(invalid_index, length(invalid_index), state);
  require(!reply.ok && contains(reply.json, "invalid_index"));

  const char invalid_transport[] =
      "{\"method\":\"cfg.set_transport\",\"value\":\"wifi\"}";
  reply = handle_config_line(invalid_transport, length(invalid_transport), state);
  require(!reply.ok && contains(reply.json, "invalid_value"));
  const char mixed_transport[] =
      "{\"method\":\"cfg.set_transport\",\"value\":\"mixed\"}";
  reply = handle_config_line(mixed_transport, length(mixed_transport), state);
  require(reply.ok && reply.action == ConfigAction::SetTransport);
  require(reply.value == static_cast<unsigned int>(TransportMode::Mixed));
  const char bluetooth_off[] =
      "{\"method\":\"cfg.set_bluetooth\",\"enabled\":false}";
  reply = handle_config_line(bluetooth_off, length(bluetooth_off), state);
  require(reply.ok && reply.action == ConfigAction::SetBluetooth &&
          !reply.enabled);

  const char protocol_auto[] =
      "{\"method\":\"cfg.set_protocol_profile\",\"value\":\"auto\"}";
  reply = handle_config_line(protocol_auto, length(protocol_auto), state);
  require(reply.ok &&
          reply.action == ConfigAction::SetProtocolProfile &&
          reply.value == static_cast<unsigned int>(ProtocolProfile::Auto));
  const char protocol_current[] =
      "{\"method\":\"cfg.set_protocol_profile\",\"value\":\"current\"}";
  reply = handle_config_line(protocol_current, length(protocol_current), state);
  require(reply.ok &&
          reply.action == ConfigAction::SetProtocolProfile &&
          reply.value == static_cast<unsigned int>(ProtocolProfile::Current));
  const char protocol_invalid[] =
      "{\"method\":\"cfg.set_protocol_profile\",\"value\":\"future\"}";
  reply = handle_config_line(protocol_invalid, length(protocol_invalid), state);
  require(!reply.ok && contains(reply.json, "invalid_value"));

  char oversized[kConfigMaxMessage + 2]{};
  reply = handle_config_line(oversized, kConfigMaxMessage + 1, state);
  require(!reply.ok && contains(reply.json, "message_too_large"));
  const char invalid[] = "not-json";
  reply = handle_config_line(invalid, length(invalid), state);
  require(!reply.ok && contains(reply.json, "invalid_json"));
  const char ota[] = "{\"method\":\"ota.start\"}";
  reply = handle_config_line(ota, length(ota), state);
  require(!reply.ok && contains(reply.json, "method_not_found"));
  ExitProcess(failures);
}
