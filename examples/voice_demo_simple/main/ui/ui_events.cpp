/**
 * @file ui_events.c
 * @brief UI event handlers implementation
 */

#include "ui_events.h"
#include "ui_init.h"
#include "ui_screens.h"
#include "network/websocket_client.h"
#include "audio/audio_playback.h"
#include "config/app_config.h"
#include "esp_log.h"

static const char *TAG = "ui_events";

// Forward declarations (these are registered as callbacks)
static void on_audio_cb(const int16_t *pcm, size_t sample_count, int sample_rate_hz);
static void on_status_cb(const char *status);
static void on_transcript_cb(const char *text);

void ui_setup_event_handlers(void)
{
    ESP_LOGI(TAG, "Setting up event handlers...");
    // Note: WebSocket client is initialized later in main. Audio deltas are already decoded
    // to PCM16 by the xAI SDK, so we do not allocate a base64 decode buffer here.
    ESP_LOGI(TAG, "Event handlers ready");
}

void ui_on_button_clicked(void)
{
    ESP_LOGI(TAG, "Button clicked - sending message");
    
    // Update UI (already locked by LVGL event handler)
    ui_set_button_state(BTN_STATE_CONNECTING);
    ui_update_status_label("Connecting...");
    ui_clear_transcript();
    
    // Send message (this happens in separate thread)
    esp_err_t ret = ws_send_text_message(VOICE_DEFAULT_PROMPT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to send message");
        
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

// Internal callback wrappers (called from WebSocket thread)
static void on_audio_cb(const int16_t *pcm, size_t sample_count, int sample_rate_hz)
{
    ui_on_audio_received(pcm, sample_count, sample_rate_hz);
}

static void on_status_cb(const char *status)
{
    ui_on_websocket_status(status);
}

static void on_transcript_cb(const char *text)
{
    ui_on_transcript_received(text);
}

// Public accessor for callbacks (used in main)
ws_audio_callback_t ui_get_audio_callback(void)
{
    return on_audio_cb;
}

ws_status_callback_t ui_get_status_callback(void)
{
    return on_status_cb;
}

ws_transcript_callback_t ui_get_transcript_callback(void)
{
    return on_transcript_cb;
}

