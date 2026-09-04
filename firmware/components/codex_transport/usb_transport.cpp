#include "codex/transport.h"

#include <atomic>

#include "codex/codex_protocol.h"
#include "codex/config_protocol.h"
#include "codex/hid_report_route.h"
#include "class/cdc/cdc_device.h"
#include "class/hid/hid_device.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/idf_additions.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "tinyusb.h"

namespace codex {
namespace {

// Signed update manifests are verified and their content keys are unwrapped
// on the USB receive worker.  The crypto path requires materially more stack
// than ordinary HID/CDC dispatch; keep explicit headroom so a valid update
// cannot reset the device before it can reply.
constexpr unsigned int kUsbRxTaskStackBytes = 16U * 1024U;
static_assert(kUsbRxTaskStackBytes >= 16U * 1024U);

constexpr char kTag[] = "codex_usb";
constexpr unsigned int kConfigurationLength =
    TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN + TUD_CDC_DESC_LEN;

const unsigned char kUsbReportMap[] = {
    0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01, 0x85, 0x06,
    0x15, 0x00, 0x26, 0xFF, 0x00, 0x75, 0x08, 0x95, 0x3F,
    0x09, 0x01, 0x81, 0x02, 0x95, 0x3F, 0x09, 0x02, 0x91, 0x02,
    0x85, 0x07, 0x95, 0x3F, 0x09, 0x03, 0x81, 0x02,
    0x95, 0x3F, 0x09, 0x04, 0x91, 0x02, 0xC0,
};

const tusb_desc_device_t kDeviceDescriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x8360,
    .bcdDevice = 0x0100,
    .iManufacturer = 1,
    .iProduct = 2,
    .iSerialNumber = 3,
    .bNumConfigurations = 1,
};

const char kLanguage[] = {0x09, 0x04};
const char* kStrings[] = {
    kLanguage,
    "Work Louder",
    "Codex Micro",
    "WS-S3-CODEX",
    "Codex Vendor RPC",
    "Codex Config",
};

const unsigned char kConfigurationDescriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 3, 0, kConfigurationLength,
                          TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 250),
    TUD_HID_INOUT_DESCRIPTOR(0, 4, HID_ITF_PROTOCOL_NONE,
                             sizeof(kUsbReportMap), 0x01, 0x81, 64, 1),
    TUD_CDC_DESCRIPTOR(1, 5, 0x82, 8, 0x02, 0x83, 64),
};

TransportHooks hooks{};
EXT_RAM_BSS_ATTR ReportAssembler assembler{};
EXT_RAM_BSS_ATTR ReportAssembler hid_config_assembler{};
EXT_RAM_BSS_ATTR char config_line[kConfigMaxMessage + 1]{};
unsigned int config_length{};
bool started{};
volatile bool stopping{};
std::atomic_uint pending_tx{};
std::atomic_uint pending_cdc_tx{};
std::atomic_bool config_busy{};
std::atomic_bool cdc_app_session{};
std::atomic_uint last_control_activity_ms{};
constexpr unsigned int kTxJsonCapacity = 1024;
constexpr unsigned int kCdcTxJsonCapacity = 256;
constexpr unsigned int kUsbPacketSize = 64;
enum class RxKind : unsigned char { CodexHid, ConfigHid, Cdc };
struct RxMessage {
  RxKind kind{};
  unsigned char interface{};
  unsigned char length{};
  unsigned char data[kUsbPacketSize]{};
};
struct TxMessage {
  unsigned char report_id{};
  unsigned short length{};
  bool critical{};
  bool input_event{};
  bool stop{};
  unsigned short release_mask{};
  unsigned int origin_us{};
  char json[kTxJsonCapacity]{};
};
struct CdcTxMessage {
  unsigned short length{};
  bool stop{};
  char json[kCdcTxJsonCapacity]{};
};
QueueHandle_t tx_queue{};
TaskHandle_t tx_task_handle{};
SemaphoreHandle_t tx_stopped{};
QueueHandle_t rx_queue{};
TaskHandle_t rx_task_handle{};
SemaphoreHandle_t rx_stopped{};
QueueHandle_t cdc_tx_queue{};
TaskHandle_t cdc_tx_task_handle{};
SemaphoreHandle_t cdc_tx_stopped{};
SemaphoreHandle_t cdc_write_mutex{};
std::atomic_uint dropped_rx{};

DeviceState snapshot() {
  return hooks.snapshot != nullptr ? hooks.snapshot(hooks.context) : DeviceState{};
}

const char* key_for(ControlId control, signed char direction);

bool transmit_json(unsigned char report_id, const char* json,
                   unsigned int length, bool critical) {
  unsigned int offset = 0;
  while (offset <= length && started) {
    unsigned char report[kCodexReportBodySize]{};
    report[0] = 2;
    const unsigned int available = length - offset + 1U;
    const unsigned int chunk = available > kCodexPayloadSize
                                   ? kCodexPayloadSize : available;
    for (unsigned int index = 0; index < chunk; ++index) {
      report[index + 2] = offset + index < length
                              ? static_cast<unsigned char>(json[offset + index])
                              : '\n';
    }
    report[1] = static_cast<unsigned char>(chunk);
    unsigned int waited_ms = 0;
    bool sent = false;
    const unsigned int wait_limit_ms = critical ? 750U : 100U;
    while (started && !stopping && tud_mounted() &&
           waited_ms < wait_limit_ms) {
      if (tud_hid_ready() &&
          tud_hid_report(report_id, report, kCodexReportBodySize)) {
        sent = true;
        break;
      }
      vTaskDelay(pdMS_TO_TICKS(2));
      waited_ms += 2;
    }
    if (!sent) return false;
    offset += chunk;
    if (offset > length) break;
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

void publish(const DeviceEvent& event) {
  if (hooks.publish != nullptr) hooks.publish(event, hooks.context);
}

void tx_task(void*) {
  TxMessage message{};
  while (xQueueReceive(tx_queue, &message, portMAX_DELAY) == pdTRUE) {
    if (message.stop) break;
    const bool sent = message.release_mask != 0
                          ? transmit_release_batch(message.release_mask)
                          : transmit_json(message.report_id, message.json,
                                           message.length, message.critical);
    if (message.input_event) {
      if (sent) codex_transport_note_input_tx(message.origin_us);
      publish(make_hid_tx_completed(sent));
    }
    if (!sent && !message.critical && message.release_mask == 0) {
      ESP_LOGW(kTag, "dropping non-critical HID report: host not ready");
    }
    pending_tx.fetch_sub(1, std::memory_order_release);
  }
  tx_task_handle = nullptr;
  xSemaphoreGive(tx_stopped);
  vTaskDelete(nullptr);
}

bool enqueue_release_batch(unsigned short release_mask) {
  if (release_mask == 0 || stopping || tx_queue == nullptr) return release_mask == 0;
  const TxMessage message{.critical = true, .release_mask = release_mask};
  pending_tx.fetch_add(1, std::memory_order_acquire);
  if (xQueueSend(tx_queue, &message, pdMS_TO_TICKS(250)) != pdTRUE) {
    pending_tx.fetch_sub(1, std::memory_order_release);
    ESP_LOGE(kTag, "unable to queue atomic USB release batch");
    return false;
  }
  return true;
}

bool send_hid_json(unsigned char report_id, const char* json, unsigned int length,
                    TickType_t wait = 0, bool critical = false,
                    bool input_event = false, unsigned int origin_us = 0) {
  if (stopping || tx_queue == nullptr || length >= kTxJsonCapacity) return false;
  TxMessage message{.report_id = report_id,
                     .length = static_cast<unsigned short>(length),
                     .critical = critical,
                     .input_event = input_event,
                     .origin_us = origin_us};
  for (unsigned int index = 0; index < length; ++index) message.json[index] = json[index];
  message.json[length] = '\0';
  pending_tx.fetch_add(1, std::memory_order_acquire);
  if (xQueueSend(tx_queue, &message, wait) != pdTRUE) {
    pending_tx.fetch_sub(1, std::memory_order_release);
    ESP_LOGW(kTag, "USB TX queue full; dropping report %u", report_id);
    return false;
  }
  if (input_event) publish(make_hid_tx_queued());
  return true;
}

void process_hid_output(const unsigned char* data, unsigned int length) {
  const AssemblerResult result = consume_codex_report(
      assembler, data, length,
      static_cast<unsigned int>(esp_timer_get_time() / 1000ULL));
  if (result.status != AssembleStatus::Complete) return;
  const RpcDispatch dispatch = dispatch_codex_rpc(
      assembler.message, assembler.length, snapshot(),
      static_cast<unsigned int>(esp_timer_get_time() / 1000ULL));
  assembler.length = 0;
  if (!dispatch.ok) publish(make_rpc_error_recorded());
  if (dispatch.ok) {
    publish(make_codex_rpc_received(
        static_cast<unsigned int>(esp_timer_get_time() / 1000ULL),
        dispatch.lighting_update, TransportLink::Usb));
  }
  for (unsigned int index = 0; index < dispatch.event_count; ++index) {
    publish(dispatch.events[index]);
  }
  if (dispatch.reply_length) {
    // A JSON-RPC reply is part of the connection contract.  Treat it like a
    // key release: it may wait for a bounded endpoint recovery window, but it
    // must never be discarded as an animation/status update when Windows is
    // briefly rebuilding the HID handle.
    send_hid_json(kCodexReportId, dispatch.reply,
                  dispatch.reply_length - 1U, pdMS_TO_TICKS(50), true);
  }
}

void process_hid_config_output(const unsigned char* data, unsigned int length) {
  const AssemblerResult result = consume_codex_report(
      hid_config_assembler, data, length,
      static_cast<unsigned int>(esp_timer_get_time() / 1000ULL));
  if (result.status != AssembleStatus::Complete) return;
  config_busy.store(true, std::memory_order_release);
  if (hooks.content != nullptr) {
    const UserContentProtocolReply content = handle_user_content_protocol(
        hid_config_assembler.message, hid_config_assembler.length,
        UserContentTransport::Usb, *hooks.content);
    if (content.handled) {
      hid_config_assembler.length = 0;
      send_hid_json(kConfigReportId, content.json, content.length,
                    pdMS_TO_TICKS(50), true);
      config_busy.store(false, std::memory_order_release);
      return;
    }
  }
  if (hooks.update != nullptr) {
    const UpdateProtocolReply update = handle_update_protocol(
        hid_config_assembler.message, hid_config_assembler.length,
        UpdateTransport::Usb, *hooks.update);
    if (update.handled) {
      hid_config_assembler.length = 0;
      send_hid_json(kConfigReportId, update.json, update.length,
                    pdMS_TO_TICKS(50), true);
      config_busy.store(false, std::memory_order_release);
      return;
    }
  }
  ConfigReply reply = handle_config_line(hid_config_assembler.message,
                                         hid_config_assembler.length, snapshot());
  hid_config_assembler.length = 0;
  if (reply.ok && reply.action == ConfigAction::AppHello) {
    cdc_app_session.store(false, std::memory_order_release);
  }
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
  send_hid_json(kConfigReportId, reply.json, reply.length,
                pdMS_TO_TICKS(50), true);
  config_busy.store(false, std::memory_order_release);
}

bool write_cdc_reply(const char* json, unsigned int length) {
  if (cdc_write_mutex == nullptr ||
      xSemaphoreTake(cdc_write_mutex, pdMS_TO_TICKS(1100)) != pdTRUE) {
    return false;
  }
  unsigned int offset = 0;
  unsigned int waited_ms = 0;
  while (offset < length && started && !stopping && tud_mounted() &&
         waited_ms < 1000U) {
    const unsigned int available = tud_cdc_write_available();
    if (available == 0) {
      tud_cdc_write_flush();
      vTaskDelay(pdMS_TO_TICKS(1));
      ++waited_ms;
      continue;
    }
    const unsigned int remaining = length - offset;
    const unsigned int chunk = remaining < available ? remaining : available;
    const unsigned int written = tud_cdc_write(json + offset, chunk);
    offset += written;
    if (written == 0) {
      vTaskDelay(pdMS_TO_TICKS(1));
      ++waited_ms;
    }
  }
  const bool complete = offset == length;
  if (complete) {
    tud_cdc_write_char('\n');
    tud_cdc_write_flush();
  }
  xSemaphoreGive(cdc_write_mutex);
  return complete;
}

void cdc_tx_task(void*) {
  CdcTxMessage message{};
  while (xQueueReceive(cdc_tx_queue, &message, portMAX_DELAY) == pdTRUE) {
    if (message.stop) break;
    if (!write_cdc_reply(message.json, message.length)) {
      ESP_LOGW(kTag, "dropping CDC event: host not ready");
    }
    pending_cdc_tx.fetch_sub(1, std::memory_order_release);
  }
  cdc_tx_task_handle = nullptr;
  xSemaphoreGive(cdc_tx_stopped);
  vTaskDelete(nullptr);
}

bool enqueue_cdc_event(const char* json, unsigned int length, bool critical) {
  if (!cdc_app_session.load(std::memory_order_acquire) || stopping ||
      cdc_tx_queue == nullptr || json == nullptr || length == 0 ||
      length >= kCdcTxJsonCapacity) {
    return false;
  }
  CdcTxMessage message{.length = static_cast<unsigned short>(length)};
  for (unsigned int index = 0; index < length; ++index) {
    message.json[index] = json[index];
  }
  message.json[length] = '\0';
  pending_cdc_tx.fetch_add(1, std::memory_order_acquire);
  if (xQueueSend(cdc_tx_queue, &message,
                 critical ? pdMS_TO_TICKS(30) : 0) != pdTRUE) {
    pending_cdc_tx.fetch_sub(1, std::memory_order_release);
    return false;
  }
  return true;
}

void process_config_line() {
  last_control_activity_ms.store(
      static_cast<unsigned int>(esp_timer_get_time() / 1000ULL),
      std::memory_order_release);
  config_busy.store(true, std::memory_order_release);
  if (hooks.content != nullptr) {
    const UserContentProtocolReply content = handle_user_content_protocol(
        config_line, config_length, UserContentTransport::Usb,
        *hooks.content);
    if (content.handled) {
      write_cdc_reply(content.json, content.length);
      config_length = 0;
      config_busy.store(false, std::memory_order_release);
      return;
    }
  }
  if (hooks.update != nullptr) {
    const UpdateProtocolReply update =
        handle_update_protocol(config_line, config_length, UpdateTransport::Usb,
                               *hooks.update);
    if (update.handled) {
      write_cdc_reply(update.json, update.length);
      config_length = 0;
      config_busy.store(false, std::memory_order_release);
      return;
    }
  }
  ConfigReply reply = handle_config_line(config_line, config_length, snapshot());
  if (reply.ok && (reply.action == ConfigAction::AppHello ||
                   reply.action == ConfigAction::AppHeartbeat)) {
    cdc_app_session.store(true, std::memory_order_release);
  } else if (reply.ok && reply.action == ConfigAction::AppDisconnect) {
    cdc_app_session.store(false, std::memory_order_release);
  }
  if (!reply.ok) publish(make_rpc_error_recorded());
  if (reply.ok && reply.action != ConfigAction::None &&
      hooks.configure != nullptr && !hooks.configure(reply, hooks.context)) {
    constexpr char failure[] = "{\"ok\":false,\"error\":\"apply_failed\"}";
    unsigned int index = 0;
    while (index + 1 < sizeof(reply.json) && failure[index]) {
      reply.json[index] = failure[index];
      ++index;
    }
    reply.json[index] = '\0';
    reply.length = static_cast<unsigned short>(index);
    reply.ok = false;
  }
  write_cdc_reply(reply.json, reply.length);
  config_length = 0;
  config_busy.store(false, std::memory_order_release);
}

void process_cdc_bytes(const unsigned char* data, unsigned int length) {
  for (unsigned int index = 0; index < length; ++index) {
    const unsigned char byte = data[index];
    if (byte == '\n') {
      // Opening or waking a Windows CDC port may produce an empty CR/LF
      // frame before the first application request.  It is transport framing,
      // not a malformed RPC.  Replying invalid_json here can be consumed as
      // the response to the immediately following app.hello request.
      if (config_length != 0U) process_config_line();
    } else if (byte == '\r') {
      // Accept the CRLF framing emitted by common Windows serial clients.
      continue;
    } else if (config_length < kConfigMaxMessage) {
      config_line[config_length++] = static_cast<char>(byte);
      config_line[config_length] = 0;
    } else {
      config_length = kConfigMaxMessage + 1U;
    }
  }
}

bool enqueue_rx(RxKind kind, unsigned char interface,
                const unsigned char* data, unsigned int length) {
  if (stopping || rx_queue == nullptr || data == nullptr || length == 0 ||
      length > kUsbPacketSize) {
    return false;
  }
  RxMessage message{.kind = kind,
                    .interface = interface,
                    .length = static_cast<unsigned char>(length)};
  for (unsigned int index = 0; index < length; ++index) {
    message.data[index] = data[index];
  }
  if (xQueueSend(rx_queue, &message, 0) == pdTRUE) return true;
  dropped_rx.fetch_add(1, std::memory_order_relaxed);
  return false;
}

void rx_task(void*) {
  RxMessage message{};
  while (xQueueReceive(rx_queue, &message, portMAX_DELAY) == pdTRUE) {
    if (message.length == 0) break;
    switch (message.kind) {
      case RxKind::CodexHid:
        process_hid_output(message.data, message.length);
        break;
      case RxKind::ConfigHid:
        process_hid_config_output(message.data, message.length);
        break;
      case RxKind::Cdc:
        process_cdc_bytes(message.data, message.length);
        break;
    }
  }
  rx_task_handle = nullptr;
  xSemaphoreGive(rx_stopped);
  vTaskDelete(nullptr);
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

esp_err_t codex_usb_start(TransportHooks new_hooks, bool recovery_gate_open) {
  if (!recovery_gate_open) {
    ESP_LOGW(kTag, "Codex USB identity blocked by recovery gate");
    return ESP_ERR_INVALID_STATE;
  }
  if (started) return ESP_OK;
  stopping = false;
  pending_tx.store(0, std::memory_order_release);
  pending_cdc_tx.store(0, std::memory_order_release);
  cdc_app_session.store(false, std::memory_order_release);
  config_busy.store(false, std::memory_order_release);
  dropped_rx.store(0, std::memory_order_release);
  hooks = new_hooks;
  tx_queue = xQueueCreateWithCaps(24, sizeof(TxMessage),
                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  tx_stopped = xSemaphoreCreateBinary();
  cdc_tx_queue = xQueueCreateWithCaps(32, sizeof(CdcTxMessage),
                                      MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  cdc_tx_stopped = xSemaphoreCreateBinary();
  cdc_write_mutex = xSemaphoreCreateMutex();
  rx_queue = xQueueCreateWithCaps(32, sizeof(RxMessage),
                                  MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  rx_stopped = xSemaphoreCreateBinary();
  if (tx_queue == nullptr || tx_stopped == nullptr ||
      cdc_tx_queue == nullptr || cdc_tx_stopped == nullptr ||
      cdc_write_mutex == nullptr || rx_queue == nullptr ||
      rx_stopped == nullptr ||
      xTaskCreate(tx_task, "codex_usb_tx", 4096, nullptr, 8,
                  &tx_task_handle) != pdPASS ||
      xTaskCreate(cdc_tx_task, "codex_cdc_tx", 4096, nullptr, 8,
                  &cdc_tx_task_handle) != pdPASS ||
      xTaskCreate(rx_task, "codex_usb_rx", kUsbRxTaskStackBytes, nullptr, 9,
                  &rx_task_handle) != pdPASS) {
    stopping = true;
    return ESP_ERR_NO_MEM;
  }
  const tinyusb_config_t config = {
      .device_descriptor = &kDeviceDescriptor,
      .string_descriptor = kStrings,
      .string_descriptor_count = sizeof(kStrings) / sizeof(kStrings[0]),
      .external_phy = false,
#if TUD_OPT_HIGH_SPEED
      .fs_configuration_descriptor = kConfigurationDescriptor,
      .hs_configuration_descriptor = kConfigurationDescriptor,
      .qualifier_descriptor = nullptr,
#else
      .configuration_descriptor = kConfigurationDescriptor,
#endif
      .self_powered = false,
      .vbus_monitor_io = -1,
  };
  const esp_err_t error = tinyusb_driver_install(&config);
  started = error == ESP_OK;
  if (!started) {
    stopping = true;
    const RxMessage rx_stop{};
    const TxMessage tx_stop{.stop = true};
    if (rx_task_handle != nullptr) xQueueSend(rx_queue, &rx_stop, 0);
    if (tx_task_handle != nullptr) xQueueSend(tx_queue, &tx_stop, 0);
    if (rx_task_handle != nullptr) xSemaphoreTake(rx_stopped, pdMS_TO_TICKS(500));
    if (tx_task_handle != nullptr) xSemaphoreTake(tx_stopped, pdMS_TO_TICKS(500));
    return error;
  }
  ESP_LOGI(kTag, "USB HID+CDC init result=%s", esp_err_to_name(error));
  return error;
}

esp_err_t codex_usb_stop() {
  if (!started && tx_queue == nullptr && tx_task_handle == nullptr) return ESP_OK;
  if (!codex_usb_flush(1500)) {
    ESP_LOGE(kTag, "USB flush timed out; refusing partial stop");
    return ESP_ERR_TIMEOUT;
  }
  stopping = true;
  if (rx_task_handle != nullptr) {
    const RxMessage stop_message{};
    if (xQueueSend(rx_queue, &stop_message, pdMS_TO_TICKS(100)) != pdTRUE ||
        xSemaphoreTake(rx_stopped, pdMS_TO_TICKS(1000)) != pdTRUE) {
      ESP_LOGE(kTag, "USB RX task did not stop cleanly");
      return ESP_ERR_TIMEOUT;
    }
  }
  if (tx_task_handle != nullptr) {
    const TxMessage stop_message{.stop = true};
    if (xQueueSend(tx_queue, &stop_message, pdMS_TO_TICKS(500)) != pdTRUE ||
        xSemaphoreTake(tx_stopped, pdMS_TO_TICKS(1500)) != pdTRUE) {
      ESP_LOGE(kTag, "USB TX task did not stop cleanly");
      return ESP_ERR_TIMEOUT;
    }
  }
  if (cdc_tx_task_handle != nullptr) {
    const CdcTxMessage stop_message{.stop = true};
    if (xQueueSend(cdc_tx_queue, &stop_message, pdMS_TO_TICKS(500)) != pdTRUE ||
        xSemaphoreTake(cdc_tx_stopped, pdMS_TO_TICKS(1500)) != pdTRUE) {
      ESP_LOGE(kTag, "USB CDC TX task did not stop cleanly");
      return ESP_ERR_TIMEOUT;
    }
  }
  started = false;
  tinyusb_driver_uninstall();
  if (tx_queue != nullptr) {
    vQueueDeleteWithCaps(tx_queue);
    tx_queue = nullptr;
  }
  if (tx_stopped != nullptr) {
    vSemaphoreDelete(tx_stopped);
    tx_stopped = nullptr;
  }
  if (cdc_tx_queue != nullptr) {
    vQueueDeleteWithCaps(cdc_tx_queue);
    cdc_tx_queue = nullptr;
  }
  if (cdc_tx_stopped != nullptr) {
    vSemaphoreDelete(cdc_tx_stopped);
    cdc_tx_stopped = nullptr;
  }
  if (cdc_write_mutex != nullptr) {
    vSemaphoreDelete(cdc_write_mutex);
    cdc_write_mutex = nullptr;
  }
  if (rx_queue != nullptr) {
    vQueueDeleteWithCaps(rx_queue);
    rx_queue = nullptr;
  }
  if (rx_stopped != nullptr) {
    vSemaphoreDelete(rx_stopped);
    rx_stopped = nullptr;
  }
  return ESP_OK;
}

bool codex_usb_flush(unsigned int timeout_ms) {
  unsigned int waited = 0;
  while ((pending_tx.load(std::memory_order_acquire) != 0 ||
          pending_cdc_tx.load(std::memory_order_acquire) != 0 ||
          config_busy.load(std::memory_order_acquire)) && waited < timeout_ms) {
    vTaskDelay(pdMS_TO_TICKS(2));
    waited += 2;
  }
  return pending_tx.load(std::memory_order_acquire) == 0 &&
         pending_cdc_tx.load(std::memory_order_acquire) == 0 &&
         !config_busy.load(std::memory_order_acquire);
}

bool codex_usb_connected() { return started && tud_mounted(); }

bool codex_usb_control_active() {
  if (config_busy.load(std::memory_order_acquire)) return true;
  const unsigned int last =
      last_control_activity_ms.load(std::memory_order_acquire);
  const unsigned int now =
      static_cast<unsigned int>(esp_timer_get_time() / 1000ULL);
  return last != 0U && now - last < 2000U;
}

void codex_usb_send_effect(const SideEffect& effect) {
  if (!codex_usb_connected()) return;
  if (effect.type == SideEffectType::ReleaseAllInputs) {
    enqueue_release_batch(effect.release_mask);
    return;
  }
  char json[192]{}; unsigned int length = 0;
  if (effect.type == SideEffectType::SendHid) {
    signed char agent = -1;
    if (effect.control >= ControlId::Agent0 && effect.control <= ControlId::Agent5) {
      agent = static_cast<signed char>(static_cast<unsigned int>(effect.control));
    }
    length = make_codex_hid_json(json, sizeof(json),
                                 key_for(effect.control, effect.direction),
                                 effect.action, agent);
  } else if (effect.type == SideEffectType::SendJoystick) {
    length = make_codex_joystick_json(json, sizeof(json), effect.angle,
                                      effect.distance);
  }
  if (length) {
    const bool input_event = effect.type == SideEffectType::SendHid;
    send_hid_json(kCodexReportId, json, length,
                  input_event ? pdMS_TO_TICKS(30) : 0,
                  input_event, input_event, effect.origin_us);
  }
}

bool codex_usb_send_config_event(const char* json, unsigned int length,
                                 bool critical) {
  if (!codex_usb_connected()) return false;
  const bool cdc_sent = enqueue_cdc_event(json, length, critical);
  const bool hid_sent = send_hid_json(kConfigReportId, json, length,
                                      critical ? portMAX_DELAY : 0,
                                      critical, false);
  return cdc_sent || hid_sent;
}

}  // namespace codex

extern "C" const unsigned char* tud_hid_descriptor_report_cb(unsigned char) {
  return codex::kUsbReportMap;
}

extern "C" unsigned short tud_hid_get_report_cb(unsigned char, unsigned char,
                                                 hid_report_type_t,
                                                 unsigned char*, unsigned short) {
  return 0;
}

extern "C" void tud_hid_set_report_cb(unsigned char, unsigned char report_id,
                                       hid_report_type_t,
                                       const unsigned char* buffer,
                                       unsigned short size) {
  if (codex::stopping) return;
  const codex::HidReportRoute route =
      codex::route_hid_output_report(report_id, buffer, size);
  if (route.report_id == codex::kCodexReportId) {
    codex::enqueue_rx(codex::RxKind::CodexHid, 0, route.data, route.size);
  } else if (route.report_id == codex::kConfigReportId) {
    codex::enqueue_rx(codex::RxKind::ConfigHid, 0, route.data, route.size);
  }
}

extern "C" void tud_cdc_rx_cb(unsigned char interface) {
  if (codex::stopping) return;
  while (tud_cdc_n_available(interface)) {
    unsigned char packet[codex::kUsbPacketSize]{};
    const unsigned int length = tud_cdc_n_read(interface, packet, sizeof(packet));
    if (length == 0) break;
    codex::enqueue_rx(codex::RxKind::Cdc, interface, packet, length);
  }
}

extern "C" void tud_mount_cb(void) {
  if (codex::stopping) return;
  codex::publish(codex::make_transport_connected(codex::TransportLink::Usb));
}

extern "C" void tud_umount_cb(void) {
  if (codex::stopping) return;
  codex::publish(
      codex::make_transport_disconnected(codex::TransportLink::Usb));
}
