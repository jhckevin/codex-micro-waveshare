#include "codex/board_controls.h"
#include "codex/button_gesture.h"

#include "bsp/esp-bsp.h"
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_io_expander.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace codex {
namespace {

constexpr gpio_num_t kBootButton = GPIO_NUM_0;
constexpr uint32_t kPowerButton = IO_EXPANDER_PIN_NUM_4;
constexpr char kTag[] = "codex_board";
BoardEventCallback callback{};
void* callback_context{};
volatile bool standby_polling{};
i2c_master_dev_handle_t ultra_touch_device{};

constexpr unsigned char kGt911Address = 0x5D;
constexpr unsigned short kGt911PointStatusRegister = 0x814E;

esp_err_t gt911_read_status(unsigned char& status) {
  if (ultra_touch_device == nullptr) return ESP_ERR_INVALID_STATE;
  const unsigned char address[] = {
      static_cast<unsigned char>(kGt911PointStatusRegister >> 8U),
      static_cast<unsigned char>(kGt911PointStatusRegister & 0xFFU),
  };
  return i2c_master_transmit_receive(ultra_touch_device, address,
                                     sizeof(address), &status, 1, 20);
}

void gt911_clear_status() {
  if (ultra_touch_device == nullptr) return;
  const unsigned char clear[] = {
      static_cast<unsigned char>(kGt911PointStatusRegister >> 8U),
      static_cast<unsigned char>(kGt911PointStatusRegister & 0xFFU), 0,
  };
  (void)i2c_master_transmit(ultra_touch_device, clear, sizeof(clear), 20);
}

bool power_pressed(esp_io_expander_handle_t expander) {
  if (expander == nullptr) return false;
  uint32_t levels = kPowerButton;
  return esp_io_expander_get_level(expander, kPowerButton, &levels) == ESP_OK &&
         (levels & kPowerButton) == 0;
}

void controls_task(void*) {
  esp_io_expander_handle_t expander = bsp_io_expander_init();
  if (expander == nullptr ||
      esp_io_expander_set_dir(expander, kPowerButton, IO_EXPANDER_INPUT) != ESP_OK) {
    ESP_LOGE(kTag, "PWR input unavailable; BOOT recovery control remains active");
    expander = nullptr;
  }
  bool boot_stable = gpio_get_level(kBootButton) == 0;
  bool power_stable = power_pressed(expander);
  const unsigned int started_ms =
      static_cast<unsigned int>(esp_timer_get_time() / 1000ULL);
  ButtonGesture boot_gesture = make_button_gesture(boot_stable, started_ms);
  ButtonGesture power_gesture = make_button_gesture(power_stable, started_ms);
  bool boot_last = boot_stable;
  bool power_last = power_stable;
  unsigned char boot_count = 0;
  unsigned char power_count = 0;
  while (true) {
    const unsigned char stable_threshold = standby_polling ? 1U : 3U;
    const bool boot = gpio_get_level(kBootButton) == 0;
    const bool power = power_pressed(expander);
    if (boot == boot_last) {
      if (boot_count < stable_threshold) ++boot_count;
    }
    else { boot_last = boot; boot_count = 0; }
    if (power == power_last) {
      if (power_count < stable_threshold) ++power_count;
    }
    else { power_last = power; power_count = 0; }
    if (boot_count == stable_threshold && boot_stable != boot) {
      boot_stable = boot;
    }
    if (power_count == stable_threshold && power_stable != power) {
      power_stable = power;
    }
    const unsigned int now_ms =
        static_cast<unsigned int>(esp_timer_get_time() / 1000ULL);
    const ButtonGestureEvent boot_event =
        update_button_gesture(boot_gesture, boot_stable, now_ms);
    if (callback != nullptr && boot_event == ButtonGestureEvent::ShortPress) {
      callback(make_connection_overlay_requested(), callback_context);
    } else if (callback != nullptr &&
               boot_event == ButtonGestureEvent::LongPress) {
      callback(make_current_ble_slot_repair_requested(now_ms), callback_context);
    }
    const ButtonGestureEvent power_event =
        update_button_gesture_on_press(power_gesture, power_stable, now_ms);
    if (callback != nullptr && power_event == ButtonGestureEvent::ShortPress) {
      callback(make_power_button_pressed(), callback_context);
    } else if (power_event == ButtonGestureEvent::LongPress) {
      // Do not reinterpret a PMIC long press as application standby. The
      // board's AXP2101 remains responsible for the physical power-off path.
      ESP_LOGI(kTag, "PWR long press reserved for PMIC shutdown");
    }
    vTaskDelay(pdMS_TO_TICKS(standby_polling ? 40 : 20));
  }
}

}  // namespace

esp_err_t board_controls_start(BoardEventCallback new_callback, void* context) {
  callback = new_callback;
  callback_context = context;
  gpio_config_t config = {
      .pin_bit_mask = 1ULL << kBootButton,
      .mode = GPIO_MODE_INPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  esp_err_t error = gpio_config(&config);
  if (error != ESP_OK) return error;
  return xTaskCreate(controls_task, "board_controls", 3072, nullptr, 7, nullptr) == pdPASS
             ? ESP_OK : ESP_ERR_NO_MEM;
}

void board_controls_set_standby(bool standby) {
  standby_polling = standby;
}

esp_err_t board_controls_start_ultra_touch() {
  if (ultra_touch_device != nullptr) return ESP_OK;
  const i2c_device_config_t config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = kGt911Address,
      .scl_speed_hz = 100000,
      .scl_wait_us = 0,
      .flags = {},
  };
  esp_err_t error = i2c_master_bus_add_device(
      bsp_i2c_get_handle(), &config, &ultra_touch_device);
  if (error == ESP_OK) gt911_clear_status();
  return error;
}

void board_controls_stop_ultra_touch() {
  if (ultra_touch_device == nullptr) return;
  (void)i2c_master_bus_rm_device(ultra_touch_device);
  ultra_touch_device = nullptr;
}

bool board_controls_ultra_touch_pressed() {
  unsigned char status{};
  if (gt911_read_status(status) != ESP_OK) return false;
  const bool pressed = (status & 0x80U) != 0U && (status & 0x0FU) != 0U;
  gt911_clear_status();
  return pressed;
}

}  // namespace codex
