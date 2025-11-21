/**
 * @file conversation_example.c
 * @brief Example of multi-turn conversations with xAI Grok
 * 
 * This example demonstrates:
 * - Using conversation helper API
 * - Automatic message history management
 * - Multi-turn stateful chat
 * - System prompts
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

static const char *TAG = "conversation_example";

// WiFi credentials
#define WIFI_SSID      "your_wifi_ssid"
#define WIFI_PASSWORD  "your_wifi_password"

// xAI API key
#define XAI_API_KEY_FALLBACK "your_xai_api_key_here"

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
    }
}

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

    return ESP_OK;
}

static void app_task(void *pvParameters) {
    ESP_LOGI(TAG, "Waiting for WiFi...");
    vTaskDelay(pdMS_TO_TICKS(5000));

    // Create xAI client
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

    // Example 1: Basic conversation with system prompt
    printf("\n=== Example 1: Tech Support Chatbot ===\n\n");
    
    xai_conversation_t conv = xai_conversation_create(
        "You are a helpful ESP32 technical support assistant. "
        "Provide concise, practical answers about ESP-IDF and embedded systems."
    );
    
    if (!conv) {
        ESP_LOGE(TAG, "Failed to create conversation");
        xai_destroy(client);
        vTaskDelete(NULL);
        return;
    }

    // Turn 1
    printf("User: How do I initialize WiFi on ESP32?\n");
    xai_conversation_add_user(conv, "How do I initialize WiFi on ESP32?");
    
    xai_response_t response;
    memset(&response, 0, sizeof(response));
    
    xai_err_t err = xai_conversation_complete(client, conv, &response);
    if (err == XAI_OK && response.content) {
        printf("Assistant: %s\n\n", response.content);
        xai_conversation_add_assistant(conv, response.content);
        xai_response_free(&response);
    } else {
        ESP_LOGE(TAG, "Conversation failed: %s", xai_err_to_string(err));
    }

    // Turn 2 - context is maintained
    printf("User: What about connecting to an access point?\n");
    xai_conversation_add_user(conv, "What about connecting to an access point?");
    
    memset(&response, 0, sizeof(response));
    err = xai_conversation_complete(client, conv, &response);
    if (err == XAI_OK && response.content) {
        printf("Assistant: %s\n\n", response.content);
        xai_conversation_add_assistant(conv, response.content);
        xai_response_free(&response);
    }

    // Turn 3 - still has full context
    printf("User: Show me error handling for that\n");
    xai_conversation_add_user(conv, "Show me error handling for that");
    
    memset(&response, 0, sizeof(response));
    err = xai_conversation_complete(client, conv, &response);
    if (err == XAI_OK && response.content) {
        printf("Assistant: %s\n\n", response.content);
        xai_response_free(&response);
    }

    // Cleanup first conversation
    xai_conversation_destroy(conv);

    // Example 2: Conversation with clearing
    printf("\n=== Example 2: Conversation Reset ===\n\n");
    
    conv = xai_conversation_create("You are a friendly AI assistant.");
    if (!conv) {
        xai_destroy(client);
        vTaskDelete(NULL);
        return;
    }

    printf("User: Tell me about Mars\n");
    xai_conversation_add_user(conv, "Tell me about Mars");
    
    memset(&response, 0, sizeof(response));
    err = xai_conversation_complete(client, conv, &response);
    if (err == XAI_OK && response.content) {
        printf("Assistant: %s\n\n", response.content);
        xai_conversation_add_assistant(conv, response.content);
        xai_response_free(&response);
    }

    // Clear history and start fresh
    printf("[Clearing conversation history]\n\n");
    xai_conversation_clear(conv);

    printf("User: What were we just talking about?\n");
    xai_conversation_add_user(conv, "What were we just talking about?");
    
    memset(&response, 0, sizeof(response));
    err = xai_conversation_complete(client, conv, &response);
    if (err == XAI_OK && response.content) {
        printf("Assistant: %s\n", response.content);
        printf("\n(Note: Assistant has no memory of Mars discussion)\n\n");
        xai_response_free(&response);
    }

    // Cleanup
    xai_conversation_destroy(conv);
    xai_destroy(client);
    
    ESP_LOGI(TAG, "Example complete");
    vTaskDelete(NULL);
}

void app_main(void) {
    ESP_LOGI(TAG, "xAI Conversation Example");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(wifi_init());

    xTaskCreate(app_task, "app_task", 8192, NULL, 5, NULL);
}

