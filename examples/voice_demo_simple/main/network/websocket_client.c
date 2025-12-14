/**
 * @file websocket_client.c
 * @brief Compatibility wrapper for the voice demo example.
 *
 * IMPORTANT:
 * The actual Grok Voice Realtime WebSocket implementation lives in the xAI SDK:
 *   - `xai/include/xai_voice_realtime.h`
 *   - `xai/src/xai_voice_realtime.c`
 *
 * This example-level file preserves the demo's existing `ws_*` API by delegating
 * to the SDK client.
 */

#include "websocket_client.h"
#include "config/app_config.h"
#include "esp_log.h"
#include "xai_voice_realtime.h"

#include <string.h>
#include <stdlib.h>

static const char *TAG = "websocket_client";

// Demo callbacks
static ws_audio_callback_t audio_callback = NULL;
static ws_status_callback_t status_callback = NULL;
static ws_transcript_callback_t transcript_callback = NULL;

// SDK client
static xai_voice_client_t voice = NULL;

static void sdk_on_state(xai_voice_client_t client, xai_voice_state_t state, const char *detail, void *user_ctx)
{
    (void)client;
    (void)user_ctx;

    if (!status_callback) return;

    switch (state) {
    case XAI_VOICE_STATE_CONNECTED:
        status_callback("connected");
        break;
    case XAI_VOICE_STATE_SESSION_READY:
        status_callback("ready");
        break;
    case XAI_VOICE_STATE_TURN_STARTED:
        status_callback("speaking");
        break;
    case XAI_VOICE_STATE_TURN_DONE:
        status_callback("done");
        break;
    case XAI_VOICE_STATE_DISCONNECTED:
        status_callback("disconnected");
        break;
    case XAI_VOICE_STATE_ERROR:
        if (detail && detail[0]) {
            char buf[128];
            snprintf(buf, sizeof(buf), "error: %s", detail);
            status_callback(buf);
        } else {
            status_callback("error: unknown");
        }
        break;
    default:
        break;
    }
}

static void sdk_on_transcript(xai_voice_client_t client, const char *utf8, size_t len, void *user_ctx)
{
    (void)client;
    (void)user_ctx;
    if (transcript_callback && utf8 && len) {
        // Ensure null-termination for callback expectations
        char tmp[256];
        size_t n = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
        memcpy(tmp, utf8, n);
        tmp[n] = '\0';
        transcript_callback(tmp);
    }
}

static void sdk_on_pcm16(xai_voice_client_t client, const int16_t *samples, size_t sample_count, int sample_rate_hz, void *user_ctx)
{
    (void)client;
    (void)user_ctx;
    (void)sample_rate_hz;
    if (audio_callback && samples && sample_count) {
        audio_callback(samples, sample_count, sample_rate_hz);
    }
}

esp_err_t ws_init(const char *api_key,
                  ws_audio_callback_t audio_cb,
                  ws_status_callback_t status_cb,
                  ws_transcript_callback_t transcript_cb)
{
    if (!api_key || !audio_cb) {
        ESP_LOGE(TAG, "API key and audio callback are required");
        return ESP_ERR_INVALID_ARG;
    }
    if (voice) {
        ESP_LOGW(TAG, "WebSocket already initialized");
        return ESP_OK;
    }

    audio_callback = audio_cb;
    status_callback = status_cb;
    transcript_callback = transcript_cb;

    xai_voice_config_t cfg = {
        .uri = WEBSOCKET_URI,
        .api_key = api_key,
        .network_timeout_ms = 60000,
        .reconnect_timeout_ms = 15000,
        .ws_rx_buffer_size = WS_BUFFER_SIZE,
        .max_message_size = WS_REASSEMBLY_SIZE,
        .pcm_buffer_bytes = 128 * 1024,
        .prefer_psram = true,
        .queue_turn_before_ready = true,
        .session = {
            .voice = VOICE_NAME,
            .instructions = "You are a helpful AI assistant. Be concise.",
            .sample_rate_hz = 16000,
            .server_vad = true,
        },
    };

    xai_voice_callbacks_t cbs = {
        .on_state = sdk_on_state,
        .on_transcript_delta = sdk_on_transcript,
        .on_pcm16 = sdk_on_pcm16,
        .on_event_json = NULL,
    };

    voice = xai_voice_client_create(&cfg, &cbs, NULL);
    if (!voice) {
        ESP_LOGE(TAG, "Failed to create xAI voice realtime client");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Connecting to: %s", WEBSOCKET_URI);
    xai_err_t err = xai_voice_client_connect(voice);
    if (err != XAI_OK) {
        ESP_LOGE(TAG, "Failed to start voice client: %s", xai_err_to_string(err));
        xai_voice_client_destroy(voice);
        voice = NULL;
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "WebSocket client started (SDK)");
    return ESP_OK;
}

esp_err_t ws_send_text_message(const char *message)
{
    if (!voice) {
        ESP_LOGE(TAG, "WebSocket client not initialized");
        return ESP_FAIL;
    }
    if (!message) {
        ESP_LOGE(TAG, "Invalid message");
        return ESP_ERR_INVALID_ARG;
    }
    ESP_LOGI(TAG, "Sending text message: %s", message);
    xai_err_t err = xai_voice_client_send_text_turn(voice, message);
    if (err != XAI_OK) {
        ESP_LOGE(TAG, "Failed to send text turn: %s", xai_err_to_string(err));
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Message sent, response requested");
    return ESP_OK;
}

bool ws_is_connected(void)
{
    return voice && xai_voice_client_is_connected(voice);
}

esp_err_t ws_disconnect(void)
{
    if (!voice) return ESP_FAIL;
    xai_voice_client_disconnect(voice);
    ESP_LOGI(TAG, "WebSocket disconnected");
    return ESP_OK;
}

void ws_deinit(void)
{
    if (voice) {
        xai_voice_client_destroy(voice);
        voice = NULL;
    }
    audio_callback = NULL;
    status_callback = NULL;
    transcript_callback = NULL;
    ESP_LOGI(TAG, "WebSocket deinitialized");
}

