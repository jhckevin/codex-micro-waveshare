#include <cstring>

#include "codex/user_content_protocol.h"

namespace {

struct FakeTransfer {
  codex::UserContentTransferSnapshot snapshot{};
  unsigned char digest[codex::kUserContentDigestBytes]{};
  unsigned char bytes[512]{};
  bool begin(unsigned int size, const unsigned char* value) {
    if (snapshot.open) return false;
    snapshot = {.open = true, .total_size = size};
    std::memcpy(digest, value, sizeof(digest));
    return true;
  }
  bool write(unsigned int offset, const unsigned char* data, std::size_t size) {
    if (!snapshot.open || offset != snapshot.expected_offset ||
        offset + size > snapshot.total_size) return false;
    std::memcpy(bytes + offset, data, size);
    snapshot.expected_offset += size;
    return true;
  }
  bool commit() {
    if (!snapshot.open ||
        snapshot.expected_offset != snapshot.total_size) return false;
    snapshot.open = false;
    snapshot.complete = true;
    return true;
  }
};

bool begin(unsigned int size, const unsigned char* digest, void* context) {
  return static_cast<FakeTransfer*>(context)->begin(size, digest);
}
bool write(unsigned int offset, const unsigned char* data, std::size_t size,
           void* context) {
  return static_cast<FakeTransfer*>(context)->write(offset, data, size);
}
bool commit(void* context) {
  return static_cast<FakeTransfer*>(context)->commit();
}
void cancel(void* context) {
  static_cast<FakeTransfer*>(context)->snapshot = {};
}
codex::UserContentTransferSnapshot snapshot(void* context) {
  return static_cast<FakeTransfer*>(context)->snapshot;
}

bool contains(const char* value, const char* expected) {
  return std::strstr(value, expected) != nullptr;
}

}  // namespace

extern "C" void __declspec(dllimport) ExitProcess(unsigned int);

extern "C" void mainCRTStartup() {
  FakeTransfer transfer{};
  codex::UserContentProtocolContext context{
      .begin = begin,
      .write = write,
      .commit = commit,
      .cancel = cancel,
      .snapshot = snapshot,
      .context = &transfer,
  };

  constexpr char ignored[] = "{\"method\":\"cfg.hello\"}";
  auto reply = codex::handle_user_content_protocol(
      ignored, sizeof(ignored) - 1, codex::UserContentTransport::Usb, context);
  if (reply.handled) ExitProcess(1);

  constexpr char hello[] = "{\"method\":\"content.hello\"}";
  reply = codex::handle_user_content_protocol(
      hello, sizeof(hello) - 1, codex::UserContentTransport::Ble, context);
  if (!reply.handled || reply.ok ||
      !contains(reply.json, "transport_not_allowed")) ExitProcess(2);
  reply = codex::handle_user_content_protocol(
      hello, sizeof(hello) - 1, codex::UserContentTransport::Usb, context);
  if (!reply.ok || !contains(reply.json, "\"maximum_bytes\":1572864")) {
    ExitProcess(8);
  }

  constexpr char begin_message[] =
      "{\"method\":\"content.begin\",\"total_size\":18,\"sha256\":\""
      "1111111111111111111111111111111111111111111111111111111111111111\"}";
  reply = codex::handle_user_content_protocol(
      begin_message, sizeof(begin_message) - 1,
      codex::UserContentTransport::Usb, context);
  if (!reply.ok || !transfer.snapshot.open ||
      transfer.snapshot.total_size != 18 || transfer.digest[0] != 0x11) {
    ExitProcess(3);
  }

  constexpr char first[] =
      "{\"method\":\"content.chunk\",\"offset\":0,\"data\":\"434d433101\"}";
  reply = codex::handle_user_content_protocol(
      first, sizeof(first) - 1, codex::UserContentTransport::Usb, context);
  if (!reply.ok || transfer.snapshot.expected_offset != 5 ||
      transfer.bytes[0] != 'C') ExitProcess(4);

  constexpr char wrong_offset[] =
      "{\"method\":\"content.chunk\",\"offset\":7,\"data\":\"00\"}";
  reply = codex::handle_user_content_protocol(
      wrong_offset, sizeof(wrong_offset) - 1,
      codex::UserContentTransport::Usb, context);
  if (reply.ok || !contains(reply.json, "chunk_rejected")) ExitProcess(5);

  constexpr char rest[] =
      "{\"method\":\"content.chunk\",\"offset\":5,\"data\":"
      "\"00000000000000000000000000\"}";
  reply = codex::handle_user_content_protocol(
      rest, sizeof(rest) - 1, codex::UserContentTransport::Usb, context);
  if (!reply.ok || transfer.snapshot.expected_offset != 18) ExitProcess(6);

  constexpr char finish[] = "{\"method\":\"content.commit\"}";
  reply = codex::handle_user_content_protocol(
      finish, sizeof(finish) - 1, codex::UserContentTransport::Usb, context);
  if (!reply.ok || transfer.snapshot.open || !transfer.snapshot.complete ||
      !contains(reply.json, "\"state\":\"complete\"")) ExitProcess(7);

  ExitProcess(0);
}
