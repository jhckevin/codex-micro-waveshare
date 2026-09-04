#include "codex/firmware_update.h"

#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

namespace codex {
namespace {

struct FirmwareWriter {
  const esp_partition_t* partition{};
  esp_ota_handle_t handle{};
  std::uint32_t written{};
  bool open{};
};

FirmwareWriter writer;

bool save_pending_sequence(std::uint64_t sequence) {
  nvs_handle_t handle{};
  if (nvs_open_from_partition("codex_nvs", "updates", NVS_READWRITE, &handle) !=
      ESP_OK) return false;
  const bool ok = nvs_set_u64(handle, "pending_seq", sequence) == ESP_OK &&
                  nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}

bool confirm_pending_sequence() {
  nvs_handle_t handle{};
  if (nvs_open_from_partition("codex_nvs", "updates", NVS_READWRITE, &handle) !=
      ESP_OK) return false;
  std::uint64_t sequence = 0;
  const esp_err_t read = nvs_get_u64(handle, "pending_seq", &sequence);
  const esp_err_t erase = nvs_erase_key(handle, "pending_seq");
  const bool ok =
      (read == ESP_ERR_NVS_NOT_FOUND ||
       (read == ESP_OK &&
        nvs_set_u64(handle, "seq_firmware", sequence) == ESP_OK)) &&
      (erase == ESP_OK || erase == ESP_ERR_NVS_NOT_FOUND) &&
      nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}

bool equal_digest(const std::uint8_t* left, const std::uint8_t* right,
                  std::size_t size) {
  unsigned int difference = 0;
  for (std::size_t index = 0; index < size; ++index) {
    difference |= left[index] ^ right[index];
  }
  return difference == 0;
}

bool hash_partition(const esp_partition_t* partition, std::uint32_t size,
                    std::uint8_t output[kUpdateDigestSize]) {
  if (partition == nullptr || size == 0 || size > partition->size) return false;
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  bool ok = mbedtls_sha256_starts(&context, 0) == 0;
  std::uint8_t buffer[1024]{};
  for (std::uint32_t offset = 0; ok && offset < size;) {
    const std::size_t chunk =
        size - offset > sizeof(buffer) ? sizeof(buffer) : size - offset;
    ok = esp_partition_read(partition, offset, buffer, chunk) == ESP_OK &&
         mbedtls_sha256_update(&context, buffer, chunk) == 0;
    offset += static_cast<std::uint32_t>(chunk);
  }
  ok = ok && mbedtls_sha256_finish(&context, output) == 0;
  mbedtls_sha256_free(&context);
  return ok;
}

bool ota_begin(const UpdateManifest& manifest, void*) {
  if (manifest.package_class != UpdatePackageClass::CompleteFirmware ||
      writer.open) {
    return false;
  }
  writer.partition = esp_ota_get_next_update_partition(nullptr);
  writer.written = 0;
  writer.handle = 0;
  if (writer.partition == nullptr ||
      manifest.payload_size > writer.partition->size ||
      esp_ota_begin(writer.partition, manifest.payload_size, &writer.handle) !=
          ESP_OK) {
    writer = {};
    return false;
  }
  writer.open = true;
  return true;
}

bool ota_write(std::uint32_t offset, const std::uint8_t* data,
               std::size_t size, void*) {
  if (!writer.open || offset != writer.written || data == nullptr ||
      esp_ota_write(writer.handle, data, size) != ESP_OK) {
    return false;
  }
  writer.written += static_cast<std::uint32_t>(size);
  return true;
}

bool ota_finalize(const UpdateManifest& manifest, void*) {
  if (!writer.open || writer.written != manifest.payload_size) return false;
  if (esp_ota_end(writer.handle) != ESP_OK) {
    writer = {};
    return false;
  }
  writer.open = false;
  std::uint8_t digest[kUpdateDigestSize]{};
  const bool verified =
      hash_partition(writer.partition, manifest.payload_size, digest) &&
      equal_digest(digest, manifest.payload_sha256, sizeof(digest));
  if (!verified || !save_pending_sequence(manifest.release_sequence) ||
      esp_ota_set_boot_partition(writer.partition) != ESP_OK) {
    writer = {};
    return false;
  }
  writer = {};
  return true;
}

void ota_cancel(void*) {
  if (writer.open) {
    esp_ota_abort(writer.handle);
  }
  writer = {};
}

}  // namespace

UpdateStorage make_firmware_update_storage() {
  return {ota_begin, ota_write, ota_finalize, ota_cancel, nullptr};
}

bool firmware_update_pending_verification() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  esp_ota_img_states_t state = ESP_OTA_IMG_UNDEFINED;
  return running != nullptr &&
         esp_ota_get_state_partition(running, &state) == ESP_OK &&
         state == ESP_OTA_IMG_PENDING_VERIFY;
}

bool confirm_running_firmware() {
  return confirm_pending_sequence() &&
         esp_ota_mark_app_valid_cancel_rollback() == ESP_OK;
}

bool reject_running_firmware_and_reboot() {
  if (!firmware_update_pending_verification()) return false;
  esp_ota_mark_app_invalid_rollback_and_reboot();
  return true;
}

}  // namespace codex
