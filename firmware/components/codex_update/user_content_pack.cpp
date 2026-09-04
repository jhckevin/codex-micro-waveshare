#include "codex/user_content_pack.h"

namespace codex {
namespace {

std::uint32_t read32(const std::uint8_t* input) {
  return static_cast<std::uint32_t>(input[0]) |
         static_cast<std::uint32_t>(input[1]) << 8U |
         static_cast<std::uint32_t>(input[2]) << 16U |
         static_cast<std::uint32_t>(input[3]) << 24U;
}

bool same_magic(const std::uint8_t* wire) {
  return wire[0] == 'C' && wire[1] == 'M' && wire[2] == 'C' &&
         wire[3] == '1';
}

bool valid_id_character(std::uint8_t value) {
  return (value >= 'a' && value <= 'z') ||
         (value >= 'A' && value <= 'Z') ||
         (value >= '0' && value <= '9') || value == '.' || value == '_' ||
         value == '-';
}

bool same_id(const char* left, const char* right) {
  std::size_t index = 0;
  while (left[index] != '\0' && right[index] != '\0' &&
         left[index] == right[index]) {
    ++index;
  }
  return left[index] == right[index];
}

UserContentPackResult failure(UserContentPackError error) {
  UserContentPackResult result{};
  result.error = error;
  return result;
}

}  // namespace

UserContentPackResult parse_user_content_index(const std::uint8_t* wire,
                                               std::size_t index_size,
                                               std::size_t wire_size) {
  if (wire == nullptr || index_size < kUserContentHeaderSize ||
      wire_size < kUserContentHeaderSize ||
      wire_size > kUserContentMaximumBytes) {
    return failure(UserContentPackError::InvalidSize);
  }
  if (!same_magic(wire)) {
    return failure(UserContentPackError::WrongMagic);
  }
  if (wire[4] != 1U) {
    return failure(UserContentPackError::UnsupportedSchema);
  }
  const unsigned int count = wire[5];
  if (count > kUserContentMaximumIcons) {
    return failure(UserContentPackError::InvalidCount);
  }
  if (wire[6] != kUserContentIconWidth ||
      wire[7] != kUserContentIconHeight || wire[12] != 0 || wire[13] != 0 ||
      wire[14] != 0 || wire[15] != 0) {
    return failure(UserContentPackError::InvalidHeader);
  }
  if (read32(wire + 8) != wire_size) {
    return failure(UserContentPackError::InvalidSize);
  }
  const std::size_t directory_end =
      kUserContentHeaderSize + count * kUserContentEntrySize;
  if (directory_end > index_size || directory_end > wire_size) {
    return failure(UserContentPackError::InvalidSize);
  }

  UserContentPackResult result{};
  result.icon_count = static_cast<unsigned char>(count);
  std::size_t expected_offset = directory_end;
  for (unsigned int index = 0; index < count; ++index) {
    const std::uint8_t* entry =
        wire + kUserContentHeaderSize + index * kUserContentEntrySize;
    std::size_t id_size = 0;
    while (id_size < kUserContentIdCapacity && entry[id_size] != 0) {
      if (!valid_id_character(entry[id_size])) {
        return failure(UserContentPackError::InvalidIdentifier);
      }
      result.icons[index].id[id_size] = static_cast<char>(entry[id_size]);
      ++id_size;
    }
    if (id_size == 0 || id_size >= kUserContentIdCapacity) {
      return failure(UserContentPackError::InvalidIdentifier);
    }
    for (std::size_t tail = id_size + 1; tail < kUserContentIdCapacity;
         ++tail) {
      if (entry[tail] != 0) {
        return failure(UserContentPackError::InvalidIdentifier);
      }
    }
    for (unsigned int previous = 0; previous < index; ++previous) {
      if (same_id(result.icons[previous].id, result.icons[index].id)) {
        return failure(UserContentPackError::DuplicateIdentifier);
      }
    }
    const std::uint32_t offset = read32(entry + 32);
    const std::uint32_t size = read32(entry + 36);
    if (offset != expected_offset || size != kUserContentIconBytes ||
        static_cast<std::size_t>(offset) + size > wire_size) {
      return failure(UserContentPackError::InvalidEntry);
    }
    result.icons[index].offset = offset;
    result.icons[index].size = size;
    expected_offset += size;
  }
  if (expected_offset != wire_size) {
    return failure(UserContentPackError::InvalidEntry);
  }
  result.ok = true;
  result.error = UserContentPackError::None;
  return result;
}

UserContentPackResult parse_user_content_pack(const std::uint8_t* wire,
                                              std::size_t wire_size) {
  return parse_user_content_index(wire, wire_size, wire_size);
}

}  // namespace codex
