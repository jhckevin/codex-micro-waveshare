#pragma once

#include <cstddef>
#include <cstdint>

#include "codex/update_manifest.h"

namespace codex {

enum class UpdateVerificationError : std::uint8_t {
  None,
  ManifestInvalid,
  DeviceMismatch,
  Replay,
  ClassNotAllowed,
  SignatureInvalid,
  PayloadDigestMismatch,
  CryptoFailure,
};

struct UpdateTrustPolicy {
  std::uint8_t expected_device_id[kUpdateDeviceIdSize]{};
  // Release streams are independent. A compatibility hotfix must not block a
  // service, firmware, or user-content package from the same release batch.
  std::uint64_t minimum_release_sequence_by_class[5]{};
  std::uint32_t allowed_class_mask{};
  const std::uint8_t* vendor_public_key_der{};
  std::size_t vendor_public_key_der_size{};
};

[[nodiscard]] constexpr std::size_t update_class_index(
    UpdatePackageClass package_class) {
  return static_cast<std::size_t>(package_class);
}

struct UpdateVerificationResult {
  ManifestParseResult parsed{};
  UpdateVerificationError error{UpdateVerificationError::ManifestInvalid};
  bool ok{};
};

[[nodiscard]] constexpr std::uint32_t update_class_bit(
    UpdatePackageClass package_class) {
  return 1U << static_cast<unsigned int>(package_class);
}

[[nodiscard]] UpdateVerificationResult verify_update_manifest(
    const std::uint8_t* wire, std::size_t wire_size,
    const UpdateTrustPolicy& policy);

[[nodiscard]] bool sha256_bytes(const std::uint8_t* data, std::size_t size,
                                std::uint8_t output[kUpdateDigestSize]);
[[nodiscard]] bool verify_update_payload_digest(
    const UpdateManifest& manifest, const std::uint8_t* plaintext,
    std::size_t plaintext_size);
[[nodiscard]] bool unwrap_update_content_key(
    const UpdateManifest& manifest,
    const std::uint8_t device_kek[kUpdateDigestSize],
    std::uint8_t content_key[kUpdateDigestSize]);
[[nodiscard]] bool decrypt_update_chunk(
    const UpdateManifest& manifest,
    const std::uint8_t content_key[kUpdateDigestSize],
    std::uint32_t offset, std::uint32_t sequence,
    const std::uint8_t* ciphertext, std::size_t size,
    const std::uint8_t tag[16], std::uint8_t* plaintext);

}  // namespace codex
