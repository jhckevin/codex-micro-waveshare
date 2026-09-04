#pragma once

#include <cstddef>
#include <cstdint>

namespace codex {

inline constexpr int kInvalidHmacKeyId = -1;

// The ESP32-S3 HMAC key identifiers are zero-based and correspond to the six
// eFuse key blocks. Production provisioning may choose any otherwise-unused
// block, so firmware must discover exactly one HMAC_UP purpose instead of
// assuming KEY0.
[[nodiscard]] constexpr int select_unique_hmac_key_id(
    const std::uint8_t* purposes, std::size_t count,
    std::uint8_t hmac_up_purpose) {
  if (purposes == nullptr || count == 0) return kInvalidHmacKeyId;
  int selected = kInvalidHmacKeyId;
  for (std::size_t index = 0; index < count; ++index) {
    if (purposes[index] != hmac_up_purpose) continue;
    if (selected != kInvalidHmacKeyId) return kInvalidHmacKeyId;
    selected = static_cast<int>(index);
  }
  return selected;
}

}  // namespace codex
