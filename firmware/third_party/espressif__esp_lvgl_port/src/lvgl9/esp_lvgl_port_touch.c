/*
 * SPDX-FileCopyrightText: 2024-2025 Espressif Systems (Shanghai) CO LTD
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include "esp_log.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_lcd_touch.h"
#include "esp_lvgl_port.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "LVGL";

/*******************************************************************************
* Types definitions
*******************************************************************************/

typedef struct {
    esp_lcd_touch_handle_t  handle;     /* LCD touch IO handle */
    lv_indev_t              *indev;     /* LVGL input device driver */
    struct {
        float x;
        float y;
    } scale;                            /* Touch scale */
    lvgl_port_touch_points_cb_t points_callback;
    void *points_user_data;
    uint8_t consecutive_read_failures;
    lv_indev_state_t last_state;
    lv_point_t last_point;
    TaskHandle_t sampler_task;
    SemaphoreHandle_t sampler_stopped;
    SemaphoreHandle_t cache_mutex;
    volatile bool sampler_stop;
    volatile bool polling;
    esp_lcd_touch_point_data_t cached_points[CONFIG_ESP_LCD_TOUCH_MAX_POINTS];
    uint8_t cached_count;
    uint32_t cached_monotonic_ms;
} lvgl_port_touch_ctx_t;

/*******************************************************************************
* Function definitions
*******************************************************************************/

static void lvgl_port_touchpad_read(lv_indev_t *indev_drv, lv_indev_data_t *data);
static void lvgl_port_touch_interrupt_callback(esp_lcd_touch_handle_t tp);
static void touch_sampler_task(void *argument);

/*******************************************************************************
* Public API functions
*******************************************************************************/

lv_indev_t *lvgl_port_add_touch(const lvgl_port_touch_cfg_t *touch_cfg)
{
    esp_err_t ret = ESP_OK;
    lv_indev_t *indev = NULL;
    assert(touch_cfg != NULL);
    assert(touch_cfg->disp != NULL);
    assert(touch_cfg->handle != NULL);

    /* Touch context */
    lvgl_port_touch_ctx_t *touch_ctx = calloc(1, sizeof(lvgl_port_touch_ctx_t));
    if (touch_ctx == NULL) {
        ESP_LOGE(TAG, "Not enough memory for touch context allocation!");
        return NULL;
    }
    touch_ctx->handle = touch_cfg->handle;
    touch_ctx->scale.x = (touch_cfg->scale.x ? touch_cfg->scale.x : 1);
    touch_ctx->scale.y = (touch_cfg->scale.y ? touch_cfg->scale.y : 1);
    touch_ctx->cache_mutex = xSemaphoreCreateMutex();
    touch_ctx->sampler_stopped = xSemaphoreCreateBinary();
    ESP_GOTO_ON_FALSE(touch_ctx->cache_mutex != NULL &&
                          touch_ctx->sampler_stopped != NULL,
                      ESP_ERR_NO_MEM, err, TAG,
                      "Not enough memory for async touch sampler");
    BaseType_t task_created = xTaskCreatePinnedToCore(
        touch_sampler_task, "touch_sampler", 4096, touch_ctx, 9,
        &touch_ctx->sampler_task, 1);
    ESP_GOTO_ON_FALSE(task_created == pdPASS, ESP_ERR_NO_MEM, err, TAG,
                      "Could not start async touch sampler");

    if (touch_ctx->handle->config.int_gpio_num != GPIO_NUM_NC) {
        /* Register touch interrupt callback */
        ret = esp_lcd_touch_register_interrupt_callback_with_data(touch_ctx->handle, lvgl_port_touch_interrupt_callback,
                touch_ctx);
        ESP_GOTO_ON_ERROR(ret, err, TAG, "Error in register touch interrupt.");
    }

    lvgl_port_lock(0);
    /* Register a touchpad input device */
    indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    /* Event mode can be set only, when touch interrupt enabled */
    if (touch_ctx->handle->config.int_gpio_num != GPIO_NUM_NC) {
        lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);
    }
    lv_indev_set_read_cb(indev, lvgl_port_touchpad_read);
    lv_indev_set_disp(indev, touch_cfg->disp);
    lv_indev_set_driver_data(indev, touch_ctx);
    touch_ctx->indev = indev;
    lvgl_port_unlock();

err:
    if (ret != ESP_OK) {
        if (touch_ctx) {
            if (touch_ctx->sampler_task != NULL) {
                touch_ctx->sampler_stop = true;
                xTaskNotifyGive(touch_ctx->sampler_task);
                xSemaphoreTake(touch_ctx->sampler_stopped,
                               pdMS_TO_TICKS(1000));
            }
            if (touch_ctx->sampler_stopped != NULL) {
                vSemaphoreDelete(touch_ctx->sampler_stopped);
            }
            if (touch_ctx->cache_mutex != NULL) {
                vSemaphoreDelete(touch_ctx->cache_mutex);
            }
            free(touch_ctx);
        }
    }

    return indev;
}

esp_err_t lvgl_port_touch_set_points_callback(
    lv_indev_t *touch, lvgl_port_touch_points_cb_t callback, void *user_data)
{
    ESP_RETURN_ON_FALSE(touch != NULL, ESP_ERR_INVALID_ARG, TAG, "Touch is null");
    lvgl_port_touch_ctx_t *touch_ctx =
        (lvgl_port_touch_ctx_t *)lv_indev_get_driver_data(touch);
    ESP_RETURN_ON_FALSE(touch_ctx != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "Touch context is null");
    xSemaphoreTake(touch_ctx->cache_mutex, portMAX_DELAY);
    touch_ctx->points_callback = callback;
    touch_ctx->points_user_data = user_data;
    xSemaphoreGive(touch_ctx->cache_mutex);
    return ESP_OK;
}

esp_err_t lvgl_port_touch_set_polling_mode(lv_indev_t *touch, bool polling)
{
    ESP_RETURN_ON_FALSE(touch != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "Touch is null");
    lvgl_port_touch_ctx_t *touch_ctx =
        (lvgl_port_touch_ctx_t *)lv_indev_get_driver_data(touch);
    ESP_RETURN_ON_FALSE(touch_ctx != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "Touch context is null");
    /* The input mode is LVGL-owned state.  Never mutate it when another
     * render/flush path owns the LVGL lock: doing so can corrupt the object
     * tree and turn an ordinary wake-up into a panic/re-enumeration. */
    if (!lvgl_port_lock(100)) {
        ESP_LOGW(TAG, "Timed out acquiring LVGL lock for touch mode change");
        return ESP_ERR_TIMEOUT;
    }
    touch_ctx->polling = polling;
    if (polling || touch_ctx->handle->config.int_gpio_num == GPIO_NUM_NC) {
        lv_indev_set_mode(touch, LV_INDEV_MODE_TIMER);
    } else {
        lv_indev_set_mode(touch, LV_INDEV_MODE_EVENT);
    }
    lvgl_port_unlock();
    if (touch_ctx->sampler_task != NULL) {
        xTaskNotifyGive(touch_ctx->sampler_task);
    }
    lvgl_port_task_wake(LVGL_PORT_EVENT_TOUCH, touch);
    return ESP_OK;
}

esp_err_t lvgl_port_remove_touch(lv_indev_t *touch)
{
    assert(touch);
    lvgl_port_touch_ctx_t *touch_ctx = (lvgl_port_touch_ctx_t *)lv_indev_get_driver_data(touch);

    if (touch_ctx->handle->config.int_gpio_num != GPIO_NUM_NC) {
        /* Stop new ISR notifications before terminating the sampler. */
        esp_lcd_touch_register_interrupt_callback(touch_ctx->handle, NULL);
    }
    touch_ctx->sampler_stop = true;
    if (touch_ctx->sampler_task != NULL) {
        xTaskNotifyGive(touch_ctx->sampler_task);
        xSemaphoreTake(touch_ctx->sampler_stopped, pdMS_TO_TICKS(1000));
    }

    lvgl_port_lock(0);
    /* Remove input device driver */
    lv_indev_delete(touch);
    lvgl_port_unlock();

    if (touch_ctx) {
        vSemaphoreDelete(touch_ctx->sampler_stopped);
        vSemaphoreDelete(touch_ctx->cache_mutex);
        free(touch_ctx);
    }

    return ESP_OK;
}

/*******************************************************************************
* Private functions
*******************************************************************************/

static void lvgl_port_touchpad_read(lv_indev_t *indev_drv, lv_indev_data_t *data)
{
    assert(indev_drv);
    lvgl_port_touch_ctx_t *touch_ctx = (lvgl_port_touch_ctx_t *)lv_indev_get_driver_data(indev_drv);
    assert(touch_ctx);
    assert(touch_ctx->handle);

    uint8_t touch_cnt = 0;
    esp_lcd_touch_point_data_t touch_data[CONFIG_ESP_LCD_TOUCH_MAX_POINTS] = {0};
    data->state = touch_ctx->last_state;
    data->point = touch_ctx->last_point;

    xSemaphoreTake(touch_ctx->cache_mutex, portMAX_DELAY);
    touch_cnt = touch_ctx->cached_count;
    memcpy(touch_data, touch_ctx->cached_points,
           sizeof(esp_lcd_touch_point_data_t) * touch_cnt);
    const uint32_t monotonic_ms = touch_ctx->cached_monotonic_ms;
    xSemaphoreGive(touch_ctx->cache_mutex);
    (void)monotonic_ms;

#if (CONFIG_ESP_LCD_TOUCH_MAX_POINTS > 1 && CONFIG_LV_USE_GESTURE_RECOGNITION)
    // Number of touch points which need to be constantly updated inside gesture recognizers
#define GESTURE_TOUCH_POINTS 2
#if GESTURE_TOUCH_POINTS > CONFIG_ESP_LCD_TOUCH_MAX_POINTS
#error "Number of touch point for gesture exceeds maximum number of aquired touch points"
#endif

    /* Initialize LVGL touch data for each activated touch point */
    lv_indev_touch_data_t touches[GESTURE_TOUCH_POINTS] = {0};

    for (int i = 0; i < touch_cnt && i < GESTURE_TOUCH_POINTS; i++) {
        touches[i].state = LV_INDEV_STATE_PRESSED;
        touches[i].point.x = touch_data[i].x;
        touches[i].point.y = touch_data[i].y;
        touches[i].id = touch_data[i].track_id;
        touches[i].timestamp = monotonic_ms;
    }

    /* Pass touch data to LVGL gesture recognizers */
    lv_indev_gesture_recognizers_update(indev_drv, touches, GESTURE_TOUCH_POINTS);
    lv_indev_gesture_recognizers_set_data(indev_drv, data);

#endif

    if (touch_cnt > 0) {
        data->point.x = touch_data[0].x;
        data->point.y = touch_data[0].y;
        data->state = LV_INDEV_STATE_PRESSED;
    } else {
        data->state = LV_INDEV_STATE_RELEASED;
    }
    touch_ctx->last_state = data->state;
    touch_ctx->last_point = data->point;
}

static void touch_sampler_task(void *argument)
{
    lvgl_port_touch_ctx_t *touch_ctx = (lvgl_port_touch_ctx_t *)argument;
    while (!touch_ctx->sampler_stop) {
        const TickType_t wait = (touch_ctx->polling ||
                touch_ctx->handle->config.int_gpio_num == GPIO_NUM_NC)
                ? pdMS_TO_TICKS(10) : portMAX_DELAY;
        ulTaskNotifyTake(pdTRUE, wait);
        if (touch_ctx->sampler_stop) {
            break;
        }

        esp_lcd_touch_point_data_t touch_data[CONFIG_ESP_LCD_TOUCH_MAX_POINTS] = {0};
        uint8_t touch_cnt = 0;
        esp_err_t touch_error = esp_lcd_touch_read_data(touch_ctx->handle);
        if (touch_error == ESP_OK) {
            touch_error = esp_lcd_touch_get_data(
                touch_ctx->handle, touch_data, &touch_cnt,
                CONFIG_ESP_LCD_TOUCH_MAX_POINTS);
        }
        if (touch_error != ESP_OK) {
            if (touch_ctx->consecutive_read_failures < UINT8_MAX) {
                touch_ctx->consecutive_read_failures++;
            }
            if (touch_ctx->consecutive_read_failures == 1 ||
                    (touch_ctx->consecutive_read_failures % 16) == 0) {
                ESP_LOGW(TAG, "Async touch read failed (%u): %s",
                         touch_ctx->consecutive_read_failures,
                         esp_err_to_name(touch_error));
            }
            if (touch_ctx->consecutive_read_failures >= 3) {
                lvgl_port_touch_points_cb_t callback = NULL;
                void *user_data = NULL;
                xSemaphoreTake(touch_ctx->cache_mutex, portMAX_DELAY);
                callback = touch_ctx->points_callback;
                user_data = touch_ctx->points_user_data;
                xSemaphoreGive(touch_ctx->cache_mutex);
                if (callback != NULL) {
                    callback(NULL, 0,
                             (uint32_t)(esp_timer_get_time() / 1000ULL),
                             user_data);
                }
            }
            continue;
        }
        touch_ctx->consecutive_read_failures = 0;
        for (int i = 0; i < touch_cnt; ++i) {
            touch_data[i].x = touch_ctx->scale.x * touch_data[i].x;
            touch_data[i].y = touch_ctx->scale.y * touch_data[i].y;
        }
        const uint32_t monotonic_ms =
            (uint32_t)(esp_timer_get_time() / 1000ULL);
        lvgl_port_touch_points_cb_t callback = NULL;
        void *user_data = NULL;
        xSemaphoreTake(touch_ctx->cache_mutex, portMAX_DELAY);
        touch_ctx->cached_count = touch_cnt;
        memcpy(touch_ctx->cached_points, touch_data,
               sizeof(esp_lcd_touch_point_data_t) * touch_cnt);
        touch_ctx->cached_monotonic_ms = monotonic_ms;
        callback = touch_ctx->points_callback;
        user_data = touch_ctx->points_user_data;
        xSemaphoreGive(touch_ctx->cache_mutex);

        if (callback != NULL) {
            callback(touch_data, touch_cnt, monotonic_ms, user_data);
        }
        if (touch_ctx->indev != NULL) {
            lvgl_port_task_wake(LVGL_PORT_EVENT_TOUCH, touch_ctx->indev);
        }
    }
    touch_ctx->sampler_task = NULL;
    xSemaphoreGive(touch_ctx->sampler_stopped);
    vTaskDelete(NULL);
}

static void IRAM_ATTR lvgl_port_touch_interrupt_callback(esp_lcd_touch_handle_t tp)
{
    lvgl_port_touch_ctx_t *touch_ctx = (lvgl_port_touch_ctx_t *) tp->config.user_data;

    BaseType_t high_priority_woken = pdFALSE;
    if (touch_ctx != NULL && touch_ctx->sampler_task != NULL) {
        vTaskNotifyGiveFromISR(touch_ctx->sampler_task,
                              &high_priority_woken);
    }
    if (high_priority_woken == pdTRUE) {
        portYIELD_FROM_ISR();
    }
}
