/**
 * @file web_search_example.c
 * @brief Example of real-time web/X/news search with xAI Grok
 * 
 * Demonstrates search grounding and citation handling
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

static const char *TAG = "web_search_example";

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

    // Example 1: Simple web search
    printf("\n=== Example 1: Web Search ===\n");
    printf("Question: What are the latest ESP32 models released in 2024?\n\n");
    
    xai_response_t response;
    memset(&response, 0, sizeof(response));
    
    xai_err_t err = xai_web_search(
        client,
        "What are the latest ESP32 models released in 2024?",
        XAI_SEARCH_AUTO,
        true,  // return citations
        &response
    );
    
    if (err == XAI_OK) {
        if (response.content) {
            printf("Answer: %s\n\n", response.content);
        }
        
        // Display citations
        if (response.citation_count > 0) {
            printf("Sources (%zu):\n", response.citation_count);
            for (size_t i = 0; i < response.citation_count; i++) {
                printf("  [%zu] %s\n", i + 1, response.citations[i].url);
            }
        }
        
        xai_response_free(&response);
    } else {
        ESP_LOGE(TAG, "Search failed: %s", xai_err_to_string(err));
    }

    // Example 2: X (Twitter) search
    printf("\n\n=== Example 2: X Search ===\n");
    
    xai_search_params_t *search_params = xai_search_params_x(
        XAI_SEARCH_AUTO,
        true,  // citations
        NULL   // all handles
    );
    
    if (search_params) {
        xai_message_t message = {
            .role = XAI_ROLE_USER,
            .content = "What are people saying about ESP-IDF on X?",
            .name = NULL,
            .tool_call_id = NULL,
            .images = NULL,
            .image_count = 0,
            .tool_calls = NULL,
            .tool_call_count = 0
        };
        
        memset(&response, 0, sizeof(response));
        err = xai_chat_completion_with_search(client, &message, 1, search_params, &response);
        
        if (err == XAI_OK) {
            if (response.content) {
                printf("Answer: %s\n", response.content);
            }
            xai_response_free(&response);
        }
        
        xai_search_params_free(search_params);
    }

    // Example 3: News search
    printf("\n\n=== Example 3: News Search ===\n");
    
    search_params = xai_search_params_news(
        XAI_SEARCH_ON,   // always search
        true,
        "US"             // US news
    );
    
    if (search_params) {
        xai_message_t message = {
            .role = XAI_ROLE_USER,
            .content = "Latest IoT security news",
            .name = NULL,
            .tool_call_id = NULL,
            .images = NULL,
            .image_count = 0,
            .tool_calls = NULL,
            .tool_call_count = 0
        };
        
        memset(&response, 0, sizeof(response));
        err = xai_chat_completion_with_search(client, &message, 1, search_params, &response);
        
        if (err == XAI_OK) {
            if (response.content) {
                printf("Answer: %s\n", response.content);
            }
            xai_response_free(&response);
        }
        
        xai_search_params_free(search_params);
    }

    xai_destroy(client);
    ESP_LOGI(TAG, "Example complete");
    vTaskDelete(NULL);
}

void app_main(void) {
    ESP_LOGI(TAG, "xAI Web Search Example");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(wifi_init());
    xTaskCreate(app_task, "app_task", 12288, NULL, 5, NULL);
}

