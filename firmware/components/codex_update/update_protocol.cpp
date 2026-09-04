#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#include "codex/update_protocol.h"

namespace codex {
namespace {

bool find(const char* data, unsigned int length, const char* token,
          unsigned int& position) {
  for (unsigned int start = 0; start < length; ++start) {
    unsigned int index = 0;
    while (token[index] && start + index < length &&
           data[start + index] == token[index]) ++index;
    if (!token[index]) { position = start; return true; }
  }
  return false;
}
bool string_value(const char* data, unsigned int length, const char* key,
                  char* output, unsigned int capacity) {
  unsigned int position = 0;
  if (!find(data, length, key, position)) return false;
  while (position < length && data[position] != ':') ++position;
  while (position < length && data[position] != '"') ++position;
  if (position++ >= length) return false;
  unsigned int used = 0;
  while (position < length && data[position] != '"') {
    if (used + 1 >= capacity || data[position] == '\\') return false;
    output[used++] = data[position++];
  }
  if (position >= length) return false;
  output[used] = '\0';
  return true;
}
bool uint_value(const char* data, unsigned int length, const char* key,
                std::uint32_t& output) {
  unsigned int position = 0;
  if (!find(data, length, key, position)) return false;
  while (position < length && data[position] != ':') ++position;
  if (position++ >= length) return false;
  while (position < length && (data[position] == ' ' || data[position] == '\t')) ++position;
  if (position >= length || data[position] < '0' || data[position] > '9') return false;
  std::uint32_t value = 0;
  while (position < length && data[position] >= '0' && data[position] <= '9') {
    const unsigned int digit = data[position++] - '0';
    if (value > 429496729U || (value == 429496729U && digit > 5U)) return false;
    value = value * 10U + digit;
  }
  output = value;
  return true;
}
bool same(const char* left, const char* right) {
  unsigned int index = 0;
  while (left[index] && right[index] && left[index] == right[index]) ++index;
  return left[index] == right[index];
}
bool same_bytes(const std::uint8_t* left, const std::uint8_t* right,
                std::size_t size) {
  std::uint8_t difference = 0;
  for (std::size_t index = 0; index < size; ++index) {
    difference |= left[index] ^ right[index];
  }
  return difference == 0;
}
bool same_manifest_identity(const UpdateManifest& left,
                            const UpdateManifest& right) {
  return left.package_class == right.package_class &&
         left.release_sequence == right.release_sequence &&
         left.payload_size == right.payload_size &&
         same(left.target_version, right.target_version) &&
         same_bytes(left.target_device_id, right.target_device_id,
                    kUpdateDeviceIdSize) &&
         same_bytes(left.payload_sha256, right.payload_sha256,
                    kUpdateDigestSize);
}
int nibble(char value) {
  if (value >= '0' && value <= '9') return value - '0';
  if (value >= 'a' && value <= 'f') return value - 'a' + 10;
  if (value >= 'A' && value <= 'F') return value - 'A' + 10;
  return -1;
}
bool decode_hex(const char* input, std::uint8_t* output, std::size_t capacity,
                std::size_t& output_size) {
  std::size_t length = 0;
  while (input[length]) ++length;
  if ((length & 1U) != 0 || length / 2U > capacity) return false;
  output_size = length / 2U;
  for (std::size_t index = 0; index < output_size; ++index) {
    const int high = nibble(input[index * 2U]);
    const int low = nibble(input[index * 2U + 1U]);
    if (high < 0 || low < 0) return false;
    output[index] = static_cast<std::uint8_t>((high << 4U) | low);
  }
  return true;
}
void append(char* output, unsigned int capacity, unsigned int& used,
            const char* value) {
  for (unsigned int index = 0; value[index] && used + 1 < capacity; ++index) {
    output[used++] = value[index];
  }
  output[used] = '\0';
}
void append_uint(char* output, unsigned int capacity, unsigned int& used,
                 std::uint32_t value) {
  char digits[11]{};
  unsigned int count = 0;
  do { digits[count++] = '0' + value % 10U; value /= 10U; } while (value);
  while (count && used + 1 < capacity) output[used++] = digits[--count];
  output[used] = '\0';
}
void append_hex(char* output, unsigned int capacity, unsigned int& used,
                const std::uint8_t* value, std::size_t size) {
  static constexpr char kHex[] = "0123456789abcdef";
  for (std::size_t index = 0; index < size && used + 2 < capacity; ++index) {
    output[used++] = kHex[value[index] >> 4U];
    output[used++] = kHex[value[index] & 0x0fU];
  }
  output[used] = '\0';
}
UpdateProtocolReply error(const char* value) {
  UpdateProtocolReply result{.handled = true};
  unsigned int used = 0;
  append(result.json, sizeof(result.json), used, "{\"ok\":false,\"error\":\"");
  append(result.json, sizeof(result.json), used, value);
  append(result.json, sizeof(result.json), used, "\"}");
  result.length = static_cast<unsigned short>(used);
  return result;
}
const char* state_name(UpdateSessionState state) {
  switch (state) {
    case UpdateSessionState::Idle: return "idle";
    case UpdateSessionState::Receiving: return "receiving";
    case UpdateSessionState::AwaitingConfirmation: return "awaiting_confirmation";
    case UpdateSessionState::Applying: return "applying";
    case UpdateSessionState::Complete: return "complete";
    case UpdateSessionState::Failed: return "failed";
  }
  return "failed";
}
UpdateProtocolReply status_reply(const UpdateSessionSnapshot& snapshot) {
  UpdateProtocolReply result{.handled = true, .ok = true};
  unsigned int used = 0;
  append(result.json, sizeof(result.json), used, "{\"ok\":true,\"state\":\"");
  append(result.json, sizeof(result.json), used, state_name(snapshot.state));
  append(result.json, sizeof(result.json), used, "\",\"expected_offset\":");
  append_uint(result.json, sizeof(result.json), used, snapshot.expected_offset);
  append(result.json, sizeof(result.json), used, ",\"next_sequence\":");
  append_uint(result.json, sizeof(result.json), used, snapshot.next_sequence);
  append(result.json, sizeof(result.json), used, ",\"total_size\":");
  append_uint(result.json, sizeof(result.json), used, snapshot.total_size);
  append(result.json, sizeof(result.json), used, ",\"target_version\":\"");
  append(result.json, sizeof(result.json), used, snapshot.target_version);
  append(result.json, sizeof(result.json), used, "\"}");
  result.length = static_cast<unsigned short>(used);
  return result;
}
}  // namespace

UpdateProtocolReply handle_update_protocol(
    const char* data, unsigned int length, UpdateTransport transport,
    UpdateProtocolContext& context) {
  char method[32]{};
  if (data == nullptr || !string_value(data, length, "\"method\"", method,
                                       sizeof(method)) ||
      method[0] != 'u' || method[1] != 'p' || method[2] != 'd' ||
      method[3] != 'a' || method[4] != 't' || method[5] != 'e' ||
      method[6] != '.') {
    return {};
  }
#if CONFIG_CODEX_OPEN_SOURCE_BUILD
  return error("encrypted_updates_disabled_source_build");
#endif
  if (transport != UpdateTransport::Usb) return error("transport_not_allowed");
  if (same(method, "update.hello")) {
    UpdateProtocolReply result{.handled = true, .ok = true};
    unsigned int used = 0;
    append(result.json, sizeof(result.json), used,
           "{\"ok\":true,\"protocol\":1,\"usb_only\":true,\"provisioned\":");
    append(result.json, sizeof(result.json), used,
           context.provisioned ? "true" : "false");
    append(result.json, sizeof(result.json), used, ",\"device_id\":\"");
    append_hex(result.json, sizeof(result.json), used,
               context.trust.expected_device_id, kUpdateDeviceIdSize);
    append(result.json, sizeof(result.json), used,
           "\",\"chunk_size\":512,\"resume\":true,\"ota\":true,"
           "\"rollback\":true}");
    result.length = static_cast<unsigned short>(used);
    return result;
  }
  if (!context.provisioned) return error("device_not_provisioned");
  if (!context.has_device_kek) return error("device_key_unavailable");
  if (same(method, "update.attest")) {
    char challenge_hex[kUpdateDigestSize * 2U + 1U]{};
    std::uint8_t challenge[kUpdateDigestSize]{};
    std::uint8_t tag[kUpdateDigestSize]{};
    std::size_t challenge_size = 0;
    if (context.attest == nullptr ||
        !string_value(data, length, "\"challenge\"", challenge_hex,
                      sizeof(challenge_hex)) ||
        !decode_hex(challenge_hex, challenge, sizeof(challenge),
                    challenge_size) ||
        challenge_size != sizeof(challenge) ||
        !context.attest(challenge, tag, context.attest_context)) {
      return error("attestation_failed");
    }
    UpdateProtocolReply result{.handled = true, .ok = true};
    unsigned int used = 0;
    append(result.json, sizeof(result.json), used,
           "{\"ok\":true,\"device_id\":\"");
    append_hex(result.json, sizeof(result.json), used,
               context.trust.expected_device_id, kUpdateDeviceIdSize);
    append(result.json, sizeof(result.json), used, "\",\"tag\":\"");
    append_hex(result.json, sizeof(result.json), used, tag, sizeof(tag));
    append(result.json, sizeof(result.json), used, "\"}");
    result.length = static_cast<unsigned short>(used);
    return result;
  }
  if (context.session == nullptr) return error("update_unavailable");

  if (same(method, "update.begin")) {
    char manifest_hex[kUpdateManifestWireSize * 2U + 1U]{};
    if (!string_value(data, length, "\"manifest\"", manifest_hex,
                      sizeof(manifest_hex))) return error("invalid_manifest");
    std::uint8_t wire[kUpdateManifestWireSize]{};
    std::size_t wire_size = 0;
    if (!decode_hex(manifest_hex, wire, sizeof(wire), wire_size) ||
        wire_size != sizeof(wire)) return error("invalid_manifest");
    const auto verified =
        verify_update_manifest(wire, wire_size, context.trust);
    if (!verified.ok) return error("verification_failed");
    const auto current = context.session->snapshot();
    if (current.state == UpdateSessionState::Receiving &&
        same_manifest_identity(verified.parsed.manifest,
                               context.active_manifest)) {
      return status_reply(current);
    }
    if (!unwrap_update_content_key(verified.parsed.manifest,
                                   context.device_kek,
                                   context.content_key)) {
      return error("content_key_rejected");
    }
    if (!context.session->begin(verified, transport)) return error("begin_failed");
    context.active_manifest = verified.parsed.manifest;
    return status_reply(context.session->snapshot());
  }
  if (same(method, "update.chunk")) {
    char data_hex[kUpdateChunkCapacity * 2U + 1U]{};
    char digest_hex[kUpdateDigestSize * 2U + 1U]{};
    char tag_hex[33]{};
    std::uint32_t offset = 0, sequence = 0;
    if (!uint_value(data, length, "\"offset\"", offset) ||
        !uint_value(data, length, "\"sequence\"", sequence) ||
        !string_value(data, length, "\"data\"", data_hex, sizeof(data_hex)) ||
        !string_value(data, length, "\"tag\"", tag_hex, sizeof(tag_hex)) ||
        !string_value(data, length, "\"sha256\"", digest_hex,
                      sizeof(digest_hex))) return error("invalid_chunk");
    std::uint8_t chunk[kUpdateChunkCapacity]{};
    std::uint8_t plaintext[kUpdateChunkCapacity]{};
    std::uint8_t digest[kUpdateDigestSize]{};
    std::uint8_t tag[16]{};
    std::size_t chunk_size = 0, digest_size = 0, tag_size = 0;
    if (!decode_hex(data_hex, chunk, sizeof(chunk), chunk_size) ||
        !decode_hex(digest_hex, digest, sizeof(digest), digest_size) ||
        !decode_hex(tag_hex, tag, sizeof(tag), tag_size) ||
        digest_size != sizeof(digest) ||
        tag_size != sizeof(tag) ||
        !decrypt_update_chunk(context.active_manifest, context.content_key,
                              offset, sequence, chunk, chunk_size, tag,
                              plaintext) ||
        !context.session->write_chunk(offset, sequence, plaintext, chunk_size,
                                      digest)) return error("chunk_rejected");
    return status_reply(context.session->snapshot());
  }
  if (same(method, "update.status")) {
    return status_reply(context.session->snapshot());
  }
  if (same(method, "update.commit")) {
    const bool complete = context.session->commit();
    const auto snapshot = context.session->snapshot();
    if (!complete &&
        snapshot.state != UpdateSessionState::AwaitingConfirmation) {
      return error("commit_failed");
    }
    if (snapshot.state == UpdateSessionState::Complete) {
      context.trust.minimum_release_sequence_by_class[
          update_class_index(context.active_manifest.package_class)] =
              context.active_manifest.release_sequence;
      for (auto& value : context.content_key) value = 0;
    }
    return status_reply(snapshot);
  }
  if (same(method, "update.confirm")) {
    if (!context.session->confirm()) return error("confirm_failed");
    context.trust.minimum_release_sequence_by_class[
        update_class_index(context.active_manifest.package_class)] =
            context.active_manifest.release_sequence;
    for (auto& value : context.content_key) value = 0;
    return status_reply(context.session->snapshot());
  }
  if (same(method, "update.cancel")) {
    context.session->cancel();
    for (auto& value : context.content_key) value = 0;
    context.active_manifest = {};
    return status_reply(context.session->snapshot());
  }
  return error("method_not_found");
}

}  // namespace codex
