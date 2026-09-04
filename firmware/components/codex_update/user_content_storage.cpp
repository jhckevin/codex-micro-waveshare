#include "codex/user_content_storage.h"
#include "codex/icon_id.h"

#include "esp_partition.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mbedtls/sha256.h"
#include "nvs.h"

namespace codex {
namespace {

constexpr char kTag[] = "codex_content";

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

constexpr esp_partition_subtype_t kContentSlotSubtypes[] = {
    static_cast<esp_partition_subtype_t>(0x42),
    static_cast<esp_partition_subtype_t>(0x43),
};
constexpr std::size_t kMaximumIndexBytes =
    kUserContentHeaderSize +
    kUserContentMaximumIcons * kUserContentEntrySize;

struct ContentWriter {
  const esp_partition_t* partition{};
  std::uint32_t written{};
  std::uint32_t expected{};
  std::uint8_t expected_digest[kUpdateDigestSize]{};
  unsigned char target_slot{};
  bool open{};
  bool complete{};
};

ContentWriter writer;
std::uint8_t index_buffer[kMaximumIndexBytes]{};
UserContentPackResult validation_index{};

struct CachedContentIndex {
  bool loaded{};
  bool valid{};
  unsigned char slot{};
  std::uint32_t size{};
  const esp_partition_t* partition{};
  UserContentPackResult parsed{};
} cached_index;

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
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  bool ok = mbedtls_sha256_starts(&context, 0) == 0;
  std::uint8_t buffer[512]{};
  for (std::uint32_t offset = 0; ok && offset < size;) {
    const std::size_t count =
        size - offset > sizeof(buffer) ? sizeof(buffer) : size - offset;
    ok = esp_partition_read(partition, offset, buffer, count) == ESP_OK &&
         mbedtls_sha256_update(&context, buffer, count) == 0;
    offset += static_cast<std::uint32_t>(count);
  }
  ok = ok && mbedtls_sha256_finish(&context, output) == 0;
  mbedtls_sha256_free(&context);
  return ok;
}

unsigned char active_slot(std::uint32_t* size = nullptr) {
  nvs_handle_t handle{};
  std::uint8_t slot = 0;
  std::uint32_t stored_size = 0;
  if (nvs_open_from_partition("codex_nvs", "updates", NVS_READONLY, &handle) ==
      ESP_OK) {
    nvs_get_u8(handle, "content_slot", &slot);
    nvs_get_u32(handle, "content_size", &stored_size);
    nvs_close(handle);
  }
  if (size != nullptr) *size = stored_size;
  return slot > 1 ? 0 : slot;
}

const esp_partition_t* content_partition(unsigned char slot) {
  return esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                  kContentSlotSubtypes[slot > 1 ? 0 : slot],
                                  nullptr);
}

bool save_active(unsigned char slot, std::uint32_t size,
                 const std::uint8_t digest[kUpdateDigestSize],
                 const std::uint64_t* sequence) {
  nvs_handle_t handle{};
  if (nvs_open_from_partition("codex_nvs", "updates", NVS_READWRITE, &handle) !=
      ESP_OK) {
    return false;
  }
  bool ok =
      nvs_set_u8(handle, "content_slot", slot) == ESP_OK &&
      nvs_set_u32(handle, "content_size", size) == ESP_OK &&
      nvs_set_blob(handle, "content_sha", digest, kUpdateDigestSize) == ESP_OK;
  if (ok && sequence != nullptr) {
    ok = nvs_set_u64(handle, "seq_content", *sequence) == ESP_OK;
  }
  ok = ok && nvs_commit(handle) == ESP_OK;
  nvs_close(handle);
  return ok;
}

bool read_index(const esp_partition_t* partition, std::uint32_t total_size,
                UserContentPackResult& output) {
  if (partition == nullptr || total_size < kUserContentHeaderSize ||
      total_size > partition->size ||
      esp_partition_read(partition, 0, index_buffer,
                         kUserContentHeaderSize) != ESP_OK) {
    return false;
  }
  const unsigned int count = index_buffer[5];
  if (count > kUserContentMaximumIcons) return false;
  const std::size_t index_size =
      kUserContentHeaderSize + count * kUserContentEntrySize;
  if (index_size > kUserContentHeaderSize &&
      esp_partition_read(partition, kUserContentHeaderSize,
                         index_buffer + kUserContentHeaderSize,
                         index_size - kUserContentHeaderSize) != ESP_OK) {
    return false;
  }
  output =
      parse_user_content_index(index_buffer, index_size, total_size);
  return output.ok;
}

bool begin_writer(std::uint32_t size,
                  const std::uint8_t digest[kUpdateDigestSize]) {
  if (digest == nullptr || size < kUserContentHeaderSize ||
      size > kUserContentMaximumBytes) {
    return false;
  }
  if (writer.open) {
    return writer.expected == size &&
           equal_digest(writer.expected_digest, digest, kUpdateDigestSize);
  }
  writer.target_slot = active_slot() == 0 ? 1 : 0;
  writer.partition = content_partition(writer.target_slot);
  if (!erase_payload_range(writer.partition, size)) {
    writer = {};
    return false;
  }
  writer.expected = size;
  writer.written = 0;
  for (std::size_t index = 0; index < kUpdateDigestSize; ++index) {
    writer.expected_digest[index] = digest[index];
  }
  writer.open = true;
  writer.complete = false;
  return true;
}

bool begin(const UpdateManifest& manifest, void*) {
  return manifest.package_class == UpdatePackageClass::UserContent &&
         begin_writer(manifest.payload_size, manifest.payload_sha256);
}

bool write_writer(std::uint32_t offset, const std::uint8_t* data,
                  std::size_t size) {
  if (!writer.open || offset != writer.written ||
      data == nullptr || size == 0 ||
      offset + size > writer.expected ||
      esp_partition_write(writer.partition, offset, data, size) != ESP_OK) {
    return false;
  }
  writer.written += static_cast<std::uint32_t>(size);
  return true;
}

bool write(std::uint32_t offset, const std::uint8_t* data, std::size_t size,
           void*) {
  return write_writer(offset, data, size);
}

bool finalize_writer(const std::uint64_t* release_sequence) {
  if (!writer.open || writer.written != writer.expected) return false;
  const std::uint64_t started_us = esp_timer_get_time();
  ESP_LOGI(kTag, "finalize start bytes=%lu slot=%u",
           static_cast<unsigned long>(writer.expected), writer.target_slot);
  std::uint8_t digest[kUpdateDigestSize]{};
  validation_index = {};
  const bool hashed = hash_partition(writer.partition, writer.expected, digest);
  ESP_LOGI(kTag, "finalize hash=%u elapsed_ms=%llu", hashed,
           (esp_timer_get_time() - started_us) / 1000ULL);
  const bool digest_ok =
      hashed && equal_digest(digest, writer.expected_digest, sizeof(digest));
  const bool indexed =
      digest_ok && read_index(writer.partition, writer.expected,
                              validation_index);
  ESP_LOGI(kTag, "finalize digest=%u index=%u icons=%u elapsed_ms=%llu",
           digest_ok, indexed, validation_index.icon_count,
           (esp_timer_get_time() - started_us) / 1000ULL);
  const bool activated = indexed &&
      save_active(writer.target_slot, writer.expected, digest,
                  release_sequence);
  const bool valid = activated;
  ESP_LOGI(kTag, "finalize activate=%u elapsed_ms=%llu", activated,
           (esp_timer_get_time() - started_us) / 1000ULL);
  const bool complete = valid;
  writer = {};
  if (valid) cached_index = {};
  writer.complete = complete;
  return valid;
}

bool finalize(const UpdateManifest& manifest, void*) {
  return finalize_writer(&manifest.release_sequence);
}

void cancel(void*) {
  writer = {};
}

bool same_id(const char* left, const char* right) {
  if (left == nullptr || right == nullptr) return false;
  std::size_t index = 0;
  while (left[index] && right[index] && left[index] == right[index]) ++index;
  return left[index] == right[index];
}

}  // namespace

UpdateStorage make_user_content_update_storage() {
  return {begin, write, finalize, cancel, nullptr};
}

bool load_active_user_content_index(UserContentPackResult& output) {
  std::uint32_t size = 0;
  const unsigned char slot = active_slot(&size);
  if (cached_index.loaded && cached_index.slot == slot &&
      cached_index.size == size) {
    if (cached_index.valid) output = cached_index.parsed;
    return cached_index.valid;
  }
  cached_index = {};
  cached_index.loaded = true;
  cached_index.slot = slot;
  cached_index.size = size;
  if (size == 0) return false;
  const esp_partition_t* partition = content_partition(slot);
  std::uint8_t expected[kUpdateDigestSize]{};
  std::size_t expected_size = sizeof(expected);
  nvs_handle_t handle{};
  bool has_digest = false;
  if (nvs_open_from_partition("codex_nvs", "updates", NVS_READONLY, &handle) ==
      ESP_OK) {
    has_digest =
        nvs_get_blob(handle, "content_sha", expected, &expected_size) == ESP_OK &&
        expected_size == sizeof(expected);
    nvs_close(handle);
  }
  std::uint8_t actual[kUpdateDigestSize]{};
  cached_index.valid =
      has_digest && partition != nullptr &&
      hash_partition(partition, size, actual) &&
      equal_digest(actual, expected, sizeof(actual)) &&
      read_index(partition, size, cached_index.parsed);
  cached_index.partition = cached_index.valid ? partition : nullptr;
  if (cached_index.valid) output = cached_index.parsed;
  return cached_index.valid;
}

bool read_active_user_content_icon(const char* id, std::uint8_t* output,
                                   std::size_t output_size) {
  if (output == nullptr || output_size != kUserContentIconBytes) return false;
  UserContentPackResult index{};
  if (!load_active_user_content_index(index)) return false;
  for (unsigned int item = 0; item < index.icon_count; ++item) {
    if (!same_id(index.icons[item].id, id)) continue;
    return cached_index.partition != nullptr &&
           esp_partition_read(cached_index.partition,
                              index.icons[item].offset, output,
                              output_size) == ESP_OK;
  }
  return false;
}

bool read_active_user_content_icon_by_hash(
    std::uint32_t id_hash, std::uint8_t* output,
    std::size_t output_size) {
  if (id_hash == 0 || output == nullptr ||
      output_size != kUserContentIconBytes) return false;
  UserContentPackResult index{};
  if (!load_active_user_content_index(index)) return false;
  for (unsigned int item = 0; item < index.icon_count; ++item) {
    if (icon_id_hash(index.icons[item].id) != id_hash) continue;
    return cached_index.partition != nullptr &&
           esp_partition_read(cached_index.partition,
                              index.icons[item].offset, output,
                              output_size) == ESP_OK;
  }
  return false;
}

bool begin_user_content_upload(
    std::uint32_t total_size,
    const std::uint8_t digest[kUserContentDigestBytes]) {
  return begin_writer(total_size, digest);
}

bool write_user_content_upload(std::uint32_t offset, const std::uint8_t* data,
                               std::size_t size) {
  return write_writer(offset, data, size);
}

bool commit_user_content_upload() {
  return finalize_writer(nullptr);
}

void cancel_user_content_upload() {
  writer = {};
}

UserContentTransferSnapshot user_content_upload_snapshot() {
  return {
      .open = writer.open,
      .complete = writer.complete,
      .expected_offset = writer.written,
      .total_size = writer.expected,
  };
}

}  // namespace codex
