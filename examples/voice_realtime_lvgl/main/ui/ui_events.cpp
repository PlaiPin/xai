/**
 * @file ui_events.c
 * @brief UI event handlers implementation
 */

#include "ui_events.h"
#include "ui_init.h"
#include "ui_screens.h"
#include "audio/audio_playback.h"
#include "config/app_config.h"
#include "esp_log.h"
#include "xai_voice_realtime.h"
#include "xai.h"

static const char *TAG = "ui_events";

// SDK client (owned by UI module for this example)
static xai_voice_client_t s_voice = NULL;

// Forward declarations (SDK callbacks)
static void sdk_on_state(xai_voice_client_t client, xai_voice_state_t state, const char *detail, void *user_ctx);
static void sdk_on_transcript_delta(xai_voice_client_t client, const char *utf8, size_t len, void *user_ctx);
static void sdk_on_pcm16(xai_voice_client_t client, const int16_t *samples, size_t sample_count, int sample_rate_hz, void *user_ctx);

void ui_setup_event_handlers(void)
{
    ESP_LOGI(TAG, "Setting up event handlers...");
    // Note: WebSocket client is initialized later in main. Audio deltas are already decoded
    // to PCM16 by the xAI SDK, so we do not allocate a base64 decode buffer here.
    ESP_LOGI(TAG, "Event handlers ready");
}

esp_err_t ui_voice_start(const char *api_key)
{
    if (!api_key || !api_key[0]) {
        ESP_LOGE(TAG, "Missing API key");
        return ESP_ERR_INVALID_ARG;
    }
    if (s_voice) {
        ESP_LOGW(TAG, "Voice client already started");
        return ESP_OK;
    }

    // NOTE: ui_events.cpp is compiled as C++.
    // Avoid C designated initializers (order-sensitive in C++); use zero-init + assignments.
    xai_voice_callbacks_t cbs = {};
    cbs.on_state = sdk_on_state;
    cbs.on_transcript_delta = sdk_on_transcript_delta;
    cbs.on_pcm16 = sdk_on_pcm16;
    cbs.on_event_json = NULL;

    xai_voice_config_t cfg = {};
    cfg.uri = WEBSOCKET_URI;
    cfg.api_key = api_key;
    cfg.network_timeout_ms = 60000;
    cfg.reconnect_timeout_ms = 15000;
    cfg.ws_rx_buffer_size = WS_BUFFER_SIZE;
    cfg.max_message_size = WS_REASSEMBLY_SIZE;
    cfg.pcm_buffer_bytes = 128 * 1024;
    cfg.prefer_psram = true;
    cfg.queue_turn_before_ready = true;
    cfg.session.voice = VOICE_NAME;
    cfg.session.instructions = "You are a helpful AI assistant. Be concise.";
    cfg.session.sample_rate_hz = 16000;
    cfg.session.server_vad = true;

    s_voice = xai_voice_client_create(&cfg, &cbs, NULL);
    if (!s_voice) {
        ESP_LOGE(TAG, "Failed to create voice client");
        return ESP_FAIL;
    }

    xai_err_t err = xai_voice_client_connect(s_voice);
    if (err != XAI_OK) {
        ESP_LOGE(TAG, "Failed to connect voice client: %s", xai_err_to_string(err));
        xai_voice_client_destroy(s_voice);
        s_voice = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

bool ui_voice_is_connected(void)
{
    return s_voice && xai_voice_client_is_connected(s_voice);
}

void ui_on_button_clicked(void)
{
    ESP_LOGI(TAG, "Button clicked - sending message");
    
    // Update UI (already locked by LVGL event handler)
    ui_set_button_state(BTN_STATE_CONNECTING);
    ui_update_status_label("Connecting...");
    ui_clear_transcript();
    
    // Send message via SDK
    if (!s_voice) {
        ESP_LOGE(TAG, "Voice client not initialized");
        return;
    }

    xai_err_t err = xai_voice_client_send_text_turn(s_voice, VOICE_DEFAULT_PROMPT);
    if (err != XAI_OK) {
        ESP_LOGE(TAG, "Failed to send text turn: %s", xai_err_to_string(err));
        
        // Update UI on error
        if (ui_lock(1000)) {
            ui_set_button_state(BTN_STATE_ERROR);
            ui_update_status_label("Error: Send failed");
            ui_unlock();
        }
    }
}

void ui_on_websocket_status(const char *status)
{
    if (!status) return;
    
    ESP_LOGI(TAG, "WebSocket status: %s", status);
    
    // Update UI (must acquire lock!)
    if (ui_lock(1000)) {
        if (strcmp(status, "connected") == 0) {
            // Connected at transport level, but may not be session-configured yet.
            ui_set_button_state(BTN_STATE_CONNECTING);
            ui_update_status_label("Connected");
            
        } else if (strcmp(status, "ready") == 0) {
            // Session configured (session.updated received) -> safe to enable the button.
            ui_set_button_state(BTN_STATE_READY);
            ui_update_status_label("Ready");

        } else if (strcmp(status, "speaking") == 0) {
            ui_set_button_state(BTN_STATE_SPEAKING);
            ui_update_status_label("Speaking...");
            
            // Clear transcript
            ui_clear_transcript();
            
        } else if (strcmp(status, "done") == 0) {
            ui_set_button_state(BTN_STATE_READY);
            ui_update_status_label("Ready");
            
        } else if (strncmp(status, "error:", 6) == 0) {
            ui_set_button_state(BTN_STATE_ERROR);
            ui_update_status_label(status);
            
        } else if (strcmp(status, "disconnected") == 0) {
            ui_set_button_state(BTN_STATE_ERROR);
            ui_update_status_label("Disconnected");
        }
        
        ui_unlock();
    } else {
        ESP_LOGW(TAG, "Failed to acquire UI lock for status update");
    }
}

void ui_on_audio_received(const int16_t *pcm, size_t sample_count, int sample_rate_hz)
{
    (void)sample_rate_hz;
    if (!pcm || sample_count == 0) {
        ESP_LOGW(TAG, "Invalid PCM audio data");
        return;
    }

    ESP_LOGI(TAG, "Audio received: %zu samples", sample_count);

    // Play audio (blocking, no UI lock needed)
    esp_err_t ret = audio_play_pcm(pcm, sample_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to play audio");

        // Update UI on error
        if (ui_lock(1000)) {
            ui_set_button_state(BTN_STATE_ERROR);
            ui_update_status_label("Error: Playback failed");
            ui_unlock();
        }
    }
}

void ui_on_transcript_received(const char *text)
{
    if (!text) return;
    
    ESP_LOGD(TAG, "Transcript: %s", text);
    
    // Update transcript label (must acquire lock!)
    if (ui_lock(1000)) {
        ui_append_transcript(text);
        ui_unlock();
    } else {
        ESP_LOGW(TAG, "Failed to acquire UI lock for transcript update");
    }
}

// SDK callbacks (called from esp_websocket_client task context)
static void sdk_on_state(xai_voice_client_t client, xai_voice_state_t state, const char *detail, void *user_ctx)
{
    (void)client;
    (void)user_ctx;
    // Map SDK state to existing UI status strings
    switch (state) {
    case XAI_VOICE_STATE_CONNECTED:
        ui_on_websocket_status("connected");
        break;
    case XAI_VOICE_STATE_SESSION_READY:
        ui_on_websocket_status("ready");
        break;
    case XAI_VOICE_STATE_TURN_STARTED:
        ui_on_websocket_status("speaking");
        break;
    case XAI_VOICE_STATE_TURN_DONE:
        ui_on_websocket_status("done");
        break;
    case XAI_VOICE_STATE_DISCONNECTED:
        ui_on_websocket_status("disconnected");
        break;
    case XAI_VOICE_STATE_ERROR:
        if (detail && detail[0]) {
            char buf[128];
            snprintf(buf, sizeof(buf), "error: %s", detail);
            ui_on_websocket_status(buf);
        } else {
            ui_on_websocket_status("error: unknown");
        }
        break;
    default:
        break;
    }
}

static void sdk_on_transcript_delta(xai_voice_client_t client, const char *utf8, size_t len, void *user_ctx)
{
    (void)client;
    (void)user_ctx;
    if (!utf8 || len == 0) return;
    // Ensure null-terminated string for ui_on_transcript_received
    char tmp[256];
    size_t n = len < sizeof(tmp) - 1 ? len : sizeof(tmp) - 1;
    memcpy(tmp, utf8, n);
    tmp[n] = '\0';
    ui_on_transcript_received(tmp);
}

static void sdk_on_pcm16(xai_voice_client_t client, const int16_t *samples, size_t sample_count, int sample_rate_hz, void *user_ctx)
{
    (void)client;
    (void)user_ctx;
    ui_on_audio_received(samples, sample_count, sample_rate_hz);
}

