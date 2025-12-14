/**
 * @file i2c_manager.h
 * @brief Shared I2C bus manager for touch and audio devices
 * 
 * This module provides centralized I2C bus initialization and management.
 * Multiple devices (touch controller, audio codec) share a single I2C bus.
 * 
 * I2C Bus Configuration:
 * - Bus: I2C_NUM_1
 * - SDA: GPIO 15
 * - SCL: GPIO 14
 * - Speed: 100 kHz
 * 
 * Connected Devices:
 * - Touch Controller (CST92xx): Address 0x5A
 * - Audio Codec (ES8311): Address 0x18
 * 
 * @copyright 2025
 */

#ifndef I2C_MANAGER_H
#define I2C_MANAGER_H

#include "esp_err.h"
#include "driver/i2c.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize shared I2C bus
 * 
 * This function initializes the I2C bus used by both touch and audio devices.
 * Must be called once before any device initialization.
 * 
 * Configuration:
 * - Port: I2C_NUM_1
 * - Mode: Master
 * - SDA: GPIO 15 (with pull-up)
 * - SCL: GPIO 14 (with pull-up)
 * - Clock: 100 kHz (compatible with all devices)
 * 
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if already initialized
 * @return ESP_FAIL on driver installation failure
 */
esp_err_t i2c_shared_init(void);

/**
 * @brief Check if I2C bus is initialized
 * 
 * @return true if I2C bus is initialized and ready
 * @return false if not initialized
 */
bool i2c_shared_is_initialized(void);

/**
 * @brief De-initialize shared I2C bus
 * 
 * Removes the I2C driver. Should only be called when all devices
 * are done using the bus.
 * 
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t i2c_shared_deinit(void);

/**
 * @brief Get I2C port number
 * 
 * @return I2C port number (I2C_NUM_1)
 */
i2c_port_t i2c_shared_get_port(void);

#ifdef __cplusplus
}
#endif

#endif // I2C_MANAGER_H

