#include "codex/config_protocol.h"
#include "codex/firmware_version.h"

#include "codex/keycap_catalog.h"
#include "codex/persistent_settings.h"
#include "codex/power_policy.h"

namespace codex {
namespace {

bool equals(const char* left, const char* right) {
  unsigned int index = 0;
  while (left[index] && right[index] && left[index] == right[index]) ++index;
  return left[index] == right[index];
}

bool find_token(const char* data, unsigned int length, const char* token,
                unsigned int* position) {
  for (unsigned int start = 0; start < length; ++start) {
    unsigned int index = 0;
    while (token[index] && start + index < length &&
           data[start + index] == token[index]) ++index;
    if (!token[index]) { if (position) *position = start; return true; }
  }
  return false;
}

bool extract_string(const char* data, unsigned int length, const char* key,
                    char* output, unsigned int capacity) {
  unsigned int position = 0;
  if (!find_token(data, length, key, &position)) return false;
  position += 1;
  while (position < length && data[position] != ':') ++position;
  while (position < length && data[position] != '"') ++position;
  if (position >= length) return false;
  ++position;
  unsigned int written = 0;
  while (position < length && data[position] != '"') {
    if (written + 1 >= capacity || data[position] == '\\') return false;
    output[written++] = data[position++];
  }
  if (position >= length) return false;
  output[written] = '\0';
  return true;
}

bool extract_uint(const char* data, unsigned int length, const char* key,
                  unsigned int* output) {
  unsigned int position = 0;
  if (output == nullptr || !find_token(data, length, key, &position)) return false;
  while (position < length && data[position] != ':') ++position;
  if (position >= length) return false;
  ++position;
  while (position < length &&
         (data[position] == ' ' || data[position] == '\t')) ++position;
  if (position >= length || data[position] < '0' || data[position] > '9') {
    return false;
  }
  unsigned int value = 0;
  while (position < length && data[position] >= '0' && data[position] <= '9') {
    const unsigned int digit = static_cast<unsigned int>(data[position] - '0');
    if (value > 429496729U || (value == 429496729U && digit > 5U)) return false;
    value = value * 10U + digit;
    ++position;
  }
  *output = value;
  return true;
}

bool extract_bool(const char* data, unsigned int length, const char* key,
                  bool* output) {
  unsigned int position = 0;
  if (output == nullptr || !find_token(data, length, key, &position)) return false;
  while (position < length && data[position] != ':') ++position;
  if (position >= length) return false;
  ++position;
  while (position < length &&
         (data[position] == ' ' || data[position] == '\t')) ++position;
  if (position + 4 <= length && data[position] == 't' &&
      data[position + 1] == 'r' && data[position + 2] == 'u' &&
      data[position + 3] == 'e') {
    *output = true;
    return true;
  }
  if (position + 5 <= length && data[position] == 'f' &&
      data[position + 1] == 'a' && data[position + 2] == 'l' &&
      data[position + 3] == 's' && data[position + 4] == 'e') {
    *output = false;
    return true;
  }
  return false;
}

void skip_space(const char* data, unsigned int length, unsigned int& position) {
  while (position < length &&
         (data[position] == ' ' || data[position] == '\t' ||
          data[position] == '\r' || data[position] == '\n')) {
    ++position;
  }
}

bool value_position(const char* data, unsigned int length, const char* key,
                    unsigned int& position) {
  if (!find_token(data, length, key, &position)) return false;
  while (position < length && data[position] != ':') ++position;
  if (position >= length) return false;
  ++position;
  skip_space(data, length, position);
  return position < length;
}

bool parse_uint_at(const char* data, unsigned int length,
                   unsigned int& position, unsigned int& value) {
  skip_space(data, length, position);
  if (position >= length || data[position] < '0' || data[position] > '9') {
    return false;
  }
  value = 0;
  while (position < length && data[position] >= '0' &&
         data[position] <= '9') {
    const unsigned int digit =
        static_cast<unsigned int>(data[position] - '0');
    if (value > 429496729U || (value == 429496729U && digit > 5U)) {
      return false;
    }
    value = value * 10U + digit;
    ++position;
  }
  return true;
}

bool extract_unit_float(const char* data, unsigned int length, const char* key,
                        float& output) {
  unsigned int position = 0;
  if (!value_position(data, length, key, position)) return false;
  unsigned int whole = 0;
  if (!parse_uint_at(data, length, position, whole) || whole > 1) return false;
  float value = static_cast<float>(whole);
  if (position < length && data[position] == '.') {
    ++position;
    float place = 0.1F;
    bool digit_seen = false;
    while (position < length && data[position] >= '0' &&
           data[position] <= '9') {
      digit_seen = true;
      value += static_cast<float>(data[position] - '0') * place;
      place *= 0.1F;
      ++position;
    }
    if (!digit_seen) return false;
  }
  if (value < 0.0F || value > 1.0F) return false;
  output = value;
  return true;
}

bool extract_fixed_uint_array(const char* data, unsigned int length,
                              const char* key, unsigned char* output,
                              unsigned int count, unsigned int maximum) {
  unsigned int position = 0;
  if (!value_position(data, length, key, position) ||
      data[position] != '[') {
    return false;
  }
  ++position;
  for (unsigned int index = 0; index < count; ++index) {
    unsigned int value = 0;
    if (!parse_uint_at(data, length, position, value) || value > maximum) {
      return false;
    }
    output[index] = static_cast<unsigned char>(value);
    skip_space(data, length, position);
    if (index + 1 < count) {
      if (position >= length || data[position] != ',') return false;
      ++position;
    }
  }
  skip_space(data, length, position);
  return position < length && data[position] == ']';
}

bool extract_icon_hash_array(const char* data, unsigned int length,
                             unsigned int output[kPhysicalControlsPerGroup]) {
  unsigned int position = 0;
  if (!value_position(data, length, "\"ids\"", position) ||
      data[position] != '[') {
    return false;
  }
  ++position;
  for (unsigned int item = 0; item < kPhysicalControlsPerGroup; ++item) {
    skip_space(data, length, position);
    if (position >= length || data[position] != '"') return false;
    ++position;
    char id[kKeycapIdCapacity]{};
    unsigned int written = 0;
    while (position < length && data[position] != '"') {
      const unsigned char value = static_cast<unsigned char>(data[position++]);
      if (value < 0x21U || value > 0x7EU || value == '\\' ||
          written + 1U >= sizeof(id)) {
        return false;
      }
      id[written++] = static_cast<char>(value);
    }
    if (position >= length || written == 0) return false;
    ++position;
    output[item] = icon_id_hash(id);
    skip_space(data, length, position);
    if (item + 1U < kPhysicalControlsPerGroup) {
      if (position >= length || data[position] != ',') return false;
      ++position;
    }
  }
  skip_space(data, length, position);
  if (position >= length || data[position] != ']') return false;
  ++position;
  skip_space(data, length, position);
  return true;
}

bool extract_routing_matrix(
    const char* data, unsigned int length,
    unsigned char output[kMaximumLayerCount - 1][kControlGroupCount]) {
  unsigned int position = 0;
  if (!value_position(data, length, "\"higher_codex_masks\"", position) ||
      data[position] != '[') {
    return false;
  }
  ++position;
  for (unsigned int layer = 0; layer < kMaximumLayerCount - 1; ++layer) {
    skip_space(data, length, position);
    if (position >= length || data[position] != '[') return false;
    ++position;
    for (unsigned int group = 0; group < kControlGroupCount; ++group) {
      unsigned int value = 0;
      if (!parse_uint_at(data, length, position, value) || value > 0x3FU) {
        return false;
      }
      output[layer][group] = static_cast<unsigned char>(value);
      skip_space(data, length, position);
      if (group + 1 < kControlGroupCount) {
        if (position >= length || data[position] != ',') return false;
        ++position;
      }
    }
    skip_space(data, length, position);
    if (position >= length || data[position] != ']') return false;
    ++position;
    skip_space(data, length, position);
    if (layer + 1 < kMaximumLayerCount - 1) {
      if (position >= length || data[position] != ',') return false;
      ++position;
    }
  }
  skip_space(data, length, position);
  return position < length && data[position] == ']';
}

void set_json(ConfigReply& reply, bool ok, const char* json) {
  reply.ok = ok;
  unsigned int index = 0;
  while (index + 1 < sizeof(reply.json) && json[index]) {
    reply.json[index] = json[index];
    ++index;
  }
  reply.json[index] = '\0';
  reply.length = static_cast<unsigned short>(index);
}

void append(char* output, unsigned int capacity, unsigned int& used,
            const char* text) {
  unsigned int index = 0;
  while (used + 1 < capacity && text[index]) output[used++] = text[index++];
  output[used] = '\0';
}

void append_uint(char* output, unsigned int capacity, unsigned int& used,
                 unsigned int value) {
  char digits[11]{};
  unsigned int count = 0;
  do { digits[count++] = static_cast<char>('0' + value % 10U); value /= 10U; } while (value);
  while (count && used + 1 < capacity) output[used++] = digits[--count];
  output[used] = '\0';
}

ConfigReply error(const char* reason) {
  ConfigReply reply{};
  unsigned int used = 0;
  append(reply.json, sizeof(reply.json), used, "{\"ok\":false,\"error\":\"");
  append(reply.json, sizeof(reply.json), used, reason);
  append(reply.json, sizeof(reply.json), used, "\"}");
  reply.length = static_cast<unsigned short>(used);
  return reply;
}

ConfigReply action_reply(ConfigAction action) {
  ConfigReply reply{};
  reply.action = action;
  set_json(reply, true, "{\"ok\":true}");
  return reply;
}

ConfigReply indexed_text_action(const char* data, unsigned int length,
                                ConfigAction action, unsigned int text_capacity) {
  ConfigReply reply = action_reply(action);
  unsigned int index = 0;
  if (!extract_uint(data, length, "\"index\"", &index) || index >= kCommandCount) {
    return error("invalid_index");
  }
  if (!extract_string(data, length, "\"value\"", reply.text, text_capacity) ||
      reply.text[0] == '\0') {
    return error("invalid_value");
  }
  reply.index = static_cast<unsigned char>(index);
  return reply;
}

}  // namespace

ConfigReply handle_config_line(const char* data, unsigned int length,
                               const DeviceState& state) {
  if (length > kConfigMaxMessage) return error("message_too_large");
  if (data == nullptr || length < 2 || data[0] != '{' || data[length - 1] != '}') {
    return error("invalid_json");
  }
  char method[48]{};
  if (!extract_string(data, length, "\"method\"", method, sizeof(method))) {
    return error("invalid_json");
  }
  ConfigReply reply{};
  if (equals(method, "app.hello")) {
    unsigned int protocol = 0;
    if (!extract_uint(data, length, "\"protocol\"", &protocol) ||
        protocol != 1) {
      return error("unsupported_protocol");
    }
    reply = action_reply(ConfigAction::AppHello);
    set_json(reply, true,
             "{\"ok\":true,\"protocol\":1,\"app_protocol\":1,"
             "\"heartbeat_ms\":500,\"lease_ms\":1500,"
             "\"lease_timeout_ms\":1500}");
    return reply;
  }
  if (equals(method, "app.heartbeat")) {
    return action_reply(ConfigAction::AppHeartbeat);
  }
  if (equals(method, "app.goodbye")) {
    return action_reply(ConfigAction::AppDisconnect);
  }
  if (equals(method, "routing.get")) {
    unsigned int used = 0;
    append(reply.json, sizeof(reply.json), used,
           "{\"ok\":true,\"enabled\":");
    append(reply.json, sizeof(reply.json), used,
           state.routing.layer_routing_enabled ? "true" : "false");
    append(reply.json, sizeof(reply.json), used, ",\"layer_count\":");
    append_uint(reply.json, sizeof(reply.json), used,
                state.routing.configured_layer_count);
    append(reply.json, sizeof(reply.json), used,
           ",\"layer1_command_targets\":[");
    for (unsigned int index = 0; index < kPhysicalControlsPerGroup; ++index) {
      if (index) append(reply.json, sizeof(reply.json), used, ",");
      append_uint(reply.json, sizeof(reply.json), used,
                  state.routing.layer1_command_app_targets[index]);
    }
    append(reply.json, sizeof(reply.json), used,
           "],\"higher_codex_masks\":[");
    for (unsigned int layer = 0; layer < kMaximumLayerCount - 1; ++layer) {
      if (layer) append(reply.json, sizeof(reply.json), used, ",");
      append(reply.json, sizeof(reply.json), used, "[");
      for (unsigned int group = 0; group < kControlGroupCount; ++group) {
        if (group) append(reply.json, sizeof(reply.json), used, ",");
        append_uint(reply.json, sizeof(reply.json), used,
                    state.routing.higher_codex_masks[layer][group]);
      }
      append(reply.json, sizeof(reply.json), used, "]");
    }
    append(reply.json, sizeof(reply.json), used, "]}");
    reply.ok = true;
    reply.length = static_cast<unsigned short>(used);
    return reply;
  }
  if (equals(method, "routing.set")) {
    reply = action_reply(ConfigAction::SetRouting);
    if (!extract_bool(data, length, "\"enabled\"",
                      &reply.routing_config.layer_routing_enabled)) {
      return error("invalid_value");
    }
    unsigned int layer_count = 0;
    if (!extract_uint(data, length, "\"layer_count\"", &layer_count) ||
        layer_count < kMinimumLayerCount ||
        layer_count > kMaximumLayerCount) {
      return error("invalid_value");
    }
    reply.routing_config.configured_layer_count =
        static_cast<unsigned char>(layer_count);
    if (!extract_fixed_uint_array(
            data, length, "\"layer1_command_targets\"",
            reply.routing_config.layer1_command_app_targets.data(),
            kPhysicalControlsPerGroup, kLayer1CommandOverrideTarget)) {
      return error("invalid_value");
    }
    for (unsigned int index = 0; index < kPhysicalControlsPerGroup; ++index) {
      const unsigned char target =
          reply.routing_config.layer1_command_app_targets[index];
      if (target != 0 && target != kLayer1CommandOverrideTarget) {
        return error("invalid_value");
      }
    }
    unsigned char matrix[kMaximumLayerCount - 1][kControlGroupCount]{};
    if (!extract_routing_matrix(data, length, matrix)) {
      return error("invalid_value");
    }
    for (unsigned int layer = 0; layer < kMaximumLayerCount - 1; ++layer) {
      for (unsigned int group = 0; group < kControlGroupCount; ++group) {
        reply.routing_config.higher_codex_masks[layer][group] =
            matrix[layer][group];
      }
    }
    return reply;
  }
  if (equals(method, "icons.set")) {
    reply = action_reply(ConfigAction::SetAppIcons);
    unsigned int layer = 0;
    if (!extract_uint(data, length, "\"layer\"", &layer) ||
        layer < 1 || layer > kMaximumLayerCount ||
        !extract_string(data, length, "\"group\"", reply.text,
                        sizeof(reply.text))) {
      return error("invalid_value");
    }
    if (equals(reply.text, "agent")) {
      reply.control_group = ControlGroup::Agent;
    } else if (equals(reply.text, "command")) {
      reply.control_group = ControlGroup::Command;
    } else {
      return error("invalid_group");
    }
    if (!extract_icon_hash_array(data, length, reply.icon_hashes)) {
      return error("invalid_value");
    }
    reply.secondary_value = layer;
    return reply;
  }
  if (equals(method, "lighting.key.set")) {
    reply = action_reply(ConfigAction::SetAppKeyLighting);
    if (!extract_string(data, length, "\"group\"", reply.text,
                        sizeof(reply.text))) {
      return error("invalid_group");
    }
    if (equals(reply.text, "agent")) {
      reply.control_group = ControlGroup::Agent;
    } else if (equals(reply.text, "command")) {
      reply.control_group = ControlGroup::Command;
    } else {
      return error("invalid_group");
    }
    unsigned int id = 0;
    unsigned int effect = 0;
    unsigned int color = 0;
    if (!extract_uint(data, length, "\"id\"", &id) ||
        id >= kPrivateControlIdCount ||
        !extract_uint(data, length, "\"e\"", &effect) ||
        effect > static_cast<unsigned int>(LightEffect::ShallowBreath) ||
        !extract_uint(data, length, "\"c\"", &color) ||
        color > 0x00FFFFFFU ||
        !extract_unit_float(data, length, "\"b\"",
                            reply.lighting.brightness) ||
        !extract_unit_float(data, length, "\"s\"", reply.lighting.speed) ||
        !extract_unit_float(data, length, "\"m\"", reply.lighting.magic)) {
      return error("invalid_value");
    }
    reply.index = static_cast<unsigned char>(id);
    reply.lighting.effect = static_cast<LightEffect>(effect);
    reply.lighting.color = color;
    return reply;
  }
  if (equals(method, "cfg.hello")) {
    set_json(reply, true,
      "{\"ok\":true,\"protocol\":1,\"max_message\":4096,"
      "\"public_version\":\"" CODEX_PUBLIC_FIRMWARE_VERSION
      "\",\"development_version\":\"waveshare-dev-local\","
      "\"capabilities\":[\"keycaps\",\"labels\",\"sound\",\"transport\","
      "\"display\",\"input\",\"bluetooth_radio\",\"ble_slots\",\"bond_management\","
      "\"diagnostics\"]}");
    return reply;
  }
  if (equals(method, "cfg.get_state")) {
    unsigned int used = 0;
    append(reply.json, sizeof(reply.json), used, "{\"ok\":true,\"layer\":");
    append_uint(reply.json, sizeof(reply.json), used, state.layer);
    append(reply.json, sizeof(reply.json), used, ",\"connected\":");
    append(reply.json, sizeof(reply.json), used,
           state.codex_connected ? "true" : "false");
    append(reply.json, sizeof(reply.json), used, ",\"transport_connected\":");
    append(reply.json, sizeof(reply.json), used,
           state.transport_connected ? "true" : "false");
    append(reply.json, sizeof(reply.json), used, ",\"ble_slot\":");
    append_uint(reply.json, sizeof(reply.json), used, state.ble_slot);
    append(reply.json, sizeof(reply.json), used, ",\"transport\":\"");
    append(reply.json, sizeof(reply.json), used,
           state.transport == TransportMode::Usb ? "usb" :
           state.transport == TransportMode::Ble ? "ble" :
           state.transport == TransportMode::Mixed ? "mixed" : "auto");
    append(reply.json, sizeof(reply.json), used, "\",\"bluetooth_enabled\":");
    append(reply.json, sizeof(reply.json), used,
           state.bluetooth_enabled ? "true" : "false");
    append(reply.json, sizeof(reply.json), used, ",\"sound\":\"");
    append(reply.json, sizeof(reply.json), used,
           state.sound_profile == SoundProfile::Clicky ? "clicky" :
           state.sound_profile == SoundProfile::Tactile ? "tactile" : "linear");
    append(reply.json, sizeof(reply.json), used, "\",\"protocol_profile\":\"");
    append(reply.json, sizeof(reply.json), used,
           state.protocol_profile == ProtocolProfile::Current ? "current" :
           state.protocol_profile == ProtocolProfile::Legacy ? "legacy" :
           state.protocol_profile == ProtocolProfile::Compatibility
               ? "compatibility" : "auto");
    append(reply.json, sizeof(reply.json), used, "\",\"brightness\":");
    append_uint(reply.json, sizeof(reply.json), used, state.display_brightness);
    append(reply.json, sizeof(reply.json), used, ",\"volume\":");
    append_uint(reply.json, sizeof(reply.json), used, state.sound_volume);
    append(reply.json, sizeof(reply.json), used, ",\"sound_enabled\":");
    append(reply.json, sizeof(reply.json), used,
           state.sound_enabled ? "true" : "false");
    append(reply.json, sizeof(reply.json), used, ",\"standby_timeout_seconds\":");
    append_uint(reply.json, sizeof(reply.json), used,
                state.standby_timeout_seconds);
    append(reply.json, sizeof(reply.json), used,
           ",\"screensaver_timeout_seconds\":");
    append_uint(reply.json, sizeof(reply.json), used,
                state.standby_timeout_seconds);
    append(reply.json, sizeof(reply.json), used, ",\"animation_strength\":");
    append_uint(reply.json, sizeof(reply.json), used,
                 state.animation_strength);
    append(reply.json, sizeof(reply.json), used,
           ",\"joystick_sensitivity_percent\":");
    append_uint(reply.json, sizeof(reply.json), used,
                state.joystick_sensitivity_percent);
    append(reply.json, sizeof(reply.json), used,
           ",\"super_standby_timeout_seconds\":");
    append_uint(reply.json, sizeof(reply.json), used,
                state.super_standby_timeout_seconds);
    append(reply.json, sizeof(reply.json), used,
           ",\"auto_shutdown_timeout_seconds\":");
    append_uint(reply.json, sizeof(reply.json), used,
                state.super_standby_timeout_seconds);
    append(reply.json, sizeof(reply.json), used,
           ",\"anti_accidental_shutdown\":");
    append(reply.json, sizeof(reply.json), used,
           state.anti_accidental_shutdown ? "true" : "false");
    append(reply.json, sizeof(reply.json), used,
           ",\"smart_screensaver_enabled\":");
    append(reply.json, sizeof(reply.json), used,
           state.smart_screensaver_enabled ? "true" : "false");
    append(reply.json, sizeof(reply.json), used,
           ",\"power_button_mode\":\"");
    append(reply.json, sizeof(reply.json), used,
           state.power_button_mode == PowerButtonMode::UltraStandby
               ? "ultra_standby"
               : "connected_standby");
    append(reply.json, sizeof(reply.json), used,
           "\",\"auto_ultra_timeout_seconds\":");
    append_uint(reply.json, sizeof(reply.json), used,
                state.auto_ultra_timeout_seconds);
    append(reply.json, sizeof(reply.json), used,
           ",\"ultra_touch_wake\":");
    append(reply.json, sizeof(reply.json), used,
           state.ultra_touch_wake ? "true" : "false");
    append(reply.json, sizeof(reply.json), used, ",\"battery_present\":");
    append(reply.json, sizeof(reply.json), used,
           state.battery_present ? "true" : "false");
    append(reply.json, sizeof(reply.json), used, ",\"battery_percent\":");
    append_uint(reply.json, sizeof(reply.json), used, state.battery_percent);
    append(reply.json, sizeof(reply.json), used, ",\"battery_charging\":");
    append(reply.json, sizeof(reply.json), used,
           state.battery_charging ? "true" : "false");
    append(reply.json, sizeof(reply.json), used,
           ",\"battery_external_power\":");
    append(reply.json, sizeof(reply.json), used,
           state.usb_power_present ? "true" : "false");
    append(reply.json, sizeof(reply.json), used, ",\"battery_voltage_mv\":");
    append_uint(reply.json, sizeof(reply.json), used,
                state.battery_voltage_mv);
    append(reply.json, sizeof(reply.json), used,
           ",\"battery_charge_limit_ma\":");
    append_uint(reply.json, sizeof(reply.json), used,
                state.battery_charge_limit_ma);
    append(reply.json, sizeof(reply.json), used, ",\"commands\":[");
    for (unsigned int index = 0; index < kCommandCount; ++index) {
      if (index) append(reply.json, sizeof(reply.json), used, ",");
      append(reply.json, sizeof(reply.json), used, "{\"keycap\":\"");
      append(reply.json, sizeof(reply.json), used, state.commands[index].keycap_id);
      append(reply.json, sizeof(reply.json), used, "\",\"label\":\"");
      append(reply.json, sizeof(reply.json), used, state.commands[index].label);
      append(reply.json, sizeof(reply.json), used, "\"}");
    }
    append(reply.json, sizeof(reply.json), used, "]}");
    reply.ok = true;
    reply.length = static_cast<unsigned short>(used);
    return reply;
  }
  if (equals(method, "cfg.get_keycaps")) {
    constexpr unsigned int kPageSize = 10;
    unsigned int offset = 0;
    if (find_token(data, length, "\"offset\"", nullptr) &&
        !extract_uint(data, length, "\"offset\"", &offset)) {
      return error("invalid_offset");
    }
    if (offset > keycap_catalog_size()) return error("invalid_offset");
    const unsigned int end =
        offset + kPageSize < keycap_catalog_size()
            ? offset + kPageSize
            : keycap_catalog_size();
    unsigned int used = 0;
    append(reply.json, sizeof(reply.json), used,
           "{\"ok\":true,\"offset\":");
    append_uint(reply.json, sizeof(reply.json), used, offset);
    append(reply.json, sizeof(reply.json), used, ",\"keycaps\":[");
    for (unsigned int index = offset; index < end; ++index) {
      if (index != offset) append(reply.json, sizeof(reply.json), used, ",");
      append(reply.json, sizeof(reply.json), used, "\"");
      append(reply.json, sizeof(reply.json), used, keycap_catalog_id(index));
      append(reply.json, sizeof(reply.json), used, "\"");
    }
    append(reply.json, sizeof(reply.json), used, "],\"next\":");
    if (end < keycap_catalog_size()) {
      append_uint(reply.json, sizeof(reply.json), used, end);
    } else {
      append(reply.json, sizeof(reply.json), used, "null");
    }
    append(reply.json, sizeof(reply.json), used, "}");
    reply.ok = true;
    reply.length = static_cast<unsigned short>(used);
    return reply;
  }
  if (equals(method, "cfg.set_keycap")) {
    return indexed_text_action(data, length, ConfigAction::SetKeycap,
                               kKeycapIdCapacity);
  } else if (equals(method, "cfg.set_label")) {
    return indexed_text_action(data, length, ConfigAction::SetLabel,
                               kCommandLabelCapacity);
  } else if (equals(method, "cfg.set_sound")) {
    reply = action_reply(ConfigAction::SetSound);
    reply.value = static_cast<unsigned int>(state.sound_profile);
    reply.secondary_value = state.sound_volume;
    reply.enabled = state.sound_enabled;
    if (find_token(data, length, "\"value\"", nullptr)) {
      if (!extract_string(data, length, "\"value\"", reply.text,
                          sizeof(reply.text))) return error("invalid_value");
      if (equals(reply.text, "linear")) reply.value = 0;
      else if (equals(reply.text, "tactile")) reply.value = 1;
      else if (equals(reply.text, "clicky")) reply.value = 2;
      else return error("invalid_value");
    }
    if (find_token(data, length, "\"enabled\"", nullptr) &&
        !extract_bool(data, length, "\"enabled\"", &reply.enabled)) {
      return error("invalid_value");
    }
    if (find_token(data, length, "\"volume\"", nullptr) &&
        (!extract_uint(data, length, "\"volume\"",
                       &reply.secondary_value) ||
         reply.secondary_value < 10 || reply.secondary_value > 100 ||
         reply.secondary_value % 10 != 0)) {
      return error("invalid_value");
    }
  } else if (equals(method, "cfg.set_transport")) {
    reply = action_reply(ConfigAction::SetTransport);
    if (!extract_string(data, length, "\"value\"", reply.text, sizeof(reply.text))) {
      return error("invalid_value");
    }
    if (equals(reply.text, "auto")) reply.value = 0;
    else if (equals(reply.text, "usb")) reply.value = 1;
    else if (equals(reply.text, "ble")) reply.value = 2;
    else if (equals(reply.text, "mixed")) reply.value = 3;
    else return error("invalid_value");
  } else if (equals(method, "cfg.set_bluetooth")) {
    reply = action_reply(ConfigAction::SetBluetooth);
    reply.enabled = state.bluetooth_enabled;
    if (!extract_bool(data, length, "\"enabled\"", &reply.enabled)) {
      return error("invalid_value");
    }
  } else if (equals(method, "cfg.set_protocol_profile")) {
    reply = action_reply(ConfigAction::SetProtocolProfile);
    if (!extract_string(data, length, "\"value\"", reply.text,
                        sizeof(reply.text))) {
      return error("invalid_value");
    }
    if (equals(reply.text, "auto")) reply.value = 0;
    else if (equals(reply.text, "current")) reply.value = 1;
    else if (equals(reply.text, "legacy")) reply.value = 2;
    else if (equals(reply.text, "compatibility")) reply.value = 3;
    else return error("invalid_value");
  } else if (equals(method, "cfg.set_display")) {
    reply = action_reply(ConfigAction::SetDisplay);
    reply.value = state.display_brightness;
    reply.secondary_value = state.standby_timeout_seconds;
    reply.tertiary_value = state.animation_strength;
    reply.quaternary_value = state.super_standby_timeout_seconds;
    reply.enabled = state.anti_accidental_shutdown;
    reply.secondary_enabled = state.smart_screensaver_enabled;
    const char* brightness_key = find_token(data, length, "\"brightness\"", nullptr)
                                     ? "\"brightness\"" : "\"value\"";
    if (find_token(data, length, brightness_key, nullptr) &&
        (!extract_uint(data, length, brightness_key, &reply.value) ||
         reply.value < 10 || reply.value > 100 ||
         reply.value % 10 != 0)) return error("invalid_value");
    const char* screensaver_key =
        find_token(data, length, "\"screensaver_timeout_seconds\"", nullptr)
            ? "\"screensaver_timeout_seconds\""
            : "\"standby_timeout_seconds\"";
    if (find_token(data, length, screensaver_key, nullptr) &&
        (!extract_uint(data, length, screensaver_key,
                       &reply.secondary_value) ||
         reply.secondary_value > 86400)) return error("invalid_value");
    if (find_token(data, length, "\"animation_strength\"", nullptr) &&
        (!extract_uint(data, length, "\"animation_strength\"",
                       &reply.tertiary_value) ||
          reply.tertiary_value > 100)) return error("invalid_value");
    const char* shutdown_key =
        find_token(data, length, "\"auto_shutdown_timeout_seconds\"", nullptr)
            ? "\"auto_shutdown_timeout_seconds\""
            : "\"super_standby_timeout_seconds\"";
    if (find_token(data, length, shutdown_key, nullptr) &&
        (!extract_uint(data, length, shutdown_key,
                       &reply.quaternary_value) ||
         (reply.quaternary_value != 0 &&
          reply.quaternary_value != 3600 &&
          reply.quaternary_value != 7200 &&
          reply.quaternary_value != 10800 &&
          reply.quaternary_value != 18000))) {
      return error("invalid_value");
    }
    if (find_token(data, length, "\"smart_screensaver_enabled\"", nullptr) &&
        !extract_bool(data, length, "\"smart_screensaver_enabled\"",
                      &reply.secondary_enabled)) {
      return error("invalid_value");
    }
    if (find_token(data, length, "\"anti_accidental_shutdown\"", nullptr) &&
        !extract_bool(data, length, "\"anti_accidental_shutdown\"",
                      &reply.enabled)) {
      return error("invalid_value");
    }
  } else if (equals(method, "cfg.set_power")) {
    reply = action_reply(ConfigAction::SetPower);
    reply.value = static_cast<unsigned int>(state.power_button_mode);
    reply.secondary_value = state.auto_ultra_timeout_seconds;
    reply.enabled = state.ultra_touch_wake;
    if (find_token(data, length, "\"power_button_mode\"", nullptr)) {
      if (!extract_string(data, length, "\"power_button_mode\"", reply.text,
                          sizeof(reply.text))) {
        return error("invalid_value");
      }
      if (equals(reply.text, "connected_standby")) {
        reply.value =
            static_cast<unsigned int>(PowerButtonMode::ConnectedStandby);
      } else if (equals(reply.text, "ultra_standby")) {
        reply.value = static_cast<unsigned int>(PowerButtonMode::UltraStandby);
      } else {
        return error("invalid_value");
      }
    }
    if (find_token(data, length, "\"auto_ultra_timeout_seconds\"", nullptr) &&
        (!extract_uint(data, length, "\"auto_ultra_timeout_seconds\"",
                       &reply.secondary_value) ||
         !valid_auto_ultra_timeout(reply.secondary_value))) {
      return error("invalid_value");
    }
    if (find_token(data, length, "\"ultra_touch_wake\"", nullptr) &&
        !extract_bool(data, length, "\"ultra_touch_wake\"",
                      &reply.enabled)) {
      return error("invalid_value");
    }
  } else if (equals(method, "cfg.set_input")) {
    reply = action_reply(ConfigAction::SetInput);
    reply.value = state.joystick_sensitivity_percent;
    if (!extract_uint(data, length, "\"joystick_sensitivity_percent\"",
                      &reply.value) ||
        reply.value < 50 || reply.value > 200 ||
        reply.value % 10 != 0) {
      return error("invalid_value");
    }
  } else if (equals(method, "cfg.set_ble_slot")) {
    reply = action_reply(ConfigAction::SetBleSlot);
    if (!extract_uint(data, length, "\"value\"", &reply.value) ||
        reply.value < 1 || reply.value > 3) return error("invalid_slot");
  } else if (equals(method, "cfg.clear_bond")) {
    reply = action_reply(ConfigAction::ClearBond);
    if (find_token(data, length, "\"slot\"", nullptr)) {
      if (!extract_uint(data, length, "\"slot\"", &reply.value) ||
          reply.value < 1 || reply.value > 3) return error("invalid_slot");
    }
  }
  else if (equals(method, "cfg.reboot")) reply = action_reply(ConfigAction::Reboot);
  else if (equals(method, "cfg.get_diagnostics")) {
    unsigned int used = 0;
    append(reply.json, sizeof(reply.json), used,
           "{\"ok\":true,\"recovery_gate\":\"");
    append(reply.json, sizeof(reply.json), used,
           state.recovery_gate_open ? "open" : "locked");
    append(reply.json, sizeof(reply.json), used,
           "\",\"ota\":false,\"rollback\":false,\"settings_schema\":");
    append_uint(reply.json, sizeof(reply.json), used, kSettingsSchemaVersion);
    append(reply.json, sizeof(reply.json), used,
           ",\"codex_connected\":");
    append(reply.json, sizeof(reply.json), used,
           state.codex_connected ? "true" : "false");
    append(reply.json, sizeof(reply.json), used, ",\"codex_rpc_count\":");
    append_uint(reply.json, sizeof(reply.json), used, state.codex_rpc_count);
    append(reply.json, sizeof(reply.json), used, ",\"lighting_rpc_count\":");
    append_uint(reply.json, sizeof(reply.json), used,
                state.lighting_rpc_count);
    append(reply.json, sizeof(reply.json), used, ",\"last_codex_rpc_ms\":");
    append_uint(reply.json, sizeof(reply.json), used, state.last_codex_rpc_ms);
    append(reply.json, sizeof(reply.json), used, ",\"input_event_count\":");
    append_uint(reply.json, sizeof(reply.json), used, state.input_event_count);
    append(reply.json, sizeof(reply.json), used, ",\"hid_tx_queued_count\":");
    append_uint(reply.json, sizeof(reply.json), used, state.hid_tx_queued_count);
    append(reply.json, sizeof(reply.json), used, ",\"hid_tx_success_count\":");
    append_uint(reply.json, sizeof(reply.json), used, state.hid_tx_success_count);
    append(reply.json, sizeof(reply.json), used, ",\"hid_tx_failure_count\":");
    append_uint(reply.json, sizeof(reply.json), used, state.hid_tx_failure_count);
    append(reply.json, sizeof(reply.json), used, ",\"ambient_effect\":");
    append_uint(reply.json, sizeof(reply.json), used,
                static_cast<unsigned int>(state.ambient.effect));
    append(reply.json, sizeof(reply.json), used, ",\"ambient_color\":");
    append_uint(reply.json, sizeof(reply.json), used, state.ambient.color);
    append(reply.json, sizeof(reply.json), used, ",\"event_queue_drops\":");
    append_uint(reply.json, sizeof(reply.json), used, state.event_queue_drops);
    append(reply.json, sizeof(reply.json), used, ",\"event_queue_high_water\":");
    append_uint(reply.json, sizeof(reply.json), used,
                state.event_queue_high_water);
    append(reply.json, sizeof(reply.json), used, ",\"rpc_errors\":");
    append_uint(reply.json, sizeof(reply.json), used, state.rpc_errors);
    append(reply.json, sizeof(reply.json), used, ",\"free_heap_bytes\":");
    append_uint(reply.json, sizeof(reply.json), used, state.free_heap_bytes);
    append(reply.json, sizeof(reply.json), used, ",\"reset_reason\":");
    append_uint(reply.json, sizeof(reply.json), used, state.reset_reason);
    append(reply.json, sizeof(reply.json), used, "}");
    reply.ok = true;
    reply.length = static_cast<unsigned short>(used);
    return reply;
  } else {
    return error("method_not_found");
  }
  return reply;
}

}  // namespace codex
