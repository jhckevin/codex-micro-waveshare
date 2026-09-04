#pragma once

#include <cstddef>
#include <cstdint>

namespace codex {

constexpr std::size_t kUserContentHeaderSize = 16;
constexpr std::size_t kUserContentEntrySize = 40;
constexpr std::size_t kUserContentIdCapacity = 32;
constexpr std::size_t kUserContentIconWidth = 48;
constexpr std::size_t kUserContentIconHeight = 48;
constexpr std::size_t kUserContentIconBytes =
    kUserContentIconWidth * kUserContentIconHeight;
constexpr std::size_t kUserContentMaximumIcons = 84;
// The two transactional content partitions are each exactly 0x180000 bytes.
// Keep the protocol limit identical so a package accepted by content.hello can
// always fit in either inactive slot.
constexpr std::size_t kUserContentMaximumBytes = 0x180000U;

enum class UserContentPackError : unsigned char {
  None,
  WrongMagic,
  UnsupportedSchema,
  InvalidHeader,
  InvalidSize,
  InvalidCount,
  InvalidIdentifier,
  DuplicateIdentifier,
  InvalidEntry,
};

struct UserContentIconView {
  char id[kUserContentIdCapacity]{};
  std::uint32_t offset{};
  std::uint32_t size{};
};

struct UserContentPackResult {
  bool ok{};
  UserContentPackError error{UserContentPackError::InvalidHeader};
  unsigned char icon_count{};
  UserContentIconView icons[kUserContentMaximumIcons]{};
};

[[nodiscard]] UserContentPackResult parse_user_content_pack(
    const std::uint8_t* wire, std::size_t wire_size);
[[nodiscard]] UserContentPackResult parse_user_content_index(
    const std::uint8_t* index, std::size_t index_size,
    std::size_t total_wire_size);

}  // namespace codex
