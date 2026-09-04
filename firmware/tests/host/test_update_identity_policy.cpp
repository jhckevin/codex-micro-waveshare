#include "codex/update_identity_policy.h"

#include <cstddef>
#include <cstdint>

extern "C" __declspec(dllimport) void __stdcall ExitProcess(unsigned long code);

namespace {
unsigned long failures{};

void require(bool value) {
  if (!value) ++failures;
}
}  // namespace

extern "C" void mainCRTStartup() {
  constexpr std::uint8_t kUser = 0;
  constexpr std::uint8_t kHmacUp = 8;

  {
    const std::uint8_t purposes[] = {
        kUser, kUser, kHmacUp, kUser, kUser, kUser};
    require(codex::select_unique_hmac_key_id(
                purposes, sizeof(purposes), kHmacUp) == 2);
  }
  {
    const std::uint8_t purposes[] = {
        kHmacUp, kUser, kUser, kUser, kUser, kUser};
    require(codex::select_unique_hmac_key_id(
                purposes, sizeof(purposes), kHmacUp) == 0);
  }
  {
    const std::uint8_t purposes[] = {
        kUser, kUser, kUser, kUser, kUser, kHmacUp};
    require(codex::select_unique_hmac_key_id(
                purposes, sizeof(purposes), kHmacUp) == 5);
  }
  {
    const std::uint8_t purposes[] = {
        kUser, kUser, kUser, kUser, kUser, kUser};
    require(codex::select_unique_hmac_key_id(
                purposes, sizeof(purposes), kHmacUp) ==
            codex::kInvalidHmacKeyId);
  }
  {
    const std::uint8_t purposes[] = {
        kHmacUp, kUser, kUser, kUser, kHmacUp, kUser};
    require(codex::select_unique_hmac_key_id(
                purposes, sizeof(purposes), kHmacUp) ==
            codex::kInvalidHmacKeyId);
  }
  require(codex::select_unique_hmac_key_id(nullptr, 6, kHmacUp) ==
          codex::kInvalidHmacKeyId);

  ExitProcess(failures);
}
