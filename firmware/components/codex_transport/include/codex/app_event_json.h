#pragma once

#include "codex/device_reducer.h"

namespace codex {
namespace app_event_json_detail {

inline bool append(char* output, unsigned int capacity, unsigned int& used,
                   const char* text) {
  for (unsigned int index = 0; text[index] != '\0'; ++index) {
    if (used + 1 >= capacity) return false;
    output[used++] = text[index];
  }
  output[used] = '\0';
  return true;
}

inline bool append_uint(char* output, unsigned int capacity,
                        unsigned int& used, unsigned int value) {
  char digits[10]{};
  unsigned int count = 0;
  do {
    digits[count++] = static_cast<char>('0' + (value % 10U));
    value /= 10U;
  } while (value != 0 && count < sizeof(digits));
  while (count != 0) {
    if (used + 1 >= capacity) return false;
    output[used++] = digits[--count];
  }
  output[used] = '\0';
  return true;
}

inline bool append_int(char* output, unsigned int capacity, unsigned int& used,
                       int value) {
  if (value < 0) {
    if (!append(output, capacity, used, "-")) return false;
    value = -value;
  }
  return append_uint(output, capacity, used,
                     static_cast<unsigned int>(value));
}

inline const char* group_name(ControlGroup group) {
  switch (group) {
    case ControlGroup::Agent:
      return "agent";
    case ControlGroup::Command:
      return "command";
    case ControlGroup::Encoder:
      return "encoder";
    case ControlGroup::Joystick:
      return "joystick";
  }
  return nullptr;
}

inline unsigned int unit_milli(float value) {
  if (value <= 0.0F) return 0;
  if (value >= 1.0F) return 1000;
  return static_cast<unsigned int>(value * 1000.0F + 0.5F);
}

}  // namespace app_event_json_detail

[[nodiscard]] inline unsigned int make_app_input_json(
    char* output, unsigned int capacity, const SideEffect& effect) {
  using namespace app_event_json_detail;
  if (output == nullptr || capacity == 0 ||
      effect.type != SideEffectType::SendAppControl ||
      effect.app_id >= kPrivateControlIdCount) {
    return 0;
  }
  const char* group = group_name(effect.app_group);
  if (group == nullptr) return 0;

  unsigned int used = 0;
  if (!append(output, capacity, used,
              "{\"event\":\"input\",\"group\":\"") ||
      !append(output, capacity, used, group) ||
      !append(output, capacity, used, "\",\"id\":") ||
      !append_uint(output, capacity, used, effect.app_id) ||
      !append(output, capacity, used, ",\"action\":") ||
      !append_uint(output, capacity, used, effect.action)) {
    return 0;
  }
  if (effect.action == 2) {
    if (!append(output, capacity, used, ",\"direction\":") ||
        !append_int(output, capacity, used, effect.direction)) {
      return 0;
    }
  } else if (effect.action == 3) {
    if (!append(output, capacity, used, ",\"angle_milli\":") ||
        !append_uint(output, capacity, used, unit_milli(effect.angle)) ||
        !append(output, capacity, used, ",\"distance_milli\":") ||
        !append_uint(output, capacity, used, unit_milli(effect.distance))) {
      return 0;
    }
  }
  if (!append(output, capacity, used, "}")) return 0;
  return used;
}

}  // namespace codex
