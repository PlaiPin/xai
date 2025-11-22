/**
 * @file image_gen_example.c
 * @brief Example of AI image generation with xAI Grok
 * 
 * Demonstrates text-to-image generation
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

static const char *TAG = "image_gen_example";

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

    // Example 1: Basic image generation
    printf("\n=== Example 1: Basic Image Generation ===\n");
    printf("Prompt: A futuristic ESP32 development board with RGB LEDs\n\n");
    
    xai_image_request_t request = {
        .prompt = "A futuristic ESP32 development board with RGB LEDs, high detail, technical diagram style",
        .model = NULL,  // Use default (grok-2-image-latest)
        .n = 1,
        .response_format = NULL,  // Default: "url"
        // Note: size, quality, style, user_id are NOT supported by xAI
        .size = NULL,
        .quality = NULL,
        .style = NULL,
        .user_id = NULL
    };
    
    xai_image_response_t response;
    memset(&response, 0, sizeof(response));
    
    xai_err_t err = xai_generate_image(client, &request, &response);
    
    if (err == XAI_OK) {
        printf("Generated %zu image(s):\n", response.image_count);
        for (size_t i = 0; i < response.image_count; i++) {
            if (response.images[i].url) {
                printf("  Image %zu URL: %s\n", i + 1, response.images[i].url);
            } else if (response.images[i].b64_json) {
                printf("  Image %zu: Base64 data (%zu bytes)\n", i + 1, 
                       strlen(response.images[i].b64_json));
            }
            
            if (response.images[i].revised_prompt) {
                printf("  Revised prompt: %s\n", response.images[i].revised_prompt);
            }
        }
        
        xai_image_response_free(&response);
    } else {
        ESP_LOGE(TAG, "Image generation failed: %s", xai_err_to_string(err));
    }

    // Example 2: Multiple images
    printf("\n\n=== Example 2: Generate Multiple Variations ===\n");
    printf("Prompt: IoT sensor node in a smart city\n\n");
    
    request.prompt = "IoT sensor node in a smart city, futuristic, technical illustration";
    request.n = 2;  // Generate 2 variations
    
    memset(&response, 0, sizeof(response));
    err = xai_generate_image(client, &request, &response);
    
    if (err == XAI_OK) {
        printf("Generated %zu variations:\n", response.image_count);
        for (size_t i = 0; i < response.image_count; i++) {
            printf("  Variation %zu: %s\n", i + 1, response.images[i].url);
        }
        xai_image_response_free(&response);
    }

    // Example 3: Detailed technical prompt
    printf("\n\n=== Example 3: Detailed Technical Diagram ===\n");
    
    request.prompt = "ESP32 microcontroller architecture diagram, detailed, professional, labeled components";
    request.n = 1;
    request.response_format = "url";
    
    memset(&response, 0, sizeof(response));
    err = xai_generate_image(client, &request, &response);
    
    if (err == XAI_OK) {
        if (response.image_count > 0) {
            if (response.images[0].url) {
                printf("Generated diagram: %s\n", response.images[0].url);
            }
            if (response.images[0].revised_prompt) {
                printf("Revised prompt: %s\n", response.images[0].revised_prompt);
            }
        }
        xai_image_response_free(&response);
    }

    /* 
     * Example 4: Base64 Response Format (COMMENTED OUT - Reference Only)
     * 
     * NOTE: Base64 image responses are ~17KB+, which exceeds the default 
     * HTTP response buffer size (16KB). This example is provided as a 
     * reference but is currently not practical for ESP32 due to:
     * 
     * 1. Response size: Base64-encoded JPEG images are 17KB+
     * 2. Buffer limit: Current HTTP client buffer is 16KB
     * 3. Memory constraints: Would require 32KB+ buffer allocation
     * 4. Limited utility: Even with larger buffer, decoding and displaying
     *    the image on ESP32 would require additional processing that exceeds
     *    typical ESP32 capabilities.
     * 
     * Recommendation: Use "url" format (Examples 1-3) for ESP32 applications.
     * The URL can be:
     * - Logged to SD card for later retrieval
     * - Sent to a mobile/web dashboard for display
     * - Encoded as a QR code for scanning
     * - Forwarded to a server for processing and resizing
     * 
     * If you need base64 format for a specific use case, increase the buffer
     * size in src/xai_http.c (line ~93) and ensure sufficient heap memory.
     */
    
    /*
    // Example 4: Base64 response format
    printf("\n\n=== Example 4: Base64 Response Format ===\n");
    
    request.prompt = "Embedded system circuit board, minimalist design, top view";
    request.n = 1;
    request.response_format = "b64_json";  // Get base64 data instead of URL
    
    memset(&response, 0, sizeof(response));
    err = xai_generate_image(client, &request, &response);
    
    if (err == XAI_OK) {
        if (response.image_count > 0 && response.images[0].b64_json) {
            printf("Received base64 encoded image (%zu bytes)\n", 
                   strlen(response.images[0].b64_json));
            printf("(You can decode and save this to flash/SD card)\n");
            if (response.images[0].revised_prompt) {
                printf("Revised prompt: %s\n", response.images[0].revised_prompt);
            }
        }
        xai_image_response_free(&response);
    }
    */

    xai_destroy(client);
    ESP_LOGI(TAG, "Example complete");
    vTaskDelete(NULL);
}

void app_main(void) {
    ESP_LOGI(TAG, "xAI Image Generation Example");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(wifi_init());
    xTaskCreate(app_task, "app_task", 12288, NULL, 5, NULL);
}

