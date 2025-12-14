/**
 * @file ui_init.h
 * @brief LVGL, display, and touch initialization
 */

#ifndef UI_INIT_H
#define UI_INIT_H

#include "esp_err.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief UI context structure
 */
typedef struct {
    SemaphoreHandle_t lvgl_mutex;
    lv_indev_t *touch_indev;
    lv_disp_t *display;
} ui_context_t;

/**
 * @brief Initialize hardware (LCD + touch via I2C)
 * 
 * Initializes SPI for display, I2C for touch controller.
 * Note: I2C is shared with audio codec.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ui_hardware_init(void);

/**
 * @brief Initialize LVGL library and start LVGL task
 * 
 * Creates LVGL draw buffers, display driver, input driver, and task.
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ui_lvgl_init(void);

/**
 * @brief Lock LVGL mutex (for thread-safe LVGL API calls)
 * 
 * MUST be called before any LVGL API call from non-LVGL tasks.
 * 
 * @param timeout_ms Timeout in milliseconds (-1 for portMAX_DELAY)
 * @return true if locked, false on timeout
 */
bool ui_lock(int timeout_ms);

/**
 * @brief Unlock LVGL mutex
 * 
 * MUST be called after LVGL API calls.
 */
void ui_unlock(void);

/**
 * @brief Get UI context
 * 
 * @return Pointer to UI context structure
 */
ui_context_t* ui_get_context(void);

/**
 * @brief Deinitialize UI subsystem
 */
void ui_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // UI_INIT_H

