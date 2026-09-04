#include "codex/user_content_protocol.h"

namespace codex {
namespace {

bool find(const char* data, unsigned int length, const char* token,
          unsigned int& position) {
  for (unsigned int start = 0; start < length; ++start) {
    unsigned int index = 0;
    while (token[index] && start + index < length &&
           data[start + index] == token[index]) ++index;
    if (!token[index]) {
      position = start;
      return true;
    }
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
  while (position < length &&
         (data[position] == ' ' || data[position] == '\t')) ++position;
  if (position >= length || data[position] < '0' || data[position] > '9') {
    return false;
  }
  std::uint32_t value = 0;
  while (position < length && data[position] >= '0' &&
         data[position] <= '9') {
    const unsigned int digit =
        static_cast<unsigned int>(data[position++] - '0');
    if (value > 429496729U || (value == 429496729U && digit > 5U)) {
      return false;
    }
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
  if (length == 0 || (length & 1U) != 0 || length / 2U > capacity) {
    return false;
  }
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
  do {
    digits[count++] = static_cast<char>('0' + value % 10U);
    value /= 10U;
  } while (value);
  while (count && used + 1 < capacity) output[used++] = digits[--count];
  output[used] = '\0';
}

UserContentProtocolReply error(const char* value) {
  UserContentProtocolReply result{.handled = true};
  unsigned int used = 0;
  append(result.json, sizeof(result.json), used,
         "{\"ok\":false,\"error\":\"");
  append(result.json, sizeof(result.json), used, value);
  append(result.json, sizeof(result.json), used, "\"}");
  result.length = static_cast<unsigned short>(used);
  return result;
}

UserContentProtocolReply status_reply(
    const UserContentTransferSnapshot& snapshot) {
  UserContentProtocolReply result{.handled = true, .ok = true};
  unsigned int used = 0;
  append(result.json, sizeof(result.json), used, "{\"ok\":true,\"state\":\"");
  append(result.json, sizeof(result.json), used,
         snapshot.open ? "receiving" : snapshot.complete ? "complete" : "idle");
  append(result.json, sizeof(result.json), used, "\",\"expected_offset\":");
  append_uint(result.json, sizeof(result.json), used, snapshot.expected_offset);
  append(result.json, sizeof(result.json), used, ",\"total_size\":");
  append_uint(result.json, sizeof(result.json), used, snapshot.total_size);
  append(result.json, sizeof(result.json), used, "}");
  result.length = static_cast<unsigned short>(used);
  return result;
}

}  // namespace

UserContentProtocolReply handle_user_content_protocol(
    const char* data, unsigned int length, UserContentTransport transport,
    UserContentProtocolContext& context) {
  char method[32]{};
  if (data == nullptr ||
      !string_value(data, length, "\"method\"", method, sizeof(method)) ||
      method[0] != 'c' || method[1] != 'o' || method[2] != 'n' ||
      method[3] != 't' || method[4] != 'e' || method[5] != 'n' ||
      method[6] != 't' || method[7] != '.') {
    return {};
  }
  if (transport != UserContentTransport::Usb) {
    return error("transport_not_allowed");
  }
  if (same(method, "content.hello")) {
    UserContentProtocolReply result{.handled = true, .ok = true};
    constexpr char reply[] =
        "{\"ok\":true,\"protocol\":1,\"usb_only\":true,\"chunk_size\":512,"
        "\"maximum_bytes\":1572864,\"resume\":true}";
    unsigned int used = 0;
    append(result.json, sizeof(result.json), used, reply);
    result.length = static_cast<unsigned short>(used);
    return result;
  }
  if (context.begin == nullptr || context.write == nullptr ||
      context.commit == nullptr || context.cancel == nullptr ||
      context.snapshot == nullptr) {
    return error("content_unavailable");
  }
  if (same(method, "content.begin")) {
    std::uint32_t total_size = 0;
    char digest_hex[kUserContentDigestBytes * 2U + 1U]{};
    std::uint8_t digest[kUserContentDigestBytes]{};
    std::size_t digest_size = 0;
    if (!uint_value(data, length, "\"total_size\"", total_size) ||
        total_size < kUserContentHeaderSize ||
        total_size > kUserContentMaximumBytes ||
        !string_value(data, length, "\"sha256\"", digest_hex,
                      sizeof(digest_hex)) ||
        !decode_hex(digest_hex, digest, sizeof(digest), digest_size) ||
        digest_size != sizeof(digest) ||
        !context.begin(total_size, digest, context.context)) {
      return error("begin_failed");
    }
    return status_reply(context.snapshot(context.context));
  }
  if (same(method, "content.chunk")) {
    std::uint32_t offset = 0;
    char data_hex[kUserContentTransferChunkBytes * 2U + 1U]{};
    std::uint8_t chunk[kUserContentTransferChunkBytes]{};
    std::size_t chunk_size = 0;
    if (!uint_value(data, length, "\"offset\"", offset) ||
        !string_value(data, length, "\"data\"", data_hex,
                      sizeof(data_hex)) ||
        !decode_hex(data_hex, chunk, sizeof(chunk), chunk_size) ||
        !context.write(offset, chunk, chunk_size, context.context)) {
      return error("chunk_rejected");
    }
    return status_reply(context.snapshot(context.context));
  }
  if (same(method, "content.status")) {
    return status_reply(context.snapshot(context.context));
  }
  if (same(method, "content.commit")) {
    if (!context.commit(context.context)) return error("commit_failed");
    return status_reply(context.snapshot(context.context));
  }
  if (same(method, "content.cancel")) {
    context.cancel(context.context);
    return status_reply(context.snapshot(context.context));
  }
  return error("method_not_found");
}

}  // namespace codex
