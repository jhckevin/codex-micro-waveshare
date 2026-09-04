#include "codex/transport.h"

#include <atomic>

#include "codex/battery_publish_policy.h"
#include "codex/ble_advertising_plan.h"
#include "codex/ble_latency_policy.h"
#include "codex/ble_rejection_policy.h"
#include "codex/ble_reconnect_policy.h"
#include "codex/ble_session.h"
#include "codex/app_event_json.h"
#include "codex/codex_protocol.h"
#include "codex/config_gatt.h"
#include "codex/reliable_tx.h"
#include "codex/protocol_trace.h"
#include "codex/transport_policy.h"
#include "esp_bt.h"
#include "esp_attr.h"
#include "esp_bt_main.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_hidd.h"
#include "esp_hidd_gatts.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs_flash.h"

namespace codex {
namespace {

constexpr char kTag[] = "codex_ble";
constexpr char kManufacturer[] = "Work Louder";
constexpr char kSerial[] = "WS-S3-CODEX";

bool ble_stage_succeeded(const char* stage, esp_err_t error) {
  if (error == ESP_OK) {
    ESP_LOGI(kTag, "BLE init stage=%s result=ESP_OK", stage);
    return true;
  }
  ESP_LOGE(kTag, "BLE init stage=%s result=%s (0x%x)", stage,
           esp_err_to_name(error), static_cast<unsigned int>(error));
  return false;
}

void request_low_latency_connection(const unsigned char* address) {
  if (address == nullptr) return;
  constexpr BleConnectionParameters preferred =
      preferred_ble_connection_parameters();
  esp_ble_conn_update_params_t parameters{
      .bda = {},
      .min_int = preferred.minimum_interval_units,
      .max_int = preferred.maximum_interval_units,
      .latency = preferred.peripheral_latency,
      .timeout = preferred.supervision_timeout_units,
  };
  for (unsigned int index = 0; index < ESP_BD_ADDR_LEN; ++index) {
    parameters.bda[index] = address[index];
  }
  const esp_err_t error = esp_ble_gap_update_conn_params(&parameters);
  if (error == ESP_OK) {
    ESP_LOGI(kTag, "requested BLE HID interval 7.5-15 ms latency=0");
  } else {
    // The central owns the final interval. Refusal must not disturb an
    // otherwise authenticated HID session.
    ESP_LOGW(kTag, "BLE low-latency interval request rejected: %s",
             esp_err_to_name(error));
  }
}

const unsigned char kReportMap[] = {
    0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x06,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x3F,
    0x09, 0x01, 0x81, 0x02, 0x95, 0x3F, 0x09, 0x02, 0x91, 0x02,
    0x85, 0x07, 0x95, 0x3F, 0x09, 0x03, 0x81, 0x02,
    0x95, 0x3F, 0x09, 0x04, 0x91, 0x02, 0xC0,
};

esp_hid_raw_report_map_t report_maps[] = {{kReportMap, sizeof(kReportMap)}};
esp_hid_device_config_t hid_config = {
    .vendor_id = 0x303A,
    .product_id = 0x8360,
    .version = 0x0101,
    .device_name = kBleDeviceName,
    .manufacturer_name = kManufacturer,
    .serial_number = kSerial,
    .report_maps = report_maps,
    .report_maps_len = 1,
};

constexpr unsigned int kBleRxStackBytes = 12U * 1024U;

struct BleRpcWorkspace {
  DeviceState state{};
  RpcDispatch dispatch{};
};
static_assert(sizeof(BleRpcWorkspace) <= 4096U,
              "BLE RPC workspace exceeded its bounded PSRAM budget");

esp_hidd_dev_t* hid_device{};
TransportHooks hooks{};
EXT_RAM_BSS_ATTR ReportAssembler assembler{};
EXT_RAM_BSS_ATTR ReportAssembler config_assembler{};
EXT_RAM_BSS_ATTR BleRpcWorkspace rpc_workspace{};
EXT_RAM_BSS_ATTR BleSession ble_session{};
ProtocolTrace protocol_trace{};
portMUX_TYPE session_lock = portMUX_INITIALIZER_UNLOCKED;
std::atomic_bool connected{};
std::atomic_bool host_ready{};
std::atomic_bool suspended{};
std::atomic_bool restart_advertising_requested{};
bool hid_started{};
bool config_gatt_started{};
bool advertising_ready{};
bool scan_response_ready{};
volatile bool stopping{};
unsigned char connected_address[6]{};
bool connected_address_valid{};
bool pre_auth_overflowed{};
bool replaying_pre_auth{};
bool bonded_cccs_restored{};
BleRejectionState rejection_state{};
unsigned char rejected_address[6]{};
esp_timer_handle_t rejected_disconnect_timer{};
esp_timer_handle_t advertising_backoff_timer{};
constexpr unsigned int kTxJsonCapacity = 1024;
struct TxMessage {
  unsigned char report_id{};
  unsigned short length{};
  bool critical{};
  bool stop{};
  unsigned short release_mask{};
  char json[kTxJsonCapacity]{};
};
QueueHandle_t tx_queue{};
QueueHandle_t input_tx_queue{};
constexpr unsigned int kRxQueueDepth = 32;
struct RxMessage {
  unsigned int generation{};
  unsigned char report_id{};
  unsigned char length{};
  bool stop{};
  unsigned char data[kPreAuthReportMaxBytes]{};
};
QueueHandle_t rx_queue{};
TaskHandle_t tx_task_handle{};
TaskHandle_t rx_task_handle{};
SemaphoreHandle_t tx_stopped{};
SemaphoreHandle_t rx_stopped{};
std::atomic_uint pending_tx{};
std::atomic_uint pending_rx{};
std::atomic_uint session_generation{};
std::atomic_uint queue_drop_count{};
std::atomic_uint consecutive_tx_failures{};
std::atomic_uint total_tx_retries{};
std::atomic_uchar input_tx_high_water{};
std::atomic_uint last_input_tx_latency_us{};
std::atomic_uint maximum_input_tx_latency_us{};
std::atomic_uint rx_stack_low_water_bytes{kBleRxStackBytes};
std::atomic_uchar battery_level{0xFF};
std::atomic_bool battery_present{};
std::atomic_bool battery_charging{};
std::atomic_bool battery_external_power{};
unsigned int dropped_tx_reports{};
unsigned int last_drop_log_ms{};

unsigned int now_ms() {
  return static_cast<unsigned int>(esp_timer_get_time() / 1000ULL);
}

void record_protocol(ProtocolTraceType type, unsigned char report_id = 0,
                     unsigned short detail = 0, const char* method = "") {
  ProtocolTraceRecord record{
      .monotonic_ms = now_ms(),
      .type = type,
      .report_id = report_id,
      .detail = detail,
  };
  unsigned int index = 0;
  while (index + 1 < sizeof(record.method) && method[index] != '\0') {
    record.method[index] = method[index];
    ++index;
  }
  record.method[index] = '\0';
  portENTER_CRITICAL(&session_lock);
  trace_protocol(protocol_trace, record);
  portEXIT_CRITICAL(&session_lock);
}

void publish_cached_battery(BatteryPublishPoint point) {
  if (hid_device == nullptr || !should_publish_battery(point)) return;
  const unsigned char percentage =
      battery_level.load(std::memory_order_acquire);
  if (!battery_percentage_is_initialized(percentage)) return;
  const esp_hid_battery_status_t status = {
      .level = percentage,
      .present = battery_present.load(std::memory_order_acquire),
      .charging = battery_charging.load(std::memory_order_acquire),
      .external_power =
          battery_external_power.load(std::memory_order_acquire),
  };
  const esp_err_t error =
      esp_hidd_dev_battery_status_set(hid_device, &status);
  if (error != ESP_OK) {
    ESP_LOGW(kTag, "battery publish failed point=%u level=%u: %s",
             static_cast<unsigned int>(point), percentage,
             esp_err_to_name(error));
  }
}

bool restore_bonded_cccs_if_ready() {
  bool should_restore = false;
  portENTER_CRITICAL(&session_lock);
  should_restore =
      should_restore_bonded_cccs(ble_session, bonded_cccs_restored);
  portEXIT_CRITICAL(&session_lock);
  if (!should_restore) return true;
  const esp_err_t error = esp_hidd_dev_restore_bonded_cccs(hid_device);
  if (error != ESP_OK) {
    ESP_LOGE(kTag, "unable to restore bonded HID CCC state: %s",
             esp_err_to_name(error));
    return false;
  }
  portENTER_CRITICAL(&session_lock);
  bonded_cccs_restored = true;
  portEXIT_CRITICAL(&session_lock);
  return true;
}

esp_ble_adv_params_t advertising_params = {
    .adv_int_min = 0x20,
    .adv_int_max = 0x40,
    .adv_type = ADV_TYPE_IND,
    .own_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .peer_addr = {},
    .peer_addr_type = BLE_ADDR_TYPE_PUBLIC,
    .channel_map = ADV_CHNL_ALL,
    .adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY,
};

// Bluedroid's structured advertising API accepts UUID lists only in 128-bit
// units. This is the Bluetooth base UUID carrying the 16-bit HID service 0x1812.
unsigned char hid_service_uuid128[] = {
    0xFB, 0x34, 0x9B, 0x5F, 0x80, 0x00, 0x00, 0x80,
    0x00, 0x10, 0x00, 0x00, 0x12, 0x18, 0x00, 0x00,
};
static_assert(advertising_service_uuid_width_valid(
    kCodexPrimaryAdvertisement));
static_assert(advertising_packet_size(kCodexPrimaryAdvertisement) <=
              kLegacyAdvertisingLimit);
static_assert(advertising_packet_margin(kCodexPrimaryAdvertisement) >=
              kAdvertisingReserveTarget);
esp_ble_adv_data_t advertising_data = {
    .set_scan_rsp = false,
    .include_name = kCodexPrimaryAdvertisement.complete_name_bytes != 0,
    .include_txpower = kCodexPrimaryAdvertisement.tx_power,
    .min_interval = kCodexPrimaryAdvertisement.connection_interval ? 0x0006 : 0,
    .max_interval = kCodexPrimaryAdvertisement.connection_interval ? 0x0010 : 0,
    .appearance = kCodexPrimaryAdvertisement.appearance ? 0x03C0 : 0,
    .manufacturer_len = 0,
    .p_manufacturer_data = nullptr,
    .service_data_len = 0,
    .p_service_data = nullptr,
    .service_uuid_len = kCodexPrimaryAdvertisement.service_uuid_bytes,
    .p_service_uuid = nullptr,
    .flag = kCodexPrimaryAdvertisement.flags
                ? ESP_BLE_ADV_FLAG_GEN_DISC | ESP_BLE_ADV_FLAG_BREDR_NOT_SPT
                : 0,
};

static_assert(kCodexScanResponse.service_uuid_bytes ==
              sizeof(hid_service_uuid128));
static_assert(advertising_service_uuid_width_valid(kCodexScanResponse));
static_assert(advertising_packet_size(kCodexScanResponse) <=
              kLegacyAdvertisingLimit);
static_assert(advertising_packet_margin(kCodexScanResponse) >=
              kAdvertisingReserveTarget);
esp_ble_adv_data_t scan_response_data = {
    .set_scan_rsp = true,
    .include_name = kCodexScanResponse.complete_name_bytes != 0,
    .include_txpower = kCodexScanResponse.tx_power,
    .min_interval = 0,
    .max_interval = 0,
    .appearance = 0,
    .manufacturer_len = 0,
    .p_manufacturer_data = nullptr,
    .service_data_len = 0,
    .p_service_data = nullptr,
    .service_uuid_len = kCodexScanResponse.service_uuid_bytes,
    .p_service_uuid = hid_service_uuid128,
    .flag = 0,
};

void start_advertising_if_ready() {
  if (suspended.load(std::memory_order_acquire)) return;
  bool backoff = false;
  portENTER_CRITICAL(&session_lock);
  backoff =
      rejection_state.phase == BleRejectionPhase::AdvertisingBackoff;
  portEXIT_CRITICAL(&session_lock);
  if (hid_started && advertising_ready && scan_response_ready && !connected &&
      !backoff) {
    esp_ble_adv_params_t reconnect_params = advertising_params;
    const DeviceState state = hooks.snapshot != nullptr
                                  ? hooks.snapshot(hooks.context)
                                  : DeviceState{};
    const BleReconnectTarget target = ble_reconnect_target(
        state.ble_slot, state.ble_peers, state.overlay == Overlay::Pairing);
    if (target.directed) {
      reconnect_params.adv_type = ADV_TYPE_DIRECT_IND_LOW;
      reconnect_params.peer_addr_type =
          static_cast<esp_ble_addr_type_t>(target.address_type);
      for (unsigned int index = 0; index < ESP_BD_ADDR_LEN; ++index) {
        reconnect_params.peer_addr[index] = target.address[index];
      }
      ESP_LOGI(kTag, "advertising directed reconnect for BLE slot %u",
               state.ble_slot);
    }
    const esp_err_t error = esp_ble_gap_start_advertising(&reconnect_params);
    if (error != ESP_OK && error != ESP_ERR_INVALID_STATE) {
      ESP_LOGE(kTag, "unable to start %s advertising: %s",
               target.directed ? "directed reconnect" : "general",
               esp_err_to_name(error));
    }
  }
}

void rejected_disconnect_timer_callback(void*) {
  unsigned char address[6]{};
  bool should_disconnect = false;
  portENTER_CRITICAL(&session_lock);
  should_disconnect = mark_deferred_disconnect_started(rejection_state);
  if (should_disconnect) {
    for (unsigned int index = 0; index < 6; ++index) {
      address[index] = rejected_address[index];
    }
  }
  portEXIT_CRITICAL(&session_lock);
  if (!should_disconnect || stopping) return;
  const esp_err_t error = esp_ble_gap_disconnect(address);
  if (error != ESP_OK) {
    ESP_LOGE(kTag, "deferred wrong-slot disconnect failed: %s",
             esp_err_to_name(error));
  }
}

void advertising_backoff_timer_callback(void*) {
  BleRejectionAction action = BleRejectionAction::None;
  portENTER_CRITICAL(&session_lock);
  action = note_advertising_backoff_elapsed(rejection_state);
  portEXIT_CRITICAL(&session_lock);
  if (action == BleRejectionAction::RestartAdvertising && !stopping) {
    start_advertising_if_ready();
  }
}

void start_config_gatt_if_ready() {
  if (!config_gatt_should_start(hid_started, config_gatt_started, stopping)) {
    return;
  }
  const esp_err_t error = config_gatt_start();
  config_gatt_started = ble_stage_succeeded("config-gatt", error);
}

void publish(DeviceEvent event) {
  if (hooks.publish != nullptr) hooks.publish(event, hooks.context);
}

const char* key_for(ControlId control, signed char direction);

bool transmit_json(unsigned char report_id, const char* json,
                   unsigned int length, bool critical) {
  if (critical) {
    unsigned int waited_ms = 0;
    while (connected.load(std::memory_order_acquire) &&
           !host_ready.load(std::memory_order_acquire) && !stopping &&
           waited_ms < 750U) {
      vTaskDelay(pdMS_TO_TICKS(2));
      waited_ms += 2U;
    }
  }
  unsigned int offset = 0;
  while (offset <= length && connected.load(std::memory_order_acquire) &&
         host_ready.load(std::memory_order_acquire) &&
         hid_device != nullptr) {
    unsigned char report[kCodexReportBodySize]{};
    report[0] = 2;
    const unsigned int available = length - offset + 1U;
    const unsigned int chunk = available > kCodexPayloadSize
                                   ? kCodexPayloadSize : available;
    report[1] = static_cast<unsigned char>(chunk);
    for (unsigned int index = 0; index < chunk; ++index) {
      report[index + 2] = offset + index < length
                              ? static_cast<unsigned char>(json[offset + index])
                              : '\n';
    }
    bool sent = false;
    unsigned int attempts = 0;
    const unsigned int attempt_limit =
        critical ? kBleTxCriticalAttemptLimit : 1U;
    while (connected.load(std::memory_order_acquire) &&
           host_ready.load(std::memory_order_acquire) && !stopping &&
           attempts < attempt_limit) {
      if (esp_hidd_dev_input_set(hid_device, 0, report_id, report,
                                 kCodexReportBodySize) == ESP_OK) {
        sent = true;
        break;
      }
      ++attempts;
      vTaskDelay(pdMS_TO_TICKS(5));
    }
    if (!sent) return false;
    offset += chunk;
    if (offset > length) break;
    vTaskDelay(pdMS_TO_TICKS(4));
  }
  return offset > length;
}

bool transmit_release_batch(unsigned short release_mask) {
  for (unsigned int value = 0; value <= 12U; ++value) {
    if ((release_mask & (1U << value)) == 0) continue;
    char json[192]{};
    const unsigned int length = make_codex_hid_json(
        json, sizeof(json), key_for(static_cast<ControlId>(value), 0), 0,
        value < kAgentCount ? static_cast<signed char>(value) : -1);
    if (!length || !transmit_json(kCodexReportId, json, length, true)) {
      return false;
    }
  }
  if ((release_mask & (1U << 13U)) != 0) {
    char json[192]{};
    const unsigned int length = make_codex_joystick_json(
        json, sizeof(json), 0.0F, 0.0F);
    if (!length || !transmit_json(kCodexReportId, json, length, true)) {
      return false;
    }
  }
  return true;
}

void tx_task(void*) {
  TxMessage message{};
  ReliableTxRecord input_message{};
  while (!stopping) {
    if (xQueueReceive(input_tx_queue, &input_message, 0) == pdTRUE) {
      char json[192]{};
      const unsigned int length = make_codex_hid_json(
          json, sizeof(json),
          key_for(input_message.control, input_message.direction),
          input_message.action, input_message.agent);
      const bool link_open = connected.load(std::memory_order_acquire);
      const bool channel_ready = host_ready.load(std::memory_order_acquire);
      const bool sent =
          length != 0 &&
          transmit_json(kCodexReportId, json, length, true);
      if (sent) codex_transport_note_input_tx(input_message.origin_us);
      if (quarantine_ble_input_channel(sent, link_open, channel_ready)) {
        consecutive_tx_failures.fetch_add(1, std::memory_order_relaxed);
        total_tx_retries.fetch_add(1, std::memory_order_relaxed);
        host_ready.store(false, std::memory_order_release);
        ESP_LOGW(kTag,
                 "BLE HID notifications unavailable; input TX paused until "
                 "the next host output");
      } else if (sent) {
        consecutive_tx_failures.store(0, std::memory_order_release);
      }
      publish(make_hid_tx_completed(sent));
      pending_tx.fetch_sub(1, std::memory_order_release);
      continue;
    }
    if (xQueueReceive(tx_queue, &message, pdMS_TO_TICKS(2)) != pdTRUE) {
      continue;
    }
    if (message.stop) break;
    const bool link_open = connected.load(std::memory_order_acquire);
    const bool channel_ready = host_ready.load(std::memory_order_acquire);
    const bool sent =
        message.release_mask != 0
            ? transmit_release_batch(message.release_mask)
            : transmit_json(message.report_id, message.json,
                            message.length, message.critical);
    if (sent) {
      consecutive_tx_failures.store(0, std::memory_order_release);
    } else if (quarantine_ble_input_channel(
                   sent, link_open, channel_ready)) {
      const unsigned int failures =
          consecutive_tx_failures.fetch_add(1, std::memory_order_acq_rel) + 1U;
      total_tx_retries.fetch_add(1, std::memory_order_relaxed);
      ++dropped_tx_reports;
      const unsigned int now_ms =
          static_cast<unsigned int>(esp_timer_get_time() / 1000ULL);
      if (last_drop_log_ms == 0 || now_ms - last_drop_log_ms >= 1000U) {
        ESP_LOGW(kTag, "BLE TX stalled; dropped=%u failures=%u",
                 dropped_tx_reports, failures);
        dropped_tx_reports = 0;
        last_drop_log_ms = now_ms;
      }
      // transmit_json already made the bounded critical attempts. The OS may
      // retain authentication while the HID input CCC is not enabled; no
      // amount of immediate retrying can change that. A subsequent host
      // output marks the channel ready again.
      host_ready.store(false, std::memory_order_release);
      ESP_LOGW(kTag, "BLE host input channel unavailable; TX paused");
    }
    pending_tx.fetch_sub(1, std::memory_order_release);
  }
  tx_task_handle = nullptr;
  xSemaphoreGive(tx_stopped);
  vTaskDeleteWithCaps(nullptr);
}

bool enqueue_release_batch(unsigned short release_mask) {
  if (release_mask == 0 || stopping || tx_queue == nullptr) return release_mask == 0;
  const TxMessage message{.critical = true, .release_mask = release_mask};
  pending_tx.fetch_add(1, std::memory_order_acquire);
  if (xQueueSend(tx_queue, &message, pdMS_TO_TICKS(250)) != pdTRUE) {
    pending_tx.fetch_sub(1, std::memory_order_release);
    ESP_LOGE(kTag, "unable to queue atomic BLE release batch");
    return false;
  }
  return true;
}

bool enqueue_json(unsigned char report_id, const char* json, unsigned int length,
                  TickType_t wait = 0, bool critical = false) {
  if (stopping || tx_queue == nullptr || length >= kTxJsonCapacity ||
      (!critical && !host_ready.load(std::memory_order_acquire)) ||
      (critical && !connected.load(std::memory_order_acquire))) {
    return false;
  }
  TxMessage message{.report_id = report_id,
                    .length = static_cast<unsigned short>(length),
                    .critical = critical};
  for (unsigned int index = 0; index < length; ++index) message.json[index] = json[index];
  message.json[length] = '\0';
  pending_tx.fetch_add(1, std::memory_order_acquire);
  const BaseType_t queued = xQueueSend(tx_queue, &message, wait);
  if (queued != pdTRUE) {
    pending_tx.fetch_sub(1, std::memory_order_release);
    const unsigned int drops =
        queue_drop_count.fetch_add(1, std::memory_order_relaxed) + 1U;
    if (drops == 1U || (drops % 64U) == 0U) {
      ESP_LOGW(kTag, "BLE TX queue full; dropped=%u", drops);
    }
    return false;
  }
  return true;
}

void send_json(const char* json, unsigned int length) {
  enqueue_json(kCodexReportId, json, length, portMAX_DELAY, true);
}

void send_config_json(const char* json, unsigned int length) {
  enqueue_json(kConfigReportId, json, length, portMAX_DELAY, true);
}

void snapshot_into_rpc_workspace() {
  hooks.snapshot_into(rpc_workspace.state, hooks.context);
}

void handle_config_output(const unsigned char* data, unsigned int length) {
  const AssemblerResult result = consume_codex_report(
      config_assembler, data, length,
      static_cast<unsigned int>(esp_timer_get_time() / 1000ULL));
  if (result.status != AssembleStatus::Complete) return;
  if (hooks.update != nullptr) {
    const UpdateProtocolReply update = handle_update_protocol(
        config_assembler.message, config_assembler.length,
        UpdateTransport::Ble, *hooks.update);
    if (update.handled) {
      config_assembler.length = 0;
      send_config_json(update.json, update.length);
      return;
    }
  }
  snapshot_into_rpc_workspace();
  ConfigReply reply = handle_config_line(config_assembler.message,
                                         config_assembler.length,
                                         rpc_workspace.state);
  config_assembler.length = 0;
  if (!reply.ok) publish(make_rpc_error_recorded());
  if (reply.ok && reply.action != ConfigAction::None && hooks.configure != nullptr &&
      !hooks.configure(reply, hooks.context)) {
    constexpr char failure[] = "{\"ok\":false,\"error\":\"apply_failed\"}";
    unsigned int index = 0;
    while (index + 1 < sizeof(reply.json) && failure[index]) {
      reply.json[index] = failure[index];
      ++index;
    }
    reply.json[index] = '\0';
    reply.length = static_cast<unsigned short>(index);
  }
  send_config_json(reply.json, reply.length);
}

void handle_output(const unsigned char* data, unsigned int length) {
  const AssemblerResult result = consume_codex_report(
      assembler, data, length,
      static_cast<unsigned int>(esp_timer_get_time() / 1000ULL));
  if (result.status != AssembleStatus::Complete) return;
  snapshot_into_rpc_workspace();
  dispatch_codex_rpc_into(
      assembler.message, assembler.length, rpc_workspace.state,
      static_cast<unsigned int>(esp_timer_get_time() / 1000ULL),
      rpc_workspace.dispatch);
  assembler.length = 0;
  const RpcDispatch& dispatch = rpc_workspace.dispatch;
  if (!dispatch.ok) publish(make_rpc_error_recorded());
  if (dispatch.ok) {
    publish(make_codex_rpc_received(
        static_cast<unsigned int>(esp_timer_get_time() / 1000ULL),
        dispatch.lighting_update, TransportLink::Ble));
  }
  for (unsigned int index = 0; index < dispatch.event_count; ++index) {
    publish(dispatch.events[index]);
  }
  if (dispatch.reply_length) send_json(dispatch.reply, dispatch.reply_length - 1U);
}

bool enqueue_rx_report(unsigned char report_id, const unsigned char* data,
                       unsigned int length) {
  if (stopping || rx_queue == nullptr || data == nullptr || length == 0 ||
      length > kPreAuthReportMaxBytes) {
    return false;
  }
  RxMessage message{
      .generation = session_generation.load(std::memory_order_acquire),
      .report_id = report_id,
      .length = static_cast<unsigned char>(length),
  };
  for (unsigned int index = 0; index < length; ++index) {
    message.data[index] = data[index];
  }
  pending_rx.fetch_add(1, std::memory_order_acq_rel);
  if (xQueueSend(rx_queue, &message, 0) != pdTRUE) {
    pending_rx.fetch_sub(1, std::memory_order_acq_rel);
    record_protocol(ProtocolTraceType::QueueOverflow, report_id,
                    static_cast<unsigned short>(length));
    ESP_LOGE(kTag, "BLE RX queue overflow report=%u length=%u",
             report_id, length);
    return false;
  }
  return true;
}

void sample_rx_stack_reserve() {
  const unsigned int available =
      static_cast<unsigned int>(uxTaskGetStackHighWaterMark(nullptr));
  unsigned int observed =
      rx_stack_low_water_bytes.load(std::memory_order_relaxed);
  while (available < observed &&
         !rx_stack_low_water_bytes.compare_exchange_weak(
             observed, available, std::memory_order_release,
             std::memory_order_relaxed)) {
  }
}

void rx_task(void*) {
  RxMessage message{};
  sample_rx_stack_reserve();
  while (!stopping) {
    if (xQueueReceive(rx_queue, &message, pdMS_TO_TICKS(20)) != pdTRUE) {
      continue;
    }
    if (message.stop) break;
    if (message.generation ==
            session_generation.load(std::memory_order_acquire) &&
        connected.load(std::memory_order_acquire)) {
      if (message.report_id == kCodexReportId) {
        handle_output(message.data, message.length);
      } else if (message.report_id == kConfigReportId) {
        handle_config_output(message.data, message.length);
      }
    }
    sample_rx_stack_reserve();
    pending_rx.fetch_sub(1, std::memory_order_acq_rel);
  }
  rx_task_handle = nullptr;
  xSemaphoreGive(rx_stopped);
  vTaskDeleteWithCaps(nullptr);
}

bool same_address(const unsigned char left[6], const unsigned char right[6]) {
  for (unsigned int index = 0; index < 6; ++index) {
    if (left[index] != right[index]) return false;
  }
  return true;
}

void gap_callback(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* params) {
  if (stopping) return;
  if (event == ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT) {
    advertising_ready = true;
    start_advertising_if_ready();
  } else if (event == ESP_GAP_BLE_SCAN_RSP_DATA_SET_COMPLETE_EVT) {
    scan_response_ready = true;
    start_advertising_if_ready();
  } else if (event == ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT) {
    if (restart_advertising_requested.exchange(false,
                                                std::memory_order_acq_rel)) {
      start_advertising_if_ready();
    }
  } else if (event == ESP_GAP_BLE_SEC_REQ_EVT && params != nullptr) {
    esp_ble_gap_security_rsp(params->ble_security.ble_req.bd_addr, true);
  } else if (event == ESP_GAP_BLE_AUTH_CMPL_EVT && params != nullptr &&
             !params->ble_security.auth_cmpl.success) {
    portENTER_CRITICAL(&session_lock);
    ble_authorization_failed(ble_session);
    pre_auth_overflowed = false;
    replaying_pre_auth = false;
    bonded_cccs_restored = false;
    portEXIT_CRITICAL(&session_lock);
    session_generation.fetch_add(1, std::memory_order_acq_rel);
    connected.store(false, std::memory_order_release);
    host_ready.store(false, std::memory_order_release);
    assembler.length = 0;
    config_assembler.length = 0;
    record_protocol(ProtocolTraceType::AuthenticationFailed, 0,
                    params->ble_security.auth_cmpl.fail_reason);
    ESP_LOGE(kTag, "BLE authentication failed reason=0x%02x",
             params->ble_security.auth_cmpl.fail_reason);
    esp_ble_remove_bond_device(params->ble_security.auth_cmpl.bd_addr);
    esp_ble_gap_disconnect(params->ble_security.auth_cmpl.bd_addr);
  } else if (event == ESP_GAP_BLE_AUTH_CMPL_EVT &&
              params != nullptr && params->ble_security.auth_cmpl.success) {
    const DeviceState state = hooks.snapshot != nullptr
                                  ? hooks.snapshot(hooks.context)
                                  : DeviceState{};
    const BlePeerResolution peer = resolve_ble_peer(
        state.ble_slot, state.ble_peers,
        params->ble_security.auth_cmpl.bd_addr,
        state.overlay == Overlay::Pairing);
    if (!peer.allowed) {
      ESP_LOGW(kTag, "rejecting peer assigned to a different BLE slot");
      BleRejectionAction rejection = BleRejectionAction::None;
      portENTER_CRITICAL(&session_lock);
      ble_authorization_failed(ble_session);
      rejection = note_wrong_slot_peer(rejection_state);
      if (rejection == BleRejectionAction::DeferDisconnect) {
        for (unsigned int index = 0; index < 6; ++index) {
          rejected_address[index] =
              params->ble_security.auth_cmpl.bd_addr[index];
        }
      }
      portEXIT_CRITICAL(&session_lock);
      if (rejection == BleRejectionAction::DeferDisconnect) {
        const esp_err_t error =
            esp_timer_start_once(rejected_disconnect_timer, 1000ULL);
        if (error != ESP_OK) {
          ESP_LOGE(kTag, "unable to defer wrong-slot disconnect: %s",
                   esp_err_to_name(error));
        }
      }
      return;
    }
    portENTER_CRITICAL(&session_lock);
    const bool overflowed = pre_auth_overflowed;
    if (!overflowed) {
      ble_authorization_succeeded(ble_session);
      replaying_pre_auth = ble_session.pre_auth.count != 0;
    }
    portEXIT_CRITICAL(&session_lock);
    if (overflowed) {
      ESP_LOGE(kTag, "rejecting BLE session after pre-auth queue overflow");
      record_protocol(ProtocolTraceType::QueueOverflow);
      esp_ble_gap_disconnect(params->ble_security.auth_cmpl.bd_addr);
      return;
    }
    connected = true;
    request_low_latency_connection(
        params->ble_security.auth_cmpl.bd_addr);
    config_gatt_set_authorized(true);
    for (unsigned int index = 0; index < 6; ++index) {
      connected_address[index] = params->ble_security.auth_cmpl.bd_addr[index];
    }
    connected_address_valid = true;
    record_protocol(ProtocolTraceType::Authorized);
    if (!restore_bonded_cccs_if_ready()) {
      esp_ble_gap_disconnect(params->ble_security.auth_cmpl.bd_addr);
      return;
    }
    portENTER_CRITICAL(&session_lock);
    const bool ready = ble_session_ready(ble_session);
    portEXIT_CRITICAL(&session_lock);
    host_ready.store(ready, std::memory_order_release);
    if (ready) {
      record_protocol(ProtocolTraceType::Ready);
      consecutive_tx_failures.store(0, std::memory_order_release);
      publish_cached_battery(BatteryPublishPoint::HostReady);
    }
    publish(make_transport_connected(TransportLink::Ble));
    publish_cached_battery(BatteryPublishPoint::Authenticated);
    publish(make_ble_peer_bonded(
        peer.slot, params->ble_security.auth_cmpl.bd_addr,
        static_cast<unsigned char>(params->ble_security.auth_cmpl.addr_type)));
    while (true) {
      PreAuthReport queued{};
      portENTER_CRITICAL(&session_lock);
      const bool available =
          pop_pre_auth_report(ble_session.pre_auth, &queued);
      if (!available) replaying_pre_auth = false;
      portEXIT_CRITICAL(&session_lock);
      if (!available) break;
      if (!enqueue_rx_report(queued.report_id, queued.data, queued.length)) {
        connected.store(false, std::memory_order_release);
        host_ready.store(false, std::memory_order_release);
        esp_ble_gap_disconnect(params->ble_security.auth_cmpl.bd_addr);
        return;
      }
    }
  }
}

void hidd_callback(void*, esp_event_base_t, int32_t id, void* event_data) {
  if (stopping) return;
  auto event = static_cast<esp_hidd_event_t>(id);
  auto* params = static_cast<esp_hidd_event_data_t*>(event_data);
  switch (event) {
    case ESP_HIDD_START_EVENT:
      hid_started = true;
      publish_cached_battery(BatteryPublishPoint::HidStarted);
      start_config_gatt_if_ready();
      start_advertising_if_ready();
      break;
    case ESP_HIDD_CONNECT_EVENT:
      connected.store(false, std::memory_order_release);
      host_ready.store(false, std::memory_order_release);
      portENTER_CRITICAL(&session_lock);
      ble_link_opened(ble_session);
      pre_auth_overflowed = false;
      replaying_pre_auth = false;
      bonded_cccs_restored = false;
      portEXIT_CRITICAL(&session_lock);
      session_generation.fetch_add(1, std::memory_order_acq_rel);
      record_protocol(ProtocolTraceType::LinkOpen);
      config_gatt_set_authorized(false);
      connected_address_valid = false;
      publish_cached_battery(BatteryPublishPoint::Connected);
      break;
    case ESP_HIDD_OUTPUT_EVENT: {
      if (params == nullptr ||
          (params->output.report_id != kCodexReportId &&
           params->output.report_id != kConfigReportId)) {
        break;
      }
      record_protocol(ProtocolTraceType::HostOutput,
                      params->output.report_id,
                      static_cast<unsigned short>(params->output.length));
      portENTER_CRITICAL(&session_lock);
      ble_host_output_seen(ble_session);
      const bool authorized =
          ble_session_authorized(ble_session) && !replaying_pre_auth;
      const PreAuthPushResult queued =
          authorized
              ? PreAuthPushResult::Invalid
              : push_pre_auth_report(ble_session.pre_auth,
                                     params->output.report_id,
                                     params->output.data,
                                     params->output.length);
      if (!authorized && queued == PreAuthPushResult::Overflow) {
        pre_auth_overflowed = true;
      }
      const bool ready = ble_session_ready(ble_session);
      portEXIT_CRITICAL(&session_lock);
      if (!authorized) {
        if (queued == PreAuthPushResult::Overflow) {
          record_protocol(ProtocolTraceType::QueueOverflow,
                          params->output.report_id);
        }
        break;
      }
      if (!restore_bonded_cccs_if_ready()) {
        if (connected_address_valid) {
          esp_ble_gap_disconnect(connected_address);
        }
        break;
      }
      host_ready.store(ready, std::memory_order_release);
      consecutive_tx_failures.store(0, std::memory_order_release);
      publish_cached_battery(BatteryPublishPoint::HostReady);
      if (!enqueue_rx_report(params->output.report_id, params->output.data,
                             params->output.length) &&
          connected_address_valid) {
        esp_ble_gap_disconnect(connected_address);
      }
      break;
    }
    case ESP_HIDD_DISCONNECT_EVENT: {
      connected.store(false, std::memory_order_release);
      host_ready.store(false, std::memory_order_release);
      BleRejectionAction rejection = BleRejectionAction::None;
      portENTER_CRITICAL(&session_lock);
      ble_disconnected(ble_session);
      pre_auth_overflowed = false;
      replaying_pre_auth = false;
      bonded_cccs_restored = false;
      rejection = note_ble_disconnected(rejection_state);
      portEXIT_CRITICAL(&session_lock);
      session_generation.fetch_add(1, std::memory_order_acq_rel);
      record_protocol(ProtocolTraceType::Disconnected);
      config_gatt_set_authorized(false);
      assembler.length = 0;
      config_assembler.length = 0;
      publish(make_transport_disconnected(TransportLink::Ble));
      if (rejection == BleRejectionAction::StartAdvertisingBackoff) {
        const esp_err_t error = esp_timer_start_once(
            advertising_backoff_timer, kWrongSlotAdvertisingBackoffUs);
        if (error != ESP_OK) {
          ESP_LOGE(kTag, "unable to start wrong-slot advertising backoff: %s",
                   esp_err_to_name(error));
          portENTER_CRITICAL(&session_lock);
          reset_ble_rejection(rejection_state);
          portEXIT_CRITICAL(&session_lock);
          start_advertising_if_ready();
        }
      } else {
        start_advertising_if_ready();
      }
      break;
    }
    default:
      break;
  }
}

void gatts_callback(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if,
                    esp_ble_gatts_cb_param_t* params) {
  if (config_gatt_owns_event(event, gatts_if, params)) {
    config_gatt_handle_event(event, gatts_if, params);
  } else {
    esp_hidd_gatts_event_handler(event, gatts_if, params);
  }
}

const char* key_for(ControlId control, signed char direction) {
  constexpr const char* keys[] = {
      "AG00", "AG01", "AG02", "AG03", "AG04", "AG05",
      "ACT06", "ACT07", "ACT08", "ACT09", "ACT10", "ACT12",
  };
  const unsigned int value = static_cast<unsigned int>(control);
  if (value < 12) return keys[value];
  return direction < 0 ? "ENC_CC" : direction > 0 ? "ENC_CW" : "ENC";
}

}  // namespace

void codex_transport_note_input_tx(unsigned int origin_us) {
  if (origin_us == 0U) return;
  const unsigned int latency_us =
      static_cast<unsigned int>(esp_timer_get_time()) - origin_us;
  last_input_tx_latency_us.store(latency_us, std::memory_order_release);
  unsigned int observed =
      maximum_input_tx_latency_us.load(std::memory_order_relaxed);
  while (latency_us > observed &&
         !maximum_input_tx_latency_us.compare_exchange_weak(
             observed, latency_us, std::memory_order_relaxed)) {
  }
}

InputTxLatencySnapshot codex_transport_input_tx_latency_snapshot() {
  return {
      .last_us = last_input_tx_latency_us.load(std::memory_order_acquire),
      .maximum_us =
          maximum_input_tx_latency_us.load(std::memory_order_acquire),
  };
}

unsigned int codex_ble_rx_stack_low_water_bytes() {
  return rx_stack_low_water_bytes.load(std::memory_order_acquire);
}

esp_err_t codex_ble_start(TransportHooks new_hooks) {
  if (hid_device != nullptr) return ESP_OK;
  if (new_hooks.snapshot_into == nullptr) return ESP_ERR_INVALID_ARG;
  hooks = new_hooks;
  stopping = false;
  suspended.store(false, std::memory_order_release);
  pending_tx.store(0, std::memory_order_release);
  queue_drop_count.store(0, std::memory_order_release);
  total_tx_retries.store(0, std::memory_order_release);
  rx_stack_low_water_bytes.store(kBleRxStackBytes,
                                 std::memory_order_release);
  input_tx_high_water.store(0, std::memory_order_release);
  host_ready.store(false, std::memory_order_release);
  pending_rx.store(0, std::memory_order_release);
  session_generation.store(0, std::memory_order_release);
  portENTER_CRITICAL(&session_lock);
  ble_disconnected(ble_session);
  protocol_trace = ProtocolTrace{};
  pre_auth_overflowed = false;
  replaying_pre_auth = false;
  bonded_cccs_restored = false;
  reset_ble_rejection(rejection_state);
  portEXIT_CRITICAL(&session_lock);
  consecutive_tx_failures.store(0, std::memory_order_release);
  dropped_tx_reports = 0;
  last_drop_log_ms = 0;
  esp_err_t error = nvs_flash_init();
  if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    error = nvs_flash_init();
  }
  if (error != ESP_OK) return error;
  esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  esp_bt_controller_config_t controller_config = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  if ((error = esp_bt_controller_init(&controller_config)) != ESP_OK) return error;
  if ((error = esp_bt_controller_enable(ESP_BT_MODE_BLE)) != ESP_OK) return error;
  if ((error = esp_bluedroid_init()) != ESP_OK) return error;
  if ((error = esp_bluedroid_enable()) != ESP_OK) return error;
  if ((error = esp_ble_gap_register_callback(gap_callback)) != ESP_OK) return error;
  esp_ble_gap_set_device_name(kBleDeviceName);
  unsigned char auth = ESP_LE_AUTH_REQ_SC_BOND;
  unsigned char io = ESP_IO_CAP_NONE;
  unsigned char key_size = 16;
  unsigned char init_key =
      ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  unsigned char response_key =
      ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK;
  esp_ble_gap_set_security_param(ESP_BLE_SM_AUTHEN_REQ_MODE, &auth, sizeof(auth));
  esp_ble_gap_set_security_param(ESP_BLE_SM_IOCAP_MODE, &io, sizeof(io));
  esp_ble_gap_set_security_param(ESP_BLE_SM_MAX_KEY_SIZE, &key_size,
                                 sizeof(key_size));
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_INIT_KEY, &init_key,
                                 sizeof(init_key));
  esp_ble_gap_set_security_param(ESP_BLE_SM_SET_RSP_KEY, &response_key,
                                 sizeof(response_key));
  error = esp_ble_gap_config_adv_data(&advertising_data);
  if (!ble_stage_succeeded("primary-advertisement", error)) return error;
  error = esp_ble_gap_config_adv_data(&scan_response_data);
  if (!ble_stage_succeeded("scan-response", error)) return error;
  error = esp_ble_gatts_register_callback(gatts_callback);
  if (!ble_stage_succeeded("gatts-callback", error)) return error;
  error = config_gatt_prepare(hooks);
  if (!ble_stage_succeeded("config-runtime", error)) {
    codex_ble_stop();
    return error;
  }
  tx_queue = xQueueCreateWithCaps(kBleTxQueueDepth, sizeof(TxMessage),
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  input_tx_queue = xQueueCreateWithCaps(
      kReliableTxCapacity, sizeof(ReliableTxRecord),
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  rx_queue = xQueueCreateWithCaps(
      kRxQueueDepth, sizeof(RxMessage),
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  tx_stopped = xSemaphoreCreateBinary();
  rx_stopped = xSemaphoreCreateBinary();
  if (tx_queue == nullptr || input_tx_queue == nullptr || rx_queue == nullptr ||
      tx_stopped == nullptr || rx_stopped == nullptr ||
      xTaskCreateWithCaps(tx_task, "codex_ble_tx", 4096, nullptr, 8,
                          &tx_task_handle,
                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS ||
      xTaskCreateWithCaps(rx_task, "codex_ble_rx", kBleRxStackBytes,
                          nullptr, 9,
                          &rx_task_handle,
                          MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
    codex_ble_stop();
    return ESP_ERR_NO_MEM;
  }
  esp_timer_create_args_t rejected_disconnect_args{};
  rejected_disconnect_args.callback = rejected_disconnect_timer_callback;
  rejected_disconnect_args.dispatch_method = ESP_TIMER_TASK;
  rejected_disconnect_args.name = "ble_reject";
  error =
      esp_timer_create(&rejected_disconnect_args, &rejected_disconnect_timer);
  if (!ble_stage_succeeded("reject-disconnect-timer", error)) {
    codex_ble_stop();
    return error;
  }
  esp_timer_create_args_t advertising_backoff_args{};
  advertising_backoff_args.callback = advertising_backoff_timer_callback;
  advertising_backoff_args.dispatch_method = ESP_TIMER_TASK;
  advertising_backoff_args.name = "ble_backoff";
  error =
      esp_timer_create(&advertising_backoff_args, &advertising_backoff_timer);
  if (!ble_stage_succeeded("advertising-backoff-timer", error)) {
    codex_ble_stop();
    return error;
  }
  error = esp_hidd_dev_init(&hid_config, ESP_HID_TRANSPORT_BLE,
                            hidd_callback, &hid_device);
  if (!ble_stage_succeeded("hid-device", error)) {
    codex_ble_stop();
    return error;
  }
  ESP_LOGI(kTag, "BLE HOGP init VID=303A PID=8360 report=6 result=%s",
           esp_err_to_name(error));
  return error;
}

esp_err_t codex_ble_stop() {
  if (!codex_ble_flush(1500)) {
    ESP_LOGE(kTag, "BLE flush timed out; refusing partial stop");
    return ESP_ERR_TIMEOUT;
  }
  stopping = true;
  suspended.store(false, std::memory_order_release);
  if (rejected_disconnect_timer != nullptr) {
    esp_timer_stop(rejected_disconnect_timer);
    esp_timer_delete(rejected_disconnect_timer);
    rejected_disconnect_timer = nullptr;
  }
  if (advertising_backoff_timer != nullptr) {
    esp_timer_stop(advertising_backoff_timer);
    esp_timer_delete(advertising_backoff_timer);
    advertising_backoff_timer = nullptr;
  }
  if (!config_gatt_stop()) {
    ESP_LOGE(kTag, "BLE stop aborted: configuration worker is still active");
    return ESP_ERR_TIMEOUT;
  }
  if (tx_task_handle != nullptr) {
    const TxMessage stop_message{.stop = true};
    if (xQueueSend(tx_queue, &stop_message, pdMS_TO_TICKS(500)) != pdTRUE ||
        xSemaphoreTake(tx_stopped, pdMS_TO_TICKS(1500)) != pdTRUE) {
      ESP_LOGE(kTag, "BLE TX task did not stop cleanly");
      return ESP_ERR_TIMEOUT;
    }
  }
  if (rx_task_handle != nullptr) {
    const RxMessage stop_message{.stop = true};
    if (xQueueSend(rx_queue, &stop_message, pdMS_TO_TICKS(500)) != pdTRUE ||
        xSemaphoreTake(rx_stopped, pdMS_TO_TICKS(1500)) != pdTRUE) {
      ESP_LOGE(kTag, "BLE RX task did not stop cleanly");
      return ESP_ERR_TIMEOUT;
    }
  }
  if (hid_device != nullptr) esp_hidd_dev_deinit(hid_device);
  hid_device = nullptr;
  connected.store(false, std::memory_order_release);
  host_ready.store(false, std::memory_order_release);
  portENTER_CRITICAL(&session_lock);
  ble_disconnected(ble_session);
  pre_auth_overflowed = false;
  replaying_pre_auth = false;
  bonded_cccs_restored = false;
  reset_ble_rejection(rejection_state);
  portEXIT_CRITICAL(&session_lock);
  connected_address_valid = false;
  hid_started = false;
  config_gatt_started = false;
  advertising_ready = false;
  scan_response_ready = false;
  if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_ENABLED) {
    esp_bluedroid_disable();
  }
  if (esp_bluedroid_get_status() == ESP_BLUEDROID_STATUS_INITIALIZED) {
    esp_bluedroid_deinit();
  }
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_ENABLED) {
    esp_bt_controller_disable();
  }
  if (esp_bt_controller_get_status() == ESP_BT_CONTROLLER_STATUS_INITED) {
    esp_bt_controller_deinit();
  }
  if (tx_queue != nullptr) {
    vQueueDeleteWithCaps(tx_queue);
    tx_queue = nullptr;
  }
  if (input_tx_queue != nullptr) {
    vQueueDeleteWithCaps(input_tx_queue);
    input_tx_queue = nullptr;
  }
  if (rx_queue != nullptr) {
    vQueueDeleteWithCaps(rx_queue);
    rx_queue = nullptr;
  }
  if (tx_stopped != nullptr) {
    vSemaphoreDelete(tx_stopped);
    tx_stopped = nullptr;
  }
  if (rx_stopped != nullptr) {
    vSemaphoreDelete(rx_stopped);
    rx_stopped = nullptr;
  }
  config_gatt_finalize_stop();
  return ESP_OK;
}

esp_err_t codex_ble_suspend() {
  if (hid_device == nullptr) return ESP_OK;
  if (suspended.load(std::memory_order_acquire)) return ESP_OK;
  if (!codex_ble_flush(1500)) {
    ESP_LOGE(kTag, "BLE flush timed out; refusing suspend");
    return ESP_ERR_TIMEOUT;
  }

  // Set the guard before stopping advertising or disconnecting. Both GAP and
  // HIDD callbacks can otherwise immediately restart advertising.
  suspended.store(true, std::memory_order_release);
  if (advertising_backoff_timer != nullptr) {
    (void)esp_timer_stop(advertising_backoff_timer);
  }
  const esp_err_t advertising_error = esp_ble_gap_stop_advertising();
  if (advertising_error != ESP_OK &&
      advertising_error != ESP_ERR_INVALID_STATE) {
    ESP_LOGW(kTag, "BLE advertising stop during suspend failed: %s",
             esp_err_to_name(advertising_error));
  }

#if CONFIG_BT_CTRL_MODEM_SLEEP
  const esp_err_t sleep_error = esp_bt_sleep_enable();
  if (sleep_error != ESP_OK) {
    suspended.store(false, std::memory_order_release);
    start_advertising_if_ready();
    ESP_LOGE(kTag, "BLE modem sleep enable failed: %s",
             esp_err_to_name(sleep_error));
    return sleep_error;
  }
#endif
  ESP_LOGI(kTag, "BLE suspended without stack teardown");
  return ESP_OK;
}

esp_err_t codex_ble_resume() {
  if (hid_device == nullptr) return ESP_ERR_INVALID_STATE;
  if (!suspended.exchange(false, std::memory_order_acq_rel)) return ESP_OK;
#if CONFIG_BT_CTRL_MODEM_SLEEP
  const esp_err_t sleep_error = esp_bt_sleep_disable();
  if (sleep_error != ESP_OK) {
    suspended.store(true, std::memory_order_release);
    ESP_LOGE(kTag, "BLE modem sleep disable failed: %s",
             esp_err_to_name(sleep_error));
    return sleep_error;
  }
#endif
  portENTER_CRITICAL(&session_lock);
  reset_ble_rejection(rejection_state);
  portEXIT_CRITICAL(&session_lock);
  start_advertising_if_ready();
  ESP_LOGI(kTag, "BLE resumed from retained stack");
  return ESP_OK;
}

void codex_transport_send_effect(const SideEffect& effect) {
  if (effect.type == SideEffectType::SendAppControl) {
    char json[192]{};
    const unsigned int length =
        make_app_input_json(json, sizeof(json), effect);
    if (length != 0) {
      const bool usb_sent =
          codex_usb_send_config_event(json, length, true);
      const bool ble_sent =
          config_gatt_send_event(json, length, true);
      if (!usb_sent && !ble_sent) {
        ESP_LOGW(kTag, "private App input has no active config transport");
      }
    }
    return;
  }
  if (effect.type == SideEffectType::ReleaseAppInputs) {
    for (unsigned int group = 0; group < kControlGroupCount; ++group) {
      for (unsigned int id = 0;
           id < kPrivateControlIdCount; ++id) {
        if ((effect.app_release_masks[group] & (1ULL << id)) == 0) continue;
        SideEffect release{
            .type = SideEffectType::SendAppControl,
            .action = 0,
            .app_group = static_cast<ControlGroup>(group),
            .app_id = static_cast<unsigned char>(id),
        };
        char json[192]{};
        const unsigned int length =
            make_app_input_json(json, sizeof(json), release);
        if (length != 0) {
          codex_usb_send_config_event(json, length, true);
          config_gatt_send_event(json, length, true);
        }
      }
    }
    return;
  }
  if (effect.type == SideEffectType::ReleaseAllInputs) {
    if (codex_usb_connected()) codex_usb_send_effect(effect);
    if (host_ready.load(std::memory_order_acquire)) {
      enqueue_release_batch(effect.release_mask);
    }
    return;
  }
  const DeviceState state = hooks.snapshot != nullptr
                                ? hooks.snapshot(hooks.context)
                                : DeviceState{};
  const ActiveTransport active = choose_active_transport(
      state.transport, codex_usb_connected(),
      host_ready.load(std::memory_order_acquire));
  if (active == ActiveTransport::Usb) {
    codex_usb_send_effect(effect);
    return;
  }
  if (active != ActiveTransport::Ble ||
      !host_ready.load(std::memory_order_acquire)) {
    return;
  }
  if (effect.type == SideEffectType::SendHid) {
    signed char agent = -1;
    if (effect.control >= ControlId::Agent0 && effect.control <= ControlId::Agent5) {
      agent = static_cast<signed char>(static_cast<unsigned int>(effect.control));
    }
      const ReliableTxRecord record{.control = effect.control,
                                  .action = effect.action,
                                  .direction = effect.direction,
                                  .agent = agent,
                                  .origin_us = effect.origin_us};
    pending_tx.fetch_add(1, std::memory_order_acquire);
    publish(make_hid_tx_queued());
    if (xQueueSend(input_tx_queue, &record, portMAX_DELAY) != pdTRUE) {
      pending_tx.fetch_sub(1, std::memory_order_release);
      publish(make_hid_tx_completed(false));
      ESP_LOGE(kTag, "reliable BLE input queue rejected an edge");
    } else {
      const unsigned char used = static_cast<unsigned char>(
          kReliableTxCapacity - uxQueueSpacesAvailable(input_tx_queue));
      unsigned char observed =
          input_tx_high_water.load(std::memory_order_relaxed);
      while (used > observed &&
             !input_tx_high_water.compare_exchange_weak(
                 observed, used, std::memory_order_relaxed)) {
      }
    }
    return;
  }
  char json[192]{};
  const unsigned int length =
      effect.type == SideEffectType::SendJoystick
          ? make_codex_joystick_json(json, sizeof(json), effect.angle,
                                     effect.distance)
          : 0;
  if (length) {
    enqueue_json(kCodexReportId, json, length, 0, false);
  }
}

bool codex_ble_connected() { return connected; }

bool codex_ble_ready() { return host_ready; }

bool codex_ble_started() { return hid_device != nullptr; }

bool codex_ble_flush(unsigned int timeout_ms) {
  unsigned int waited = 0;
  while ((pending_tx.load(std::memory_order_acquire) != 0 ||
          pending_rx.load(std::memory_order_acquire) != 0 ||
          !config_gatt_flush(0)) && waited < timeout_ms) {
    vTaskDelay(pdMS_TO_TICKS(2));
    waited += 2;
  }
  return pending_tx.load(std::memory_order_acquire) == 0 &&
         pending_rx.load(std::memory_order_acquire) == 0 &&
         config_gatt_flush(0);
}

bool codex_transport_flush(unsigned int timeout_ms) {
  const unsigned int half = timeout_ms / 2U;
  const bool usb_ok = codex_usb_flush(half);
  const bool ble_ok = codex_ble_flush(timeout_ms - half);
  return usb_ok && ble_ok;
}

esp_err_t codex_ble_clear_bonds(unsigned char slot) {
  if (esp_bluedroid_get_status() != ESP_BLUEDROID_STATUS_ENABLED) {
    return ESP_ERR_INVALID_STATE;
  }
  if (slot >= 1 && slot <= 3 && hooks.snapshot != nullptr) {
    const DeviceState state = hooks.snapshot(hooks.context);
    const BlePeerState& peer = state.ble_peers[slot - 1];
    if (!peer.bonded) return ESP_OK;
    unsigned char address[6]{};
    for (unsigned int index = 0; index < 6; ++index) address[index] = peer.address[index];
    if (connected_address_valid && same_address(connected_address, address)) {
      esp_ble_gap_disconnect(address);
      vTaskDelay(pdMS_TO_TICKS(60));
    }
    return esp_ble_remove_bond_device(address);
  }
  int count = esp_ble_get_bond_device_num();
  if (count <= 0) return count == 0 ? ESP_OK : ESP_FAIL;
  if (count > 16) count = 16;
  esp_ble_bond_dev_t devices[16]{};
  esp_err_t error = esp_ble_get_bond_device_list(&count, devices);
  if (error != ESP_OK) return error;
  if (connected_address_valid) {
    esp_ble_gap_disconnect(connected_address);
    vTaskDelay(pdMS_TO_TICKS(60));
  }
  for (int index = 0; index < count; ++index) {
    error = esp_ble_remove_bond_device(devices[index].bd_addr);
    if (error != ESP_OK) return error;
  }
  return ESP_OK;
}

esp_err_t codex_ble_select_slot() {
  const BleSlotSwitchAction action =
      ble_slot_switch_action(codex_ble_started(), connected_address_valid);
  if (action == BleSlotSwitchAction::KeepAdvertising) {
    // The active slot is already part of the published DeviceState. Restarting
    // switches a directed advertisement away from the old peer immediately.
    restart_advertising_requested.store(true, std::memory_order_release);
    const esp_err_t error = esp_ble_gap_stop_advertising();
    if (error == ESP_ERR_INVALID_STATE) {
      restart_advertising_requested.store(false, std::memory_order_release);
      start_advertising_if_ready();
      return ESP_OK;
    }
    return error;
  }
  if (action != BleSlotSwitchAction::DisconnectPeer) return ESP_OK;
  portENTER_CRITICAL(&session_lock);
  clear_pre_auth_reports(ble_session.pre_auth);
  reset_ble_rejection(rejection_state);
  portEXIT_CRITICAL(&session_lock);
  host_ready.store(false, std::memory_order_release);
  return esp_ble_gap_disconnect(connected_address);
}

void codex_transport_set_battery(unsigned char percentage, bool present,
                                 bool charging, bool external_power) {
  if (percentage > 100) percentage = 100;
  battery_level.store(percentage, std::memory_order_release);
  battery_present.store(present, std::memory_order_release);
  battery_charging.store(charging, std::memory_order_release);
  battery_external_power.store(external_power, std::memory_order_release);
  publish_cached_battery(BatteryPublishPoint::ReadingChanged);
}

unsigned int codex_ble_retry_count() {
  return total_tx_retries.load(std::memory_order_relaxed);
}

unsigned char codex_ble_input_high_water() {
  return input_tx_high_water.load(std::memory_order_relaxed);
}

ProtocolTrace codex_ble_protocol_trace_snapshot() {
  ProtocolTrace snapshot{};
  portENTER_CRITICAL(&session_lock);
  snapshot = protocol_trace;
  portEXIT_CRITICAL(&session_lock);
  return snapshot;
}

}  // namespace codex
