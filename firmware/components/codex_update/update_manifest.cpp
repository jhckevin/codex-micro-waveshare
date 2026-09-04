#include "codex/update_manifest.h"

namespace codex {
namespace {

constexpr std::uint16_t kRequiredFlags = 0x0003U;

std::uint16_t read16(const std::uint8_t* input) {
  return static_cast<std::uint16_t>(input[0]) |
         static_cast<std::uint16_t>(input[1]) << 8U;
}

std::uint32_t read32(const std::uint8_t* input) {
  std::uint32_t value = 0;
  for (unsigned int index = 0; index < 4; ++index) {
    value |= static_cast<std::uint32_t>(input[index]) << (index * 8U);
  }
  return value;
}

std::uint64_t read64(const std::uint8_t* input) {
  std::uint64_t value = 0;
  for (unsigned int index = 0; index < 8; ++index) {
    value |= static_cast<std::uint64_t>(input[index]) << (index * 8U);
  }
  return value;
}

bool bytes_equal(const std::uint8_t* left, const char* right,
                 std::size_t size) {
  unsigned int different = 0;
  for (std::size_t index = 0; index < size; ++index) {
    different |= left[index] ^ static_cast<std::uint8_t>(right[index]);
  }
  return different == 0;
}

bool reserved_zero(const std::uint8_t* wire) {
  for (std::size_t index = 129; index < 136; ++index) {
    if (wire[index] != 0) return false;
  }
  for (std::size_t index = 169; index < 176; ++index) {
    if (wire[index] != 0) return false;
  }
  return true;
}

bool valid_version_character(std::uint8_t value) {
  return (value >= 'a' && value <= 'z') ||
         (value >= 'A' && value <= 'Z') ||
         (value >= '0' && value <= '9') || value == '.' || value == '_' ||
         value == '+' || value == '-';
}

ManifestParseResult failure(ManifestParseError error) {
  ManifestParseResult result{};
  result.error = error;
  return result;
}

}  // namespace

std::uint32_t maximum_payload_size(UpdatePackageClass package_class) {
  switch (package_class) {
    case UpdatePackageClass::Compatibility:
      return 256U * 1024U;
    case UpdatePackageClass::ServiceReload:
      return 512U * 1024U;
    case UpdatePackageClass::CompleteFirmware:
      return 5U * 1024U * 1024U;
    case UpdatePackageClass::UserContent:
      return 2U * 1024U * 1024U;
  }
  return 0;
}

ManifestParseResult parse_update_manifest(const std::uint8_t* wire,
                                          std::size_t wire_size) {
  if (wire == nullptr || wire_size != kUpdateManifestWireSize) {
    return failure(ManifestParseError::WrongSize);
  }
  if (!bytes_equal(wire, "CMU1", 4)) {
    return failure(ManifestParseError::WrongMagic);
  }
  if (wire[4] != 1U) {
    return failure(ManifestParseError::UnsupportedSchema);
  }
  const auto package_class = static_cast<UpdatePackageClass>(wire[5]);
  if (maximum_payload_size(package_class) == 0) {
    return failure(ManifestParseError::InvalidClass);
  }
  const std::uint16_t flags = read16(wire + 6);
  if (flags != kRequiredFlags) {
    return failure(ManifestParseError::InvalidFlags);
  }
  if (!reserved_zero(wire)) {
    return failure(ManifestParseError::InvalidReserved);
  }
  const std::uint32_t payload_size = read32(wire + 32);
  if (payload_size == 0 ||
      payload_size > maximum_payload_size(package_class)) {
    return failure(ManifestParseError::InvalidPayloadSize);
  }
  const std::uint8_t version_size = wire[128];
  if (version_size == 0 || version_size >= kUpdateVersionCapacity) {
    return failure(ManifestParseError::InvalidVersion);
  }
  for (std::size_t index = 0; index < version_size; ++index) {
    if (!valid_version_character(wire[136 + index])) {
      return failure(ManifestParseError::InvalidVersion);
    }
  }
  for (std::size_t index = version_size; index < kUpdateVersionCapacity;
       ++index) {
    if (wire[136 + index] != 0) {
      return failure(ManifestParseError::InvalidVersion);
    }
  }
  const std::uint8_t signature_size = wire[168];
  if (signature_size < 8 || signature_size > kUpdateSignatureCapacity ||
      wire[176] != 0x30) {
    return failure(ManifestParseError::InvalidSignature);
  }
  for (std::size_t index = signature_size;
       index < kUpdateSignatureCapacity; ++index) {
    if (wire[176 + index] != 0) {
      return failure(ManifestParseError::InvalidSignature);
    }
  }

  ManifestParseResult result{};
  result.ok = true;
  result.error = ManifestParseError::None;
  result.manifest.package_class = package_class;
  result.manifest.flags = flags;
  result.manifest.release_sequence = read64(wire + 24);
  result.manifest.payload_size = payload_size;
  result.manifest.signature_size = signature_size;
  for (std::size_t index = 0; index < kUpdateDeviceIdSize; ++index) {
    result.manifest.target_device_id[index] = wire[8 + index];
  }
  for (std::size_t index = 0; index < kUpdateDigestSize; ++index) {
    result.manifest.payload_sha256[index] = wire[36 + index];
  }
  for (std::size_t index = 0; index < kUpdateNonceSize; ++index) {
    result.manifest.payload_nonce[index] = wire[68 + index];
  }
  for (std::size_t index = 0; index < kUpdateWrappedKeySize; ++index) {
    result.manifest.wrapped_content_key[index] = wire[80 + index];
  }
  for (std::size_t index = 0; index < version_size; ++index) {
    result.manifest.target_version[index] =
        static_cast<char>(wire[136 + index]);
  }
  result.manifest.target_version[version_size] = '\0';
  for (std::size_t index = 0; index < signature_size; ++index) {
    result.manifest.signature[index] = wire[176 + index];
  }
  return result;
}

}  // namespace codex
