#include "codex/service_pack_storage.h"

namespace codex {
namespace {

constexpr std::size_t kMaximumIndexBytes =
    kServicePackHeaderSize +
    kServicePackMaximumResources * kServicePackEntrySize;

bool same_digest(const std::uint8_t* left, const std::uint8_t* right) {
  std::uint8_t difference = 0;
  for (unsigned int index = 0; index < 32; ++index) {
    difference |= static_cast<std::uint8_t>(left[index] ^ right[index]);
  }
  return difference == 0;
}

}  // namespace

ServicePackInstaller::ServicePackInstaller(ServicePackStorageBackend backend)
    : backend_(backend) {}

bool ServicePackInstaller::begin(const UpdateManifest& manifest) {
  cancel();
  if (manifest.package_class != UpdatePackageClass::ServiceReload ||
      manifest.payload_size < kServicePackHeaderSize ||
      manifest.payload_size > kServicePackMaximumBytes ||
      backend_.erase == nullptr || backend_.write == nullptr ||
      backend_.read == nullptr || backend_.hash == nullptr ||
      backend_.active_slot == nullptr || backend_.activate == nullptr) {
    return false;
  }
  const std::uint8_t active = backend_.active_slot(backend_.context);
  if (active > 1U) return false;
  target_slot_ = static_cast<std::uint8_t>(active ^ 1U);
  if (!backend_.erase(target_slot_, manifest.payload_size,
                      backend_.context)) {
    return false;
  }
  expected_size_ = manifest.payload_size;
  release_sequence_ = manifest.release_sequence;
  for (unsigned int index = 0; index < 32; ++index) {
    expected_digest_[index] = manifest.payload_sha256[index];
  }
  open_ = true;
  return true;
}

bool ServicePackInstaller::write(std::uint32_t offset,
                                 const std::uint8_t* data,
                                 std::size_t size) {
  if (!open_ || data == nullptr || size == 0 || offset != written_ ||
      size > expected_size_ - written_) {
    return false;
  }
  if (!backend_.write(target_slot_, offset, data, size, backend_.context)) {
    cancel();
    return false;
  }
  written_ += static_cast<std::uint32_t>(size);
  return true;
}

bool ServicePackInstaller::finalize(const UpdateManifest& manifest) {
  if (!open_ || written_ != expected_size_ ||
      manifest.package_class != UpdatePackageClass::ServiceReload ||
      manifest.payload_size != expected_size_ ||
      manifest.release_sequence != release_sequence_ ||
      !same_digest(manifest.payload_sha256, expected_digest_)) {
    cancel();
    return false;
  }

  std::uint8_t payload_digest[32]{};
  if (!backend_.hash(target_slot_, 0, expected_size_, payload_digest,
                     backend_.context) ||
      !same_digest(payload_digest, expected_digest_)) {
    cancel();
    return false;
  }

  std::uint8_t header[kServicePackHeaderSize]{};
  if (!backend_.read(target_slot_, 0, header, sizeof(header),
                     backend_.context)) {
    cancel();
    return false;
  }
  const unsigned int resource_count = header[5];
  if (resource_count == 0 ||
      resource_count > kServicePackMaximumResources) {
    cancel();
    return false;
  }
  const std::size_t index_size =
      kServicePackHeaderSize + resource_count * kServicePackEntrySize;
  if (index_size > kMaximumIndexBytes || index_size > expected_size_) {
    cancel();
    return false;
  }
  std::uint8_t index[kMaximumIndexBytes]{};
  if (!backend_.read(target_slot_, 0, index, index_size,
                     backend_.context)) {
    cancel();
    return false;
  }
  const ServicePackResult parsed =
      parse_service_pack_index(index, index_size, expected_size_);
  if (!parsed.ok) {
    cancel();
    return false;
  }
  for (unsigned int resource_index = 0;
       resource_index < parsed.snapshot.resource_count; ++resource_index) {
    const ServiceResourceDescriptor& resource =
        parsed.snapshot.resources[resource_index];
    std::uint8_t resource_digest[32]{};
    if (!backend_.hash(target_slot_, resource.offset, resource.size,
                       resource_digest, backend_.context) ||
        !same_digest(resource_digest, resource.sha256)) {
      cancel();
      return false;
    }
  }
  if (backend_.prepare != nullptr &&
      !backend_.prepare(target_slot_, parsed.snapshot, backend_.context)) {
    cancel();
    return false;
  }
  if (!backend_.activate(target_slot_, expected_size_, release_sequence_,
                         backend_.context)) {
    cancel();
    return false;
  }
  activate_service_snapshot(parsed.snapshot);
  if (backend_.apply != nullptr) {
    backend_.apply(target_slot_, parsed.snapshot, backend_.context);
  }
  open_ = false;
  return true;
}

void ServicePackInstaller::cancel() {
  target_slot_ = 0;
  expected_size_ = 0;
  written_ = 0;
  release_sequence_ = 0;
  for (auto& value : expected_digest_) value = 0;
  open_ = false;
}

}  // namespace codex
