/**
 * @file main.c
 * @brief Basic Chat Example for xAI ESP32 SDK
 * 
 * This example demonstrates:
 * - Initializing the xAI client
 * - Sending a simple chat message
 * - Receiving a response
 * - Proper cleanup
 * 
 * Hardware Required: ESP32 with WiFi
 * 
 * Setup:
 * 1. Set your WiFi credentials in menuconfig
 * 2. Set your xAI API key in menuconfig or code
 * 3. Build and flash: idf.py build flash monitor
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "xai.h"

static const char *TAG = "xai_example";

// WiFi credentials - EDIT THESE!
#define EXAMPLE_ESP_WIFI_SSID      "your_wifi_ssid"
#define EXAMPLE_ESP_WIFI_PASS      "your_wifi_password"

// xAI API key - EDIT THIS! (get yours from console.x.ai)
#define XAI_API_KEY_FALLBACK "your_xai_api_key_here"

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Retry connecting to WiFi...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
    }
}

static void wifi_init_sta(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "WiFi init complete. Connecting to %s...", EXAMPLE_ESP_WIFI_SSID);
}

static void xai_chat_example(void)
{
    ESP_LOGI(TAG, "Starting xAI chat example...");

    // Get API key from menuconfig, or use hardcoded fallback if empty
    const char *api_key = CONFIG_XAI_API_KEY;
    if (api_key == NULL || api_key[0] == '\0') {
        api_key = XAI_API_KEY_FALLBACK;
        ESP_LOGW(TAG, "Using hardcoded API key placeholder");
    }

    // Create xAI client
    xai_client_t client = xai_create(api_key);
    if (!client) {
        ESP_LOGE(TAG, "Failed to create xAI client");
        return;
    }

    ESP_LOGI(TAG, "xAI client created successfully");

    // Simple text completion
    char *response_text = NULL;
    size_t response_len = 0;
    xai_err_t err = xai_text_completion(
        client,
        "Hello! Tell me a fun fact about ESP32 in one sentence.",
        &response_text,
        &response_len
    );

    if (err == XAI_OK && response_text) {
        ESP_LOGI(TAG, "=== Response ===");
        ESP_LOGI(TAG, "%s", response_text);
        ESP_LOGI(TAG, "================");
        free(response_text);
    } else {
        ESP_LOGE(TAG, "Chat completion failed: %s", xai_err_to_string(err));
    }

    // Cleanup
    xai_destroy(client);
    ESP_LOGI(TAG, "xAI client destroyed");
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== xAI ESP32 SDK - Basic Chat Example ===");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi
    wifi_init_sta();

    // Wait for WiFi connection
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Run xAI example
    xai_chat_example();

    ESP_LOGI(TAG, "Example complete. Restarting in 10 seconds...");
    vTaskDelay(pdMS_TO_TICKS(10000));
    esp_restart();
}

