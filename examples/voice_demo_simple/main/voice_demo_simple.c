/**
 * @file voice_demo_simple.c
 * @brief WebSocket Realtime Voice Demo with I2S Audio Playback
 * 
 * This example demonstrates the xAI Grok Voice API using WebSocket:
 * 1. Connect to wss://api.x.ai/v1/realtime
 * 2. Send text message to Grok
 * 3. Receive PCM audio response in real-time
 * 4. Decode base64 and play through I2S speaker
 * 
 * Hardware Requirements:
 * - Waveshare ESP32-S3-Touch-AMOLED-1.75
 * - ES8311 audio codec (on-board)
 * - WiFi connection
 * 
 * Note: This example is configured specifically for the Waveshare board.
 * For other boards, adjust the GPIO pin definitions and codec initialization.
 * 
 * @copyright 2025
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"
#include "mbedtls/base64.h"
#include "cJSON.h"

// ESP Codec Device Framework
#include "driver/i2c.h"
#include "es8311.h"

static const char *TAG = "voice_demo";

// WiFi credentials - EDIT THESE!
#define WIFI_SSID      "WIFI_SSID"
#define WIFI_PASSWORD  "WIFI_PASSWORD"

// xAI API key - EDIT THIS! (get yours from console.x.ai)
#define XAI_API_KEY    "XAI_API_KEY"
#define WEBSOCKET_URI  "wss://api.x.ai/v1/realtime"

// I2S Configuration - Waveshare ESP32-S3-Touch-AMOLED-1.75 (from their 06_I2SCodec example)
#define I2S_NUM         (I2S_NUM_0)
#define I2S_MCLK_IO     (GPIO_NUM_42)  // Master clock (required for ES8311)
#define I2S_BCK_IO      (GPIO_NUM_9)   // Bit clock
#define I2S_WS_IO       (GPIO_NUM_45)  // Word select (LRCK) 
#define I2S_DO_IO       (GPIO_NUM_8)   // Data out (to ES8311)
#define I2S_DI_IO       (GPIO_NUM_10)  // Data in (from ES8311, for mic if needed)
#define I2S_SAMPLE_RATE (16000)        // 16kHz - matches xAI voice API
#define I2S_MCLK_MULTIPLE (384)        // From Waveshare example - critical for ES8311
#define I2S_MCLK_FREQ_HZ (I2S_SAMPLE_RATE * I2S_MCLK_MULTIPLE)
#define I2S_DMA_BUF_COUNT (6)          // From Waveshare example
#define I2S_DMA_BUF_LEN   (1200)       // From Waveshare example

// ES8311 Audio Codec Configuration
#define I2C_NUM         (I2C_NUM_0)
#define I2C_SDA_IO      (GPIO_NUM_15)
#define I2C_SCL_IO      (GPIO_NUM_14)
#define I2C_FREQ_HZ     (100000)
#define ES8311_ADDR     (0x18)
#define PA_ENABLE_GPIO  (GPIO_NUM_46)  // Power amplifier enable


// Audio buffer configuration
// 20480 samples = 40KB buffer for decoded PCM audio
// Server sends chunks up to ~30KB decoded, need headroom for largest deltas
#define AUDIO_BUFFER_SIZE (20480)

// WebSocket message buffering for fragmented messages
// 48KB buffer - balances message size handling with RAM constraints
// Some audio deltas can exceed 32KB (especially at 16kHz with longer durations)
#define WS_BUFFER_SIZE (49152)

// Event group bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;
static esp_websocket_client_handle_t client = NULL;

// I2S and ES8311 handles (using Waveshare's direct approach)
static i2s_chan_handle_t tx_handle = NULL;  // I2S TX channel for audio output
static es8311_handle_t es8311_handle = NULL;  // ES8311 codec handle

// Buffer for handling fragmented WebSocket messages
static char ws_message_buffer[WS_BUFFER_SIZE] = {0};
static size_t ws_buffer_len = 0;

// Audio buffer for PCM decoding (40KB - too large for stack!)
// Declared static to avoid stack overflow
static int16_t pcm_buffer[AUDIO_BUFFER_SIZE];

// ============================================================================
// WiFi Setup
// ============================================================================

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
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

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
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi SSID:%s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
    }
}

// ============================================================================
// ES8311 Audio Codec Initialization (Waveshare 06_I2SCodec approach)
// ============================================================================

static void codec_init(void)
{
    ESP_LOGI(TAG, "Initializing audio codec (Waveshare method)...");

    // 1. Enable PA (Power Amplifier) - must be done first
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << PA_ENABLE_GPIO),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(PA_ENABLE_GPIO, 1);  // Enable PA
    ESP_LOGI(TAG, "PA enabled on GPIO %d", PA_ENABLE_GPIO);

    // 2. Initialize I2C (legacy driver, matching Waveshare 06_I2SCodec)
    i2c_config_t es_i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_IO,
        .scl_io_num = I2C_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM, &es_i2c_cfg));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM, I2C_MODE_MASTER, 0, 0, 0));
    ESP_LOGI(TAG, "I2C initialized (SDA=%d, SCL=%d)", I2C_SDA_IO, I2C_SCL_IO);

    // 3. Create and initialize ES8311 codec
    es8311_handle = es8311_create(I2C_NUM, ES8311_ADDRRES_0);
    if (!es8311_handle) {
        ESP_LOGE(TAG, "Failed to create ES8311 handle");
        return;
    }

    es8311_clock_config_t es_clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = I2S_MCLK_FREQ_HZ,
        .sample_frequency = I2S_SAMPLE_RATE,
    };
    ESP_ERROR_CHECK(es8311_init(es8311_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16));
    ESP_ERROR_CHECK(es8311_sample_frequency_config(es8311_handle, I2S_SAMPLE_RATE * I2S_MCLK_MULTIPLE, I2S_SAMPLE_RATE));
    ESP_ERROR_CHECK(es8311_voice_volume_set(es8311_handle, 80, NULL));  // 80% volume
    ESP_ERROR_CHECK(es8311_microphone_config(es8311_handle, false));  // Disable mic
    ESP_LOGI(TAG, "ES8311 codec initialized");

    // 4. Initialize I2S driver (matching Waveshare 06_I2SCodec)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;  // Auto clear legacy data in DMA buffer
    chan_cfg.dma_desc_num = I2S_DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = I2S_DMA_BUF_LEN;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));  // TX only

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_IO,
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din = I2S_GPIO_UNUSED,  // No mic input
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE;  // Override with 384

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_LOGI(TAG, "I2S initialized and enabled - MONO, %d Hz, MCLK multiple=%d", I2S_SAMPLE_RATE, I2S_MCLK_MULTIPLE);
    
    ESP_LOGI(TAG, "Audio codec ready for playback (mono mode)");
}

// ============================================================================
// Base64 Decoder Helper
// ============================================================================

static int base64_decode_audio(const char *base64_data, size_t base64_len, 
                               int16_t *pcm_out, size_t pcm_out_size)
{
    if (!base64_data || base64_len == 0) {
        ESP_LOGE(TAG, "Invalid base64 input");
        return -1;
    }
    
    // Validate base64 length (should be multiple of 4 when properly padded)
    if (base64_len % 4 != 0) {
        ESP_LOGW(TAG, "Base64 length %zu not multiple of 4 (may need padding)", base64_len);
    }
    
    // Log first/last few chars for debugging
    ESP_LOGD(TAG, "Base64 decode: first chars='%.20s...', last chars='...%.20s'", 
             base64_data, 
             base64_len > 20 ? base64_data + base64_len - 20 : base64_data);
    
    size_t decoded_len = 0;
    unsigned char *decoded_buffer = (unsigned char *)pcm_out;
    
    int ret = mbedtls_base64_decode(decoded_buffer, pcm_out_size * sizeof(int16_t), 
                                     &decoded_len, (const unsigned char *)base64_data, 
                                     base64_len);
    
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 decode failed: %d (len=%zu, first='%c', last='%c')", 
                 ret, base64_len, 
                 base64_data[0], 
                 base64_data[base64_len - 1]);
        
        // Log more details for debugging
        if (ret == -0x002A) {  // MBEDTLS_ERR_BASE64_INVALID_CHARACTER
            ESP_LOGE(TAG, "Invalid character in base64 string");
            // Check for common issues
            bool has_whitespace = false;
            bool has_newline = false;
            for (size_t i = 0; i < base64_len && i < 100; i++) {
                if (base64_data[i] == ' ' || base64_data[i] == '\t') has_whitespace = true;
                if (base64_data[i] == '\n' || base64_data[i] == '\r') has_newline = true;
            }
            if (has_whitespace) ESP_LOGE(TAG, "Base64 contains whitespace");
            if (has_newline) ESP_LOGE(TAG, "Base64 contains newlines");
        }
        return -1;
    }
    
    ESP_LOGD(TAG, "Base64 decoded: %zu bytes → %zu PCM samples", base64_len, decoded_len / sizeof(int16_t));
    
    return decoded_len / sizeof(int16_t);  // Return number of PCM samples
}

// ============================================================================
// WebSocket Event Handler
// ============================================================================

static void websocket_event_handler(void *handler_args, esp_event_base_t base, 
                                   int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected!");
        
        // Send session configuration (per Grok Voice API docs)
        // Note: 'modalities' field does NOT exist in the Voice API - removed
        // Using 16kHz to reduce message size by 33% (helps with fragmentation)
        const char *session_config = 
            "{"
            "\"type\":\"session.update\","
            "\"session\":{"
                "\"voice\":\"Ara\","
                "\"instructions\":\"You are a helpful AI assistant. Be concise.\","
                "\"turn_detection\":{\"type\":\"server_vad\"},"
                "\"audio\":{"
                    "\"input\":{"
                        "\"format\":{\"type\":\"audio/pcm\",\"rate\":16000}"
                    "},"
                    "\"output\":{"
                        "\"format\":{\"type\":\"audio/pcm\",\"rate\":16000}"
                    "}"
                "}"
            "}}";
        
        esp_websocket_client_send_text(client, session_config, strlen(session_config), portMAX_DELAY);
        ESP_LOGI(TAG, "Sent session config (voice=Ara, turn_detection=server_vad, audio=16kHz PCM)");
        
        // Wait a bit for session to be configured
        vTaskDelay(pdMS_TO_TICKS(500));
        
        // Send a text message to Grok
        const char *text_message = 
            "{"
            "\"type\":\"conversation.item.create\","
            "\"item\":{"
                "\"type\":\"message\","
                "\"role\":\"user\","
                "\"content\":[{\"type\":\"input_text\",\"text\":\"Hello! Tell me a short joke.\"}]"
            "}}";
        
        esp_websocket_client_send_text(client, text_message, strlen(text_message), portMAX_DELAY);
        ESP_LOGI(TAG, "Sent text message");
        
        // Request response
        const char *response_create = "{\"type\":\"response.create\"}";
        esp_websocket_client_send_text(client, response_create, strlen(response_create), portMAX_DELAY);
        ESP_LOGI(TAG, "Requested response");
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data->data_len > 0) {
            char *json_str = NULL;
            cJSON *root = NULL;
            
            // Strategy: Keep buffering until we have valid JSON
            // Don't assume we know when "final fragment" arrives
            
            // If we're already buffering, continue accumulating
            if (ws_buffer_len > 0) {
                // Continue buffering
                if (ws_buffer_len + data->data_len < WS_BUFFER_SIZE) {
                    memcpy(ws_message_buffer + ws_buffer_len, data->data_ptr, data->data_len);
                    ws_buffer_len += data->data_len;
                    ws_message_buffer[ws_buffer_len] = '\0';
                    
                    // Try to parse the accumulated buffer
                    root = cJSON_Parse(ws_message_buffer);
                    if (root) {
                        // Success! We have a complete message
                        ESP_LOGI(TAG, "✓ Reassembled message (%zu bytes)", ws_buffer_len);
                        ws_buffer_len = 0; // Clear buffer for next message
                        // Continue to message processing below
                    } else {
                        // Not complete yet, keep buffering
                        ESP_LOGD(TAG, "Still buffering... (%zu bytes accumulated)", ws_buffer_len);
                        break; // Wait for more data
                    }
                } else {
                    // Buffer overflow - message too large or corrupted
                    ESP_LOGW(TAG, "Buffer overflow at %zu bytes! Discarding.", ws_buffer_len);
                    ws_buffer_len = 0;
                    break;
                }
            } else {
                // No buffered data - try to parse this chunk directly
                json_str = (char *)malloc(data->data_len + 1);
                if (!json_str) {
                    ESP_LOGE(TAG, "Failed to allocate memory for JSON");
                    break;
                }
                
                memcpy(json_str, data->data_ptr, data->data_len);
                json_str[data->data_len] = '\0';
                
                root = cJSON_Parse(json_str);
                if (!root) {
                    // Parse failed - this is likely the start of a fragmented message
                    // Start buffering it
                    if (data->data_len < WS_BUFFER_SIZE) {
                        memcpy(ws_message_buffer, data->data_ptr, data->data_len);
                        ws_buffer_len = data->data_len;
                        ws_message_buffer[ws_buffer_len] = '\0';
                        ESP_LOGD(TAG, "Started buffering (%d bytes)", data->data_len);
                    } else {
                        ESP_LOGW(TAG, "First chunk too large (%d bytes)!", data->data_len);
                    }
                    free(json_str);
                    break;
                }
                // Parsed successfully as standalone message
                // Continue to message processing below
            }
            
            // At this point, 'root' is always valid
            cJSON *type = cJSON_GetObjectItem(root, "type");
            if (!type || !type->valuestring) {
                ESP_LOGW(TAG, "JSON missing 'type' field");
                cJSON_Delete(root);
                if (json_str) free(json_str);
                break;
            }
            
            // Log all event types for debugging
            ESP_LOGI(TAG, "Event: %s", type->valuestring);
            
            // Log full session.updated to verify configuration
            if (strcmp(type->valuestring, "session.updated") == 0) {
                cJSON *session = cJSON_GetObjectItem(root, "session");
                if (session) {
                    char *session_str = cJSON_PrintUnformatted(session);
                    if (session_str) {
                        ESP_LOGI(TAG, "Session accepted config: %s", session_str);
                        free(session_str);
                    }
                }
            }
            
            // Handle audio delta - THIS IS THE ACTUAL AUDIO DATA
            if (strcmp(type->valuestring, "response.output_audio.delta") == 0) {
                cJSON *delta = cJSON_GetObjectItem(root, "delta");
                if (!delta || !delta->valuestring) {
                    ESP_LOGW(TAG, "Audio delta missing 'delta' field");
                } else {
                    // base64_str is guaranteed valid by the check above
                    const char *base64_str = delta->valuestring;
                    size_t base64_len = strlen(base64_str);
                    ESP_LOGI(TAG, "🔊 Received audio delta (base64 len: %zu)", base64_len);
                    
                    // DIAGNOSTIC: Log first 100 chars (only if string is valid)
                    if (base64_len > 0 && base64_len < 200000) {  // Sanity check
                        char preview[101];
                        size_t preview_len = base64_len < 100 ? base64_len : 100;
                        memcpy(preview, base64_str, preview_len);
                        preview[preview_len] = '\0';
                        ESP_LOGI(TAG, "Base64 preview: %.100s", preview);
                    }
                    
                    // Quick validation: check first/last chars are valid base64
                    // (Full scan removed to reduce logging overhead and prevent crashes)
                    
                    // Validate base64 string
                    if (base64_len == 0) {
                        ESP_LOGW(TAG, "Empty base64 string");
                    } else if (base64_len > 100000) {
                        ESP_LOGW(TAG, "Suspiciously large base64 string: %zu bytes", base64_len);
                    } else {
                        // Decode base64 audio into static buffer (not on stack!)
                        int samples = base64_decode_audio(base64_str, 
                                                          base64_len,
                                                          pcm_buffer, 
                                                          AUDIO_BUFFER_SIZE);
                        
                        if (samples > 0) {
                            // Write mono PCM directly to I2S (configured as mono)
                            size_t bytes_written = 0;
                            esp_err_t err = i2s_channel_write(tx_handle, pcm_buffer, 
                                                              samples * sizeof(int16_t),
                                                              &bytes_written, portMAX_DELAY);
                            
                            if (err == ESP_OK) {
                                ESP_LOGI(TAG, "✓ Played %d mono samples (%zu bytes written)", 
                                        samples, bytes_written);
                            } else {
                                ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(err));
                            }
                        } else {
                            ESP_LOGE(TAG, "Failed to decode audio (samples=%d)", samples);
                        }
                    }
                }
            }
            // Handle transcript delta (text of what's being spoken)
            else if (strcmp(type->valuestring, "response.output_audio_transcript.delta") == 0) {
                cJSON *delta = cJSON_GetObjectItem(root, "delta");
                if (delta && delta->valuestring) {
                    printf("%s", delta->valuestring);
                    fflush(stdout);
                }
            }
            // Handle transcript done
            else if (strcmp(type->valuestring, "response.output_audio_transcript.done") == 0) {
                printf("\n");
                ESP_LOGI(TAG, "Audio transcript complete");
            }
            // Handle audio done
            else if (strcmp(type->valuestring, "response.output_audio.done") == 0) {
                ESP_LOGI(TAG, "Audio stream complete");
            }
            // Handle response done
            else if (strcmp(type->valuestring, "response.done") == 0) {
                ESP_LOGI(TAG, "✓ Response complete!");
            }
            // Log other important events
            else if (strcmp(type->valuestring, "response.created") == 0) {
                cJSON *response = cJSON_GetObjectItem(root, "response");
                if (response) {
                    cJSON *id = cJSON_GetObjectItem(response, "id");
                    if (id && id->valuestring) {
                        ESP_LOGI(TAG, "Response started (id: %s)", id->valuestring);
                    }
                }
            }
            
            cJSON_Delete(root);
            if (json_str) free(json_str);
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error");
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WebSocket disconnected");
        break;

    default:
        break;
    }
}

// ============================================================================
// Main Application
// ============================================================================

void app_main(void)
{
    ESP_LOGI(TAG, "xAI Grok Voice WebSocket Demo");
    ESP_LOGI(TAG, "================================");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi
    wifi_init_sta();

    // Initialize audio codec using ESP Codec Dev framework
    codec_init();    // Handles I2C, I2S, ES8311, PA, and mono→stereo conversion

    // Configure WebSocket client with proper headers
    char auth_header[1024];
    snprintf(auth_header, sizeof(auth_header), 
             "Authorization: Bearer %s\r\n"
             "Content-Type: application/json\r\n",
             XAI_API_KEY);

    esp_websocket_client_config_t websocket_cfg = {
        .uri = WEBSOCKET_URI,
        .headers = auth_header,
        .buffer_size = 4096,
        .cert_pem = NULL,  // Use certificate bundle
        .crt_bundle_attach = esp_crt_bundle_attach,  // Enable certificate verification
        .network_timeout_ms = 60000,  // 60 seconds (for very weak WiFi signals)
        .reconnect_timeout_ms = 15000,
    };

    ESP_LOGI(TAG, "Connecting to WebSocket: %s", WEBSOCKET_URI);
    client = esp_websocket_client_init(&websocket_cfg);
    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);
    
    esp_websocket_client_start(client);

    ESP_LOGI(TAG, "WebSocket client started. Waiting for audio...");
    
    // Keep running
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
