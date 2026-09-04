#pragma once

#include "codex/config_gatt_limits.h"
#include "codex/transport.h"
#include "esp_err.h"
#include "esp_gatts_api.h"

namespace codex {

[[nodiscard]] esp_err_t config_gatt_prepare(TransportHooks hooks);
[[nodiscard]] esp_err_t config_gatt_start();
bool config_gatt_stop();
void config_gatt_finalize_stop();
void config_gatt_set_authorized(bool authorized);
bool config_gatt_flush(unsigned int timeout_ms);
bool config_gatt_send_event(const char* json, unsigned int length,
                            bool critical);
[[nodiscard]] bool config_gatt_owns_event(
    esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
    const esp_ble_gatts_cb_param_t* params);
void config_gatt_handle_event(esp_gatts_cb_event_t event,
                              esp_gatt_if_t gatts_if,
                              esp_ble_gatts_cb_param_t* params);

}  // namespace codex
