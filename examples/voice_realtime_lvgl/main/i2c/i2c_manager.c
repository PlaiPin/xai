/**
 * @file i2c_manager.c
 * @brief Shared I2C bus manager implementation
 */

#include "i2c_manager.h"
#include "app_config.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "i2c_manager";

// Track initialization state
static bool i2c_initialized = false;

esp_err_t i2c_shared_init(void)
{
    // Check if already initialized
    if (i2c_initialized) {
        ESP_LOGW(TAG, "I2C bus already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing shared I2C bus...");
    ESP_LOGI(TAG, "  Port: I2C_NUM_%d", I2C_NUM);
    ESP_LOGI(TAG, "  SDA:  GPIO %d", I2C_SDA_IO);
    ESP_LOGI(TAG, "  SCL:  GPIO %d", I2C_SCL_IO);
    ESP_LOGI(TAG, "  Freq: %d Hz", I2C_FREQ_HZ);

    // Configure I2C bus
    i2c_config_t i2c_conf = {};
    i2c_conf.mode = I2C_MODE_MASTER;
    i2c_conf.sda_io_num = I2C_SDA_IO;
    i2c_conf.scl_io_num = I2C_SCL_IO;
    i2c_conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    i2c_conf.master.clk_speed = I2C_FREQ_HZ;

    // Apply configuration
    esp_err_t ret = i2c_param_config(I2C_NUM, &i2c_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Install I2C driver
    // Master mode: no need for RX/TX buffers
    ret = i2c_driver_install(I2C_NUM, I2C_MODE_MASTER, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_initialized = true;
    ESP_LOGI(TAG, "✓ I2C bus initialized successfully");
    ESP_LOGI(TAG, "  Devices can now use I2C_NUM_%d", I2C_NUM);

    return ESP_OK;
}

bool i2c_shared_is_initialized(void)
{
    return i2c_initialized;
}

esp_err_t i2c_shared_deinit(void)
{
    if (!i2c_initialized) {
        ESP_LOGW(TAG, "I2C bus not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "De-initializing I2C bus...");
    
    esp_err_t ret = i2c_driver_delete(I2C_NUM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver delete failed: %s", esp_err_to_name(ret));
        return ret;
    }

    i2c_initialized = false;
    ESP_LOGI(TAG, "✓ I2C bus de-initialized");

    return ESP_OK;
}

i2c_port_t i2c_shared_get_port(void)
{
    return I2C_NUM;
}

