#pragma once

#include <cstddef>
#include <cstdint>

namespace codex {

constexpr std::size_t kServicePackHeaderSize = 32;
constexpr std::size_t kServicePackEntrySize = 56;
constexpr unsigned int kServicePackMaximumResources = 16;
constexpr std::size_t kServicePackMaximumBytes = 512U * 1024U;

enum class ServiceResourceType : std::uint8_t {
  ProtocolPolicy = 1,
  ConfigSchema = 2,
  AudioPreset = 3,
  UiResource = 4,
};

enum class ServicePackError : std::uint8_t {
  None,
  InvalidSize,
  WrongMagic,
  UnsupportedSchema,
  InvalidHeader,
  InvalidReserved,
  InvalidResourceType,
  DuplicateResource,
  InvalidEntry,
  InvalidDigest,
};

struct ServiceResourceDescriptor {
  ServiceResourceType type{ServiceResourceType::ProtocolPolicy};
  std::uint32_t resource_id{};
  std::uint32_t offset{};
  std::uint32_t size{};
  std::uint8_t sha256[32]{};
};

struct ServicePackSnapshot {
  std::uint32_t generation{};
  std::uint32_t reload_mask{};
  std::uint8_t resource_count{};
  ServiceResourceDescriptor resources[kServicePackMaximumResources]{};
};

struct ServicePackResult {
  ServicePackSnapshot snapshot{};
  ServicePackError error{ServicePackError::InvalidSize};
  bool ok{};
};

[[nodiscard]] ServicePackResult parse_service_pack(
    const std::uint8_t* wire, std::size_t wire_size);
[[nodiscard]] ServicePackResult parse_service_pack_index(
    const std::uint8_t* index, std::size_t index_size,
    std::size_t total_wire_size);
void activate_service_snapshot(const ServicePackSnapshot& snapshot);
[[nodiscard]] ServicePackSnapshot service_snapshot();

}  // namespace codex
