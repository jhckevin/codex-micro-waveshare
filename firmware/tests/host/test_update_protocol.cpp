#include "codex/update_protocol.h"

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures{};
void require(bool value) { if (!value) ++failures; }
bool contains(const char* text, const char* needle) {
  for (unsigned int start = 0; text[start]; ++start) {
    unsigned int index = 0;
    while (needle[index] && text[start + index] == needle[index]) ++index;
    if (!needle[index]) return true;
  }
  return false;
}
unsigned int text_size(const char* text) {
  unsigned int size = 0;
  while (text[size]) ++size;
  return size;
}
bool fake_attestation(const std::uint8_t challenge[codex::kUpdateDigestSize],
                      std::uint8_t tag[codex::kUpdateDigestSize], void*) {
  for (std::size_t index = 0; index < codex::kUpdateDigestSize; ++index) {
    tag[index] = challenge[index] ^ 0xa5U;
  }
  return true;
}
}  // namespace

extern "C" void mainCRTStartup() {
  codex::UpdateProtocolContext context{};
  auto hello = codex::handle_update_protocol(
      "{\"method\":\"update.hello\"}", 25, codex::UpdateTransport::Ble,
      context);
  require(hello.handled && !hello.ok);
  require(contains(hello.json, "transport_not_allowed"));
  auto usb_hello = codex::handle_update_protocol(
      "{\"method\":\"update.hello\"}", 25, codex::UpdateTransport::Usb,
      context);
  require(usb_hello.handled && usb_hello.ok);
  require(contains(usb_hello.json, "\"usb_only\":true"));
  require(contains(usb_hello.json, "\"provisioned\":false"));
  require(contains(usb_hello.json,
                   "\"device_id\":\"00000000000000000000000000000000\""));
  auto rejected = codex::handle_update_protocol(
      "{\"method\":\"update.begin\"}", 25, codex::UpdateTransport::Ble,
      context);
  require(rejected.handled && !rejected.ok);
  require(contains(rejected.json, "transport_not_allowed"));
  context.provisioned = true;
  context.has_device_kek = true;
  context.attest = fake_attestation;
  const char* attest_request =
      "{\"method\":\"update.attest\",\"params\":{\"challenge\":"
      "\"000102030405060708090a0b0c0d0e0f"
      "101112131415161718191a1b1c1d1e1f\"}}";
  auto attested = codex::handle_update_protocol(
      attest_request, text_size(attest_request), codex::UpdateTransport::Usb,
      context);
  require(attested.handled && attested.ok);
  require(contains(attested.json,
                   "\"tag\":\"a5a4a7a6a1a0a3a2adacafaea9a8abaa"
                   "b5b4b7b6b1b0b3b2bdbcbfbeb9b8bbba\""));
  auto ordinary = codex::handle_update_protocol(
      "{\"method\":\"cfg.get\"}", 20, codex::UpdateTransport::Usb, context);
  require(!ordinary.handled);
  ExitProcess(failures);
}
