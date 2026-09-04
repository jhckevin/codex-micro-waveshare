#include "codex/config_gatt.h"

#include <cstring>
#include <atomic>

#include "codex/config_protocol.h"
#include "esp_attr.h"
#include "esp_gatt_common_api.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace codex {
namespace {

constexpr char kTag[] = "codex_cfg_gatt";
constexpr unsigned int kGattReplyCapacity = 1024;

// UUIDs are encoded least-significant byte first by Bluedroid.
unsigned char service_uuid[16] = {
    0x31, 0xF4, 0x98, 0x2A, 0x57, 0xB6, 0x45, 0xA1,
    0x91, 0x38, 0x52, 0x4F, 0x43, 0x44, 0x58, 0x01,
};
unsigned char rx_uuid[16] = {
    0x31, 0xF4, 0x98, 0x2A, 0x57, 0xB6, 0x45, 0xA1,
    0x91, 0x38, 0x52, 0x4F, 0x43, 0x44, 0x58, 0x02,
};
unsigned char tx_uuid[16] = {
    0x31, 0xF4, 0x98, 0x2A, 0x57, 0xB6, 0x45, 0xA1,
    0x91, 0x38, 0x52, 0x4F, 0x43, 0x44, 0x58, 0x03,
};
unsigned short primary_service_uuid = ESP_GATT_UUID_PRI_SERVICE;
unsigned short characteristic_uuid = ESP_GATT_UUID_CHAR_DECLARE;
unsigned short client_config_uuid = ESP_GATT_UUID_CHAR_CLIENT_CONFIG;
unsigned char rx_properties = ESP_GATT_CHAR_PROP_BIT_WRITE |
                              ESP_GATT_CHAR_PROP_BIT_WRITE_NR;
unsigned char tx_properties = ESP_GATT_CHAR_PROP_BIT_NOTIFY;
unsigned char empty_value = 0;
unsigned char ccc_value[2] = {0, 0};

enum AttributeIndex : unsigned char {
  Service,
  RxDeclaration,
  RxValue,
  TxDeclaration,
  TxValue,
  TxCcc,
  AttributeCount,
};

esp_gatts_attr_db_t attribute_database[AttributeCount] = {
    [Service] = {{ESP_GATT_AUTO_RSP},
                 {ESP_UUID_LEN_16, reinterpret_cast<unsigned char*>(&primary_service_uuid),
                  ESP_GATT_PERM_READ, ESP_UUID_LEN_128, ESP_UUID_LEN_128,
                  service_uuid}},
    [RxDeclaration] = {{ESP_GATT_AUTO_RSP},
                       {ESP_UUID_LEN_16,
                        reinterpret_cast<unsigned char*>(&characteristic_uuid),
                        ESP_GATT_PERM_READ, 1, 1, &rx_properties}},
    [RxValue] = {{ESP_GATT_AUTO_RSP},
                 {ESP_UUID_LEN_128, rx_uuid, ESP_GATT_PERM_WRITE_ENCRYPTED,
                  kConfigGattChunkCapacity, 1, &empty_value}},
    [TxDeclaration] = {{ESP_GATT_AUTO_RSP},
                       {ESP_UUID_LEN_16,
                        reinterpret_cast<unsigned char*>(&characteristic_uuid),
                        ESP_GATT_PERM_READ, 1, 1, &tx_properties}},
    [TxValue] = {{ESP_GATT_AUTO_RSP},
                 {ESP_UUID_LEN_128, tx_uuid, ESP_GATT_PERM_READ_ENCRYPTED,
                  kGattReplyCapacity, 1, &empty_value}},
    [TxCcc] = {{ESP_GATT_AUTO_RSP},
               {ESP_UUID_LEN_16,
                reinterpret_cast<unsigned char*>(&client_config_uuid),
                ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED,
                sizeof(ccc_value), sizeof(ccc_value), ccc_value}},
};

struct RxChunk {
  unsigned short length{};
  unsigned int received_ms{};
  unsigned int generation{};
  bool stop{};
  bool outbound{};
  unsigned char data[kConfigGattChunkCapacity]{};
};

TransportHooks config_hooks{};
esp_gatt_if_t config_interface = ESP_GATT_IF_NONE;
unsigned short handles[AttributeCount]{};
unsigned short connection_id{};
unsigned short negotiated_mtu{23};
bool connection_open{};
bool notifications_enabled{};
volatile bool stopping{};
volatile bool congested{};
volatile bool authorized{};
unsigned int connection_generation{};
std::atomic_bool worker_busy{};
QueueHandle_t rx_queue{};
TaskHandle_t worker_handle{};
SemaphoreHandle_t worker_stopped{};
EXT_RAM_BSS_ATTR char line_buffer[kConfigMaxMessage + 1]{};

void log_internal_heap(const char* stage) {
  ESP_LOGI(kTag, "heap stage=%s free=%u largest=%u", stage,
           static_cast<unsigned int>(
               heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
           static_cast<unsigned int>(heap_caps_get_largest_free_block(
               MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)));
}

void send_reply(const char* json, unsigned int length) {
  if (stopping || !authorized || !connection_open ||
      config_interface == ESP_GATT_IF_NONE ||
      handles[TxValue] == 0) return;
  const unsigned short target_connection = connection_id;
  const unsigned int target_generation = connection_generation;
  esp_ble_gatts_set_attr_value(handles[TxValue], length,
                              reinterpret_cast<const unsigned char*>(json));
  if (!notifications_enabled) return;
  const unsigned int payload = negotiated_mtu > 3 ? negotiated_mtu - 3U : 20U;
  unsigned int offset = 0;
  while (offset < length) {
    unsigned int waited_ms = 0;
    while (congested && !stopping && connection_open &&
           connection_generation == target_generation && waited_ms < 1000U) {
      vTaskDelay(pdMS_TO_TICKS(5));
      waited_ms += 5;
    }
    if (stopping || !connection_open || congested ||
        connection_generation != target_generation) break;
    const unsigned int chunk = length - offset > payload ? payload : length - offset;
    if (esp_ble_gatts_send_indicate(
            config_interface, target_connection, handles[TxValue], chunk,
            reinterpret_cast<unsigned char*>(const_cast<char*>(json + offset)),
            false) != ESP_OK) {
      ESP_LOGW(kTag, "notification failed at offset %u", offset);
      break;
    }
    offset += chunk;
    vTaskDelay(pdMS_TO_TICKS(4));
  }
  if (!stopping && connection_open &&
      connection_generation == target_generation && !congested) {
    unsigned char newline = '\n';
    esp_ble_gatts_send_indicate(config_interface, target_connection,
                                handles[TxValue], 1, &newline, false);
  }
}

void process_line(const char* line, unsigned int length) {
  if (config_hooks.update != nullptr) {
    const UpdateProtocolReply update = handle_update_protocol(
        line, length, UpdateTransport::Ble, *config_hooks.update);
    if (update.handled) {
      send_reply(update.json, update.length);
      return;
    }
  }
  const DeviceState state = config_hooks.snapshot != nullptr
                                ? config_hooks.snapshot(config_hooks.context)
                                : DeviceState{};
  ConfigReply reply = handle_config_line(line, length, state);
  if (!reply.ok && config_hooks.publish != nullptr) {
    config_hooks.publish(make_rpc_error_recorded(), config_hooks.context);
  }
  if (reply.ok && reply.action != ConfigAction::None &&
      config_hooks.configure != nullptr &&
      !config_hooks.configure(reply, config_hooks.context)) {
    constexpr char failure[] = "{\"ok\":false,\"error\":\"apply_failed\"}";
    std::memcpy(reply.json, failure, sizeof(failure));
    reply.length = sizeof(failure) - 1;
  }
  send_reply(reply.json, reply.length);
}

void worker(void*) {
  unsigned int used = 0;
  bool overflow = false;
  unsigned int last_fragment_ms = 0;
  unsigned int worker_generation = 0;
  RxChunk chunk{};
  while (xQueueReceive(rx_queue, &chunk, portMAX_DELAY) == pdTRUE) {
    if (chunk.stop) break;
    worker_busy.store(true, std::memory_order_release);
    if (chunk.outbound) {
      if (chunk.generation == connection_generation && authorized &&
          !stopping) {
        send_reply(reinterpret_cast<const char*>(chunk.data), chunk.length);
      }
      worker_busy.store(false, std::memory_order_release);
      continue;
    }
    if (chunk.generation != worker_generation) {
      used = 0;
      overflow = false;
      last_fragment_ms = 0;
      worker_generation = chunk.generation;
    }
    if (chunk.generation != connection_generation || !authorized || stopping) {
      worker_busy.store(false, std::memory_order_release);
      continue;
    }
    if (used != 0 && last_fragment_ms != 0 &&
        chunk.received_ms - last_fragment_ms > 500U) {
      used = 0;
      overflow = false;
    }
    last_fragment_ms = chunk.received_ms;
    for (unsigned int index = 0; index < chunk.length; ++index) {
      const unsigned char value = chunk.data[index];
      if (value == '\n') {
        if (overflow) {
          constexpr char failure[] =
              "{\"ok\":false,\"error\":\"message_too_large\"}";
          send_reply(failure, sizeof(failure) - 1);
        } else if (used != 0) {
          process_line(line_buffer, used);
        }
        used = 0;
        overflow = false;
      } else if (!overflow && used < kConfigMaxMessage) {
        line_buffer[used++] = static_cast<char>(value);
      } else {
        overflow = true;
      }
    }
    worker_busy.store(false, std::memory_order_release);
  }
  worker_handle = nullptr;
  xSemaphoreGive(worker_stopped);
  vTaskDelete(nullptr);
}

}  // namespace

bool config_gatt_send_event(const char* json, unsigned int length,
                            bool critical) {
  if (json == nullptr || length == 0 ||
      length > kConfigGattChunkCapacity || rx_queue == nullptr ||
      stopping || !authorized || !connection_open) {
    return false;
  }
  RxChunk chunk{};
  chunk.length = static_cast<unsigned short>(length);
  chunk.generation = connection_generation;
  chunk.outbound = true;
  for (unsigned int index = 0; index < length; ++index) {
    chunk.data[index] = static_cast<unsigned char>(json[index]);
  }
  return xQueueSend(rx_queue, &chunk,
                    critical ? portMAX_DELAY : 0) == pdTRUE;
}

esp_err_t config_gatt_prepare(TransportHooks hooks) {
  config_hooks = hooks;
  stopping = false;
  congested = false;
  authorized = false;
  worker_busy.store(false, std::memory_order_release);
  log_internal_heap("before-queue");
  rx_queue = xQueueCreateWithCaps(kConfigGattRxQueueDepth, sizeof(RxChunk),
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (rx_queue == nullptr) {
    ESP_LOGE(kTag, "RX queue allocation failed");
    log_internal_heap("queue-failed");
    return ESP_ERR_NO_MEM;
  }
  log_internal_heap("after-queue");
  worker_stopped = xSemaphoreCreateBinary();
  if (worker_stopped == nullptr) {
    ESP_LOGE(kTag, "worker semaphore allocation failed");
    log_internal_heap("semaphore-failed");
    vQueueDeleteWithCaps(rx_queue);
    rx_queue = nullptr;
    return ESP_ERR_NO_MEM;
  }
  log_internal_heap("after-semaphore");
  if (xTaskCreate(worker, "codex_cfg_gatt", kConfigGattWorkerStackBytes,
                  nullptr, 7,
                  &worker_handle) != pdPASS) {
    ESP_LOGE(kTag, "worker task allocation failed");
    log_internal_heap("worker-failed");
    vSemaphoreDelete(worker_stopped);
    worker_stopped = nullptr;
    vQueueDeleteWithCaps(rx_queue);
    rx_queue = nullptr;
    return ESP_ERR_NO_MEM;
  }
  log_internal_heap("after-worker");
  return ESP_OK;
}

esp_err_t config_gatt_start() {
  if (rx_queue == nullptr || worker_handle == nullptr ||
      worker_stopped == nullptr) {
    return ESP_ERR_INVALID_STATE;
  }
  const esp_err_t error = esp_ble_gatts_app_register(kConfigGattAppId);
  if (error != ESP_OK) {
    ESP_LOGE(kTag, "GATT registration failed: %s", esp_err_to_name(error));
  }
  return error;
}

bool config_gatt_stop() {
  if (!config_gatt_flush(1500)) {
    ESP_LOGW(kTag, "configuration worker flush timed out");
  }
  stopping = true;
  if (worker_handle != nullptr) {
    const RxChunk stop_chunk{.stop = true};
    if (xQueueSend(rx_queue, &stop_chunk, pdMS_TO_TICKS(500)) != pdTRUE ||
        xSemaphoreTake(worker_stopped, pdMS_TO_TICKS(1500)) != pdTRUE) {
      ESP_LOGE(kTag, "configuration worker did not stop cleanly");
      return false;
    }
  }
  if (config_interface != ESP_GATT_IF_NONE) {
    esp_ble_gatts_app_unregister(config_interface);
  }
  connection_open = false;
  authorized = false;
  notifications_enabled = false;
  ++connection_generation;
  return true;
}

void config_gatt_finalize_stop() {
  if (rx_queue != nullptr) {
    vQueueDeleteWithCaps(rx_queue);
    rx_queue = nullptr;
  }
  if (worker_stopped != nullptr) {
    vSemaphoreDelete(worker_stopped);
    worker_stopped = nullptr;
  }
  config_interface = ESP_GATT_IF_NONE;
  negotiated_mtu = 23;
  congested = false;
  authorized = false;
  std::memset(handles, 0, sizeof(handles));
}

bool config_gatt_owns_event(esp_gatts_cb_event_t event,
                            esp_gatt_if_t gatts_if,
                            const esp_ble_gatts_cb_param_t* params) {
  if (event == ESP_GATTS_REG_EVT && params != nullptr) {
    return params->reg.app_id == kConfigGattAppId;
  }
  return config_interface != ESP_GATT_IF_NONE && gatts_if == config_interface;
}

void config_gatt_handle_event(esp_gatts_cb_event_t event,
                              esp_gatt_if_t gatts_if,
                              esp_ble_gatts_cb_param_t* params) {
  if (params == nullptr) return;
  if (stopping && event != ESP_GATTS_UNREG_EVT) return;
  switch (event) {
    case ESP_GATTS_REG_EVT:
      if (params->reg.status == ESP_GATT_OK) {
        config_interface = gatts_if;
        const esp_err_t error = esp_ble_gatts_create_attr_tab(
            attribute_database, gatts_if, AttributeCount, 0);
        if (error != ESP_OK) ESP_LOGE(kTag, "create table: %s", esp_err_to_name(error));
      }
      break;
    case ESP_GATTS_CREAT_ATTR_TAB_EVT:
      if (params->add_attr_tab.status == ESP_GATT_OK &&
          params->add_attr_tab.num_handle == AttributeCount) {
        std::memcpy(handles, params->add_attr_tab.handles, sizeof(handles));
        esp_ble_gatts_start_service(handles[Service]);
        ESP_LOGI(kTag, "encrypted configuration service ready");
      } else {
        ESP_LOGE(kTag, "attribute table creation failed");
      }
      break;
    case ESP_GATTS_CONNECT_EVT:
      connection_id = params->connect.conn_id;
      connection_open = true;
      authorized = false;
      ++connection_generation;
      negotiated_mtu = 23;
      notifications_enabled = false;
      break;
    case ESP_GATTS_DISCONNECT_EVT:
      connection_open = false;
      authorized = false;
      notifications_enabled = false;
      ++connection_generation;
      break;
    case ESP_GATTS_MTU_EVT:
      negotiated_mtu = params->mtu.mtu;
      break;
    case ESP_GATTS_CONGEST_EVT:
      congested = params->congest.congested;
      break;
    case ESP_GATTS_WRITE_EVT:
      if (authorized && !params->write.is_prep &&
          params->write.handle == handles[RxValue]) {
        if (params->write.len <= kConfigGattChunkCapacity &&
            rx_queue != nullptr) {
          RxChunk chunk{
              .length = params->write.len,
              .received_ms = static_cast<unsigned int>(
                  esp_timer_get_time() / 1000ULL),
              .generation = connection_generation,
          };
          std::memcpy(chunk.data, params->write.value, params->write.len);
          if (xQueueSend(rx_queue, &chunk, 0) != pdTRUE) {
            ESP_LOGW(kTag, "RX queue full");
          }
        }
      } else if (authorized && !params->write.is_prep &&
                 params->write.handle == handles[TxCcc] &&
                 params->write.len == 2) {
        notifications_enabled = params->write.value[0] == 1 &&
                                params->write.value[1] == 0;
      }
      break;
    default:
      break;
  }
}

void config_gatt_set_authorized(bool value) {
  authorized = value;
}

bool config_gatt_flush(unsigned int timeout_ms) {
  unsigned int waited = 0;
  while (((rx_queue != nullptr && uxQueueMessagesWaiting(rx_queue) != 0) ||
          worker_busy.load(std::memory_order_acquire)) && waited < timeout_ms) {
    vTaskDelay(pdMS_TO_TICKS(2));
    waited += 2;
  }
  return (rx_queue == nullptr || uxQueueMessagesWaiting(rx_queue) == 0) &&
         !worker_busy.load(std::memory_order_acquire);
}

}  // namespace codex
