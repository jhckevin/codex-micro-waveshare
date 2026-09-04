#include "codex/board_power.h"
#include "codex/smart_charge.h"
#include "sdkconfig.h"
#include "esp_timer.h"

#include "bsp/esp-bsp.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace codex {
namespace {

constexpr char kTag[] = "codex_power";
constexpr unsigned char kAddress = 0x34;
constexpr unsigned char kChipIdRegister = 0x03;
constexpr unsigned char kExpectedChipId = 0x4A;
constexpr unsigned char kStatus1 = 0x00;
constexpr unsigned char kStatus2 = 0x01;
constexpr unsigned char kCommonConfig = 0x10;
constexpr unsigned char kChargerGaugeControl = 0x18;
constexpr unsigned char kPowerOffControl = 0x22;
constexpr unsigned char kPowerKeyTiming = 0x27;
constexpr unsigned char kAdcControl = 0x30;
constexpr unsigned char kBatteryVoltageHigh = 0x34;
constexpr unsigned char kBatteryVoltageLow = 0x35;
constexpr unsigned char kBatteryDetectControl = 0x68;
constexpr unsigned char kBatteryPercent = 0xA4;
constexpr unsigned char kChargeCurrentControl = 0x62;

i2c_master_dev_handle_t device{};
PowerEventCallback callback{};
void* callback_context{};
SmartCharge smart_charge{};
SemaphoreHandle_t sample_mutex{};

unsigned char estimate_percent_from_voltage(unsigned short voltage_mv) {
  struct Point {
    unsigned short voltage;
    unsigned char percent;
  };
  constexpr Point curve[] = {
      {3300, 0},  {3600, 10}, {3700, 20}, {3800, 40},
      {3900, 60}, {4000, 80}, {4100, 95}, {4200, 100},
  };
  if (voltage_mv <= curve[0].voltage) return curve[0].percent;
  for (unsigned int index = 1; index < sizeof(curve) / sizeof(curve[0]);
       ++index) {
    if (voltage_mv <= curve[index].voltage) {
      const unsigned int voltage_span =
          curve[index].voltage - curve[index - 1].voltage;
      const unsigned int percent_span =
          curve[index].percent - curve[index - 1].percent;
      return static_cast<unsigned char>(
          curve[index - 1].percent +
          (static_cast<unsigned int>(voltage_mv - curve[index - 1].voltage) *
           percent_span) /
              voltage_span);
    }
  }
  return 100;
}

esp_err_t read_register(unsigned char address, unsigned char* value) {
  if (device == nullptr || value == nullptr) return ESP_ERR_INVALID_STATE;
  return i2c_master_transmit_receive(device, &address, 1, value, 1, 100);
}

esp_err_t write_register(unsigned char address, unsigned char value) {
  if (device == nullptr) return ESP_ERR_INVALID_STATE;
  const unsigned char bytes[2] = {address, value};
  return i2c_master_transmit(device, bytes, sizeof(bytes), 100);
}

esp_err_t update_register(unsigned char address, unsigned char set_mask,
                          unsigned char clear_mask) {
  unsigned char value = 0;
  esp_err_t error = read_register(address, &value);
  if (error != ESP_OK) return error;
  value = static_cast<unsigned char>((value & static_cast<unsigned char>(~clear_mask)) | set_mask);
  return write_register(address, value);
}

esp_err_t sample_and_publish_locked() {
  unsigned char status1 = 0;
  unsigned char status2 = 0;
  unsigned char percent = 100;
  unsigned char gauge_percent = 100;
  unsigned char voltage_high = 0;
  unsigned char voltage_low = 0;
  unsigned char charge_current = 0;
  esp_err_t error = read_register(kStatus1, &status1);
  if (error == ESP_OK) error = read_register(kStatus2, &status2);
  if (error != ESP_OK) return error;

  const bool present = (status1 & (1U << 3U)) != 0;
  const bool charging = present && ((status2 >> 5U) & 3U) == 1U;
  const bool vbus_good = (status1 & (1U << 5U)) != 0;
  if (read_register(kChargeCurrentControl, &charge_current) != ESP_OK) {
    charge_current = 0;
  }
  unsigned short charge_limit_ma =
      charge_current_ma(charge_current);
  unsigned short voltage_mv = 0;
  if (present) {
    if (read_register(kBatteryPercent, &gauge_percent) != ESP_OK) {
      gauge_percent = 0xFF;
    }
    if (read_register(kBatteryVoltageHigh, &voltage_high) == ESP_OK &&
        read_register(kBatteryVoltageLow, &voltage_low) == ESP_OK) {
      voltage_mv = static_cast<unsigned short>(
          (static_cast<unsigned short>(voltage_high & 0x3FU) << 8U) |
          voltage_low);
    }
    const bool gauge_valid =
        gauge_percent <= 100 &&
        !(gauge_percent == 0 && voltage_mv >= 3400);
    percent = gauge_valid ? gauge_percent
                          : estimate_percent_from_voltage(voltage_mv);
  }
  unsigned char vh=0, vl=0, input_code=0, cv_code=0;
  bool telemetry_ok = read_register(0x38, &vh) == ESP_OK;
  telemetry_ok = read_register(0x39, &vl) == ESP_OK && telemetry_ok;
  telemetry_ok = read_register(0x16, &input_code) == ESP_OK && telemetry_ok;
  telemetry_ok = read_register(0x64, &cv_code) == ESP_OK && telemetry_ok;
  constexpr unsigned short input_limits[] = {100,500,900,1000,1500,2000,0,0};
  const unsigned short vbus_mv = ((vh & 0x3fU) << 8U) | vl;
  const ChargeSample sample{telemetry_ok, present, vbus_good,
      (status1 & 3U) != 0 || (status2 & 8U) != 0,
      voltage_mv, vbus_mv, input_limits[input_code & 7U],
      static_cast<unsigned char>(status2 & 7U), static_cast<unsigned char>(cv_code & 7U)};
  auto requested = smart_charge_next(smart_charge, sample,
      static_cast<unsigned int>(esp_timer_get_time()/1000ULL));
  if (requested > CONFIG_CODEX_CHARGE_MAX_MA) requested = CONFIG_CODEX_CHARGE_MAX_MA;
  requested = charge_current_ma(charge_current_code(requested));
  if (requested != charge_limit_ma) {
    error = update_register(kChargeCurrentControl, charge_current_code(requested), 0x1fU);
    // Read back every change. Never report an unconfirmed write as applied.
    if (error == ESP_OK) error = read_register(kChargeCurrentControl, &charge_current);
    if (error != ESP_OK || charge_current_ma(charge_current) != requested) {
      (void)update_register(kChargerGaugeControl, 0, 1U << 1U);
      ESP_LOGE(kTag, "smart charge write failed; charger disable requested");
      return error == ESP_OK ? ESP_FAIL : error;
    }
    charge_limit_ma = charge_current_ma(charge_current);
    ESP_LOGI(kTag, "smart charge limit=%umA vbus=%umV phase=%u constrained=%u (not measured current)",
        charge_limit_ma, vbus_mv, sample.phase, sample.constrained);
  }
  if (percent > 100) percent = 100;
  if (callback != nullptr) {
    callback(make_battery_changed(present, present ? percent : 100, charging,
                                  vbus_good, voltage_mv,
                                  present ? charge_limit_ma : 0U),
             callback_context);
  }
  ESP_LOGI(kTag,
           "battery=%s percent=%u gauge=%u charging=%s vbus=%s voltage=%umV "
           "charge_limit=%umA status1=0x%02x status2=0x%02x phase=%u",
           present ? "present" : "absent", present ? percent : 100,
           gauge_percent, charging ? "yes" : "no",
           vbus_good ? "yes" : "no", voltage_mv,
           present ? charge_limit_ma : 0U, status1, status2,
           status2 & 0x07U);
  return ESP_OK;
}

esp_err_t sample_and_publish() {
  if (sample_mutex == nullptr) return ESP_ERR_INVALID_STATE;
  xSemaphoreTake(sample_mutex, portMAX_DELAY);
  const esp_err_t error = sample_and_publish_locked();
  if (error != ESP_OK) {
    smart_charge = {};
    // Fail conservatively on missing telemetry; do not keep a stale fast limit.
    if (update_register(kChargeCurrentControl, charge_current_code(200), 0x1fU) != ESP_OK)
      (void)update_register(kChargerGaugeControl, 0, 1U << 1U);
  }
  xSemaphoreGive(sample_mutex);
  return error;
}

void power_task(void*) {
  while (true) {
    const esp_err_t error = sample_and_publish();
    if (error != ESP_OK) {
      ESP_LOGW(kTag, "AXP2101 sample failed: %s", esp_err_to_name(error));
    }
    vTaskDelay(pdMS_TO_TICKS(10000));
  }
}

}  // namespace

esp_err_t board_power_start(PowerEventCallback new_callback, void* context) {
  if (device != nullptr) return ESP_OK;
  callback = new_callback;
  callback_context = context;
  i2c_master_bus_handle_t bus = bsp_i2c_get_handle();
  if (bus == nullptr) return ESP_ERR_INVALID_STATE;
  const i2c_device_config_t config = {
      .dev_addr_length = I2C_ADDR_BIT_LEN_7,
      .device_address = kAddress,
      .scl_speed_hz = 400000,
      .scl_wait_us = 0,
      .flags = {},
  };
  esp_err_t error = i2c_master_bus_add_device(bus, &config, &device);
  if (error != ESP_OK) return error;
  unsigned char chip_id = 0;
  error = read_register(kChipIdRegister, &chip_id);
  if (error != ESP_OK || chip_id != kExpectedChipId) {
    ESP_LOGE(kTag, "AXP2101 unavailable chip_id=0x%02x error=%s", chip_id,
             esp_err_to_name(error));
    i2c_master_bus_rm_device(device);
    device = nullptr;
    return error == ESP_OK ? ESP_ERR_NOT_FOUND : error;
  }

  sample_mutex = xSemaphoreCreateMutex();
  if (sample_mutex == nullptr) return ESP_ERR_NO_MEM;
  // Start at 200mA on every boot. Preserve CV, precharge and safety timers.
  if ((error = update_register(kChargeCurrentControl, charge_current_code(200), 0x1fU)) != ESP_OK ||
      // REG65: choose the lowest supported PMIC thermal regulation threshold, 60C.
      (error = update_register(0x65, 0, 3U)) != ESP_OK) return error;
  // Battery/VBUS ADC on, TS ADC off (the board has no battery thermistor).
  if ((error = update_register(kAdcControl, (1U << 2U) | 1U,
                               1U << 1U)) != ESP_OK ||
      // The board support package may leave the e-gauge disabled.
      (error = update_register(kChargerGaugeControl, 1U << 3U, 0)) != ESP_OK ||
      (error = update_register(kBatteryDetectControl, 1U, 0)) != ESP_OK ||
      // Long PWR press powers off instead of restarting the PMIC.
      (error = update_register(kPowerOffControl, 1U << 1U, 1U)) != ESP_OK ||
      // 512 ms power-on; 4 s hardware power-off.
      (error = update_register(kPowerKeyTiming, 1U, 0x0EU)) != ESP_OK) {
    return error;
  }
  unsigned char verified_current=0, verified_thermal=0, verified_cv=0, verified_timers=0;
  if (read_register(kChargeCurrentControl,&verified_current)!=ESP_OK ||
      read_register(0x65,&verified_thermal)!=ESP_OK ||
      read_register(0x64,&verified_cv)!=ESP_OK ||
      read_register(0x67,&verified_timers)!=ESP_OK ||
      charge_current_ma(verified_current)!=200 || (verified_thermal&3U)!=0) {
    (void)update_register(kChargerGaugeControl,0,1U<<1U);
    return ESP_FAIL;
  }
  ESP_LOGI(kTag,"smart charge profile max=%dmA initial=%umA thermal_reg=60C cv_code=%u timers=0x%02x; no battery-temperature sensor",
      CONFIG_CODEX_CHARGE_MAX_MA, charge_current_ma(verified_current), verified_cv&7U, verified_timers);
  if (xTaskCreate(power_task, "codex_power", 4096, nullptr, 6, nullptr) !=
      pdPASS) {
    return ESP_ERR_NO_MEM;
  }
  ESP_LOGI(kTag, "AXP2101 ready; PWR long-off and long-on retained");
  return ESP_OK;
}

esp_err_t board_power_sample_now() { return sample_and_publish(); }

esp_err_t board_power_shutdown() {
  if (device == nullptr) return ESP_ERR_INVALID_STATE;
  unsigned char value = 0;
  esp_err_t error = read_register(kCommonConfig, &value);
  if (error != ESP_OK) return error;
  ESP_LOGI(kTag, "requesting AXP2101 soft power-off");
  return write_register(kCommonConfig,
                        static_cast<unsigned char>(value | 1U));
}

}  // namespace codex
