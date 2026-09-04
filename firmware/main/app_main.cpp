#include "recovery_guard.h"
#include "codex/arcade_input.h"
#include "codex/arcade_trigger.h"
#include "codex/audio.h"
#include "codex/board_controls.h"
#include "codex/board_power.h"
#include "codex/battery_publish_policy.h"
#include "codex/device_reducer.h"
#include "codex/display_transition_policy.h"
#include "codex/firmware_update.h"
#include "codex/device_update_storage.h"
#include "codex/update_identity.h"
#include "codex/update_protocol.h"
#include "codex/user_content_protocol.h"
#include "codex/user_content_storage.h"
#include "codex/input.h"
#include "codex/input_priority.h"
#include "codex/touch_contact_policy.h"
#include "codex/joystick_policy.h"
#include "codex/lighting_compositor.h"
#include "codex/lighting_mailbox.h"
#include "codex/lighting_lut.h"
#include "codex/persistent_settings.h"
#include "codex/power_policy.h"
#include "codex/protected_unlock.h"
#include "codex/settings_store.h"
#include "codex/ui.h"
#include "codex/transport.h"
#include "codex/transport_policy.h"

#include <atomic>

#include "sdkconfig.h"
#if CONFIG_CODEX_DEVELOPMENT_HIL
#include "display_monitor_runtime.h"
#include "hil_console.h"
#endif

#include "esp_err.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_pm.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "esp_lvgl_port.h"
#if CONFIG_CODEX_DEVELOPMENT_HIL
#include "driver/uart.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "lvgl.h"

namespace {

constexpr char kTag[] = "codex_micro";
QueueHandle_t event_queue{};
QueueHandle_t critical_event_queue{};
TaskHandle_t reducer_task_handle{};
codex::MultiTouchNormalizer input = codex::make_multi_touch_normalizer();
const codex::ControlLayout input_layout = codex::make_control_layout();
codex::DeviceState device_state = codex::make_default_state();
codex::AppKeyLightingState app_key_lighting{};
codex::LightingMailbox lighting_mailbox{};
codex::PersistentSettings settings = codex::make_default_settings();
codex::TransportHooks transport_hooks{};
codex::UpdateSession update_session(codex::make_device_update_storage());
codex::UpdateProtocolContext update_protocol_context{
    .session = &update_session,
};
bool begin_user_content(std::uint32_t size, const std::uint8_t* digest, void*) {
  return codex::begin_user_content_upload(size, digest);
}
bool write_user_content(std::uint32_t offset, const std::uint8_t* data,
                        std::size_t size, void*) {
  return codex::write_user_content_upload(offset, data, size);
}
bool commit_user_content(void*) {
  return codex::commit_user_content_upload();
}
void cancel_user_content(void*) {
  codex::cancel_user_content_upload();
}
codex::UserContentTransferSnapshot snapshot_user_content(void*) {
  return codex::user_content_upload_snapshot();
}
codex::UserContentProtocolContext user_content_protocol_context{
    .begin = begin_user_content,
    .write = write_user_content,
    .commit = commit_user_content,
    .cancel = cancel_user_content,
    .snapshot = snapshot_user_content,
};

bool provide_update_attestation(
    const std::uint8_t challenge[codex::kUpdateDigestSize],
    std::uint8_t tag[codex::kUpdateDigestSize], void*) {
  return codex::make_update_attestation(
      update_protocol_context.trust.expected_device_id, challenge, tag);
}
bool usb_identity_allowed{};
SemaphoreHandle_t state_mutex{};
SemaphoreHandle_t settings_io_mutex{};
unsigned int reboot_at_ms{};
unsigned int settings_dirty_at_ms{};
unsigned int last_user_activity_ms{};
unsigned int lighting_dark_since_ms{};
unsigned int diagnostics_at_ms{};
unsigned int runtime_log_at_ms{};
unsigned int overlay_tick_at_ms{};
unsigned int last_ui_render_ms{};
unsigned int slow_frame_count{};
unsigned int disconnected_since_ms{};
unsigned int auto_transport_retry_at_ms{};
codex::AutoTransportDebounce auto_transport_debounce{};
codex::UltraStandbyGuard ultra_standby_guard{};
bool ultra_hardware_active{};
bool ultra_wake_enqueued{};
bool ultra_usb_retained{};
portMUX_TYPE diagnostics_lock = portMUX_INITIALIZER_UNLOCKED;
portMUX_TYPE lighting_mailbox_lock = portMUX_INITIALIZER_UNLOCKED;
unsigned int dropped_events{};
unsigned int event_queue_high_water{};
unsigned int last_input_queue_latency_us{};
unsigned int maximum_input_queue_latency_us{};
unsigned int last_touch_callback_us{};
unsigned int last_touch_callback_gap_us{};
unsigned int maximum_touch_callback_gap_interval_us{};
bool board_touch_enabled{true};
codex::TouchContactState touch_contact_state{};
unsigned char power_mode_mirror{
    static_cast<unsigned char>(codex::PowerMode::Active)};
codex::ProtectedUnlockState protected_unlock{};
bool protected_touch_down{};
unsigned char joystick_sensitivity_mirror{100};
bool arcade_trigger_requested{};
codex::ArcadeInputState arcade_input = codex::make_arcade_input_state();
codex::ArcadeTrigger arcade_trigger{};
unsigned char arcade_phase{};
unsigned char arcade_ui_command_requested{};
lv_indev_t* touch_input_device{};
unsigned int touch_polling_restore_at_ms{};
#if CONFIG_CODEX_DEVELOPMENT_HIL
QueueHandle_t hil_command_queue{};
lv_indev_t* hil_pointer_device{};
std::atomic_int hil_pointer_x{};
std::atomic_int hil_pointer_y{};
std::atomic_bool hil_pointer_pressed{};
std::atomic_bool hil_touch_override{};
bool hil_touch_active[codex::kMaxTouchPoints]{};
codex::Point hil_touch_points[codex::kMaxTouchPoints]{};
unsigned int hil_tap_release_at_ms{};
#endif

bool enqueue_event(const codex::DeviceEvent& event, TickType_t wait,
                   const char* source) {
  codex::DeviceEvent queued = event;
  queued.enqueued_at_us =
      static_cast<unsigned int>(esp_timer_get_time());
  if (event_queue != nullptr &&
      xQueueSend(event_queue, &queued, wait) == pdTRUE) {
    const unsigned int used = 24U - uxQueueSpacesAvailable(event_queue);
    portENTER_CRITICAL(&diagnostics_lock);
    if (used > event_queue_high_water) event_queue_high_water = used;
    portEXIT_CRITICAL(&diagnostics_lock);
    return true;
  }
  portENTER_CRITICAL(&diagnostics_lock);
  ++dropped_events;
  const unsigned int drops = dropped_events;
  portEXIT_CRITICAL(&diagnostics_lock);
  if (drops == 1U || (drops % 16U) == 0U) {
    ESP_LOGW(kTag, "event queue drop source=%s total=%u", source, drops);
  }
  return false;
}

bool enqueue_touch_event(const codex::DeviceEvent& event) {
  const bool critical = codex::input_event_is_latency_critical(event.type);
  if (!critical) return enqueue_event(event, 0, "touch");
  codex::DeviceEvent queued = event;
  queued.enqueued_at_us =
      static_cast<unsigned int>(esp_timer_get_time());
  if (critical_event_queue != nullptr &&
      xQueueSend(critical_event_queue, &queued, 0) == pdTRUE) {
    if (reducer_task_handle != nullptr) {
      xTaskNotifyGive(reducer_task_handle);
    }
    return true;
  }
  portENTER_CRITICAL(&diagnostics_lock);
  ++dropped_events;
  const unsigned int drops = dropped_events;
  portEXIT_CRITICAL(&diagnostics_lock);
  ESP_LOGE(kTag, "critical input release queue full total_drops=%u", drops);
  return false;
}

esp_err_t apply_transport_mode(codex::TransportMode mode,
                               bool bluetooth_enabled = true) {
  if (!usb_identity_allowed) mode = codex::TransportMode::Ble;
  if (!bluetooth_enabled) {
    esp_err_t error = codex::codex_ble_stop();
    if (error != ESP_OK) return error;
    if (usb_identity_allowed && mode != codex::TransportMode::Ble) {
      return codex::codex_usb_start(transport_hooks, true);
    }
    return codex::codex_usb_stop();
  }
  if (mode == codex::TransportMode::Ble) {
    // Keep the native USB composite mounted as the wired configuration and
    // recovery channel while BLE owns Codex input. Active-transport routing
    // already ignores USB HID in BLE mode, so this preserves configuration
    // access without duplicating key events or changing the selected slot.
    // It also makes a persisted BLE slot recoverable with a cable attached.
    esp_err_t error = usb_identity_allowed
                          ? codex::codex_usb_start(transport_hooks, true)
                          : codex::codex_usb_stop();
    if (error != ESP_OK) return error;
    return codex::codex_ble_start(transport_hooks);
  }
  if (mode == codex::TransportMode::Usb) {
    esp_err_t error = codex::codex_ble_stop();
    if (error != ESP_OK) return error;
    return codex::codex_usb_start(transport_hooks, true);
  }

  // Initialize BLE/NVS before exposing native USB to the host.  With both
  // cables attached Windows enumerates TinyUSB immediately; starting BLE/NVS
  // concurrently with those callbacks can corrupt the allocator during cold
  // boot.  Serializing the two subsystem starts removes that race without
  // adding any steady-state latency.
  esp_err_t error = codex::codex_ble_start(transport_hooks);
  if (error != ESP_OK) return error;
  return codex::codex_usb_start(transport_hooks, true);
}

// A transport setting can arrive over USB while connected standby is active.
// The reducer deliberately defers hardware reconstruction in that state, so
// wake must reconcile the persisted selection once.  Auto is handled without
// briefly starting BLE when an already-mounted USB link should own the device.
esp_err_t reconcile_transport_after_wake(codex::TransportMode mode,
                                         bool bluetooth_enabled) {
  if (mode != codex::TransportMode::Auto || !usb_identity_allowed) {
    return apply_transport_mode(mode, bluetooth_enabled);
  }
  esp_err_t error = codex::codex_usb_start(transport_hooks, true);
  if (error != ESP_OK) return error;
  const bool should_run_ble = codex::keep_ble_running(
      mode, codex::codex_usb_connected(), bluetooth_enabled);
  return should_run_ble ? codex::codex_ble_start(transport_hooks)
                        : codex::codex_ble_stop();
}

void set_power_profile(bool standby) {
  const esp_pm_config_t config = {
      .max_freq_mhz = standby ? 80 : 240,
      .min_freq_mhz = 80,
      // Bluedroid cannot coexist with ESP-IDF automatic light sleep on this
      // target. Keep protocol work at 80 MHz while the display is dark;
      // super-standby uses PMIC off.
      .light_sleep_enable = false,
  };
  const esp_err_t error = esp_pm_configure(&config);
  if (error != ESP_OK) {
    ESP_LOGW(kTag, "power profile change failed: %s", esp_err_to_name(error));
  }
}

unsigned char physical_brightness(unsigned char setting) {
  return setting;
}

void hard_off_display_backlight() {
  const esp_err_t error = bsp_display_backlight_off();
  if (error != ESP_OK) {
    ESP_LOGE(kTag, "backlight hard-off failed: %s", esp_err_to_name(error));
  }
}

void fade_display_out(unsigned char brightness) {
  if (!bsp_display_lock(50)) {
    hard_off_display_backlight();
  } else {
    for (unsigned int step = 0; step < codex::kDisplayFadeStepCount; ++step) {
      const unsigned char level = codex::display_fade_level(brightness, step);
      if (level == 0U) break;
      bsp_display_brightness_set(physical_brightness(level));
      vTaskDelay(pdMS_TO_TICKS(codex::kDisplayFadeStepDelayMs));
    }
    hard_off_display_backlight();
    bsp_display_unlock();
  }
  const esp_err_t panel_error = bsp_display_panel_set_enabled(false);
  if (panel_error != ESP_OK) {
    ESP_LOGW(kTag, "panel standby failed: %s",
             esp_err_to_name(panel_error));
  }
}

void fade_display_in(unsigned char brightness) {
  const esp_err_t backlight_error = bsp_display_backlight_prepare_wake();
  if (backlight_error != ESP_OK) {
    ESP_LOGE(kTag, "backlight wake failed: %s",
             esp_err_to_name(backlight_error));
    return;
  }
  const esp_err_t panel_error = bsp_display_panel_set_enabled(true);
  if (panel_error != ESP_OK) {
    ESP_LOGW(kTag, "panel wake failed: %s", esp_err_to_name(panel_error));
  }
  if (!bsp_display_lock(50)) {
    bsp_display_brightness_set(physical_brightness(brightness));
    return;
  }
  for (unsigned int reverse = codex::kDisplayFadeStepCount; reverse > 0U;
       --reverse) {
    const unsigned char level =
        codex::display_fade_level(brightness, reverse - 1U);
    if (level == 0U) continue;
    bsp_display_brightness_set(physical_brightness(level));
    vTaskDelay(pdMS_TO_TICKS(codex::kDisplayFadeStepDelayMs));
  }
  bsp_display_unlock();
}

void copy_text(char* destination, unsigned int capacity, const char* source) {
  unsigned int index = 0;
  while (index + 1 < capacity && source[index]) {
    destination[index] = source[index];
    ++index;
  }
  destination[index] = '\0';
}

bool same_text(const char* left, const char* right) {
  unsigned int index = 0;
  while (left[index] && right[index] && left[index] == right[index]) ++index;
  return left[index] == right[index];
}

bool same_peer(const codex::BlePeerState& left, const codex::BlePeerState& right) {
  if (left.bonded != right.bonded || left.address_type != right.address_type) return false;
  for (unsigned int index = 0; index < 6; ++index) {
    if (left.address[index] != right.address[index]) return false;
  }
  return true;
}

bool same_routing_preferences(const codex::RoutingConfig& left,
                              const codex::RoutingConfig& right) {
  if (left.layer_routing_enabled != right.layer_routing_enabled ||
      left.configured_layer_count != right.configured_layer_count) {
    return false;
  }
  for (unsigned int index = 0;
       index < codex::kPhysicalControlsPerGroup; ++index) {
    if (left.layer1_command_app_targets[index] !=
        right.layer1_command_app_targets[index]) {
      return false;
    }
  }
  for (unsigned int layer = 0; layer < codex::kMaximumLayerCount - 1;
       ++layer) {
    for (unsigned int group = 0; group < codex::kControlGroupCount; ++group) {
      if (left.higher_codex_masks[layer][group] !=
          right.higher_codex_masks[layer][group]) {
        return false;
      }
    }
  }
  return true;
}

void copy_routing_to_settings(const codex::RoutingConfig& routing) {
  settings.layer_routing_enabled = routing.layer_routing_enabled;
  settings.configured_layer_count = routing.configured_layer_count;
  for (unsigned int index = 0;
       index < codex::kPhysicalControlsPerGroup; ++index) {
    settings.layer1_command_app_targets[index] =
        routing.layer1_command_app_targets[index];
  }
  for (unsigned int layer = 0; layer < codex::kMaximumLayerCount - 1;
       ++layer) {
    for (unsigned int group = 0; group < codex::kControlGroupCount; ++group) {
      settings.higher_codex_masks[layer][group] =
          routing.higher_codex_masks[layer][group];
    }
  }
}

void apply_settings_to_state() {
  if (!usb_identity_allowed && settings.transport != codex::TransportMode::Ble) {
    settings.transport = codex::TransportMode::Ble;
  }
  for (unsigned int index = 0; index < codex::kCommandCount; ++index) {
    copy_text(device_state.commands[index].keycap_id,
              sizeof(device_state.commands[index].keycap_id),
              settings.keycap_ids[index]);
    copy_text(device_state.commands[index].label,
              sizeof(device_state.commands[index].label), settings.labels[index]);
  }
  device_state.sound_profile = settings.sound_profile;
  device_state.sound_enabled = settings.sound_enabled;
  device_state.sound_volume = settings.sound_volume;
  device_state.transport = settings.transport;
  device_state.bluetooth_enabled = settings.bluetooth_enabled;
  device_state.protocol_profile = settings.protocol_profile;
  // Layers are a live workspace selector, not a boot preference. Always enter
  // the predictable first layer after power-up; its physical indicator is the
  // bottom lamp.
  settings.layer = 1;
  device_state.layer = 1;
  device_state.ble_slot = settings.ble_slot;
  for (unsigned int index = 0; index < 3; ++index) {
    device_state.ble_peers[index] = settings.ble_peers[index];
  }
  device_state.display_brightness = settings.display_brightness;
  device_state.standby_timeout_seconds = settings.standby_timeout_seconds;
  bool smart_screensaver = true;
  if (codex::settings_store_load_smart_screensaver(&smart_screensaver) == ESP_OK)
    device_state.smart_screensaver_enabled = smart_screensaver;
  device_state.animation_strength = settings.animation_strength;
  device_state.joystick_sensitivity_percent =
      settings.joystick_sensitivity_percent;
  __atomic_store_n(&joystick_sensitivity_mirror,
                   settings.joystick_sensitivity_percent,
                   __ATOMIC_RELEASE);
  device_state.super_standby_timeout_seconds =
      settings.super_standby_timeout_seconds;
  device_state.anti_accidental_shutdown =
      settings.anti_accidental_shutdown;
  device_state.power_button_mode = settings.power_button_mode;
  device_state.auto_ultra_timeout_seconds =
      settings.auto_ultra_timeout_seconds;
  device_state.ultra_touch_wake = settings.ultra_touch_wake;
  device_state.routing = {};
  device_state.routing.layer_routing_enabled =
      settings.layer_routing_enabled;
  device_state.routing.configured_layer_count =
      settings.configured_layer_count;
  for (unsigned int index = 0;
       index < codex::kPhysicalControlsPerGroup; ++index) {
    device_state.routing.layer1_command_app_targets[index] =
        settings.layer1_command_app_targets[index];
  }
  for (unsigned int layer = 0; layer < codex::kMaximumLayerCount - 1;
       ++layer) {
    for (unsigned int group = 0; group < codex::kControlGroupCount; ++group) {
      device_state.routing.higher_codex_masks[layer][group] =
          settings.higher_codex_masks[layer][group];
    }
  }
  device_state.routing.app_session_active = false;
  device_state.app_session = {};
  device_state.reset_reason = static_cast<unsigned char>(esp_reset_reason());
  device_state.recovery_gate_open = usb_identity_allowed;
}

bool persist_settings_snapshot() {
  xSemaphoreTake(settings_io_mutex, portMAX_DELAY);
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  codex::PersistentSettings snapshot = settings;
  xSemaphoreGive(state_mutex);
  const esp_err_t error = codex::settings_store_save(&snapshot);
  bool smart_screensaver = true;
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  smart_screensaver = device_state.smart_screensaver_enabled;
  xSemaphoreGive(state_mutex);
  const esp_err_t smart_error =
      codex::settings_store_save_smart_screensaver(smart_screensaver);
  if (error == ESP_OK) {
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    settings.schema_version = snapshot.schema_version;
    settings.crc32 = snapshot.crc32;
    xSemaphoreGive(state_mutex);
  }
  xSemaphoreGive(settings_io_mutex);
  return error == ESP_OK && smart_error == ESP_OK;
}

void transport_snapshot_into(codex::DeviceState& output, void*) {
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  output = device_state;
  xSemaphoreGive(state_mutex);
}

codex::DeviceState transport_snapshot(void* context) {
  codex::DeviceState copy{};
  transport_snapshot_into(copy, context);
  return copy;
}

void transport_publish(const codex::DeviceEvent& event, void*) {
  portENTER_CRITICAL(&lighting_mailbox_lock);
  const bool lighting = codex::publish_lighting_event(lighting_mailbox, event);
  portEXIT_CRITICAL(&lighting_mailbox_lock);
  if (lighting) return;
  codex::DeviceEvent routed = event;
  if (routed.type == codex::EventType::BatteryChanged &&
      routed.payload.battery.monotonic_ms == 0) {
    routed.payload.battery.monotonic_ms =
        static_cast<unsigned int>(esp_timer_get_time() / 1000ULL);
  }
  if (codex::transport_event_requires_order(routed.type)) {
    if (critical_event_queue != nullptr &&
        xQueueSend(critical_event_queue, &routed, portMAX_DELAY) == pdTRUE) {
      return;
    }
    ESP_LOGE(kTag, "ordered transport event could not be queued type=%u",
             static_cast<unsigned int>(routed.type));
    return;
  }
  enqueue_event(routed, 0, "transport");
}

bool transport_configure(const codex::ConfigReply& request, void*) {
  codex::DeviceEvent deferred_event{};
  switch (request.action) {
    case codex::ConfigAction::SetKeycap:
      deferred_event = codex::make_command_keycap_changed(request.index,
                                                          request.text);
      break;
    case codex::ConfigAction::SetLabel:
      deferred_event = codex::make_command_label_changed(request.index,
                                                         request.text);
      break;
    case codex::ConfigAction::SetSound:
      deferred_event = codex::make_sound_settings_changed(
          static_cast<codex::SoundProfile>(request.value), request.enabled,
          static_cast<unsigned char>(request.secondary_value));
      break;
    case codex::ConfigAction::SetTransport:
      if (!usb_identity_allowed &&
          request.value == static_cast<unsigned int>(codex::TransportMode::Usb)) {
        return false;
      }
      deferred_event = codex::make_transport_mode_changed(
          static_cast<codex::TransportMode>(request.value));
      break;
    case codex::ConfigAction::SetBluetooth:
      deferred_event =
          codex::make_bluetooth_enabled_changed(request.enabled);
      break;
    case codex::ConfigAction::SetProtocolProfile:
      deferred_event = codex::make_protocol_profile_changed(
          static_cast<codex::ProtocolProfile>(request.value));
      break;
    case codex::ConfigAction::SetDisplay:
      deferred_event = codex::make_display_settings_changed(
          static_cast<unsigned char>(request.value),
          static_cast<unsigned short>(request.secondary_value),
          static_cast<unsigned char>(request.tertiary_value),
          request.quaternary_value, request.enabled,
          request.secondary_enabled);
      break;
    case codex::ConfigAction::SetPower:
      deferred_event = codex::make_power_settings_changed(
          static_cast<codex::PowerButtonMode>(request.value),
          request.secondary_value, request.enabled);
      break;
    case codex::ConfigAction::SetInput:
      deferred_event = codex::make_joystick_sensitivity_changed(
          static_cast<unsigned char>(request.value));
      break;
    case codex::ConfigAction::SetBleSlot:
      deferred_event = codex::make_ble_slot_changed(
          static_cast<unsigned char>(request.value));
      break;
    case codex::ConfigAction::Reboot:
      xSemaphoreTake(state_mutex, portMAX_DELAY);
      reboot_at_ms = static_cast<unsigned int>(esp_timer_get_time() / 1000ULL) + 250U;
      xSemaphoreGive(state_mutex);
      return true;
    case codex::ConfigAction::ClearBond:
      deferred_event = codex::make_clear_bonds_requested(
          static_cast<unsigned char>(request.value));
      break;
    case codex::ConfigAction::AppHello:
      deferred_event = codex::make_app_session_connected(
          static_cast<unsigned int>(esp_timer_get_time() / 1000ULL));
      break;
    case codex::ConfigAction::AppHeartbeat:
      deferred_event = codex::make_app_session_heartbeat(
          static_cast<unsigned int>(esp_timer_get_time() / 1000ULL));
      break;
    case codex::ConfigAction::AppDisconnect:
      deferred_event = codex::make_app_session_disconnected();
      break;
    case codex::ConfigAction::SetRouting:
      deferred_event =
          codex::make_routing_config_changed(request.routing_config);
      break;
    case codex::ConfigAction::SetAppKeyLighting:
      deferred_event = codex::make_app_key_lighting_changed(
          request.control_group, request.index, request.lighting);
      break;
    case codex::ConfigAction::SetAppIcons:
      deferred_event = codex::make_app_icons_changed(
          static_cast<unsigned char>(request.secondary_value),
          request.control_group, request.icon_hashes);
      break;
    case codex::ConfigAction::None:
      return true;
  }
  return enqueue_event(deferred_event, pdMS_TO_TICKS(20), "config");
}

void on_touch_points(const esp_lcd_touch_point_data_t* points,
                     uint8_t point_count, uint32_t monotonic_ms, void*) {
  const unsigned int callback_us =
      static_cast<unsigned int>(esp_timer_get_time());
  portENTER_CRITICAL(&diagnostics_lock);
  if (last_touch_callback_us != 0U) {
    last_touch_callback_gap_us = callback_us - last_touch_callback_us;
    if (last_touch_callback_gap_us >
        maximum_touch_callback_gap_interval_us) {
      maximum_touch_callback_gap_interval_us = last_touch_callback_gap_us;
    }
  }
  last_touch_callback_us = callback_us;
  portEXIT_CRITICAL(&diagnostics_lock);
#if CONFIG_CODEX_DEVELOPMENT_HIL
  // A synthetic contact and a physical "no contacts" frame must not fight
  // over the same normalizer. HIL owns the source only while it actively holds
  // at least one contact, then immediately returns control to GT911.
  if (hil_touch_override.load(std::memory_order_acquire)) return;
#endif
  const codex::TouchSampleKind sample =
      points == nullptr
          ? codex::TouchSampleKind::ReadFailure
          : point_count == 0 ? codex::TouchSampleKind::Empty
                             : codex::TouchSampleKind::Contacts;
  const codex::TouchContactUpdate contact =
      codex::update_touch_contact(touch_contact_state, sample);
  touch_contact_state = contact.state;
  if (!contact.valid_frame) {
    return;
  }
  codex::RawTouchFrame raw{};
  raw.monotonic_ms = monotonic_ms;
  if (codex::touch_frame_is_user_activity(point_count)) {
    __atomic_store_n(&last_user_activity_ms, monotonic_ms, __ATOMIC_RELEASE);
  }
  const codex::PowerMode power = static_cast<codex::PowerMode>(
      __atomic_load_n(&power_mode_mirror, __ATOMIC_ACQUIRE));
  const bool touching = point_count != 0;
  if (power == codex::PowerMode::Screensaver) {
    if (touching && !protected_touch_down) {
      const codex::DeviceEvent wake = codex::make_exit_standby_requested();
      if (critical_event_queue == nullptr ||
          xQueueSend(critical_event_queue, &wake, 0) != pdTRUE) {
        enqueue_event(wake, 0, "screensaver-touch-fallback");
      }
    }
    protected_touch_down = touching;
    return;
  }
  if (power == codex::PowerMode::ProtectedBlack) {
    if (touching && !protected_touch_down) {
      const codex::ProtectedUnlockResult result =
          codex::update_protected_unlock(
              protected_unlock, codex::ProtectedUnlockInput::Tap,
              monotonic_ms);
      if (result.show_unlock) {
        const codex::DeviceEvent show =
            codex::make_protected_unlock_shown(monotonic_ms);
        if (critical_event_queue == nullptr ||
            xQueueSend(critical_event_queue, &show, 0) != pdTRUE) {
          enqueue_event(show, 0, "protected-double-tap-fallback");
        }
      }
    }
    protected_touch_down = touching;
    return;
  }
  if (power == codex::PowerMode::ProtectedUnlock) {
    protected_touch_down = touching;
    return;
  }
  protected_touch_down = touching;
  const unsigned char current_arcade_phase =
      __atomic_load_n(&arcade_phase, __ATOMIC_ACQUIRE);
  if (current_arcade_phase != 0U) {
    if (current_arcade_phase != 3U) return;
    raw.count = point_count > codex::kMaxTouchPoints
                    ? codex::kMaxTouchPoints
                    : point_count;
    for (unsigned int index = 0; index < raw.count; ++index) {
      raw.points[index].track_id = points[index].track_id;
      raw.points[index].point.x = static_cast<int>(points[index].x);
      raw.points[index].point.y = static_cast<int>(points[index].y);
    }
    const codex::ArcadeInputOutput output =
        codex::normalize_arcade_touches(arcade_input, raw);
    for (unsigned int index = 0; index < output.frame.count; ++index) {
      enqueue_touch_event(output.frame.events[index]);
    }
    if (output.command != codex::ArcadeUiCommand::None) {
      __atomic_store_n(
          &arcade_ui_command_requested,
          static_cast<unsigned char>(output.command), __ATOMIC_RELEASE);
    }
    return;
  }
  if (__atomic_load_n(&board_touch_enabled, __ATOMIC_ACQUIRE)) {
    raw.count = point_count > codex::kMaxTouchPoints
                    ? codex::kMaxTouchPoints
                    : point_count;
    for (unsigned int index = 0; index < raw.count; ++index) {
      raw.points[index].track_id = points[index].track_id;
      raw.points[index].point.x = static_cast<int>(points[index].x);
      raw.points[index].point.y = static_cast<int>(points[index].y);
    }
  }
  const codex::InputFrame frame =
      codex::normalize_touches(input, input_layout, raw);
  for (unsigned int index = 0; index < frame.count; ++index) {
    codex::DeviceEvent event = frame.events[index];
    if (event.type == codex::EventType::JoystickChanged) {
      const unsigned char sensitivity = __atomic_load_n(
          &joystick_sensitivity_mirror, __ATOMIC_ACQUIRE);
      event.payload.joystick.distance = codex::scale_classic_joystick(
          event.payload.joystick.distance, sensitivity);
    }
    enqueue_touch_event(event);
  }
}

#if CONFIG_CODEX_DEVELOPMENT_HIL
void hil_pointer_read(lv_indev_t*, lv_indev_data_t* data) {
  data->point.x = hil_pointer_x.load(std::memory_order_acquire);
  data->point.y = hil_pointer_y.load(std::memory_order_acquire);
  data->state = hil_pointer_pressed.load(std::memory_order_acquire)
                    ? LV_INDEV_STATE_PRESSED
                    : LV_INDEV_STATE_RELEASED;
}

void emit_hil_touch_frame(unsigned int monotonic_ms) {
  codex::RawTouchFrame raw{};
  raw.monotonic_ms = monotonic_ms;
  for (unsigned int track = 0; track < codex::kMaxTouchPoints; ++track) {
    if (!hil_touch_active[track]) continue;
    codex::RawTouchPoint& point = raw.points[raw.count++];
    point.track_id = static_cast<unsigned char>(track);
    point.point = hil_touch_points[track];
  }

  const codex::PowerMode power = static_cast<codex::PowerMode>(
      __atomic_load_n(&power_mode_mirror, __ATOMIC_ACQUIRE));
  const bool touching = raw.count != 0U;
  if (power == codex::PowerMode::Screensaver) {
    if (touching && !protected_touch_down) {
      const codex::DeviceEvent wake = codex::make_exit_standby_requested();
      if (critical_event_queue == nullptr ||
          xQueueSend(critical_event_queue, &wake, 0) != pdTRUE) {
        enqueue_event(wake, 0, "hil-screensaver-touch-fallback");
      }
    }
    protected_touch_down = touching;
    return;
  }

  if (__atomic_load_n(&arcade_phase, __ATOMIC_ACQUIRE) == 3U) {
    const codex::ArcadeInputOutput output =
        codex::normalize_arcade_touches(arcade_input, raw);
    for (unsigned int index = 0; index < output.frame.count; ++index) {
      enqueue_touch_event(output.frame.events[index]);
    }
    if (output.command != codex::ArcadeUiCommand::None) {
      __atomic_store_n(
          &arcade_ui_command_requested,
          static_cast<unsigned char>(output.command), __ATOMIC_RELEASE);
    }
    return;
  }

  // An empty frame is still passed through when classic input is disabled so
  // a control captured before an overlay opened can never remain held.
  if (!__atomic_load_n(&board_touch_enabled, __ATOMIC_ACQUIRE)) {
    raw.count = 0;
  }
  const codex::InputFrame frame =
      codex::normalize_touches(input, input_layout, raw);
  for (unsigned int index = 0; index < frame.count; ++index) {
    codex::DeviceEvent event = frame.events[index];
    if (event.type == codex::EventType::JoystickChanged) {
      const unsigned char sensitivity = __atomic_load_n(
          &joystick_sensitivity_mirror, __ATOMIC_ACQUIRE);
      event.payload.joystick.distance = codex::scale_classic_joystick(
          event.payload.joystick.distance, sensitivity);
    }
    enqueue_touch_event(event);
  }
}

void apply_hil_touch(const codex::HilTouchCommand& touch,
                     unsigned int monotonic_ms) {
  const unsigned int track = touch.track_id;
  if (track >= codex::kMaxTouchPoints) return;
  if (touch.phase == codex::HilTouchPhase::Down ||
      touch.phase == codex::HilTouchPhase::Move) {
    hil_touch_override.store(true, std::memory_order_release);
    hil_touch_active[track] = true;
    hil_touch_points[track] = {touch.x, touch.y};
  } else {
    hil_touch_active[track] = false;
  }
  if (track == 0U) {
    if (touch.phase == codex::HilTouchPhase::Down ||
        touch.phase == codex::HilTouchPhase::Move) {
      hil_pointer_x.store(touch.x, std::memory_order_release);
      hil_pointer_y.store(touch.y, std::memory_order_release);
      hil_pointer_pressed.store(true, std::memory_order_release);
    } else {
      hil_pointer_pressed.store(false, std::memory_order_release);
    }
  }
  __atomic_store_n(&last_user_activity_ms, monotonic_ms, __ATOMIC_RELEASE);
  emit_hil_touch_frame(monotonic_ms);
  bool any_active = false;
  for (bool active : hil_touch_active) any_active = any_active || active;
  if (!any_active) {
    hil_touch_override.store(false, std::memory_order_release);
  }
}

bool submit_hil_command(const codex::HilCommand& command, void*) {
  return hil_command_queue != nullptr &&
         xQueueSend(hil_command_queue, &command, pdMS_TO_TICKS(20)) == pdTRUE;
}

codex::DeviceState hil_state_snapshot(void*) {
  xSemaphoreTake(state_mutex, portMAX_DELAY);
  const codex::DeviceState snapshot = device_state;
  xSemaphoreGive(state_mutex);
  return snapshot;
}

codex::HilRuntimeMetrics hil_metrics_snapshot(void*) {
  codex::HilRuntimeMetrics metrics{};
  metrics.monotonic_ms =
      static_cast<unsigned int>(esp_timer_get_time() / 1000ULL);
  portENTER_CRITICAL(&diagnostics_lock);
  metrics.event_queue_drops = dropped_events;
  metrics.event_queue_high_water = event_queue_high_water;
  metrics.last_input_queue_latency_us = last_input_queue_latency_us;
  metrics.maximum_input_queue_latency_us = maximum_input_queue_latency_us;
  metrics.last_touch_callback_gap_us = last_touch_callback_gap_us;
  metrics.maximum_touch_callback_gap_interval_us =
      maximum_touch_callback_gap_interval_us;
  maximum_touch_callback_gap_interval_us = 0U;
  portEXIT_CRITICAL(&diagnostics_lock);
  metrics.slow_frames = slow_frame_count;
  metrics.free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
  metrics.minimum_internal =
      heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);
  metrics.free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
  metrics.minimum_psram =
      heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);
  metrics.ble_retries = codex::codex_ble_retry_count();
  metrics.ble_queue_high_water = codex::codex_ble_input_high_water();
  metrics.ble_rx_stack_low_water_bytes =
      codex::codex_ble_rx_stack_low_water_bytes();
  const codex::InputTxLatencySnapshot tx_latency =
      codex::codex_transport_input_tx_latency_snapshot();
  metrics.last_input_tx_latency_us = tx_latency.last_us;
  metrics.maximum_input_tx_latency_us = tx_latency.maximum_us;
  metrics.ble_started = codex::codex_ble_started();
  metrics.ble_link = codex::codex_ble_connected();
  metrics.ble_ready = codex::codex_ble_ready();
  metrics.usb_link = codex::codex_usb_connected();
  metrics.display = codex::display_monitor_snapshot();
  return metrics;
}

codex::ProtocolTrace hil_trace_snapshot(void*) {
  return codex::codex_ble_protocol_trace_snapshot();
}

bool hil_read_pixel(unsigned short x, unsigned short y, unsigned short& pixel,
                    void*) {
  return codex::display_monitor_read_pixel(x, y, pixel);
}

void service_hil_commands(unsigned int monotonic_ms) {
  if (hil_tap_release_at_ms != 0U &&
      static_cast<signed int>(monotonic_ms - hil_tap_release_at_ms) >= 0) {
    apply_hil_touch(
        {codex::HilTouchPhase::Up, 0,
         hil_pointer_x.load(std::memory_order_acquire),
         hil_pointer_y.load(std::memory_order_acquire)},
        monotonic_ms);
    hil_tap_release_at_ms = 0U;
  }

  codex::HilCommand command{};
  unsigned int processed = 0;
  while (processed < 8U && hil_command_queue != nullptr &&
         xQueueReceive(hil_command_queue, &command, 0) == pdTRUE) {
    ++processed;
    switch (command.type) {
      case codex::HilCommandType::Touch:
        apply_hil_touch(command.touch, monotonic_ms);
        break;
      case codex::HilCommandType::Tap:
        apply_hil_touch(
            {codex::HilTouchPhase::Down, 0, command.touch.x,
             command.touch.y},
            monotonic_ms);
        hil_tap_release_at_ms = monotonic_ms + 80U;
        break;
      case codex::HilCommandType::Key:
        enqueue_touch_event(
            command.key_down ? codex::make_key_pressed(command.control)
                             : codex::make_key_released(command.control));
        break;
      case codex::HilCommandType::PowerButton:
        enqueue_touch_event(codex::make_power_button_pressed());
        break;
      case codex::HilCommandType::UltraStandby:
        enqueue_touch_event(codex::make_enter_ultra_standby_requested());
        break;
      case codex::HilCommandType::UltraWake:
        if (hil_state_snapshot(nullptr).power ==
            codex::PowerMode::UltraStandby) {
          enqueue_touch_event(codex::make_power_button_pressed());
        }
        break;
      case codex::HilCommandType::Joystick:
        enqueue_touch_event(codex::make_joystick_changed(
            command.angle, command.distance));
        break;
      case codex::HilCommandType::Encoder:
        enqueue_touch_event(
            codex::make_encoder_step(command.encoder_step));
        break;
      case codex::HilCommandType::ClearBleBonds:
        enqueue_event(codex::make_clear_bonds_requested(command.ble_slot), 0,
                      "hil-ble-clear");
        break;
      case codex::HilCommandType::LightingStress: {
        const codex::Lighting agent = command.enabled
                                          ? codex::Lighting{
                                                codex::LightEffect::Breath,
                                                1.0F, 0.7F, 0.0F, 0x304FFE}
                                          : codex::Lighting{};
        const codex::Lighting ambient = command.enabled
                                            ? codex::Lighting{
                                                  codex::LightEffect::Snake,
                                                  1.0F, 0.35F, 0.0F,
                                                  0x304FFE}
                                            : codex::Lighting{};
        for (unsigned int agent_index = 0;
             agent_index < codex::kAgentCount; ++agent_index) {
          enqueue_event(codex::make_agent_lighting_changed(
                            static_cast<unsigned char>(agent_index), agent),
                        0, "hil-lighting");
        }
        enqueue_event(codex::make_ambient_lighting_changed(ambient), 0,
                      "hil-lighting");
        break;
      }
      case codex::HilCommandType::AppConnect:
        enqueue_event(codex::make_app_session_connected(monotonic_ms), 0,
                      "hil-app-connect");
        break;
      case codex::HilCommandType::AppHeartbeat:
        enqueue_event(codex::make_app_session_heartbeat(monotonic_ms), 0,
                      "hil-app-heartbeat");
        break;
      case codex::HilCommandType::AppDisconnect:
        enqueue_event(codex::make_app_session_disconnected(), 0,
                      "hil-app-disconnect");
        break;
      case codex::HilCommandType::SetTransport:
        enqueue_event(codex::make_transport_mode_changed(command.transport), 0,
                      "hil-transport");
        break;
      case codex::HilCommandType::SetLayer: {
        const codex::DeviceState state = hil_state_snapshot(nullptr);
        const unsigned char layer_count =
            state.app_session.active && state.routing.layer_routing_enabled
                ? codex::normalized_layer_count(
                      state.routing.configured_layer_count)
                : codex::kMaximumLayerCount;
        if (command.layer <= layer_count) {
          unsigned char simulated_layer = state.layer;
          for (unsigned int attempt = 0;
               attempt < layer_count && simulated_layer != command.layer;
               ++attempt) {
            enqueue_touch_event(codex::make_layer_next());
            simulated_layer =
                codex::next_configured_layer(simulated_layer, layer_count);
          }
        }
        break;
      }
      case codex::HilCommandType::SetRoutingLayerCount: {
        codex::RoutingConfig config = hil_state_snapshot(nullptr).routing;
        config.layer_routing_enabled = true;
        config.configured_layer_count = command.layer_count;
        enqueue_event(codex::make_routing_config_changed(config), 0,
                      "hil-routing-layers");
        break;
      }
      case codex::HilCommandType::SetLayer1Route: {
        codex::RoutingConfig config = hil_state_snapshot(nullptr).routing;
        config.layer_routing_enabled = true;
        config.layer1_command_app_targets[command.physical_index] =
            command.target_layer;
        enqueue_event(codex::make_routing_config_changed(config), 0,
                      "hil-routing-layer1");
        break;
      }
      case codex::HilCommandType::SetHigherCodexMask: {
        codex::RoutingConfig config = hil_state_snapshot(nullptr).routing;
        config.layer_routing_enabled = true;
        config.higher_codex_masks[command.layer - 2U]
                                   [codex::control_group_index(command.group)] =
            command.mask;
        enqueue_event(codex::make_routing_config_changed(config), 0,
                      "hil-routing-passthrough");
        break;
      }
      case codex::HilCommandType::AppKeyLighting:
        enqueue_event(
            codex::make_app_key_lighting_changed(
                command.group, command.control_id, command.lighting),
            0, "hil-app-lighting");
        break;
      default:
        break;
    }
  }
}
#endif

lv_display_t* start_display() {
  bsp_display_cfg_t config{};
  config.lvgl_port_cfg = {
      .task_priority = 7,
      .task_stack = 8192,
      .task_affinity = 1,
      .task_max_sleep_ms = 5,
      .task_stack_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT,
      .timer_period_ms = 2,
  };
  lv_display_t* display = bsp_display_start_with_config(&config);
  if (display != nullptr) {
    touch_input_device = bsp_display_get_input_dev();
    lvgl_port_touch_set_points_callback(touch_input_device, on_touch_points,
                                        nullptr);
#if CONFIG_CODEX_DEVELOPMENT_HIL
    if (bsp_display_lock(0)) {
      hil_pointer_device = lv_indev_create();
      lv_indev_set_type(hil_pointer_device, LV_INDEV_TYPE_POINTER);
      lv_indev_set_display(hil_pointer_device, display);
      lv_indev_set_read_cb(hil_pointer_device, hil_pointer_read);
      bsp_display_unlock();
    }
#endif
  }
  return display;
}

lv_display_t* resume_display() {
  lv_display_t* display = bsp_display_resume();
  if (display != nullptr) {
    touch_input_device = bsp_display_get_input_dev();
    lvgl_port_touch_set_points_callback(touch_input_device, on_touch_points,
                                        nullptr);
#if CONFIG_CODEX_DEVELOPMENT_HIL
    if (bsp_display_lock(0)) {
      hil_pointer_device = lv_indev_create();
      lv_indev_set_type(hil_pointer_device, LV_INDEV_TYPE_POINTER);
      lv_indev_set_display(hil_pointer_device, display);
      lv_indev_set_read_cb(hil_pointer_device, hil_pointer_read);
      bsp_display_unlock();
    }
#endif
  }
  return display;
}

void on_ui_action(codex::UiAction action, unsigned char value, void*) {
  codex::DeviceEvent event{};
  switch (action) {
    case codex::UiAction::CloseOverlay:
      event = codex::make_connection_overlay_requested();
      break;
    case codex::UiAction::SelectAuto:
      event = codex::make_transport_mode_changed(codex::TransportMode::Auto);
      break;
    case codex::UiAction::SelectUsb:
      event = codex::make_transport_mode_changed(codex::TransportMode::Usb);
      break;
    case codex::UiAction::SelectBle:
      event = codex::make_transport_mode_changed(codex::TransportMode::Ble);
      break;
    case codex::UiAction::SelectMixed:
      event = codex::make_transport_mode_changed(codex::TransportMode::Mixed);
      break;
    case codex::UiAction::SelectBle1:
      event = codex::make_ble_slot_changed(1);
      break;
    case codex::UiAction::SelectBle2:
      event = codex::make_ble_slot_changed(2);
      break;
    case codex::UiAction::SelectBle3:
      event = codex::make_ble_slot_changed(3);
      break;
    case codex::UiAction::ToggleBluetooth: {
      xSemaphoreTake(state_mutex, portMAX_DELAY);
      const bool enabled = device_state.bluetooth_enabled;
      xSemaphoreGive(state_mutex);
      event = codex::make_bluetooth_enabled_changed(!enabled);
      break;
    }
    case codex::UiAction::ClearBonds:
      event = value >= 1 && value <= 3
                  ? codex::make_clear_bonds_requested(value)
                  : codex::make_clear_bonds_requested();
      break;
    case codex::UiAction::SetVolume: {
      xSemaphoreTake(state_mutex, portMAX_DELAY);
      const codex::DeviceState snapshot = device_state;
      xSemaphoreGive(state_mutex);
      event = codex::make_sound_settings_changed(
          snapshot.sound_profile, snapshot.sound_enabled, value);
      break;
    }
    case codex::UiAction::SetBrightness: {
      xSemaphoreTake(state_mutex, portMAX_DELAY);
      const codex::DeviceState snapshot = device_state;
      xSemaphoreGive(state_mutex);
      event = codex::make_display_settings_changed(
          value, snapshot.standby_timeout_seconds,
          snapshot.animation_strength,
          snapshot.super_standby_timeout_seconds,
          snapshot.anti_accidental_shutdown,
          snapshot.smart_screensaver_enabled);
      break;
    }
    case codex::UiAction::SetJoystickSensitivity:
      event = codex::make_joystick_sensitivity_changed(value);
      break;
    case codex::UiAction::TriggerArcade:
      __atomic_store_n(&arcade_trigger_requested, true, __ATOMIC_RELEASE);
      return;
    case codex::UiAction::CycleScreensaver: {
      xSemaphoreTake(state_mutex, portMAX_DELAY);
      const codex::DeviceState snapshot = device_state;
      xSemaphoreGive(state_mutex);
      const unsigned short current = snapshot.standby_timeout_seconds;
      const unsigned short next =
          current == 0 ? 30
          : current == 30 ? 60
          : current == 60 ? 180
          : current == 180 ? 600
          : current == 600 ? 1800
          : current == 1800 ? 3600
          : 0;
      event = codex::make_display_settings_changed(
          snapshot.display_brightness, next, snapshot.animation_strength,
          snapshot.super_standby_timeout_seconds,
          snapshot.anti_accidental_shutdown,
          snapshot.smart_screensaver_enabled);
      break;
    }
    case codex::UiAction::ToggleSmartScreensaver: {
      xSemaphoreTake(state_mutex, portMAX_DELAY);
      const codex::DeviceState snapshot = device_state;
      xSemaphoreGive(state_mutex);
      event = codex::make_display_settings_changed(
          snapshot.display_brightness, snapshot.standby_timeout_seconds,
          snapshot.animation_strength, snapshot.super_standby_timeout_seconds,
          snapshot.anti_accidental_shutdown, !snapshot.smart_screensaver_enabled);
      break;
    }
    case codex::UiAction::CycleSuperStandby: {
      xSemaphoreTake(state_mutex, portMAX_DELAY);
      const codex::DeviceState snapshot = device_state;
      xSemaphoreGive(state_mutex);
      const unsigned int next =
          snapshot.super_standby_timeout_seconds == 3600
              ? 7200
              : snapshot.super_standby_timeout_seconds == 7200
                    ? 10800
                    : snapshot.super_standby_timeout_seconds == 10800
                          ? 18000
                          : snapshot.super_standby_timeout_seconds == 18000
                                ? 0
                                : 3600;
      event = codex::make_display_settings_changed(
          snapshot.display_brightness, snapshot.standby_timeout_seconds,
          snapshot.animation_strength, next,
          snapshot.anti_accidental_shutdown,
          snapshot.smart_screensaver_enabled);
      break;
    }
    case codex::UiAction::ToggleAntiAccidentalShutdown: {
      xSemaphoreTake(state_mutex, portMAX_DELAY);
      const codex::DeviceState snapshot = device_state;
      xSemaphoreGive(state_mutex);
      event = codex::make_display_settings_changed(
          snapshot.display_brightness, snapshot.standby_timeout_seconds,
          snapshot.animation_strength,
          snapshot.super_standby_timeout_seconds,
          !snapshot.anti_accidental_shutdown,
          snapshot.smart_screensaver_enabled);
      break;
    }
    case codex::UiAction::CyclePowerButtonMode: {
      xSemaphoreTake(state_mutex, portMAX_DELAY);
      const codex::DeviceState snapshot = device_state;
      xSemaphoreGive(state_mutex);
      const codex::PowerButtonMode next =
          snapshot.power_button_mode == codex::PowerButtonMode::ConnectedStandby
              ? codex::PowerButtonMode::UltraStandby
              : codex::PowerButtonMode::ConnectedStandby;
      event = codex::make_power_settings_changed(
          next, snapshot.auto_ultra_timeout_seconds,
          snapshot.ultra_touch_wake);
      break;
    }
    case codex::UiAction::CycleAutoUltraStandby: {
      xSemaphoreTake(state_mutex, portMAX_DELAY);
      const codex::DeviceState snapshot = device_state;
      xSemaphoreGive(state_mutex);
      const unsigned int current = snapshot.auto_ultra_timeout_seconds;
      const unsigned int next =
          current == 0 ? 3600
          : current == 3600 ? 10800
          : current == 10800 ? 18000
          : current == 18000 ? 28800
          : current == 28800 ? 43200
                             : 0;
      event = codex::make_power_settings_changed(
          snapshot.power_button_mode, next, snapshot.ultra_touch_wake);
      break;
    }
    case codex::UiAction::ToggleUltraTouchWake: {
      xSemaphoreTake(state_mutex, portMAX_DELAY);
      const codex::DeviceState snapshot = device_state;
      xSemaphoreGive(state_mutex);
      event = codex::make_power_settings_changed(
          snapshot.power_button_mode, snapshot.auto_ultra_timeout_seconds,
          !snapshot.ultra_touch_wake);
      break;
    }
    case codex::UiAction::EnterProtectedStandby:
      event = codex::make_protected_standby_requested();
      break;
    case codex::UiAction::UnlockProtected:
      event = codex::make_protected_unlock_completed();
      break;
    case codex::UiAction::AcknowledgeBatteryWarning:
      event = codex::make_battery_warning_acknowledged();
      break;
  }
  if (critical_event_queue == nullptr ||
      xQueueSend(critical_event_queue, &event, pdMS_TO_TICKS(5)) != pdTRUE) {
    enqueue_event(event, 0, "ui-fallback");
  }
}

bool auto_ultra_work_blocked(const codex::DeviceState& state) {
  if (state.voice == codex::VoiceMode::Recording ||
      state.voice == codex::VoiceMode::Processing ||
      state.encoder.pressed || state.joystick.captured) {
    return true;
  }
  for (unsigned int index = 0; index < codex::kAgentCount; ++index) {
    if (state.agents[index].pressed ||
        state.agents[index].lighting.effect == codex::LightEffect::Breath) {
      return true;
    }
  }
  for (unsigned int index = 0; index < codex::kCommandCount; ++index) {
    if (state.commands[index].pressed) return true;
  }
  if (state.ambient.effect == codex::LightEffect::Snake) return true;
  const unsigned int completed =
      state.hid_tx_success_count + state.hid_tx_failure_count;
  return state.hid_tx_queued_count > completed;
}

void log_ultra_heap(const char* stage) {
  ESP_LOGI(kTag, "ultra heap stage=%s internal=%u psram=%u", stage,
           heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
           heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}

bool enter_ultra_hardware(const codex::DeviceState& state) {
  if (ultra_hardware_active) return true;
  log_ultra_heap("enter-begin");
  if (!persist_settings_snapshot()) {
    ESP_LOGE(kTag, "settings flush before ultra standby failed");
  }
  if (!codex::codex_transport_flush(2000)) {
    ESP_LOGE(kTag, "transport drain timed out; ultra standby cancelled");
    return false;
  }
  const esp_err_t ble_error = codex::codex_ble_suspend();
  log_ultra_heap("ble-suspended");
  // An initialized but unmounted TinyUSB device has no live bus and does not
  // prevent light sleep. Retain it to avoid repeatedly rebuilding endpoint
  // state; a genuinely mounted native-USB host is still disconnected cleanly.
  ultra_usb_retained = !codex::codex_usb_connected();
  const esp_err_t usb_error =
      ultra_usb_retained ? ESP_OK : codex::codex_usb_stop();
  log_ultra_heap(ultra_usb_retained ? "usb-retained" : "usb-stopped");
  if (ble_error != ESP_OK || usb_error != ESP_OK) {
    ESP_LOGE(kTag, "transport stop failed before ultra: ble=%s usb=%s",
             esp_err_to_name(ble_error), esp_err_to_name(usb_error));
    (void)codex::codex_ble_resume();
    (void)apply_transport_mode(state.transport, state.bluetooth_enabled);
    return false;
  }

  codex::codex_audio_set_enabled(false);
  (void)bsp_audio_poweramp_enable(false);
  __atomic_store_n(&board_touch_enabled, false, __ATOMIC_RELEASE);
  touch_polling_restore_at_ms = 0;
  if (!bsp_display_lock(500)) {
    ESP_LOGE(kTag, "LVGL lock unavailable during ultra teardown");
    (void)codex::codex_ble_resume();
    (void)apply_transport_mode(state.transport, state.bluetooth_enabled);
    (void)bsp_audio_poweramp_enable(true);
    codex::codex_audio_set_enabled(true);
    return false;
  }
#if CONFIG_CODEX_DEVELOPMENT_HIL
  codex::display_monitor_suspend_framebuffer();
  hil_pointer_device = nullptr;
#endif
  codex::ui_deinit();
  bsp_display_unlock();
  touch_input_device = nullptr;
  touch_contact_state = {};
  const esp_err_t display_error = bsp_display_suspend();
  codex::ui_release_resources();
  log_ultra_heap("display-stopped");
  if (display_error != ESP_OK) {
    ESP_LOGE(kTag, "display teardown for ultra failed: %s",
             esp_err_to_name(display_error));
    esp_restart();
    return false;
  }
  if (state.ultra_touch_wake) {
    const esp_err_t touch_error = codex::board_controls_start_ultra_touch();
    if (touch_error != ESP_OK) {
      ESP_LOGW(kTag, "ultra touch wake unavailable: %s",
               esp_err_to_name(touch_error));
    }
  }
  codex::board_controls_set_standby(true);
  set_power_profile(true);
#if CONFIG_CODEX_DEVELOPMENT_HIL
  // UART0 exists only as a development recovery/HIL channel. Let an incoming
  // command wake light sleep; the first short preamble may be consumed by the
  // wake detector, so the host sends a blank line before the actual command.
  (void)uart_set_wakeup_threshold(UART_NUM_0, 3);
  (void)esp_sleep_enable_uart_wakeup(UART_NUM_0);
#endif
  ultra_wake_enqueued = false;
  ultra_hardware_active = true;
  ESP_LOGI(kTag, "ultra standby hardware entered");
  return true;
}

bool exit_ultra_hardware(const codex::DeviceState& state) {
  if (!ultra_hardware_active) return true;
  log_ultra_heap("exit-begin");
  codex::board_controls_stop_ultra_touch();
#if CONFIG_CODEX_DEVELOPMENT_HIL
  (void)esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_UART);
#endif
  set_power_profile(false);
  codex::board_controls_set_standby(false);
  if (resume_display() == nullptr) {
    ESP_LOGE(kTag, "display restart after ultra failed");
    return false;
  }
  log_ultra_heap("display-started");
  if (bsp_display_backlight_prepare_wake() != ESP_OK) {
    ESP_LOGE(kTag, "backlight reattach after ultra failed");
    return false;
  }
  if (bsp_display_brightness_set(
          physical_brightness(state.display_brightness)) != ESP_OK) {
    ESP_LOGW(kTag, "brightness restore after ultra failed");
  }
  if (bsp_display_lock(100)) {
    codex::ui_init(state);
    codex::ui_set_action_callback(on_ui_action, nullptr);
    bsp_display_unlock();
  } else {
    ESP_LOGE(kTag, "LVGL lock unavailable after ultra wake");
    return false;
  }
  (void)bsp_audio_poweramp_enable(true);
  codex::codex_audio_set_enabled(true);
  log_ultra_heap("audio-requested");
  esp_err_t transport_error = ESP_OK;
  if (codex::codex_ble_started() && state.bluetooth_enabled) {
    transport_error = codex::codex_ble_resume();
  }
  if (transport_error == ESP_OK) {
    transport_error =
        apply_transport_mode(state.transport, state.bluetooth_enabled);
  }
  log_ultra_heap("transport-started");
  if (transport_error != ESP_OK) {
    ESP_LOGE(kTag, "transport restart after ultra failed: %s",
             esp_err_to_name(transport_error));
    return false;
  }
  ultra_hardware_active = false;
  ultra_wake_enqueued = false;
  ultra_usb_retained = false;
  const unsigned int now_ms =
      static_cast<unsigned int>(esp_timer_get_time() / 1000ULL);
  __atomic_store_n(&last_user_activity_ms, now_ms, __ATOMIC_RELEASE);
  disconnected_since_ms = now_ms;
  ESP_LOGI(kTag, "ultra standby hardware exited");
  return true;
}

void service_ultra_hardware(bool touch_wake_enabled) {
  if (!ultra_hardware_active) return;
  const bool ble_link_active = codex::codex_ble_connected();
  if (ble_link_active) {
    // ESP32-S3 Bluedroid explicitly does not support CPU light sleep while a
    // connection is enabled. Controller modem sleep plus an idle 80 MHz CPU
    // preserves the authenticated link and avoids repeated host handshakes.
    vTaskDelay(pdMS_TO_TICKS(50));
  } else {
    esp_sleep_enable_timer_wakeup(250000ULL);
    const esp_err_t sleep_error = esp_light_sleep_start();
    if (sleep_error != ESP_OK) {
      ESP_LOGW(kTag, "ultra light sleep failed: %s",
               esp_err_to_name(sleep_error));
    }
    vTaskDelay(pdMS_TO_TICKS(12));
  }
  // The high-priority TCA9554 task runs during either scheduling window and
  // publishes PWR immediately on the press edge.
  if (touch_wake_enabled && !ultra_wake_enqueued &&
      codex::board_controls_ultra_touch_pressed()) {
    ultra_wake_enqueued = true;
    __atomic_store_n(
        &last_user_activity_ms,
        static_cast<unsigned int>(esp_timer_get_time() / 1000ULL),
        __ATOMIC_RELEASE);
    enqueue_touch_event(codex::make_power_button_pressed());
  }
}

void service_arcade(unsigned int now_ms) {
  unsigned char phase =
      __atomic_load_n(&arcade_phase, __ATOMIC_ACQUIRE);
  if (phase == 0U) {
    if (!__atomic_exchange_n(&arcade_trigger_requested, false,
                             __ATOMIC_ACQ_REL)) {
      return;
    }
    arcade_trigger = codex::start_arcade_trigger(now_ms);
    __atomic_store_n(&arcade_phase, 1U, __ATOMIC_RELEASE);
    enqueue_event(codex::make_connection_overlay_requested(), 0,
                  "arcade-close-settings");
    ESP_LOGI(kTag, "arcade trigger started");
    return;
  }

  if (phase == 1U) {
    const codex::ArcadeTriggerOutput output =
        codex::advance_arcade_trigger(arcade_trigger, now_ms);
    if (!output.ready) return;
    enqueue_touch_event(
        codex::make_joystick_changed(output.angle, output.distance));
    if (!output.complete) return;
    __atomic_store_n(&arcade_phase, 2U, __ATOMIC_RELEASE);
    return;
  }

  if (phase == 2U) {
    if (bsp_display_lock(20)) {
      codex::ui_begin_arcade_entry();
      codex::ui_enter_arcade();
      bsp_display_unlock();
      arcade_input = codex::make_arcade_input_state();
      __atomic_store_n(&arcade_phase, 3U, __ATOMIC_RELEASE);
      ESP_LOGI(kTag, "arcade active");
    }
    return;
  }

  if (phase == 3U) {
    const unsigned char command = __atomic_load_n(
        &arcade_ui_command_requested, __ATOMIC_ACQUIRE);
    if (command == 0U) return;
    const auto decoded = static_cast<codex::ArcadeUiCommand>(command);
    if (decoded != codex::ArcadeUiCommand::Exit) {
      unsigned char mode = 0U;
      if (decoded == codex::ArcadeUiCommand::UseDpad) mode = 1U;
      if (decoded == codex::ArcadeUiCommand::UseTouchRegion) mode = 2U;
      if (bsp_display_lock(10)) {
        codex::ui_set_arcade_control_mode(mode);
        bsp_display_unlock();
        __atomic_store_n(&arcade_ui_command_requested, 0U,
                         __ATOMIC_RELEASE);
      }
      return;
    }

    codex::RawTouchFrame empty{};
    empty.monotonic_ms = now_ms;
    const codex::ArcadeInputOutput released =
        codex::normalize_arcade_touches(arcade_input, empty);
    for (unsigned int index = 0; index < released.frame.count; ++index) {
      enqueue_touch_event(released.frame.events[index]);
    }
    if (bsp_display_lock(20)) {
      codex::ui_begin_arcade_exit();
      codex::ui_finish_arcade_exit();
      bsp_display_unlock();
      __atomic_store_n(&arcade_ui_command_requested, 0U,
                       __ATOMIC_RELEASE);
      __atomic_store_n(&arcade_phase, 0U, __ATOMIC_RELEASE);
      __atomic_store_n(&last_user_activity_ms, now_ms, __ATOMIC_RELEASE);
      ESP_LOGI(kTag, "classic UI restored");
    }
    return;
  }
}

}  // namespace

extern "C" void app_main() {
  reducer_task_handle = xTaskGetCurrentTaskHandle();
  vTaskPrioritySet(nullptr, 5);
  const RecoveryBootMode boot_mode = recovery_guard_detect();
  ESP_LOGI(kTag, "boot mode=%s", boot_mode == RecoveryBootMode::Maintenance
                                      ? "maintenance"
                                      : "normal");
  ESP_LOGI(kTag, "Codex USB gate=%s",
           recovery_guard_allows_codex_usb() ? "open" : "locked");
  usb_identity_allowed = recovery_guard_allows_codex_usb();

  bool restored_defaults = false;
  state_mutex = xSemaphoreCreateMutex();
  settings_io_mutex = xSemaphoreCreateMutex();
  ESP_ERROR_CHECK(state_mutex == nullptr || settings_io_mutex == nullptr
                      ? ESP_ERR_NO_MEM : ESP_OK);
  ESP_ERROR_CHECK(codex::settings_store_load(&settings, &restored_defaults));
  apply_settings_to_state();
  set_power_profile(false);
  ESP_LOGI(kTag, "settings=%s layer=%u brightness=%u",
           restored_defaults ? "defaults" : "stored", settings.layer,
           settings.display_brightness);

  event_queue = xQueueCreateWithCaps(24, sizeof(codex::DeviceEvent),
                                     MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  critical_event_queue =
      xQueueCreateWithCaps(32, sizeof(codex::DeviceEvent),
                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#if CONFIG_CODEX_DEVELOPMENT_HIL
  hil_command_queue =
      xQueueCreateWithCaps(24, sizeof(codex::HilCommand),
                           MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
#endif
  ESP_ERROR_CHECK(event_queue == nullptr || critical_event_queue == nullptr
                      ? ESP_ERR_NO_MEM
                      : ESP_OK);
#if CONFIG_CODEX_DEVELOPMENT_HIL
  ESP_ERROR_CHECK(hil_command_queue == nullptr ? ESP_ERR_NO_MEM : ESP_OK);
#endif
  std::uint8_t update_mac[6]{};
  ESP_ERROR_CHECK(esp_read_mac(update_mac, ESP_MAC_BASE));
  codex::make_update_device_id(
      update_mac, update_protocol_context.trust.expected_device_id);
  for (const codex::UpdatePackageClass package_class : {
           codex::UpdatePackageClass::Compatibility,
           codex::UpdatePackageClass::ServiceReload,
           codex::UpdatePackageClass::CompleteFirmware,
           codex::UpdatePackageClass::UserContent}) {
    update_protocol_context.trust.minimum_release_sequence_by_class[
        codex::update_class_index(package_class)] =
        codex::stored_update_release_sequence(package_class);
  }
  update_protocol_context.trust.allowed_class_mask =
      codex::update_class_bit(codex::UpdatePackageClass::Compatibility) |
      codex::update_class_bit(codex::UpdatePackageClass::ServiceReload) |
      codex::update_class_bit(codex::UpdatePackageClass::CompleteFirmware) |
      codex::update_class_bit(codex::UpdatePackageClass::UserContent);
  update_protocol_context.trust.vendor_public_key_der =
      codex::update_public_key_der();
  update_protocol_context.trust.vendor_public_key_der_size =
      codex::update_public_key_der_size();
  update_protocol_context.has_device_kek = codex::derive_update_kek_from_efuse(
      update_protocol_context.trust.expected_device_id,
      update_protocol_context.device_kek);
  update_protocol_context.provisioned =
      update_protocol_context.has_device_kek;
  update_protocol_context.attest = provide_update_attestation;
  if (!update_protocol_context.provisioned) {
    ESP_LOGW(kTag,
             "secure updates disabled: exactly one eFuse HMAC_UP key is required");
  }
  if (!codex::load_active_compatibility_pack()) {
    ESP_LOGI(kTag, "no valid compatibility pack; using compiled defaults");
  }
  if (!codex::load_active_service_pack()) {
    ESP_LOGI(kTag, "no valid service pack; using compiled defaults");
  }
  transport_hooks = {
      .snapshot = transport_snapshot,
      .snapshot_into = transport_snapshot_into,
      .publish = transport_publish,
      .configure = transport_configure,
      .update = &update_protocol_context,
      .content = &user_content_protocol_context,
      .context = nullptr,
  };
  ESP_ERROR_CHECK(apply_transport_mode(device_state.transport,
                                       device_state.bluetooth_enabled));
  if (!usb_identity_allowed) {
    ESP_LOGW(kTag, "USB HID/CDC not started: recovery proof pending");
  }

  ESP_ERROR_CHECK(start_display() == nullptr ? ESP_FAIL : ESP_OK);
  ESP_ERROR_CHECK(
      bsp_display_brightness_set(physical_brightness(settings.display_brightness)));
  ESP_ERROR_CHECK(codex::codex_audio_start());
  codex::codex_audio_set_volume(settings.sound_volume);
  ESP_ERROR_CHECK(codex::board_power_start(transport_publish, nullptr));
  ESP_ERROR_CHECK(codex::board_controls_start(transport_publish, nullptr));
  bsp_display_lock(0);
  codex::ui_init(device_state);
  codex::ui_set_action_callback(on_ui_action, nullptr);
  bsp_display_unlock();

#if CONFIG_CODEX_DEVELOPMENT_HIL
  codex::display_monitor_start();
  ESP_ERROR_CHECK(codex::hil_console_start({
      .submit = submit_hil_command,
      .snapshot = hil_state_snapshot,
      .metrics = hil_metrics_snapshot,
      .trace = hil_trace_snapshot,
      .read_pixel = hil_read_pixel,
      .context = nullptr,
  }));
#endif

  ESP_LOGI(kTag, "CODEX_MICRO_BOARD_READY");
  if (codex::firmware_update_pending_verification()) {
    // Reaching this point proves that storage, transports, display, audio,
    // PMIC, controls and the UI initialized. Earlier failures stay eligible
    // for bootloader rollback.
    ESP_ERROR_CHECK(codex::confirm_running_firmware() ? ESP_OK : ESP_FAIL);
    update_protocol_context.trust.minimum_release_sequence_by_class[
        codex::update_class_index(
            codex::UpdatePackageClass::CompleteFirmware)] =
        codex::stored_update_release_sequence(
            codex::UpdatePackageClass::CompleteFirmware);
    ESP_LOGI(kTag, "confirmed pending OTA image after board self-test");
  }
  const unsigned int boot_ms =
      static_cast<unsigned int>(esp_timer_get_time() / 1000ULL);
  __atomic_store_n(&last_user_activity_ms, boot_ms, __ATOMIC_RELEASE);
  while (true) {
#if CONFIG_CODEX_DEVELOPMENT_HIL
    service_hil_commands(
        static_cast<unsigned int>(esp_timer_get_time() / 1000ULL));
#endif
    codex::DeviceEvent event{};
    codex::DeviceEvent lighting_events[codex::kLightingMailboxSlotCount]{};
    portENTER_CRITICAL(&lighting_mailbox_lock);
    const codex::MailboxRead lighting_read =
        codex::consume_lighting_mailbox(
            lighting_mailbox, lighting_events,
            codex::kLightingMailboxSlotCount);
    portEXIT_CRITICAL(&lighting_mailbox_lock);
    unsigned int lighting_event_index = 0;
    unsigned int critical_events_processed = 0;
    unsigned int normal_events_processed = 0;
    while (true) {
      bool have_event =
          critical_events_processed < codex::kMaxCriticalEventsPerFrame &&
          xQueueReceive(critical_event_queue, &event, 0) == pdTRUE;
      if (have_event) ++critical_events_processed;
      if (!have_event && lighting_event_index < lighting_read.count) {
        event = lighting_events[lighting_event_index++];
        have_event = true;
      }
      if (!have_event && normal_events_processed < 6U) {
        have_event = xQueueReceive(event_queue, &event, 0) == pdTRUE;
        if (have_event) ++normal_events_processed;
      }
      if (!have_event) break;
      if (event.enqueued_at_us != 0U &&
          (event.type == codex::EventType::KeyPressed ||
           event.type == codex::EventType::KeyReleased ||
           event.type == codex::EventType::JoystickChanged ||
           event.type == codex::EventType::EncoderStep)) {
        const unsigned int latency_us =
            static_cast<unsigned int>(esp_timer_get_time()) -
            event.enqueued_at_us;
        portENTER_CRITICAL(&diagnostics_lock);
        last_input_queue_latency_us = latency_us;
        if (latency_us > maximum_input_queue_latency_us) {
          maximum_input_queue_latency_us = latency_us;
        }
        portEXIT_CRITICAL(&diagnostics_lock);
      }
      xSemaphoreTake(state_mutex, portMAX_DELAY);
      const codex::DeviceState before = device_state;
      if (before.power == codex::PowerMode::ProtectedBlack &&
          (event.type == codex::EventType::PowerButtonPressed ||
           event.type == codex::EventType::ConnectionOverlayRequested)) {
        event = codex::make_protected_unlock_shown(
            static_cast<unsigned int>(esp_timer_get_time() / 1000ULL));
      }
      codex::ReduceResult reduced = codex::reduce(before, event);
      if (event.type == codex::EventType::AppKeyLightingChanged &&
          event.payload.app_key_lighting.id <
              codex::kPrivateLightingCount) {
        const unsigned int id = event.payload.app_key_lighting.id;
        if (event.payload.app_key_lighting.group ==
            codex::ControlGroup::Agent) {
          app_key_lighting.agents[id] =
              event.payload.app_key_lighting.lighting;
          app_key_lighting.agent_set[id] = true;
        } else if (event.payload.app_key_lighting.group ==
                   codex::ControlGroup::Command) {
          app_key_lighting.commands[id] =
              event.payload.app_key_lighting.lighting;
          app_key_lighting.command_set[id] = true;
        }
      }
      if (before.app_session.active && !reduced.state.app_session.active) {
        app_key_lighting = codex::AppKeyLightingState{};
      }
      if (!usb_identity_allowed &&
          reduced.state.transport == codex::TransportMode::Usb) {
        reduced.state.transport = codex::TransportMode::Ble;
      }
      const bool ble_slot_changed = reduced.state.ble_slot != before.ble_slot;
      const bool transport_changed = reduced.state.transport != before.transport;
      const bool bluetooth_changed =
          reduced.state.bluetooth_enabled != before.bluetooth_enabled;
      const bool display_changed =
          reduced.state.display_brightness != before.display_brightness;
      const bool joystick_sensitivity_changed =
          reduced.state.joystick_sensitivity_percent !=
          before.joystick_sensitivity_percent;
      const bool sound_changed =
          reduced.state.sound_profile != before.sound_profile ||
          reduced.state.sound_enabled != before.sound_enabled ||
          reduced.state.sound_volume != before.sound_volume;
      const bool routing_changed =
          !same_routing_preferences(reduced.state.routing, before.routing);
      device_state = reduced.state;
      __atomic_store_n(
          &power_mode_mirror,
          static_cast<unsigned char>(device_state.power),
          __ATOMIC_RELEASE);
      if (device_state.power == codex::PowerMode::Active ||
          (device_state.power == codex::PowerMode::ProtectedBlack &&
           before.power == codex::PowerMode::ProtectedUnlock)) {
        protected_unlock = {};
      }
      __atomic_store_n(
          &board_touch_enabled,
          device_state.power == codex::PowerMode::Active &&
              !device_state.battery_warning_visible &&
              device_state.overlay != codex::Overlay::Connection &&
              device_state.overlay != codex::Overlay::Pairing,
          __ATOMIC_RELEASE);
      if (event.type == codex::EventType::KeyPressed ||
          event.type == codex::EventType::JoystickChanged ||
          event.type == codex::EventType::EncoderStep ||
          event.type == codex::EventType::LayerNext ||
          event.type == codex::EventType::PowerButtonPressed) {
        __atomic_store_n(
            &last_user_activity_ms,
            static_cast<unsigned int>(esp_timer_get_time() / 1000ULL),
            __ATOMIC_RELEASE);
      }
      if (device_state.layer != before.layer) {
        settings.layer = device_state.layer;
        settings_dirty_at_ms = static_cast<unsigned int>(esp_timer_get_time() / 1000ULL) + 750U;
      }
      if (device_state.transport != before.transport) {
        settings.transport = device_state.transport;
        settings_dirty_at_ms = static_cast<unsigned int>(esp_timer_get_time() / 1000ULL) + 250U;
      }
      if (bluetooth_changed) {
        settings.bluetooth_enabled = device_state.bluetooth_enabled;
        settings_dirty_at_ms =
            static_cast<unsigned int>(esp_timer_get_time() / 1000ULL) + 250U;
      }
      if (device_state.protocol_profile != before.protocol_profile) {
        settings.protocol_profile = device_state.protocol_profile;
        settings_dirty_at_ms =
            static_cast<unsigned int>(esp_timer_get_time() / 1000ULL) + 250U;
      }
      if (routing_changed) {
        copy_routing_to_settings(device_state.routing);
        settings_dirty_at_ms =
            static_cast<unsigned int>(esp_timer_get_time() / 1000ULL) + 250U;
      }
      if (ble_slot_changed) {
        settings.ble_slot = device_state.ble_slot;
        settings_dirty_at_ms = static_cast<unsigned int>(esp_timer_get_time() / 1000ULL) + 250U;
      }
      for (unsigned int command = 0; command < codex::kCommandCount; ++command) {
        if (!same_text(device_state.commands[command].keycap_id,
                       before.commands[command].keycap_id)) {
          copy_text(settings.keycap_ids[command],
                    sizeof(settings.keycap_ids[command]),
                    device_state.commands[command].keycap_id);
        }
        if (!same_text(device_state.commands[command].label,
                       before.commands[command].label)) {
          copy_text(settings.labels[command], sizeof(settings.labels[command]),
                    device_state.commands[command].label);
        }
      }
      if (event.type == codex::EventType::CommandKeycapChanged ||
          event.type == codex::EventType::CommandLabelChanged) {
        settings_dirty_at_ms = static_cast<unsigned int>(esp_timer_get_time() / 1000ULL) + 250U;
      }
      if (sound_changed) {
        settings.sound_profile = device_state.sound_profile;
        settings.sound_enabled = device_state.sound_enabled;
        settings.sound_volume = device_state.sound_volume;
        settings_dirty_at_ms = static_cast<unsigned int>(esp_timer_get_time() / 1000ULL) + 250U;
      }
      if (display_changed ||
          device_state.standby_timeout_seconds != before.standby_timeout_seconds ||
          device_state.super_standby_timeout_seconds !=
              before.super_standby_timeout_seconds ||
          device_state.anti_accidental_shutdown !=
              before.anti_accidental_shutdown ||
          device_state.power_button_mode != before.power_button_mode ||
          device_state.auto_ultra_timeout_seconds !=
              before.auto_ultra_timeout_seconds ||
          device_state.ultra_touch_wake != before.ultra_touch_wake ||
          device_state.animation_strength != before.animation_strength) {
        settings.display_brightness = device_state.display_brightness;
        settings.standby_timeout_seconds = device_state.standby_timeout_seconds;
        settings.animation_strength = device_state.animation_strength;
        settings.super_standby_timeout_seconds =
            device_state.super_standby_timeout_seconds;
        settings.anti_accidental_shutdown =
            device_state.anti_accidental_shutdown;
        settings.power_button_mode = device_state.power_button_mode;
        settings.auto_ultra_timeout_seconds =
            device_state.auto_ultra_timeout_seconds;
        settings.ultra_touch_wake = device_state.ultra_touch_wake;
        settings_dirty_at_ms = static_cast<unsigned int>(esp_timer_get_time() / 1000ULL) + 250U;
      }
      if (joystick_sensitivity_changed) {
        settings.joystick_sensitivity_percent =
            device_state.joystick_sensitivity_percent;
        settings_dirty_at_ms =
            static_cast<unsigned int>(esp_timer_get_time() / 1000ULL) + 250U;
        __atomic_store_n(&joystick_sensitivity_mirror,
                         device_state.joystick_sensitivity_percent,
                         __ATOMIC_RELEASE);
      }
      for (unsigned int slot = 0; slot < 3; ++slot) {
        if (!same_peer(device_state.ble_peers[slot], before.ble_peers[slot])) {
          settings.ble_peers[slot] = device_state.ble_peers[slot];
          settings_dirty_at_ms = static_cast<unsigned int>(esp_timer_get_time() / 1000ULL) + 250U;
        }
      }
      xSemaphoreGive(state_mutex);
      if (event.type == codex::EventType::BatteryChanged) {
        codex::codex_transport_set_battery(
            codex::host_battery_percentage(reduced.state.battery_present,
                                           reduced.state.battery_percent),
            reduced.state.battery_present, reduced.state.battery_charging,
            reduced.state.usb_power_present);
      }
      for (unsigned int index = 0; index < reduced.effect_count; ++index) {
        codex::codex_transport_send_effect(reduced.effects[index]);
        if (reduced.state.sound_enabled) {
          codex::codex_audio_play(reduced.effects[index], reduced.state.sound_profile);
        }
        if (reduced.effects[index].type == codex::SideEffectType::EnterStandby) {
          const bool manual_power_standby =
              reduced.effects[index].direction == 1;
          const unsigned int flush_timeout_ms =
              manual_power_standby ? 120U : 2000U;
          if (!codex::codex_transport_flush(flush_timeout_ms)) {
            if (!manual_power_standby) {
              ESP_LOGE(kTag,
                       "transport drain timed out; standby cancelled");
              enqueue_event(codex::make_standby_transition_cancelled(
                                before.transport_connected),
                            pdMS_TO_TICKS(20), "standby-cancel");
              continue;
            }
            ESP_LOGW(kTag,
                     "manual standby continuing after bounded transport "
                     "drain");
          }
          esp_err_t touch_mode_error = ESP_ERR_TIMEOUT;
          for (unsigned int attempt = 0; attempt < 3U; ++attempt) {
            touch_mode_error = lvgl_port_touch_set_polling_mode(
                touch_input_device, true);
            if (touch_mode_error == ESP_OK) break;
            vTaskDelay(pdMS_TO_TICKS(5));
          }
          if (touch_mode_error != ESP_OK) {
            ESP_LOGE(kTag,
                     "touch polling transition failed; standby cancelled: %s",
                     esp_err_to_name(touch_mode_error));
            enqueue_event(codex::make_standby_transition_cancelled(
                              before.transport_connected),
                          pdMS_TO_TICKS(20), "standby-touch-cancel");
            continue;
          }
          if (!persist_settings_snapshot()) {
            ESP_LOGE(kTag, "settings flush before standby failed");
          }
          codex::codex_audio_set_enabled(false);
          bsp_audio_poweramp_enable(false);
          fade_display_out(reduced.state.display_brightness);
          input = codex::make_multi_touch_normalizer();
          codex::board_controls_set_standby(true);
          set_power_profile(true);
        } else if (reduced.effects[index].type == codex::SideEffectType::ExitStandby) {
          set_power_profile(false);
          codex::board_controls_set_standby(false);
          __atomic_store_n(&board_touch_enabled, false, __ATOMIC_RELEASE);
          fade_display_in(reduced.state.display_brightness);
          __atomic_store_n(&board_touch_enabled, true, __ATOMIC_RELEASE);
          bsp_audio_poweramp_enable(true);
          codex::codex_audio_set_enabled(true);
          const esp_err_t transport_error = reconcile_transport_after_wake(
              reduced.state.transport, reduced.state.bluetooth_enabled);
          if (transport_error != ESP_OK) {
            ESP_LOGE(kTag, "wake transport reconcile failed: %s",
                     esp_err_to_name(transport_error));
            persist_settings_snapshot();
            esp_restart();
          }
          touch_polling_restore_at_ms =
              static_cast<unsigned int>(esp_timer_get_time() / 1000ULL) +
              750U;
        } else if (reduced.effects[index].type ==
                   codex::SideEffectType::EnterUltraStandby) {
          if (!enter_ultra_hardware(reduced.state)) {
            ESP_LOGE(kTag, "ultra entry failed; returning to active mode");
            enqueue_event(codex::make_power_button_pressed(),
                          pdMS_TO_TICKS(20), "ultra-entry-cancel");
          }
        } else if (reduced.effects[index].type ==
                   codex::SideEffectType::ExitUltraStandby) {
          if (!exit_ultra_hardware(reduced.state)) {
            ESP_LOGE(kTag, "ultra wake recovery failed; restarting");
            esp_restart();
          }
        } else if (reduced.effects[index].type ==
                   codex::SideEffectType::PowerOff) {
          __atomic_store_n(&board_touch_enabled, false, __ATOMIC_RELEASE);
          persist_settings_snapshot();
          codex::codex_transport_flush(1500);
          codex::codex_ble_stop();
          codex::codex_usb_stop();
          codex::codex_audio_set_enabled(false);
          bsp_audio_poweramp_enable(false);
          hard_off_display_backlight();
          const esp_err_t power_off_error = codex::board_power_shutdown();
          if (power_off_error != ESP_OK) {
            ESP_LOGE(kTag, "AXP2101 power-off failed: %s",
                     esp_err_to_name(power_off_error));
          }
        } else if (reduced.effects[index].type == codex::SideEffectType::ClearBonds) {
          const unsigned char slot =
              static_cast<unsigned char>(reduced.effects[index].direction);
          if (!codex::codex_ble_started()) {
            const esp_err_t start_error =
                codex::codex_ble_start(transport_hooks);
            if (start_error != ESP_OK) {
              ESP_LOGE(kTag, "BLE start for bond clear failed: %s",
                       esp_err_to_name(start_error));
              continue;
            }
          }
          if (codex::codex_ble_clear_bonds(slot) != ESP_OK) {
            ESP_LOGE(kTag, "failed to clear BLE bonds");
          } else {
            enqueue_event(codex::make_ble_bonds_cleared(slot),
                          pdMS_TO_TICKS(20), "bond-clear");
          }
        }
      }
      if (display_changed && reduced.state.power == codex::PowerMode::Active) {
        const esp_err_t display_error =
            bsp_display_brightness_set(
                physical_brightness(reduced.state.display_brightness));
        if (display_error != ESP_OK) {
          ESP_LOGE(kTag, "brightness change failed: %s",
                   esp_err_to_name(display_error));
        }
      }
      if (sound_changed) {
        codex::codex_audio_set_volume(reduced.state.sound_volume);
      }
      if ((transport_changed || ble_slot_changed || bluetooth_changed) &&
          reduced.state.power == codex::PowerMode::Active) {
        if (!codex::codex_transport_flush(2000)) {
          ESP_LOGE(kTag, "transport drain timed out; switch cancelled");
          enqueue_event(codex::make_selection_transition_cancelled(
                            before.transport, before.ble_slot,
                            before.transport_connected),
                        pdMS_TO_TICKS(20), "selection-cancel");
          continue;
        }
        const esp_err_t transport_error =
            transport_changed || bluetooth_changed
                ? apply_transport_mode(reduced.state.transport,
                                       reduced.state.bluetooth_enabled)
                : codex::codex_ble_select_slot();
        if (transport_error != ESP_OK) {
          ESP_LOGE(kTag, "transport switch failed: %s; rebooting safely",
                   esp_err_to_name(transport_error));
          persist_settings_snapshot();
          esp_restart();
        }
      }
    }
    const unsigned int now_ms =
        static_cast<unsigned int>(esp_timer_get_time() / 1000ULL);
    if (touch_polling_restore_at_ms != 0U &&
        static_cast<signed int>(now_ms - touch_polling_restore_at_ms) >= 0) {
      if (lvgl_port_touch_set_polling_mode(touch_input_device, false) ==
          ESP_OK) {
        touch_polling_restore_at_ms = 0U;
      }
    }
    service_arcade(now_ms);
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    const codex::TransportMode active_mode = device_state.transport;
    const codex::PowerMode active_power = device_state.power;
    const bool bluetooth_enabled = device_state.bluetooth_enabled;
    xSemaphoreGive(state_mutex);
    if (active_mode == codex::TransportMode::Auto &&
        codex::allow_auto_transport_handoff(active_power) &&
        (auto_transport_retry_at_ms == 0 ||
         static_cast<signed int>(now_ms - auto_transport_retry_at_ms) >= 0)) {
      const bool usb_mounted = codex::codex_usb_connected();
      if (codex::auto_transport_observation_stable(
              auto_transport_debounce, usb_mounted, now_ms)) {
        esp_err_t auto_error = ESP_OK;
        const bool should_run_ble = codex::keep_ble_running(
            active_mode, usb_mounted, bluetooth_enabled);
        if (!should_run_ble && codex::codex_ble_started()) {
          auto_error = codex::codex_ble_stop();
        } else if (should_run_ble && !codex::codex_ble_started()) {
          auto_error = codex::codex_ble_start(transport_hooks);
        }
        if (auto_error != ESP_OK) {
          ESP_LOGW(kTag, "automatic transport handoff failed: %s",
                   esp_err_to_name(auto_error));
          auto_transport_retry_at_ms = now_ms + 2000U;
        } else {
          auto_transport_retry_at_ms = 0;
        }
      }
    }
    if (diagnostics_at_ms == 0 ||
        static_cast<signed int>(now_ms - diagnostics_at_ms) >= 0) {
      portENTER_CRITICAL(&diagnostics_lock);
      const unsigned int drops = dropped_events;
      const unsigned int high_water = event_queue_high_water;
      portEXIT_CRITICAL(&diagnostics_lock);
      xSemaphoreTake(state_mutex, portMAX_DELAY);
      const unsigned int rpc_errors = device_state.rpc_errors;
      xSemaphoreGive(state_mutex);
      portENTER_CRITICAL(&lighting_mailbox_lock);
      const unsigned int lighting_coalesced =
          lighting_mailbox.coalesced_count;
      portEXIT_CRITICAL(&lighting_mailbox_lock);
      enqueue_event(codex::make_diagnostics_changed(
                        drops, static_cast<unsigned char>(high_water), rpc_errors,
                        esp_get_free_heap_size(),
                        static_cast<unsigned char>(esp_reset_reason()),
                        usb_identity_allowed),
                    0, "diagnostics");
      if (runtime_log_at_ms == 0 ||
          static_cast<signed int>(now_ms - runtime_log_at_ms) >= 0) {
        ESP_LOGI(kTag,
                 "runtime slow=%u coalesced=%u ble_retry=%u ble_q=%u "
                 "internal=%u min_internal=%u psram=%u min_psram=%u "
                 "reset=%u",
                 slow_frame_count, lighting_coalesced,
                 codex::codex_ble_retry_count(),
                 codex::codex_ble_input_high_water(),
                 heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                 heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
                 heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM),
                 static_cast<unsigned int>(esp_reset_reason()));
        runtime_log_at_ms = now_ms + 10000U;
      }
      diagnostics_at_ms = now_ms + 1000U;
    }
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    const codex::DeviceState power_snapshot = device_state;
    xSemaphoreGive(state_mutex);
    const unsigned short standby_timeout =
        power_snapshot.standby_timeout_seconds;
    const codex::PowerMode power_mode = power_snapshot.power;
    const unsigned int super_standby_timeout =
        power_snapshot.super_standby_timeout_seconds;
    const bool anti_accidental_shutdown =
        power_snapshot.anti_accidental_shutdown;
    const bool transport_ready =
        codex::codex_usb_connected() || codex::codex_ble_connected();
    if (transport_ready) {
      disconnected_since_ms = now_ms;
    } else if (disconnected_since_ms == 0) {
      disconnected_since_ms = now_ms;
    }
    if (codex::codex_usb_control_active()) {
      __atomic_store_n(&last_user_activity_ms, now_ms, __ATOMIC_RELEASE);
    }
    const unsigned int inactive_ms =
        now_ms - __atomic_load_n(&last_user_activity_ms, __ATOMIC_ACQUIRE);
    bool lighting_active =
        power_snapshot.ambient.effect != codex::LightEffect::Off &&
        power_snapshot.ambient.brightness > 0.001F;
    lighting_active = lighting_active ||
        (power_snapshot.keys_lighting.effect != codex::LightEffect::Off &&
         power_snapshot.keys_lighting.brightness > 0.001F);
    for (unsigned int index = 0;
         index < codex::kAgentCount && !lighting_active; ++index) {
      const codex::Lighting& light = power_snapshot.agents[index].lighting;
      lighting_active = light.effect != codex::LightEffect::Off &&
                        light.brightness > 0.001F;
    }
    if (lighting_active) lighting_dark_since_ms = 0;
    else if (lighting_dark_since_ms == 0) lighting_dark_since_ms = now_ms;
    const unsigned int dark_ms = lighting_dark_since_ms == 0
                                     ? 0U : now_ms - lighting_dark_since_ms;
    const bool smart_screensaver_ready =
        !power_snapshot.smart_screensaver_enabled ||
        (!lighting_active &&
         (standby_timeout == 0 ||
          (inactive_ms >= static_cast<unsigned int>(standby_timeout) * 1000U &&
           dark_ms >= static_cast<unsigned int>(standby_timeout) * 1000U)));
    const bool arcade_session =
        __atomic_load_n(&arcade_phase, __ATOMIC_ACQUIRE) != 0U;
    const bool ultra_blocked = auto_ultra_work_blocked(power_snapshot);
    const bool ultra_grace_elapsed = codex::update_ultra_standby_guard(
        ultra_standby_guard, ultra_blocked, now_ms);
    const bool ultra_eligible =
        (power_mode == codex::PowerMode::Active ||
         power_mode == codex::PowerMode::Screensaver) &&
        !codex::codex_usb_connected() && !ultra_blocked;
    const codex::PowerDeadline deadline =
        arcade_session
            ? codex::PowerDeadline::None
            : codex::evaluate_power_deadline({
                  static_cast<unsigned int>(standby_timeout) * 1000U,
                  super_standby_timeout * 1000U,
                  inactive_ms,
                  static_cast<unsigned int>(now_ms - disconnected_since_ms),
                  transport_ready,
                  anti_accidental_shutdown,
                  power_mode == codex::PowerMode::Active &&
                      smart_screensaver_ready,
                  power_snapshot.auto_ultra_timeout_seconds * 1000U,
                  ultra_eligible,
                  ultra_grace_elapsed,
              });
    if (deadline == codex::PowerDeadline::AutoShutdown &&
        power_mode != codex::PowerMode::PowerOffPending) {
      enqueue_event(codex::make_super_standby_requested(), 0,
                    "auto-shutdown");
      disconnected_since_ms = now_ms;
    } else if (deadline == codex::PowerDeadline::UltraStandby) {
      enqueue_event(codex::make_enter_ultra_standby_requested(), 0,
                    "auto-ultra");
    } else if (deadline == codex::PowerDeadline::Screensaver) {
      enqueue_event(codex::make_enter_standby_requested(), 0, "idle-timeout");
    }
    if (settings_dirty_at_ms != 0 &&
        static_cast<signed int>(now_ms - settings_dirty_at_ms) >= 0) {
      if (!persist_settings_snapshot()) {
        ESP_LOGE(kTag, "failed to persist settings");
      }
      settings_dirty_at_ms = 0;
    }
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    const unsigned int pending_reboot_ms = reboot_at_ms;
    xSemaphoreGive(state_mutex);
    if (pending_reboot_ms != 0 &&
        static_cast<signed int>(now_ms - pending_reboot_ms) >= 0) {
      esp_restart();
    }
    xSemaphoreTake(state_mutex, portMAX_DELAY);
    const codex::DeviceState render_state = device_state;
    xSemaphoreGive(state_mutex);
    if ((render_state.overlay == codex::Overlay::Pairing ||
         render_state.power == codex::PowerMode::ProtectedUnlock ||
         render_state.battery_warning_visible) &&
        (overlay_tick_at_ms == 0 ||
         static_cast<signed int>(now_ms - overlay_tick_at_ms) >= 0)) {
      enqueue_event(codex::make_tick(now_ms), 0, "overlay-tick");
      overlay_tick_at_ms = now_ms + 50U;
    } else if (render_state.overlay != codex::Overlay::Pairing &&
               render_state.power != codex::PowerMode::ProtectedUnlock &&
               !render_state.battery_warning_visible) {
      overlay_tick_at_ms = 0;
    }
    const codex::CompositedLighting lighting =
        codex::compose_lighting(render_state, app_key_lighting, now_ms);
#if CONFIG_CODEX_DEVELOPMENT_HIL
    codex::display_monitor_set_expected_visible(
        render_state.power == codex::PowerMode::Active ||
        render_state.power == codex::PowerMode::ProtectedUnlock);
#endif
    if (render_state.power != codex::PowerMode::Active &&
        render_state.power != codex::PowerMode::ProtectedUnlock) {
      if (render_state.power == codex::PowerMode::UltraStandby) {
        service_ultra_hardware(render_state.ultra_touch_wake);
        continue;
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (codex::should_render_lighting(last_ui_render_ms, now_ms) &&
        bsp_display_lock(2)) {
      const unsigned int render_started_ms =
          static_cast<unsigned int>(esp_timer_get_time() / 1000ULL);
      codex::ui_apply_snapshot(render_state, lighting);
      bsp_display_unlock();
      const unsigned int render_duration_ms =
          static_cast<unsigned int>(esp_timer_get_time() / 1000ULL) -
          render_started_ms;
      if (render_duration_ms > codex::kLightingFrameIntervalMs) {
        ++slow_frame_count;
      }
      last_ui_render_ms = now_ms;
    }
    (void)ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(16));
  }
}
