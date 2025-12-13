/**
 * @file wifi_manager.h
 * @brief WiFi connection management for xAI Voice Demo
 */

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and connect to WiFi in station mode
 * 
 * This function blocks until WiFi is connected or fails.
 * 
 * @param ssid WiFi network SSID
 * @param password WiFi network password
 * @return ESP_OK on success, ESP_FAIL on failure
 */
esp_err_t wifi_init_sta(const char *ssid, const char *password);

/**
 * @brief Check if WiFi is currently connected
 * 
 * @return true if connected, false otherwise
 */
bool wifi_is_connected(void);

/**
 * @brief Disconnect from WiFi and cleanup
 */
void wifi_disconnect(void);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H

