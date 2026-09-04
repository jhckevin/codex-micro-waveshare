#include "codex/compatibility_pack.h"

#include <atomic>

namespace codex {
namespace {

CompatibilitySnapshot snapshots[2]{};
std::atomic_uchar active_snapshot{};

bool same(const std::uint8_t* left, const char* right, std::size_t size) {
  unsigned int difference = 0;
  for (std::size_t index = 0; index < size; ++index) {
    difference |= left[index] ^ static_cast<std::uint8_t>(right[index]);
  }
  return difference == 0;
}
bool zero_range(const std::uint8_t* wire, std::size_t begin,
                std::size_t end) {
  for (std::size_t index = begin; index < end; ++index) {
    if (wire[index] != 0) return false;
  }
  return true;
}
bool method_character(std::uint8_t value) {
  return (value >= 'a' && value <= 'z') ||
         (value >= 'A' && value <= 'Z') ||
         (value >= '0' && value <= '9') || value == '.' || value == '_' ||
         value == '-';
}
bool parse_method(const std::uint8_t* input, char* output) {
  std::size_t length = 0;
  while (length < kCompatibilityMethodCapacity && input[length] != 0) {
    if (!method_character(input[length])) return false;
    output[length] = static_cast<char>(input[length]);
    ++length;
  }
  if (length == 0 || length >= kCompatibilityMethodCapacity) return false;
  output[length] = '\0';
  for (std::size_t index = length + 1;
       index < kCompatibilityMethodCapacity; ++index) {
    if (input[index] != 0) return false;
  }
  return true;
}
bool strings_equal(const char* left, const char* right) {
  std::size_t index = 0;
  while (left[index] && right[index] && left[index] == right[index]) ++index;
  return left[index] == right[index];
}

}  // namespace

CompatibilityPackResult parse_compatibility_pack(
    const std::uint8_t* wire, std::size_t size) {
  CompatibilityPackResult result{};
  if (wire == nullptr || size != kCompatibilityPackWireSize) return result;
  if (!same(wire, "CCP1", 4)) {
    result.error = CompatibilityPackError::WrongMagic;
    return result;
  }
  if (wire[4] != 1) {
    result.error = CompatibilityPackError::UnsupportedSchema;
    return result;
  }
  if (wire[5] > kCompatibilityAliasCount || wire[7] != 0 ||
      !zero_range(wire, 17, 24) || !zero_range(wire, 392, 400)) {
    result.error = wire[5] > kCompatibilityAliasCount
                       ? CompatibilityPackError::InvalidAlias
                       : CompatibilityPackError::InvalidReserved;
    return result;
  }
  const std::uint16_t maximum_rpc_size =
      static_cast<std::uint16_t>(wire[8]) |
      static_cast<std::uint16_t>(wire[9]) << 8U;
  if (wire[6] == 0 || wire[6] > 8 || maximum_rpc_size < 512 ||
      maximum_rpc_size > 4096) {
    result.error = CompatibilityPackError::InvalidPolicy;
    return result;
  }
  for (unsigned int index = 0; index < 7; ++index) {
    if (wire[10 + index] > 6) {
      result.error = CompatibilityPackError::InvalidEffect;
      return result;
    }
    result.snapshot.effect_map[index] = wire[10 + index];
  }
  result.snapshot.alias_count = wire[5];
  result.snapshot.preauth_queue_depth = wire[6];
  result.snapshot.maximum_rpc_size = maximum_rpc_size;
  for (unsigned int index = 0; index < result.snapshot.alias_count; ++index) {
    const std::size_t offset =
        24 + index * kCompatibilityMethodCapacity * 2U;
    if (!parse_method(wire + offset,
                      result.snapshot.aliases[index].incoming) ||
        !parse_method(wire + offset + kCompatibilityMethodCapacity,
                      result.snapshot.aliases[index].canonical)) {
      result.error = CompatibilityPackError::InvalidAlias;
      return result;
    }
  }
  result.error = CompatibilityPackError::None;
  result.ok = true;
  return result;
}

void activate_compatibility_snapshot(const CompatibilitySnapshot& snapshot) {
  const unsigned char next =
      active_snapshot.load(std::memory_order_relaxed) == 0 ? 1 : 0;
  snapshots[next] = snapshot;
  active_snapshot.store(next, std::memory_order_release);
}

CompatibilitySnapshot compatibility_snapshot() {
  return snapshots[active_snapshot.load(std::memory_order_acquire)];
}

std::uint8_t map_compatibility_effect(std::uint8_t effect) {
  if (effect > 6) return effect;
  const unsigned char active =
      active_snapshot.load(std::memory_order_acquire);
  return snapshots[active].effect_map[effect];
}

bool resolve_compatibility_method(const char* incoming, char* canonical,
                                  std::size_t capacity) {
  if (incoming == nullptr || canonical == nullptr || capacity == 0) return false;
  const CompatibilitySnapshot snapshot = compatibility_snapshot();
  const char* selected = incoming;
  for (unsigned int index = 0; index < snapshot.alias_count; ++index) {
    if (strings_equal(incoming, snapshot.aliases[index].incoming)) {
      selected = snapshot.aliases[index].canonical;
      break;
    }
  }
  std::size_t used = 0;
  while (selected[used] && used + 1 < capacity) {
    canonical[used] = selected[used];
    ++used;
  }
  if (selected[used]) return false;
  canonical[used] = '\0';
  return true;
}

}  // namespace codex
