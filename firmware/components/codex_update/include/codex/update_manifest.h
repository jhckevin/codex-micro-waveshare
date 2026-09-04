#pragma once

#include <cstddef>
#include <cstdint>

namespace codex {

constexpr std::size_t kUpdateDeviceIdSize = 16;
constexpr std::size_t kUpdateDigestSize = 32;
constexpr std::size_t kUpdateNonceSize = 12;
constexpr std::size_t kUpdateWrappedKeySize = 48;
constexpr std::size_t kUpdateVersionCapacity = 32;
constexpr std::size_t kUpdateSignatureCapacity = 72;
constexpr std::size_t kUpdateManifestSignedSize = 168;
constexpr std::size_t kUpdateManifestWireSize = 248;

enum class UpdatePackageClass : std::uint8_t {
  Compatibility = 1,
  ServiceReload = 2,
  CompleteFirmware = 3,
  UserContent = 4,
};

enum class ManifestParseError : std::uint8_t {
  None,
  WrongSize,
  WrongMagic,
  UnsupportedSchema,
  InvalidClass,
  InvalidFlags,
  InvalidReserved,
  InvalidVersion,
  InvalidSignature,
  InvalidPayloadSize,
};

struct UpdateManifest {
  UpdatePackageClass package_class{UpdatePackageClass::Compatibility};
  std::uint16_t flags{};
  std::uint8_t target_device_id[kUpdateDeviceIdSize]{};
  std::uint64_t release_sequence{};
  std::uint32_t payload_size{};
  std::uint8_t payload_sha256[kUpdateDigestSize]{};
  std::uint8_t payload_nonce[kUpdateNonceSize]{};
  std::uint8_t wrapped_content_key[kUpdateWrappedKeySize]{};
  char target_version[kUpdateVersionCapacity]{};
  std::uint8_t signature[kUpdateSignatureCapacity]{};
  std::uint8_t signature_size{};
};

struct ManifestParseResult {
  UpdateManifest manifest{};
  ManifestParseError error{ManifestParseError::WrongSize};
  bool ok{};
};

[[nodiscard]] ManifestParseResult parse_update_manifest(
    const std::uint8_t* wire, std::size_t wire_size);
[[nodiscard]] std::uint32_t maximum_payload_size(
    UpdatePackageClass package_class);

}  // namespace codex
