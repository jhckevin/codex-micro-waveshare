/*
 * SPDX-License-Identifier: Apache-2.0
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t level;
    bool present;
    bool charging;
    bool external_power;
} esp_hid_battery_status_t;

typedef struct {
    uint8_t bytes[4];
    uint8_t length;
} esp_hid_battery_level_status_value_t;

static inline esp_hid_battery_level_status_value_t
esp_hid_make_battery_level_status(bool present, uint8_t level,
                                  bool charging, bool external_power)
{
    esp_hid_battery_level_status_value_t value = {{0, 0, 0, 0}, 3};
    uint16_t power_state = 0;
    if (present) {
        value.bytes[0] = 1U << 1U; /* Battery Level field present. */
        value.bytes[3] = level > 100U ? 100U : level;
        value.length = 4;
        power_state |= 1U; /* Battery present. */
        if (external_power) {
            power_state |= 1U << 1U; /* Wired external power: yes. */
        }
        if (charging) {
            power_state |= 1U << 5U; /* Charging. */
        } else if (external_power) {
            power_state |= 3U << 5U; /* Discharging inactive / full. */
        } else {
            power_state |= 2U << 5U; /* Discharging active. */
        }
        if (value.bytes[3] < 5U) {
            power_state |= 3U << 7U; /* Critical. */
        } else if (value.bytes[3] < 10U) {
            power_state |= 2U << 7U; /* Low. */
        } else {
            power_state |= 1U << 7U; /* Good. */
        }
    }
    value.bytes[1] = (uint8_t)(power_state & 0xFFU);
    value.bytes[2] = (uint8_t)(power_state >> 8U);
    return value;
}

#ifdef __cplusplus
}
#endif
