/**
 * @file tools_example.c
 * @brief Example of client-side tool/function calling with xAI Grok
 * 
 * Demonstrates how to define tools, execute them locally, and integrate results
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
#include "driver/temperature_sensor.h"
#include "xai.h"
#include "cJSON.h"

static const char *TAG = "tools_example";

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

// Tool: Get ESP32 internal temperature
static char* tool_get_temperature(const char *args_json) {
    ESP_LOGI(TAG, "Executing tool: get_temperature");
    
    // Read ESP32 internal temperature sensor
    float temp_celsius = 35.5f;  // Placeholder - implement actual sensor reading
    
    // Return JSON result
    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "temperature_celsius", temp_celsius);
    cJSON_AddStringToObject(result, "unit", "celsius");
    cJSON_AddStringToObject(result, "sensor", "ESP32 internal");
    
    char *json_str = cJSON_PrintUnformatted(result);
    cJSON_Delete(result);
    return json_str;
}

// Tool: Get system memory info
static char* tool_get_memory(const char *args_json) {
    ESP_LOGI(TAG, "Executing tool: get_memory");
    
    size_t free_heap = esp_get_free_heap_size();
    size_t min_free = esp_get_minimum_free_heap_size();
    
    cJSON *result = cJSON_CreateObject();
    cJSON_AddNumberToObject(result, "free_heap_bytes", free_heap);
    cJSON_AddNumberToObject(result, "minimum_free_bytes", min_free);
    cJSON_AddNumberToObject(result, "free_heap_kb", free_heap / 1024);
    
    char *json_str = cJSON_PrintUnformatted(result);
    cJSON_Delete(result);
    return json_str;
}

// Tool: Control LED
static char* tool_control_led(const char *args_json) {
    ESP_LOGI(TAG, "Executing tool: control_led with args: %s", args_json);
    
    cJSON *args = cJSON_Parse(args_json);
    if (!args) {
        return strdup("{\"error\":\"Invalid arguments\"}");
    }
    
    cJSON *state_json = cJSON_GetObjectItem(args, "state");
    const char *state = cJSON_GetStringValue(state_json);
    
    ESP_LOGI(TAG, "LED state requested: %s", state ? state : "unknown");
    // Implement actual LED control here
    
    cJSON *result = cJSON_CreateObject();
    cJSON_AddStringToObject(result, "status", "success");
    cJSON_AddStringToObject(result, "led_state", state ? state : "unknown");
    
    char *json_str = cJSON_PrintUnformatted(result);
    cJSON_Delete(args);
    cJSON_Delete(result);
    return json_str;
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

    // Define available tools
    xai_tool_t tools[] = {
        {
            .name = "get_temperature",
            .description = "Get the current internal temperature of the ESP32 chip",
            .parameters_json = "{\"type\":\"object\",\"properties\":{},\"required\":[]}"
        },
        {
            .name = "get_memory",
            .description = "Get current free heap memory information",
            .parameters_json = "{\"type\":\"object\",\"properties\":{},\"required\":[]}"
        },
        {
            .name = "control_led",
            .description = "Control the LED state (on/off)",
            .parameters_json = "{\"type\":\"object\",\"properties\":{\"state\":{\"type\":\"string\",\"enum\":[\"on\",\"off\"],\"description\":\"LED state\"}},\"required\":[\"state\"]}"
        }
    };

    printf("\n=== Client-Side Tool Calling Example ===\n\n");
    
    // Initial user message
    xai_message_t user_msg = {
        .role = XAI_ROLE_USER,
        .content = "What's the current temperature of the ESP32? Also check the memory status.",
        .name = NULL,
        .tool_call_id = NULL,
        .images = NULL,
        .image_count = 0,
        .tool_calls = NULL,
        .tool_call_count = 0
    };
    
    xai_options_t options = xai_options_default();
    options.tools = tools;
    options.tool_count = 3;
    options.tool_choice = "auto";
    
    xai_response_t response;
    memset(&response, 0, sizeof(response));
    
    printf("User: %s\n\n", user_msg.content);
    
    xai_err_t err = xai_chat_completion(client, &user_msg, 1, &options, &response);
    
    if (err == XAI_OK) {
        // Check if model wants to call tools
        if (response.tool_call_count > 0) {
            printf("Model requested %zu tool call(s):\n", response.tool_call_count);
            
            // Execute all tool calls and collect results
            char **tool_results = malloc(sizeof(char*) * response.tool_call_count);
            if (!tool_results) {
                ESP_LOGE(TAG, "Failed to allocate memory for tool results");
                xai_response_free(&response);
                xai_destroy(client);
                vTaskDelete(NULL);
                return;
            }
            
            for (size_t i = 0; i < response.tool_call_count; i++) {
                xai_tool_call_t *call = &response.tool_calls[i];
                printf("  - %s(%s)\n", call->name, call->arguments);
                
                // Execute the tool locally
                if (strcmp(call->name, "get_temperature") == 0) {
                    tool_results[i] = tool_get_temperature(call->arguments);
                } else if (strcmp(call->name, "get_memory") == 0) {
                    tool_results[i] = tool_get_memory(call->arguments);
                } else if (strcmp(call->name, "control_led") == 0) {
                    tool_results[i] = tool_control_led(call->arguments);
                } else {
                    tool_results[i] = strdup("{\"error\":\"Unknown tool\"}");
                }
                
                if (tool_results[i]) {
                    printf("    Result: %s\n", tool_results[i]);
                }
            }
            
            printf("\n");
            
            // Build complete message history: user message + assistant response + all tool results
            size_t total_messages = 2 + response.tool_call_count;  // user + assistant + tool results
            xai_message_t *messages = malloc(sizeof(xai_message_t) * total_messages);
            
            if (!messages) {
                ESP_LOGE(TAG, "Failed to allocate memory for messages");
                for (size_t i = 0; i < response.tool_call_count; i++) {
                    if (tool_results[i]) free(tool_results[i]);
                }
                free(tool_results);
                xai_response_free(&response);
                xai_destroy(client);
                vTaskDelete(NULL);
                return;
            }
            
            // Message 1: Original user message
            messages[0] = user_msg;
            
            // Message 2: Assistant's response with tool calls
            messages[1].role = XAI_ROLE_ASSISTANT;
            messages[1].content = response.content;  // May be NULL
            messages[1].name = NULL;
            messages[1].tool_call_id = NULL;
            messages[1].images = NULL;
            messages[1].image_count = 0;
            messages[1].tool_calls = response.tool_calls;
            messages[1].tool_call_count = response.tool_call_count;
            
            // Messages 3+: Tool results
            for (size_t i = 0; i < response.tool_call_count; i++) {
                messages[2 + i].role = XAI_ROLE_TOOL;
                messages[2 + i].content = tool_results[i];
                messages[2 + i].name = response.tool_calls[i].name;
                messages[2 + i].tool_call_id = response.tool_calls[i].id;
                messages[2 + i].images = NULL;
                messages[2 + i].image_count = 0;
                messages[2 + i].tool_calls = NULL;
                messages[2 + i].tool_call_count = 0;
            }
            
            // Send all messages together for final response
            xai_response_t final_response;
            memset(&final_response, 0, sizeof(final_response));
            
            err = xai_chat_completion(client, messages, total_messages, &options, &final_response);
            
            if (err == XAI_OK && final_response.content) {
                printf("Assistant: %s\n", final_response.content);
                xai_response_free(&final_response);
            } else {
                ESP_LOGE(TAG, "Final completion failed: %s", xai_err_to_string(err));
            }
            
            // Cleanup
            for (size_t i = 0; i < response.tool_call_count; i++) {
                if (tool_results[i]) free(tool_results[i]);
            }
            free(tool_results);
            free(messages);
        } else if (response.content) {
            printf("Assistant: %s\n", response.content);
        }
        
        xai_response_free(&response);
    } else {
        ESP_LOGE(TAG, "Chat failed: %s", xai_err_to_string(err));
    }

    xai_destroy(client);
    ESP_LOGI(TAG, "Example complete");
    vTaskDelete(NULL);
}

void app_main(void) {
    ESP_LOGI(TAG, "xAI Tools Example");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_ERROR_CHECK(wifi_init());
    xTaskCreate(app_task, "app_task", 12288, NULL, 5, NULL);
}

