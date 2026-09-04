#pragma once

#include <cstddef>
#include <cstdint>

namespace codex {

constexpr std::size_t kCompatibilityPackWireSize = 400;
constexpr unsigned int kCompatibilityAliasCount = 8;
constexpr unsigned int kCompatibilityMethodCapacity = 23;

struct CompatibilityAlias {
  char incoming[kCompatibilityMethodCapacity]{};
  char canonical[kCompatibilityMethodCapacity]{};
};

struct CompatibilitySnapshot {
  std::uint8_t effect_map[7]{0, 1, 2, 3, 4, 5, 6};
  std::uint8_t preauth_queue_depth{4};
  std::uint16_t maximum_rpc_size{4096};
  std::uint8_t alias_count{};
  CompatibilityAlias aliases[kCompatibilityAliasCount]{};
};

enum class CompatibilityPackError : std::uint8_t {
  None,
  WrongSize,
  WrongMagic,
  UnsupportedSchema,
  InvalidReserved,
  InvalidPolicy,
  InvalidEffect,
  InvalidAlias,
};

struct CompatibilityPackResult {
  CompatibilitySnapshot snapshot{};
  CompatibilityPackError error{CompatibilityPackError::WrongSize};
  bool ok{};
};

[[nodiscard]] CompatibilityPackResult parse_compatibility_pack(
    const std::uint8_t* wire, std::size_t size);
void activate_compatibility_snapshot(const CompatibilitySnapshot& snapshot);
[[nodiscard]] CompatibilitySnapshot compatibility_snapshot();
[[nodiscard]] std::uint8_t map_compatibility_effect(std::uint8_t effect);
[[nodiscard]] bool resolve_compatibility_method(const char* incoming,
                                                char* canonical,
                                                std::size_t capacity);

}  // namespace codex
