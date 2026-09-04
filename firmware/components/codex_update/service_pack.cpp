#include "codex/service_pack.h"

#include <atomic>

namespace codex {
namespace {

ServicePackSnapshot snapshots[2]{};
std::atomic<unsigned int> active_snapshot{0};

std::uint16_t read16(const std::uint8_t* input) {
  return static_cast<std::uint16_t>(input[0]) |
         static_cast<std::uint16_t>(input[1]) << 8U;
}

std::uint32_t read32(const std::uint8_t* input) {
  return static_cast<std::uint32_t>(input[0]) |
         static_cast<std::uint32_t>(input[1]) << 8U |
         static_cast<std::uint32_t>(input[2]) << 16U |
         static_cast<std::uint32_t>(input[3]) << 24U;
}

bool same_magic(const std::uint8_t* wire) {
  return wire[0] == 'C' && wire[1] == 'S' && wire[2] == 'R' &&
         wire[3] == '1';
}

bool zero_range(const std::uint8_t* input, std::size_t begin,
                std::size_t end) {
  for (std::size_t index = begin; index < end; ++index) {
    if (input[index] != 0) return false;
  }
  return true;
}

bool supported_type(std::uint8_t value) {
  return value >= static_cast<std::uint8_t>(
                      ServiceResourceType::ProtocolPolicy) &&
         value <=
             static_cast<std::uint8_t>(ServiceResourceType::UiResource);
}

bool nonzero_digest(const std::uint8_t* digest) {
  std::uint8_t combined = 0;
  for (unsigned int index = 0; index < 32; ++index) {
    combined |= digest[index];
  }
  return combined != 0;
}

ServicePackResult failure(ServicePackError error) {
  ServicePackResult result{};
  result.error = error;
  return result;
}

}  // namespace

ServicePackResult parse_service_pack_index(const std::uint8_t* wire,
                                           std::size_t index_size,
                                           std::size_t total_wire_size) {
  if (wire == nullptr || index_size < kServicePackHeaderSize ||
      total_wire_size < kServicePackHeaderSize ||
      total_wire_size > kServicePackMaximumBytes) {
    return failure(ServicePackError::InvalidSize);
  }
  if (!same_magic(wire)) {
    return failure(ServicePackError::WrongMagic);
  }
  if (wire[4] != 1U) {
    return failure(ServicePackError::UnsupportedSchema);
  }
  const unsigned int count = wire[5];
  const std::uint16_t flags = read16(wire + 6);
  const std::uint32_t declared_size = read32(wire + 8);
  const std::uint32_t generation = read32(wire + 12);
  const std::uint32_t reload_mask = read32(wire + 16);
  if (count == 0 || count > kServicePackMaximumResources || flags != 0 ||
      declared_size != total_wire_size || generation == 0 ||
      reload_mask == 0 || (reload_mask & ~0x0FU) != 0) {
    return failure(ServicePackError::InvalidHeader);
  }
  if (!zero_range(wire, 20, kServicePackHeaderSize)) {
    return failure(ServicePackError::InvalidReserved);
  }
  const std::size_t directory_end =
      kServicePackHeaderSize + count * kServicePackEntrySize;
  if (directory_end > index_size || directory_end > total_wire_size) {
    return failure(ServicePackError::InvalidSize);
  }

  ServicePackResult result{};
  result.snapshot.generation = generation;
  result.snapshot.reload_mask = reload_mask;
  result.snapshot.resource_count = static_cast<std::uint8_t>(count);
  std::size_t expected_offset = directory_end;
  for (unsigned int index = 0; index < count; ++index) {
    const std::uint8_t* entry =
        wire + kServicePackHeaderSize + index * kServicePackEntrySize;
    if (!supported_type(entry[0])) {
      return failure(ServicePackError::InvalidResourceType);
    }
    if (entry[1] != 0 || entry[2] != 0 || entry[3] != 0 ||
        !zero_range(entry, 48, kServicePackEntrySize)) {
      return failure(ServicePackError::InvalidReserved);
    }
    const std::uint32_t resource_id = read32(entry + 4);
    const std::uint32_t offset = read32(entry + 8);
    const std::uint32_t size = read32(entry + 12);
    if (resource_id == 0 || offset != expected_offset || size == 0 ||
        size > total_wire_size - offset) {
      return failure(ServicePackError::InvalidEntry);
    }
    for (unsigned int previous = 0; previous < index; ++previous) {
      if (result.snapshot.resources[previous].resource_id == resource_id) {
        return failure(ServicePackError::DuplicateResource);
      }
    }
    if (!nonzero_digest(entry + 16)) {
      return failure(ServicePackError::InvalidDigest);
    }
    ServiceResourceDescriptor& descriptor =
        result.snapshot.resources[index];
    descriptor.type = static_cast<ServiceResourceType>(entry[0]);
    descriptor.resource_id = resource_id;
    descriptor.offset = offset;
    descriptor.size = size;
    for (unsigned int digest_index = 0; digest_index < 32; ++digest_index) {
      descriptor.sha256[digest_index] = entry[16 + digest_index];
    }
    expected_offset += size;
  }
  if (expected_offset != total_wire_size) {
    return failure(ServicePackError::InvalidEntry);
  }
  result.ok = true;
  result.error = ServicePackError::None;
  return result;
}

ServicePackResult parse_service_pack(const std::uint8_t* wire,
                                     std::size_t wire_size) {
  return parse_service_pack_index(wire, wire_size, wire_size);
}

void activate_service_snapshot(const ServicePackSnapshot& snapshot) {
  const unsigned int next =
      (active_snapshot.load(std::memory_order_relaxed) + 1U) & 1U;
  snapshots[next] = snapshot;
  active_snapshot.store(next, std::memory_order_release);
}

ServicePackSnapshot service_snapshot() {
  return snapshots[active_snapshot.load(std::memory_order_acquire)];
}

}  // namespace codex
