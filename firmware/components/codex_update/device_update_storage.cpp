#include "codex/device_update_storage.h"

#include "codex/compatibility_pack.h"
#include "codex/firmware_update.h"
#include "codex/service_resource_runtime.h"
#include "codex/service_pack_storage.h"
#include "codex/user_content_storage.h"
#include "esp_partition.h"
#include "mbedtls/sha256.h"
#include "nvs.h"
#include "nvs_flash.h"

namespace codex {
namespace {

constexpr std::uint32_t kFlashEraseSectorBytes = 4096U;

bool erase_payload_range(const esp_partition_t* partition,
                         std::uint32_t payload_size) {
  if (partition == nullptr || payload_size == 0U ||
      payload_size > partition->size) {
    return false;
  }
  const std::uint32_t erase_size =
      (payload_size + kFlashEraseSectorBytes - 1U) &
      ~(kFlashEraseSectorBytes - 1U);
  return erase_size <= partition->size &&
         esp_partition_erase_range(partition, 0, erase_size) == ESP_OK;
}

struct CompatibilityWriter {
  const esp_partition_t* partition{};
  std::uint32_t written{};
  std::uint32_t expected{};
  unsigned char target_slot{};
  bool open{};
};
CompatibilityWriter compatibility;
UpdateStorage selected;

struct ServiceStorageContext {
  const esp_partition_t* partitions[2]{};
  PreparedServiceResources prepared{};
  std::uint8_t preparing_slot{};
};
ServiceStorageContext service_context;

bool equal_digest(const std::uint8_t* left, const std::uint8_t* right,
                  std::size_t size) {
  unsigned int difference = 0;
  for (std::size_t index = 0; index < size; ++index) difference |= left[index] ^ right[index];
  return difference == 0;
}
bool hash_partition_range(const esp_partition_t* partition,
                          std::uint32_t begin, std::uint32_t size,
                          std::uint8_t output[kUpdateDigestSize]);
bool hash_partition(const esp_partition_t* partition, std::uint32_t size,
                    std::uint8_t output[kUpdateDigestSize]) {
  return hash_partition_range(partition, 0, size, output);
}
bool hash_partition_range(const esp_partition_t* partition,
                          std::uint32_t begin, std::uint32_t size,
                          std::uint8_t output[kUpdateDigestSize]) {
  if (partition == nullptr || begin > partition->size ||
      size > partition->size - begin) {
    return false;
  }
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  bool ok = mbedtls_sha256_starts(&context, 0) == 0;
  std::uint8_t buffer[512]{};
  for (std::uint32_t offset = 0; ok && offset < size;) {
    const std::size_t count =
        size - offset > sizeof(buffer) ? sizeof(buffer) : size - offset;
    ok = esp_partition_read(partition, begin + offset, buffer, count) == ESP_OK &&
         mbedtls_sha256_update(&context, buffer, count) == 0;
    offset += static_cast<std::uint32_t>(count);
  }
  ok = ok && mbedtls_sha256_finish(&context, output) == 0;
  mbedtls_sha256_free(&context);
  return ok;
}
unsigned char active_compatibility_slot() {
  nvs_handle_t handle{};
  std::uint8_t slot = 0;
  if (nvs_open_from_partition("codex_nvs", "updates", NVS_READONLY, &handle) ==
      ESP_OK) {
    nvs_get_u8(handle, "compat_slot", &slot);
    nvs_close(handle);
  }
  return slot > 1 ? 0 : slot;
}
bool save_compatibility_slot(unsigned char slot, std::uint64_t sequence) {
  nvs_handle_t handle{};
  if (nvs_open_from_partition("codex_nvs", "updates", NVS_READWRITE, &handle) !=
      ESP_OK) return false;
  const bool ok = nvs_set_u8(handle, "compat_slot", slot) == ESP_OK &&
                  nvs_set_u64(handle, "seq_compat", sequence) == ESP_OK &&
                  nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}
bool compatibility_begin(const UpdateManifest& manifest, void*) {
  if (manifest.package_class != UpdatePackageClass::Compatibility ||
      manifest.payload_size != kCompatibilityPackWireSize) return false;
  compatibility.target_slot = active_compatibility_slot() == 0 ? 1 : 0;
  compatibility.partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA,
      static_cast<esp_partition_subtype_t>(
          compatibility.target_slot == 0 ? 0x40 : 0x41), nullptr);
  if (!erase_payload_range(compatibility.partition, manifest.payload_size)) {
    compatibility = {};
    return false;
  }
  compatibility.expected = manifest.payload_size;
  compatibility.written = 0;
  compatibility.open = true;
  return true;
}
bool compatibility_write(std::uint32_t offset, const std::uint8_t* data,
                         std::size_t size, void*) {
  if (!compatibility.open || offset != compatibility.written ||
      offset + size > compatibility.expected ||
      esp_partition_write(compatibility.partition, offset, data, size) !=
          ESP_OK) return false;
  compatibility.written += static_cast<std::uint32_t>(size);
  return true;
}
bool compatibility_finalize(const UpdateManifest& manifest, void*) {
  if (!compatibility.open ||
      compatibility.written != compatibility.expected) return false;
  std::uint8_t digest[kUpdateDigestSize]{};
  std::uint8_t wire[kCompatibilityPackWireSize]{};
  const bool valid =
      hash_partition(compatibility.partition, compatibility.expected, digest) &&
      equal_digest(digest, manifest.payload_sha256, sizeof(digest)) &&
      esp_partition_read(compatibility.partition, 0, wire, sizeof(wire)) ==
          ESP_OK;
  const CompatibilityPackResult parsed =
      valid ? parse_compatibility_pack(wire, sizeof(wire))
            : CompatibilityPackResult{};
  if (!parsed.ok ||
      !save_compatibility_slot(compatibility.target_slot,
                               manifest.release_sequence)) {
    compatibility = {};
    return false;
  }
  activate_compatibility_snapshot(parsed.snapshot);
  compatibility = {};
  return true;
}
void compatibility_cancel(void*) { compatibility = {}; }

UpdateStorage compatibility_storage() {
  return {compatibility_begin, compatibility_write, compatibility_finalize,
          compatibility_cancel, nullptr};
}

unsigned char active_service_slot() {
  nvs_handle_t handle{};
  std::uint8_t slot = 0;
  if (nvs_open_from_partition("codex_nvs", "updates", NVS_READONLY, &handle) ==
      ESP_OK) {
    nvs_get_u8(handle, "service_slot", &slot);
    nvs_close(handle);
  }
  return slot > 1 ? 0 : slot;
}
bool service_partitions_ready() {
  for (unsigned int slot = 0; slot < 2; ++slot) {
    if (service_context.partitions[slot] == nullptr) {
      service_context.partitions[slot] = esp_partition_find_first(
          ESP_PARTITION_TYPE_DATA,
          static_cast<esp_partition_subtype_t>(slot == 0 ? 0x44 : 0x45),
          nullptr);
    }
    if (service_context.partitions[slot] == nullptr ||
        service_context.partitions[slot]->size < kServicePackMaximumBytes) {
      return false;
    }
  }
  return true;
}
bool service_erase(std::uint8_t slot, std::uint32_t size, void*) {
  return slot < 2 && size <= kServicePackMaximumBytes &&
         service_partitions_ready() &&
         erase_payload_range(service_context.partitions[slot], size);
}
bool service_write(std::uint8_t slot, std::uint32_t offset,
                   const std::uint8_t* data, std::size_t size, void*) {
  return slot < 2 && data != nullptr && service_partitions_ready() &&
         offset <= service_context.partitions[slot]->size &&
         size <= service_context.partitions[slot]->size - offset &&
         esp_partition_write(service_context.partitions[slot], offset, data,
                             size) == ESP_OK;
}
bool service_read(std::uint8_t slot, std::uint32_t offset,
                  std::uint8_t* data, std::size_t size, void*) {
  return slot < 2 && data != nullptr && service_partitions_ready() &&
         offset <= service_context.partitions[slot]->size &&
         size <= service_context.partitions[slot]->size - offset &&
         esp_partition_read(service_context.partitions[slot], offset, data,
                            size) == ESP_OK;
}
bool service_hash(std::uint8_t slot, std::uint32_t offset,
                  std::uint32_t size, std::uint8_t output[32], void*) {
  return slot < 2 && service_partitions_ready() &&
         hash_partition_range(service_context.partitions[slot], offset, size,
                              output);
}
std::uint8_t service_active(void*) { return active_service_slot(); }
bool service_activate(std::uint8_t slot, std::uint32_t size,
                      std::uint64_t sequence, void*) {
  nvs_handle_t handle{};
  if (slot > 1 || size > kServicePackMaximumBytes ||
      nvs_open_from_partition("codex_nvs", "updates", NVS_READWRITE, &handle) !=
          ESP_OK) {
    return false;
  }
  const bool ok = nvs_set_u8(handle, "service_slot", slot) == ESP_OK &&
                  nvs_set_u32(handle, "service_size", size) == ESP_OK &&
                  nvs_set_u64(handle, "seq_service", sequence) == ESP_OK &&
                  nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}
bool read_preparing_service_resource(std::uint32_t offset,
                                     std::uint8_t* output,
                                     std::size_t size, void*) {
  return service_read(service_context.preparing_slot, offset, output, size,
                      nullptr);
}
bool service_prepare(std::uint8_t slot,
                     const ServicePackSnapshot& snapshot, void*) {
  service_context.preparing_slot = slot;
  service_context.prepared = {};
  return prepare_service_resources(
      snapshot, read_preparing_service_resource, nullptr,
      service_context.prepared);
}
void service_apply(std::uint8_t, const ServicePackSnapshot&, void*) {
  activate_service_resources(service_context.prepared);
}
ServicePackStorageBackend service_backend() {
  return {service_erase, service_write, service_read, service_hash,
          service_active, service_activate, &service_context,
          service_prepare, service_apply};
}
ServicePackInstaller& service_installer() {
  static ServicePackInstaller installer(service_backend());
  return installer;
}
bool service_begin(const UpdateManifest& manifest, void*) {
  return service_partitions_ready() && service_installer().begin(manifest);
}
bool service_write_chunk(std::uint32_t offset, const std::uint8_t* data,
                         std::size_t size, void*) {
  return service_installer().write(offset, data, size);
}
bool service_finalize(const UpdateManifest& manifest, void*) {
  return service_installer().finalize(manifest);
}
void service_cancel(void*) { service_installer().cancel(); }
UpdateStorage service_storage() {
  return {service_begin, service_write_chunk, service_finalize, service_cancel,
          nullptr};
}
bool dispatch_begin(const UpdateManifest& manifest, void*) {
  switch (manifest.package_class) {
    case UpdatePackageClass::CompleteFirmware:
      selected = make_firmware_update_storage();
      break;
    case UpdatePackageClass::UserContent:
      selected = make_user_content_update_storage();
      break;
    case UpdatePackageClass::ServiceReload:
      selected = service_storage();
      break;
    case UpdatePackageClass::Compatibility:
      selected = compatibility_storage();
      break;
    default:
      selected = {};
      return false;
  }
  return selected.begin != nullptr &&
         selected.begin(manifest, selected.context);
}
bool dispatch_write(std::uint32_t offset, const std::uint8_t* data,
                    std::size_t size, void*) {
  return selected.write != nullptr &&
         selected.write(offset, data, size, selected.context);
}
bool dispatch_finalize(const UpdateManifest& manifest, void*) {
  return selected.finalize != nullptr &&
         selected.finalize(manifest, selected.context);
}
void dispatch_cancel(void*) {
  if (selected.cancel != nullptr) selected.cancel(selected.context);
  selected = {};
}

}  // namespace

UpdateStorage make_device_update_storage() {
  return {dispatch_begin, dispatch_write, dispatch_finalize, dispatch_cancel,
          nullptr};
}

std::uint64_t stored_update_release_sequence(UpdatePackageClass package_class) {
  nvs_handle_t handle{};
  std::uint64_t sequence = 0;
  const char* key = nullptr;
  switch (package_class) {
    case UpdatePackageClass::Compatibility: key = "seq_compat"; break;
    case UpdatePackageClass::ServiceReload: key = "seq_service"; break;
    case UpdatePackageClass::CompleteFirmware: key = "seq_firmware"; break;
    case UpdatePackageClass::UserContent: key = "seq_content"; break;
    default: return 0;
  }
  if (nvs_open_from_partition("codex_nvs", "updates", NVS_READONLY, &handle) ==
      ESP_OK) {
    const esp_err_t result = nvs_get_u64(handle, key, &sequence);
    // The first production preview stored one global sequence. Only the
    // compatibility package had been activated in that build, so migrate that
    // value for this class without blocking the other independent channels.
    if (result == ESP_ERR_NVS_NOT_FOUND &&
        package_class == UpdatePackageClass::Compatibility) {
      nvs_get_u64(handle, "release_seq", &sequence);
    }
    nvs_close(handle);
  }
  return sequence;
}

bool load_active_compatibility_pack() {
  const unsigned char slot = active_compatibility_slot();
  const esp_partition_t* partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA,
      static_cast<esp_partition_subtype_t>(slot == 0 ? 0x40 : 0x41),
      nullptr);
  if (partition == nullptr) return false;
  std::uint8_t wire[kCompatibilityPackWireSize]{};
  if (esp_partition_read(partition, 0, wire, sizeof(wire)) != ESP_OK) {
    return false;
  }
  const CompatibilityPackResult parsed =
      parse_compatibility_pack(wire, sizeof(wire));
  if (!parsed.ok) return false;
  activate_compatibility_snapshot(parsed.snapshot);
  return true;
}

bool load_active_service_pack() {
  if (!service_partitions_ready()) return false;
  nvs_handle_t handle{};
  std::uint32_t size = 0;
  if (nvs_open_from_partition("codex_nvs", "updates", NVS_READONLY, &handle) !=
      ESP_OK) {
    return false;
  }
  nvs_get_u32(handle, "service_size", &size);
  nvs_close(handle);
  if (size < kServicePackHeaderSize || size > kServicePackMaximumBytes) {
    return false;
  }
  const std::uint8_t slot = active_service_slot();
  std::uint8_t header[kServicePackHeaderSize]{};
  if (!service_read(slot, 0, header, sizeof(header), nullptr)) return false;
  const unsigned int count = header[5];
  if (count == 0 || count > kServicePackMaximumResources) return false;
  constexpr std::size_t kMaximumIndexBytes =
      kServicePackHeaderSize +
      kServicePackMaximumResources * kServicePackEntrySize;
  const std::size_t index_size =
      kServicePackHeaderSize + count * kServicePackEntrySize;
  std::uint8_t index[kMaximumIndexBytes]{};
  if (!service_read(slot, 0, index, index_size, nullptr)) return false;
  const ServicePackResult parsed =
      parse_service_pack_index(index, index_size, size);
  if (!parsed.ok) return false;
  for (unsigned int resource_index = 0;
       resource_index < parsed.snapshot.resource_count; ++resource_index) {
    const ServiceResourceDescriptor& resource =
        parsed.snapshot.resources[resource_index];
    std::uint8_t digest[32]{};
    if (!service_hash(slot, resource.offset, resource.size, digest, nullptr) ||
        !equal_digest(digest, resource.sha256, sizeof(digest))) {
      return false;
    }
  }
  if (!service_prepare(slot, parsed.snapshot, nullptr)) return false;
  activate_service_snapshot(parsed.snapshot);
  service_apply(slot, parsed.snapshot, nullptr);
  return true;
}

}  // namespace codex
