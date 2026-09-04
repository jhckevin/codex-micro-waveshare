#pragma once

#include "codex/persistent_settings.h"
#include "esp_err.h"

namespace codex {

// Loads the validated settings blob. Missing, corrupt, or old-schema data is
// replaced atomically with defaults so boot never depends on host software.
esp_err_t settings_store_load(PersistentSettings* settings,
                              bool* restored_defaults = nullptr);
esp_err_t settings_store_save(PersistentSettings* settings);
esp_err_t settings_store_load_smart_screensaver(bool* enabled);
esp_err_t settings_store_save_smart_screensaver(bool enabled);

}  // namespace codex
