#pragma once

#include <cstddef>
#include <cstdint>

#include "codex/user_content_pack.h"

namespace codex {

constexpr std::size_t kUserContentTransferChunkBytes = 512;
constexpr std::size_t kUserContentDigestBytes = 32;

enum class UserContentTransport : unsigned char {
  Usb,
  Ble,
};

struct UserContentTransferSnapshot {
  bool open{};
  bool complete{};
  std::uint32_t expected_offset{};
  std::uint32_t total_size{};
};

using UserContentBegin = bool (*)(
    std::uint32_t total_size,
    const std::uint8_t digest[kUserContentDigestBytes],
    void* context);
using UserContentWrite = bool (*)(
    std::uint32_t offset, const std::uint8_t* data, std::size_t size,
    void* context);
using UserContentCommit = bool (*)(void* context);
using UserContentCancel = void (*)(void* context);
using UserContentSnapshot = UserContentTransferSnapshot (*)(void* context);

struct UserContentProtocolContext {
  UserContentBegin begin{};
  UserContentWrite write{};
  UserContentCommit commit{};
  UserContentCancel cancel{};
  UserContentSnapshot snapshot{};
  void* context{};
};

struct UserContentProtocolReply {
  bool handled{};
  bool ok{};
  char json[256]{};
  unsigned short length{};
};

[[nodiscard]] UserContentProtocolReply handle_user_content_protocol(
    const char* data, unsigned int length, UserContentTransport transport,
    UserContentProtocolContext& context);

}  // namespace codex
