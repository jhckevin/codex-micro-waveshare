#include "codex/settings_store.h"

#include "nvs.h"
#include "nvs_flash.h"

namespace codex {
namespace {

constexpr char kNamespace[] = "codex_cfg";
constexpr char kBlobKey[] = "settings";
constexpr char kSmartScreensaverKey[] = "smart_saver";
constexpr char kPartition[] = "codex_nvs";

esp_err_t ensure_nvs() {
  esp_err_t error = nvs_flash_init_partition(kPartition);
  if (error == ESP_ERR_NVS_NO_FREE_PAGES ||
      error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    error = nvs_flash_erase_partition(kPartition);
    if (error == ESP_OK) error = nvs_flash_init_partition(kPartition);
  }
  return error;
}

}  // namespace

esp_err_t settings_store_save(PersistentSettings* settings) {
  if (settings == nullptr) return ESP_ERR_INVALID_ARG;
  settings->schema_version = kSettingsSchemaVersion;
  settings->crc32 = settings_crc32(*settings);
  esp_err_t error = ensure_nvs();
  if (error != ESP_OK) return error;
  nvs_handle_t handle{};
  error = nvs_open_from_partition(kPartition, kNamespace, NVS_READWRITE, &handle);
  if (error != ESP_OK) return error;
  error = nvs_set_blob(handle, kBlobKey, settings, sizeof(*settings));
  if (error == ESP_OK) error = nvs_commit(handle);
  nvs_close(handle);
  return error;
}

esp_err_t settings_store_load(PersistentSettings* settings,
                              bool* restored_defaults) {
  if (settings == nullptr) return ESP_ERR_INVALID_ARG;
  if (restored_defaults != nullptr) *restored_defaults = false;
  esp_err_t error = ensure_nvs();
  if (error != ESP_OK) return error;
  nvs_handle_t handle{};
  error = nvs_open_from_partition(kPartition, kNamespace, NVS_READONLY, &handle);
  size_t size = sizeof(*settings);
  if (error == ESP_OK) {
    error = nvs_get_blob(handle, kBlobKey, settings, &size);
    nvs_close(handle);
  }
  if (error == ESP_OK && size == sizeof(*settings) &&
      validate_settings(*settings) == SettingsLoadResult::Valid) {
    return ESP_OK;
  }
  *settings = make_default_settings();
  if (restored_defaults != nullptr) *restored_defaults = true;
  return settings_store_save(settings);
}


esp_err_t settings_store_load_smart_screensaver(bool* enabled) {
  if (enabled == nullptr) return ESP_ERR_INVALID_ARG;
  *enabled = true;
  esp_err_t error = ensure_nvs();
  if (error != ESP_OK) return error;
  nvs_handle_t handle{};
  error = nvs_open_from_partition(kPartition, kNamespace, NVS_READONLY, &handle);
  if (error != ESP_OK) return error;
  std::uint8_t stored = 1;
  error = nvs_get_u8(handle, kSmartScreensaverKey, &stored);
  nvs_close(handle);
  if (error == ESP_ERR_NVS_NOT_FOUND) return ESP_OK;
  if (error != ESP_OK || stored > 1U) return ESP_ERR_INVALID_STATE;
  *enabled = stored != 0U;
  return ESP_OK;
}

esp_err_t settings_store_save_smart_screensaver(bool enabled) {
  esp_err_t error = ensure_nvs();
  if (error != ESP_OK) return error;
  nvs_handle_t handle{};
  error = nvs_open_from_partition(kPartition, kNamespace, NVS_READWRITE, &handle);
  if (error != ESP_OK) return error;
  error = nvs_set_u8(handle, kSmartScreensaverKey, enabled ? 1U : 0U);
  if (error == ESP_OK) error = nvs_commit(handle);
  nvs_close(handle);
  return error;
}

}  // namespace codex
