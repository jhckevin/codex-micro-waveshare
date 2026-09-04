#pragma once

#include <cstddef>

#include "codex/update_session.h"

namespace codex {

using UpdateAttestationProvider = bool (*)(
    const std::uint8_t challenge[kUpdateDigestSize],
    std::uint8_t tag[kUpdateDigestSize], void* context);

struct UpdateProtocolContext {
  UpdateSession* session{};
  UpdateTrustPolicy trust{};
  UpdateAttestationProvider attest{};
  void* attest_context{};
  bool provisioned{};
  bool has_device_kek{};
  std::uint8_t device_kek[kUpdateDigestSize]{};
  std::uint8_t content_key[kUpdateDigestSize]{};
  UpdateManifest active_manifest{};
};

struct UpdateProtocolReply {
  bool handled{};
  bool ok{};
  char json[512]{};
  unsigned short length{};
};

[[nodiscard]] UpdateProtocolReply handle_update_protocol(
    const char* data, unsigned int length, UpdateTransport transport,
    UpdateProtocolContext& context);

}  // namespace codex
