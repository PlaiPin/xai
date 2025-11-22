/**
 * @file streaming_chat_example.c
 * @brief Example of streaming chat completions with xAI Grok
 * 
 * This example demonstrates:
 * - Real-time streaming responses
 * - Incremental content delivery
 * - Callback-based async processing
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

static const char *TAG = "streaming_example";

// WiFi credentials - update these
#define WIFI_SSID      "your_wifi_ssid"
#define WIFI_PASSWORD  "your_wifi_password"

// xAI API key - get from console.x.ai
#define XAI_API_KEY_FALLBACK "your_xai_api_key_here"

/**
 * @brief Stream callback function
 * 
 * Called for each chunk of text as it arrives from the API
 */
static void stream_callback(const char *chunk, size_t length, void *user_data) {
    if (chunk == NULL) {
        // End of stream
        printf("\n[Stream ended]\n\n");
        return;
    }
    
    // Print chunk immediately (real-time output)
    printf("%.*s", (int)length, chunk);
    fflush(stdout);
}

/**
 * @brief WiFi event handler
 */
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Disconnected, retrying...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

/**
 * @brief Initialize WiFi in station mode
 */
static esp_err_t wifi_init(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization complete");
    return ESP_OK;
}

/**
 * @brief Main application task
 */
static void app_task(void *pvParameters) {
    // Wait for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Create xAI client
    ESP_LOGI(TAG, "Creating xAI client...");
    
    // Get API key from menuconfig, or use hardcoded fallback if empty
    const char *api_key = CONFIG_XAI_API_KEY;
    if (api_key == NULL || api_key[0] == '\0') {
        api_key = XAI_API_KEY_FALLBACK;
        ESP_LOGW(TAG, "Using hardcoded API key placeholder");
    }
    
    xai_client_t client = xai_create(api_key);
    if (!client) {
        ESP_LOGE(TAG, "Failed to create xAI client");
        vTaskDelete(NULL);
        return;
    }

    // Prepare message
    xai_message_t message = {
        .role = XAI_ROLE_USER,
        .content = "Write a haiku about embedded systems programming on ESP32",
        .name = NULL,
        .tool_call_id = NULL,
        .images = NULL,
        .image_count = 0,
        .tool_calls = NULL,
        .tool_call_count = 0
    };

    // Configure options for streaming
    xai_options_t options = xai_options_default();
    options.stream = true;
    options.temperature = 0.8f;
    options.max_tokens = 150;

    // Example 1: Simple streaming chat
    printf("\n=== Example 1: Streaming Chat ===\n");
    printf("User: %s\n", message.content);
    printf("Grok: ");
    
    xai_err_t err = xai_chat_completion_stream(
        client,
        &message,
        1,
        &options,
        stream_callback,
        NULL
    );

    if (err != XAI_OK) {
        ESP_LOGE(TAG, "Streaming failed: %s", xai_err_to_string(err));
    }

    // Example 2: Multi-turn streaming conversation
    printf("\n\n=== Example 2: Multi-turn Streaming ===\n");
    
    xai_message_t conversation[] = {
        {
            .role = XAI_ROLE_USER,
            .content = "Explain RTOS in one sentence",
            .name = NULL,
            .tool_call_id = NULL,
            .images = NULL,
            .image_count = 0,
            .tool_calls = NULL,
            .tool_call_count = 0
        },
        {
            .role = XAI_ROLE_ASSISTANT,
            .content = "A Real-Time Operating System (RTOS) is specialized software that manages hardware resources and schedules tasks with deterministic timing guarantees, ensuring critical operations meet strict deadlines in embedded systems.",
            .name = NULL,
            .tool_call_id = NULL,
            .images = NULL,
            .image_count = 0,
            .tool_calls = NULL,
            .tool_call_count = 0
        },
        {
            .role = XAI_ROLE_USER,
            .content = "Give me 3 examples of RTOS",
            .name = NULL,
            .tool_call_id = NULL,
            .images = NULL,
            .image_count = 0,
            .tool_calls = NULL,
            .tool_call_count = 0
        }
    };

    printf("Grok: ");
    err = xai_chat_completion_stream(
        client,
        conversation,
        3,
        &options,
        stream_callback,
        NULL
    );

    if (err != XAI_OK) {
        ESP_LOGE(TAG, "Streaming failed: %s", xai_err_to_string(err));
    }

    // Cleanup
    xai_destroy(client);
    
    ESP_LOGI(TAG, "Example complete");
    vTaskDelete(NULL);
}

void app_main(void) {
    ESP_LOGI(TAG, "xAI Streaming Chat Example");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi
    ESP_ERROR_CHECK(wifi_init());

    // Start application task
    xTaskCreate(app_task, "app_task", 8192, NULL, 5, NULL);
}

