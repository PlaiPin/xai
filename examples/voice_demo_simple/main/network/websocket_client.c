/**
 * @file websocket_client.c
 * @brief xAI Grok Voice API WebSocket client implementation
 */

#include "websocket_client.h"
#include "config/app_config.h"
#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "websocket_client";

// WebSocket client handle
static esp_websocket_client_handle_t client = NULL;

// Callbacks
static ws_audio_callback_t audio_callback = NULL;
static ws_status_callback_t status_callback = NULL;
static ws_transcript_callback_t transcript_callback = NULL;

// Message reassembly buffer (allocated in PSRAM)
static char *ws_reassembly_buffer = NULL;
static size_t ws_reassembly_len = 0;

// Connection state
static bool is_connected = false;

// Forward declarations
static void websocket_event_handler(void *handler_args, esp_event_base_t base, 
                                   int32_t event_id, void *event_data);

esp_err_t ws_init(const char *api_key,
                  ws_audio_callback_t audio_cb,
                  ws_status_callback_t status_cb,
                  ws_transcript_callback_t transcript_cb)
{
    if (!api_key || !audio_cb) {
        ESP_LOGE(TAG, "API key and audio callback are required");
        return ESP_ERR_INVALID_ARG;
    }

    if (client) {
        ESP_LOGW(TAG, "WebSocket already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing WebSocket client...");

    // Store callbacks
    audio_callback = audio_cb;
    status_callback = status_cb;
    transcript_callback = transcript_cb;

    // Allocate reassembly buffer in PSRAM
    ws_reassembly_buffer = (char *)heap_caps_malloc(WS_REASSEMBLY_SIZE, MALLOC_CAP_SPIRAM);
    if (!ws_reassembly_buffer) {
        ESP_LOGE(TAG, "Failed to allocate WebSocket buffer in PSRAM!");
        return ESP_ERR_NO_MEM;
    }
    ESP_LOGI(TAG, "Allocated %d KB WebSocket buffer in PSRAM", WS_REASSEMBLY_SIZE / 1024);

    // Configure WebSocket client with authentication
    char auth_header[1024];
    snprintf(auth_header, sizeof(auth_header), 
             "Authorization: Bearer %s\r\n"
             "Content-Type: application/json\r\n",
             api_key);

    esp_websocket_client_config_t websocket_cfg = {
        .uri = WEBSOCKET_URI,
        .headers = auth_header,
        .buffer_size = WS_BUFFER_SIZE,
        .cert_pem = NULL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .network_timeout_ms = 60000,
        .reconnect_timeout_ms = 15000,
    };

    ESP_LOGI(TAG, "Connecting to: %s", WEBSOCKET_URI);
    client = esp_websocket_client_init(&websocket_cfg);
    if (!client) {
        ESP_LOGE(TAG, "Failed to initialize WebSocket client");
        heap_caps_free(ws_reassembly_buffer);
        ws_reassembly_buffer = NULL;
        return ESP_FAIL;
    }

    esp_websocket_register_events(client, WEBSOCKET_EVENT_ANY, websocket_event_handler, NULL);
    esp_websocket_client_start(client);

    ESP_LOGI(TAG, "WebSocket client started");
    return ESP_OK;
}

esp_err_t ws_send_text_message(const char *message)
{
    if (!client || !is_connected) {
        ESP_LOGE(TAG, "WebSocket not connected");
        return ESP_FAIL;
    }

    if (!message) {
        ESP_LOGE(TAG, "Invalid message");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Sending text message: %s", message);

    // Create conversation item
    char text_message[512];
    snprintf(text_message, sizeof(text_message),
            "{"
            "\"type\":\"conversation.item.create\","
            "\"item\":{"
                "\"type\":\"message\","
                "\"role\":\"user\","
                "\"content\":[{\"type\":\"input_text\",\"text\":\"%s\"}]"
            "}}", message);
    
    int ret = esp_websocket_client_send_text(client, text_message, strlen(text_message), portMAX_DELAY);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to send text message");
        return ESP_FAIL;
    }

    // Small delay
    vTaskDelay(pdMS_TO_TICKS(100));

    // Request response
    const char *response_create = "{\"type\":\"response.create\"}";
    ret = esp_websocket_client_send_text(client, response_create, strlen(response_create), portMAX_DELAY);
    if (ret < 0) {
        ESP_LOGE(TAG, "Failed to request response");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Message sent, response requested");
    return ESP_OK;
}

bool ws_is_connected(void)
{
    return is_connected && client != NULL;
}

esp_err_t ws_disconnect(void)
{
    if (client) {
        esp_websocket_client_stop(client);
        is_connected = false;
        ESP_LOGI(TAG, "WebSocket disconnected");
        return ESP_OK;
    }
    return ESP_FAIL;
}

void ws_deinit(void)
{
    if (client) {
        esp_websocket_client_destroy(client);
        client = NULL;
    }

    if (ws_reassembly_buffer) {
        heap_caps_free(ws_reassembly_buffer);
        ws_reassembly_buffer = NULL;
    }

    audio_callback = NULL;
    status_callback = NULL;
    transcript_callback = NULL;
    is_connected = false;

    ESP_LOGI(TAG, "WebSocket deinitialized");
}

// WebSocket event handler
static void websocket_event_handler(void *handler_args, esp_event_base_t base, 
                                   int32_t event_id, void *event_data)
{
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;
    
    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        ESP_LOGI(TAG, "WebSocket connected!");
        is_connected = true;
        
        if (status_callback) {
            status_callback("connected");
        }
        
        // Send session configuration
        const char *session_config = 
            "{"
            "\"type\":\"session.update\","
            "\"session\":{"
                "\"voice\":\"" VOICE_NAME "\","
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
        ESP_LOGI(TAG, "Sent session config (voice=%s, audio=16kHz PCM)", VOICE_NAME);
        break;

    case WEBSOCKET_EVENT_DATA:
        if (data->data_len > 0) {
            // IMPORTANT:
            // - The Grok Voice API sends JSON text messages (opcode=0x1)
            // - The websocket layer can also deliver control frames (PING/PONG/CLOSE) which are NOT JSON.
            //   If we append those bytes into our JSON reassembly buffer, it becomes "poisoned" and
            //   future JSON parsing will fail (UI stuck in CONNECTING).
            // - Large JSON messages may be fragmented across multiple WEBSOCKET_EVENT_DATA events;
            //   use payload_len/payload_offset/fin to reassemble correctly.

            // Only parse text frames as JSON
            if (data->op_code != 0x01) {
                ESP_LOGD(TAG, "Ignoring non-text WS frame: opcode=%u len=%d", (unsigned)data->op_code, data->data_len);
                break;
            }

            // New payload start: drop any previous partial data
            if (data->payload_offset == 0) {
                ws_reassembly_len = 0;
            }

            if (data->payload_len <= 0) {
                ESP_LOGD(TAG, "Empty WS payload");
                break;
            }

            if ((size_t)data->payload_len >= WS_REASSEMBLY_SIZE) {
                ESP_LOGE(TAG, "WS payload too large: payload_len=%d max=%d", data->payload_len, WS_REASSEMBLY_SIZE);
                ws_reassembly_len = 0;
                if (status_callback) {
                    status_callback("error: ws payload too large");
                }
                break;
            }

            // Bounds check for this chunk
            if ((size_t)data->payload_offset + (size_t)data->data_len > WS_REASSEMBLY_SIZE) {
                ESP_LOGE(TAG, "WS chunk overflow: off=%d len=%d max=%d", data->payload_offset, data->data_len, WS_REASSEMBLY_SIZE);
                ws_reassembly_len = 0;
                if (status_callback) {
                    status_callback("error: ws chunk overflow");
                }
                break;
            }

            // Place this fragment into its proper offset.
            memcpy(ws_reassembly_buffer + data->payload_offset, data->data_ptr, data->data_len);
            size_t buffered = (size_t)data->payload_offset + (size_t)data->data_len;
            if (buffered > ws_reassembly_len) {
                ws_reassembly_len = buffered;
            }

            ESP_LOGD(TAG, "WS chunk: opcode=%u payload_len=%d off=%d len=%d buffered=%zu fin=%d",
                     (unsigned)data->op_code, data->payload_len, data->payload_offset, data->data_len,
                     ws_reassembly_len, data->fin ? 1 : 0);

            // Wait until complete payload has been received.
            if ((data->payload_offset + data->data_len) < data->payload_len || !data->fin) {
                break;
            }

            // Parse complete JSON payload
            cJSON *root = cJSON_ParseWithLength(ws_reassembly_buffer, (size_t)data->payload_len);
            if (!root) {
                ESP_LOGW(TAG, "Failed to parse complete JSON payload (len=%d). Dropping message.", data->payload_len);
                ws_reassembly_len = 0;
                if (status_callback) {
                    status_callback("error: ws json parse failed");
                }
                break;
            }

            ESP_LOGI(TAG, "✓ Reassembled JSON (%d bytes)", data->payload_len);
            ws_reassembly_len = 0;
            
            // Process JSON message
            cJSON *type = cJSON_GetObjectItem(root, "type");
            if (!type || !type->valuestring) {
                ESP_LOGW(TAG, "JSON missing 'type' field");
                cJSON_Delete(root);
                break;
            }
            
            const char *event_type = type->valuestring;
            ESP_LOGD(TAG, "Event: %s", event_type);
            
            // Handle different event types
            if (strcmp(event_type, "session.updated") == 0) {
                ESP_LOGI(TAG, "Session configured");
                
            } else if (strcmp(event_type, "response.created") == 0) {
                ESP_LOGI(TAG, "Response started");
                if (status_callback) {
                    status_callback("speaking");
                }
                
            } else if (strcmp(event_type, "ping") == 0) {
                // Application-level ping (JSON). Safe to ignore.
                ESP_LOGD(TAG, "Server ping event (JSON)");

            } else if (strcmp(event_type, "response.output_audio.delta") == 0) {
                // AUDIO DATA!
                cJSON *delta = cJSON_GetObjectItem(root, "delta");
                if (delta && delta->valuestring) {
                    size_t base64_len = strlen(delta->valuestring);
                    ESP_LOGI(TAG, "🔊 Audio delta (%zu bytes)", base64_len);
                    
                    if (audio_callback) {
                        audio_callback(delta->valuestring, base64_len);
                    }
                }
                
            } else if (strcmp(event_type, "response.output_audio_transcript.delta") == 0) {
                // Transcript text
                cJSON *delta = cJSON_GetObjectItem(root, "delta");
                if (delta && delta->valuestring && transcript_callback) {
                    transcript_callback(delta->valuestring);
                }
                
            } else if (strcmp(event_type, "response.output_audio_transcript.done") == 0) {
                ESP_LOGI(TAG, "Transcript complete");
                
            } else if (strcmp(event_type, "response.output_audio.done") == 0) {
                ESP_LOGI(TAG, "Audio stream complete");
                
            } else if (strcmp(event_type, "response.done") == 0) {
                ESP_LOGI(TAG, "✓ Response complete");
                if (status_callback) {
                    status_callback("done");
                }
            }
            
            cJSON_Delete(root);
        }
        break;

    case WEBSOCKET_EVENT_ERROR:
        ESP_LOGE(TAG, "WebSocket error");
        is_connected = false;
        if (status_callback) {
            status_callback("error: connection error");
        }
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "WebSocket disconnected");
        is_connected = false;
        if (status_callback) {
            status_callback("disconnected");
        }
        break;

    default:
        break;
    }
}

