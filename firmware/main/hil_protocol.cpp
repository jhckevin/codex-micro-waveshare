#include "hil_protocol.h"

#include <stdlib.h>
#include <string.h>

namespace codex {
namespace {

constexpr int kDisplayExtent = 480;
constexpr unsigned int kMaximumReadPixels = 32U * 32U;

HilCommand error_command(const char* message) {
  HilCommand command{};
  command.claimed = true;
  command.error = message;
  return command;
}

bool parse_long(const char*& cursor, long& value) {
  while (*cursor == ' ') ++cursor;
  if (*cursor == '\0') return false;
  char* end{};
  value = strtol(cursor, &end, 0);
  if (end == cursor) return false;
  cursor = end;
  return true;
}

bool parse_unsigned(const char*& cursor, unsigned long& value) {
  while (*cursor == ' ') ++cursor;
  if (*cursor == '\0' || *cursor == '-') return false;
  char* end{};
  value = strtoul(cursor, &end, 0);
  if (end == cursor) return false;
  cursor = end;
  return true;
}

bool parse_float(const char*& cursor, float& value) {
  while (*cursor == ' ') ++cursor;
  if (*cursor == '\0') return false;
  char* end{};
  value = strtof(cursor, &end);
  if (end == cursor) return false;
  cursor = end;
  return true;
}

bool at_end(const char* cursor) {
  while (*cursor == ' ' || *cursor == '\r' || *cursor == '\n') ++cursor;
  return *cursor == '\0';
}

bool consume(const char*& cursor, const char* word) {
  while (*cursor == ' ') ++cursor;
  const unsigned int length = static_cast<unsigned int>(strlen(word));
  if (strncmp(cursor, word, length) != 0) return false;
  const char next = cursor[length];
  if (next != '\0' && next != ' ' && next != '\r' && next != '\n') {
    return false;
  }
  cursor += length;
  return true;
}

bool parse_control(const char*& cursor, ControlId& control) {
  struct Mapping {
    const char* name;
    ControlId control;
  };
  constexpr Mapping mappings[] = {
      {"ag00", ControlId::Agent0},       {"ag01", ControlId::Agent1},
      {"ag02", ControlId::Agent2},       {"ag03", ControlId::Agent3},
      {"ag04", ControlId::Agent4},       {"ag05", ControlId::Agent5},
      {"command0", ControlId::Command0}, {"command1", ControlId::Command1},
      {"command2", ControlId::Command2}, {"command3", ControlId::Command3},
      {"mic", ControlId::Command4},      {"command5", ControlId::Command5},
      {"encoder", ControlId::Encoder},
  };
  for (const Mapping& mapping : mappings) {
    const char* candidate = cursor;
    if (consume(candidate, mapping.name)) {
      cursor = candidate;
      control = mapping.control;
      return true;
    }
  }
  return false;
}

bool parse_group(const char*& cursor, ControlGroup& group) {
  struct Mapping {
    const char* name;
    ControlGroup group;
  };
  constexpr Mapping mappings[] = {
      {"agent", ControlGroup::Agent},
      {"command", ControlGroup::Command},
      {"encoder", ControlGroup::Encoder},
      {"joystick", ControlGroup::Joystick},
  };
  for (const Mapping& mapping : mappings) {
    const char* candidate = cursor;
    if (consume(candidate, mapping.name)) {
      cursor = candidate;
      group = mapping.group;
      return true;
    }
  }
  return false;
}

}  // namespace

HilCommand parse_hil_command(const char* line) {
  if (line == nullptr) return {};
  const char* cursor = line;
  while (*cursor == ' ') ++cursor;
  if (strncmp(cursor, "@hil", 4) != 0 ||
      (cursor[4] != '\0' && cursor[4] != ' ')) {
    return {};
  }
  cursor += 4;

  HilCommand command{};
  command.claimed = true;
  command.valid = true;

  if (consume(cursor, "ping") && at_end(cursor)) {
    command.type = HilCommandType::Ping;
    return command;
  }
  if (consume(cursor, "snapshot") && at_end(cursor)) {
    command.type = HilCommandType::Snapshot;
    return command;
  }
  if (consume(cursor, "metrics") && at_end(cursor)) {
    command.type = HilCommandType::Metrics;
    return command;
  }
  if (consume(cursor, "trace") && at_end(cursor)) {
    command.type = HilCommandType::Trace;
    return command;
  }
  if (consume(cursor, "app")) {
    if (consume(cursor, "connect") && at_end(cursor)) {
      command.type = HilCommandType::AppConnect;
      return command;
    }
    if (consume(cursor, "heartbeat") && at_end(cursor)) {
      command.type = HilCommandType::AppHeartbeat;
      return command;
    }
    if (consume(cursor, "disconnect") && at_end(cursor)) {
      command.type = HilCommandType::AppDisconnect;
      return command;
    }
    return error_command("invalid app session command");
  }
  if (consume(cursor, "transport")) {
    command.type = HilCommandType::SetTransport;
    if (consume(cursor, "auto") && at_end(cursor)) {
      command.transport = TransportMode::Auto;
      return command;
    }
    if (consume(cursor, "usb") && at_end(cursor)) {
      command.transport = TransportMode::Usb;
      return command;
    }
    if (consume(cursor, "ble") && at_end(cursor)) {
      command.transport = TransportMode::Ble;
      return command;
    }
    if (consume(cursor, "mixed") && at_end(cursor)) {
      command.transport = TransportMode::Mixed;
      return command;
    }
    return error_command("invalid transport mode");
  }
  if (consume(cursor, "layer")) {
    long layer{};
    if (!parse_long(cursor, layer) || !at_end(cursor) || layer < 1 ||
        layer > 6) {
      return error_command("invalid layer");
    }
    command.type = HilCommandType::SetLayer;
    command.layer = static_cast<unsigned char>(layer);
    return command;
  }
  if (consume(cursor, "routing")) {
    if (consume(cursor, "snapshot") && at_end(cursor)) {
      command.type = HilCommandType::RoutingSnapshot;
      return command;
    }
    if (consume(cursor, "layers")) {
      long count{};
      if (!parse_long(cursor, count) || !at_end(cursor) || count < 2 ||
          count > 6) {
        return error_command("invalid routing layer count");
      }
      command.type = HilCommandType::SetRoutingLayerCount;
      command.layer_count = static_cast<unsigned char>(count);
      return command;
    }
    if (consume(cursor, "layer1")) {
      long physical_index{}, target_layer{};
      if (!parse_long(cursor, physical_index) ||
          !parse_long(cursor, target_layer) || !at_end(cursor) ||
          physical_index < 0 || physical_index >= 6 ||
          (target_layer != 0 && (target_layer < 2 || target_layer > 6))) {
        return error_command("invalid Layer 1 route");
      }
      command.type = HilCommandType::SetLayer1Route;
      command.physical_index = static_cast<unsigned char>(physical_index);
      command.target_layer = static_cast<unsigned char>(target_layer);
      return command;
    }
    if (consume(cursor, "passthrough")) {
      long layer{};
      unsigned long mask{};
      ControlGroup group{};
      if (!parse_long(cursor, layer) || !parse_group(cursor, group) ||
          !parse_unsigned(cursor, mask) || !at_end(cursor) || layer < 2 ||
          layer > 6) {
        return error_command("invalid passthrough route");
      }
      const unsigned long maximum_mask =
          group == ControlGroup::Agent || group == ControlGroup::Command
              ? 0x3FUL
              : 0x01UL;
      if (mask > maximum_mask) {
        return error_command("passthrough mask out of range");
      }
      command.type = HilCommandType::SetHigherCodexMask;
      command.layer = static_cast<unsigned char>(layer);
      command.group = group;
      command.mask = static_cast<unsigned char>(mask);
      return command;
    }
    return error_command("invalid routing command");
  }
  const char* app_lighting_cursor = cursor;
  if (consume(app_lighting_cursor, "lighting") &&
      consume(app_lighting_cursor, "app")) {
    cursor = app_lighting_cursor;
    ControlGroup group{};
    long id{}, effect{};
    float brightness{}, speed{}, magic{};
    unsigned long color{};
    if (!parse_group(cursor, group) ||
        (group != ControlGroup::Agent && group != ControlGroup::Command) ||
        !parse_long(cursor, id) || !parse_long(cursor, effect) ||
        !parse_float(cursor, brightness) || !parse_float(cursor, speed) ||
        !parse_float(cursor, magic) || !parse_unsigned(cursor, color) ||
        !at_end(cursor) || id < 0 || id > 35 || effect < 0 || effect > 6 ||
        brightness < 0.0F || brightness > 1.0F || speed < 0.0F ||
        speed > 1.0F || magic < 0.0F || magic > 1.0F ||
        color > 0xFFFFFFUL) {
      return error_command("invalid App key lighting");
    }
    command.type = HilCommandType::AppKeyLighting;
    command.group = group;
    command.control_id = static_cast<unsigned char>(id);
    command.lighting = {
        static_cast<LightEffect>(effect),
        brightness,
        speed,
        magic,
        static_cast<unsigned int>(color),
    };
    return command;
  }
  if (consume(cursor, "tap")) {
    long x{}, y{};
    if (!parse_long(cursor, x) || !parse_long(cursor, y) || !at_end(cursor)) {
      return error_command("invalid tap");
    }
    if (x < 0 || x >= kDisplayExtent || y < 0 || y >= kDisplayExtent) {
      return error_command("touch coordinate out of range");
    }
    command.type = HilCommandType::Tap;
    command.touch.x = static_cast<int>(x);
    command.touch.y = static_cast<int>(y);
    return command;
  }
  if (consume(cursor, "touch")) {
    HilTouchPhase phase{};
    if (consume(cursor, "down")) {
      phase = HilTouchPhase::Down;
    } else if (consume(cursor, "move")) {
      phase = HilTouchPhase::Move;
    } else if (consume(cursor, "up")) {
      phase = HilTouchPhase::Up;
    } else if (consume(cursor, "cancel")) {
      phase = HilTouchPhase::Cancel;
    } else {
      return error_command("invalid touch phase");
    }
    long track{}, x{}, y{};
    if (!parse_long(cursor, track) || track < 0 || track > 4) {
      return error_command("invalid touch track");
    }
    if (phase == HilTouchPhase::Down || phase == HilTouchPhase::Move) {
      if (!parse_long(cursor, x) || !parse_long(cursor, y)) {
        return error_command("invalid touch coordinate");
      }
      if (x < 0 || x >= kDisplayExtent || y < 0 || y >= kDisplayExtent) {
        return error_command("touch coordinate out of range");
      }
    }
    if (!at_end(cursor)) return error_command("invalid touch");
    command.type = HilCommandType::Touch;
    command.touch = {
        phase,
        static_cast<unsigned char>(track),
        static_cast<int>(x),
        static_cast<int>(y),
    };
    return command;
  }
  if (consume(cursor, "key")) {
    if (consume(cursor, "down")) {
      command.key_down = true;
    } else if (consume(cursor, "up")) {
      command.key_down = false;
    } else {
      return error_command("invalid key phase");
    }
    if (!parse_control(cursor, command.control) || !at_end(cursor)) {
      return error_command("invalid control");
    }
    command.type = HilCommandType::Key;
    return command;
  }
  if (consume(cursor, "power") && at_end(cursor)) {
    command.type = HilCommandType::PowerButton;
    return command;
  }
  if (consume(cursor, "ultra") && at_end(cursor)) {
    command.type = HilCommandType::UltraStandby;
    return command;
  }
  if (consume(cursor, "wake") && at_end(cursor)) {
    command.type = HilCommandType::UltraWake;
    return command;
  }
  if (consume(cursor, "joystick")) {
    if (!parse_float(cursor, command.angle) ||
        !parse_float(cursor, command.distance) || !at_end(cursor)) {
      return error_command("invalid joystick");
    }
    if (command.angle < 0.0F || command.angle >= 360.0F ||
        command.distance < 0.0F || command.distance > 1.0F) {
      return error_command("joystick out of range");
    }
    command.type = HilCommandType::Joystick;
    return command;
  }
  if (consume(cursor, "encoder")) {
    long step{};
    if (!parse_long(cursor, step) || !at_end(cursor) ||
        (step != -1 && step != 1)) {
      return error_command("invalid encoder step");
    }
    command.type = HilCommandType::Encoder;
    command.encoder_step = static_cast<signed char>(step);
    return command;
  }
  if (consume(cursor, "ble") && consume(cursor, "clear")) {
    long slot{};
    if (!parse_long(cursor, slot) || !at_end(cursor) || slot < 0 ||
        slot > 3) {
      return error_command("invalid BLE slot");
    }
    command.type = HilCommandType::ClearBleBonds;
    command.ble_slot = static_cast<unsigned char>(slot);
    return command;
  }
  if (consume(cursor, "lighting") && consume(cursor, "stress")) {
    if (consume(cursor, "on")) {
      command.enabled = true;
    } else if (consume(cursor, "off")) {
      command.enabled = false;
    } else {
      return error_command("invalid lighting stress state");
    }
    if (!at_end(cursor)) return error_command("invalid lighting stress");
    command.type = HilCommandType::LightingStress;
    return command;
  }
  if (consume(cursor, "screen")) {
    if (consume(cursor, "crc") && at_end(cursor)) {
      command.type = HilCommandType::ScreenCrc;
      return command;
    }
    if (consume(cursor, "read")) {
      long x{}, y{}, width{}, height{};
      if (!parse_long(cursor, x) || !parse_long(cursor, y) ||
          !parse_long(cursor, width) || !parse_long(cursor, height) ||
          !at_end(cursor)) {
        return error_command("invalid screen region");
      }
      if (x < 0 || y < 0 || width <= 0 || height <= 0 ||
          x + width > kDisplayExtent || y + height > kDisplayExtent) {
        return error_command("screen region out of range");
      }
      if (static_cast<unsigned long>(width * height) >
          kMaximumReadPixels) {
        return error_command("screen region too large");
      }
      command.type = HilCommandType::ScreenRead;
      command.region = {
          static_cast<unsigned short>(x),
          static_cast<unsigned short>(y),
          static_cast<unsigned short>(width),
          static_cast<unsigned short>(height),
      };
      return command;
    }
  }
  return error_command("unknown command");
}

}  // namespace codex
