/**
 * @file vision_example.c
 * @brief Example of vision capabilities with xAI Grok
 * 
 * Demonstrates image analysis using grok-2-vision models
 * 
 * @copyright 2025
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "xai.h"

static const char *TAG = "vision_example";

#define WIFI_SSID      "your_wifi_ssid"
#define WIFI_PASSWORD  "your_wifi_password"
#define XAI_API_KEY_FALLBACK "your_xai_api_key_here"

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Connected");
    }
}

static esp_err_t wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    wifi_config_t wifi_config = {.sta = {.ssid = WIFI_SSID, .password = WIFI_PASSWORD}};
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    return ESP_OK;
}

static void app_task(void *pvParameters) {
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Get API key from menuconfig, or use hardcoded fallback if empty
    const char *api_key = CONFIG_XAI_API_KEY;
    if (api_key == NULL || api_key[0] == '\0') {
        api_key = XAI_API_KEY_FALLBACK;
        ESP_LOGW(TAG, "Using hardcoded API key placeholder");
    }
    
    xai_client_t client = xai_create(api_key);
    if (!client) {
        ESP_LOGE(TAG, "Failed to create client");
        vTaskDelete(NULL);
        return;
    }

    // Example 1: Analyze image from URL
    printf("\n=== Example 1: Image Analysis ===\n");
    
    xai_image_t image = {
        .url = "https://imgen.x.ai/xai-imgen/xai-tmp-imgen-72c705ee-b983-489e-b97e-d607bd14198c.jpeg",  // Replace with actual image URL
        .data = NULL,
        .data_len = 0,
        .detail = "auto"
    };
    
    xai_response_t response;
    memset(&response, 0, sizeof(response));
    
    xai_err_t err = xai_vision_completion(
        client,
        "Describe this ESP32 development board in detail. What components can you see?",
        &image,
        1,
        &response
    );
    
    if (err == XAI_OK) {
        if (response.content) {
            printf("Analysis: %s\n", response.content);
        }
        xai_response_free(&response);
    } else {
        ESP_LOGE(TAG, "Vision completion failed: %s", xai_err_to_string(err));
    }

    // Example 2: Multi-modal conversation
    printf("\n\n=== Example 2: Multi-modal Conversation ===\n");
    
    xai_message_t message = {
        .role = XAI_ROLE_USER,
        .content = "What sensors are visible on this development board?",
        .name = NULL,
        .tool_call_id = NULL,
        .images = &image,
        .image_count = 1,
        .tool_calls = NULL,
        .tool_call_count = 0
    };
    
    xai_options_t options = xai_options_default();
    options.model = "grok-2-vision-latest";  // Use vision model
    options.temperature = 0.7f;
    
    memset(&response, 0, sizeof(response));
    err = xai_chat_completion(client, &message, 1, &options, &response);
    
    if (err == XAI_OK) {
        if (response.content) {
            printf("Response: %s\n", response.content);
        }
        xai_response_free(&response);
    }

    // Example 3: Multiple images
    printf("\n\n=== Example 3: Compare Images ===\n");
    
    xai_image_t images[2] = {
        {
            .url = "https://www.espressif.com/sites/default/files/dev-board/ESP32-C61-DevKitC-1_L_0.png",
            .data = NULL,
            .data_len = 0,
            .detail = "auto"
        },
        {
            .url = "https://www.sparkfun.com/media/catalog/product/cache/a793f13fd3d678cea13d28206895ba0c/E/S/ESP-Module-Programmer-Feature-2.jpg",
            .data = NULL,
            .data_len = 0,
            .detail = "auto"
        }
    };
    
    xai_message_t compare_message = {
        .role = XAI_ROLE_USER,
        .content = "Compare these two ESP32 boards. What are the key differences?",
        .name = NULL,
        .tool_call_id = NULL,
        .images = images,
        .image_count = 2,
        .tool_calls = NULL,
        .tool_call_count = 0
    };
    
    memset(&response, 0, sizeof(response));
    err = xai_chat_completion(client, &compare_message, 1, &options, &response);
    
    if (err == XAI_OK) {
        if (response.content) {
            printf("Comparison: %s\n", response.content);
        }
        xai_response_free(&response);
    }

    xai_destroy(client);
    ESP_LOGI(TAG, "Example complete");
    vTaskDelete(NULL);
}

void app_main(void) {
    ESP_LOGI(TAG, "xAI Vision Example");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(wifi_init());
    xTaskCreate(app_task, "app_task", 10240, NULL, 5, NULL);
}

