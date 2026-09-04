#include "codex/update_identity.h"
#include "codex/update_identity_policy.h"
#include "codex/update_public_key_config.h"

#include "sdkconfig.h"
#include "esp_efuse.h"
#include "esp_efuse_chip.h"
#include "esp_hmac.h"
#include "mbedtls/sha256.h"

namespace codex {
namespace {

constexpr std::size_t kHmacKeyCount = 6;

int discover_update_hmac_key_id() {
#if CONFIG_CODEX_OPEN_SOURCE_BUILD
  return kInvalidHmacKeyId;
#else
  std::uint8_t purposes[kHmacKeyCount]{};
  for (std::size_t index = 0; index < kHmacKeyCount; ++index) {
    const auto block = static_cast<esp_efuse_block_t>(
        static_cast<int>(EFUSE_BLK_KEY0) + static_cast<int>(index));
    purposes[index] = static_cast<std::uint8_t>(
        esp_efuse_get_key_purpose(block));
  }
  return select_unique_hmac_key_id(
      purposes, kHmacKeyCount,
      static_cast<std::uint8_t>(ESP_EFUSE_KEY_PURPOSE_HMAC_UP));
#endif
}

bool calculate_update_hmac(const void* message, std::size_t message_size,
                           std::uint8_t output[kUpdateDigestSize]) {
  const int key_id = discover_update_hmac_key_id();
  if (key_id == kInvalidHmacKeyId) return false;
  return esp_hmac_calculate(static_cast<hmac_key_id_t>(key_id), message,
                            message_size, output) == ESP_OK;
}

}  // namespace

const std::uint8_t* update_public_key_der() {
  return kConfiguredUpdatePublicKeyDer;
}

std::size_t update_public_key_der_size() {
  return kConfiguredUpdatePublicKeyDerSize;
}

void make_update_device_id(
    const std::uint8_t base_mac[6],
    std::uint8_t output[kUpdateDeviceIdSize]) {
  static constexpr std::uint8_t kDomain[] = {
      'c', 'o', 'd', 'e', 'x', '-', 'm', 'i', 'c', 'r', 'o',
      '-', 'u', 'p', 'd', 'a', 't', 'e', '-', 'v', '1'};
  std::uint8_t input[sizeof(kDomain) + 6]{};
  for (std::size_t index = 0; index < sizeof(kDomain); ++index) {
    input[index] = kDomain[index];
  }
  for (std::size_t index = 0; index < 6; ++index) {
    input[sizeof(kDomain) + index] = base_mac[index];
  }
  std::uint8_t digest[kUpdateDigestSize]{};
  mbedtls_sha256(input, sizeof(input), digest, 0);
  for (std::size_t index = 0; index < kUpdateDeviceIdSize; ++index) {
    output[index] = digest[index];
  }
}

bool derive_update_kek_from_efuse(
    const std::uint8_t device_id[kUpdateDeviceIdSize],
    std::uint8_t output[kUpdateDigestSize]) {
  std::uint8_t message[32] = {
      'c', 'o', 'd', 'e', 'x', '-', 'u', 'p',
      'd', 'a', 't', 'e', '-', 'k', 'e', 'k'};
  for (std::size_t index = 0; index < kUpdateDeviceIdSize; ++index) {
    message[16 + index] = device_id[index];
  }
  return calculate_update_hmac(message, sizeof(message), output);
}

bool make_update_attestation(
    const std::uint8_t device_id[kUpdateDeviceIdSize],
    const std::uint8_t challenge[kUpdateDigestSize],
    std::uint8_t output[kUpdateDigestSize]) {
  static constexpr std::uint8_t kDomain[] = {
      'c', 'o', 'd', 'e', 'x', '-', 'u', 'p', 'd', 'a', 't', 'e',
      '-', 'a', 't', 't', 'e', 's', 't', '-', 'v', '1'};
  std::uint8_t message[sizeof(kDomain) + kUpdateDeviceIdSize +
                       kUpdateDigestSize]{};
  for (std::size_t index = 0; index < sizeof(kDomain); ++index) {
    message[index] = kDomain[index];
  }
  for (std::size_t index = 0; index < kUpdateDeviceIdSize; ++index) {
    message[sizeof(kDomain) + index] = device_id[index];
  }
  for (std::size_t index = 0; index < kUpdateDigestSize; ++index) {
    message[sizeof(kDomain) + kUpdateDeviceIdSize + index] = challenge[index];
  }
  return calculate_update_hmac(message, sizeof(message), output);
}

}  // namespace codex
