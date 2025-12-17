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

extern "C" {
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
}

static const char *TAG = "ui_events";

// SDK client (owned by UI module for this example)
static xai_voice_client_t s_voice = NULL;

typedef enum {
    UI_EVT_SDK_STATE = 0,
    UI_EVT_TRANSCRIPT_APPEND,
    UI_EVT_ERROR_TEXT,
} ui_evt_type_t;

typedef struct {
    ui_evt_type_t type;
    xai_voice_state_t sdk_state;
    char text[256]; // used for transcript or error detail (null-terminated)
} ui_evt_t;

static QueueHandle_t s_ui_evtq = NULL;
static char *s_pending_turn = NULL; // strdup'd prompt to send after SESSION_READY

// Forward declarations (SDK callbacks)
static void sdk_on_state(xai_voice_client_t client, xai_voice_state_t state, const char *detail, void *user_ctx);
static void sdk_on_transcript_delta(xai_voice_client_t client, const char *utf8, size_t len, void *user_ctx);
static void sdk_on_pcm16(xai_voice_client_t client, const int16_t *samples, size_t sample_count, int sample_rate_hz, void *user_ctx);

static void enqueue_evt(const ui_evt_t *evt)
{
    if (!s_ui_evtq || !evt) return;
    // Never block in callbacks. If full, drop the oldest event and try once more.
    if (xQueueSend(s_ui_evtq, evt, 0) != pdTRUE) {
        ui_evt_t drop;
        (void)xQueueReceive(s_ui_evtq, &drop, 0);
        (void)xQueueSend(s_ui_evtq, evt, 0);
    }
}

static void enqueue_error_text(const char *msg)
{
    ui_evt_t e = {};
    e.type = UI_EVT_ERROR_TEXT;
    if (msg && msg[0]) {
        snprintf(e.text, sizeof(e.text), "%s", msg);
    } else {
        snprintf(e.text, sizeof(e.text), "unknown");
    }
    enqueue_evt(&e);
}

void ui_setup_event_handlers(void)
{
    ESP_LOGI(TAG, "Setting up event handlers...");
    // Note: WebSocket client is initialized later in main. Audio deltas are already decoded
    // to PCM16 by the xAI SDK, so we do not allocate a base64 decode buffer here.
    if (!s_ui_evtq) {
        s_ui_evtq = xQueueCreate(16, sizeof(ui_evt_t));
        if (!s_ui_evtq) {
            ESP_LOGE(TAG, "Failed to create UI event queue");
        }
    }
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
    ESP_LOGI(TAG, "Button clicked");

    if (!s_voice) {
        ESP_LOGE(TAG, "Voice client not initialized");
        ui_set_button_state(BTN_STATE_ERROR);
        ui_update_status_label("Error: Voice client not started");
        return;
    }

    // Clear transcript for a new user action.
    ui_clear_transcript();

    const bool connected = xai_voice_client_is_connected(s_voice);
    const bool ready = xai_voice_client_is_ready(s_voice);

    // Tap-to-reconnect: if disconnected (common after idle), reconnect first and send once SESSION_READY arrives.
    if (!connected) {
        free(s_pending_turn);
        s_pending_turn = strdup(VOICE_DEFAULT_PROMPT);
        ui_set_button_state(BTN_STATE_CONNECTING);
        ui_update_status_label("Reconnecting to Grok...");
        (void)xai_voice_client_connect(s_voice);
        return;
    }

    // Connected but not session-ready yet (connect in progress). Queue one turn locally.
    if (!ready) {
        free(s_pending_turn);
        s_pending_turn = strdup(VOICE_DEFAULT_PROMPT);
        ui_set_button_state(BTN_STATE_CONNECTING);
        ui_update_status_label("Connecting to Grok...");
        return;
    }

    // Ready: send immediately.
    ui_set_button_state(BTN_STATE_CONNECTING);
    ui_update_status_label("Sending...");
    xai_err_t err = xai_voice_client_send_text_turn(s_voice, VOICE_DEFAULT_PROMPT);
    if (err != XAI_OK) {
        ESP_LOGE(TAG, "Failed to send text turn: %s", xai_err_to_string(err));
        ui_set_button_state(BTN_STATE_DISCONNECTED);
        ui_update_status_label("Disconnected\nTap to reconnect");
    }
}

void ui_on_websocket_status(const char *status)
{
    // Back-compat shim: enqueue a human status line as an ERROR_TEXT event so it updates from LVGL task.
    if (!status) return;
    enqueue_error_text(status);
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
        enqueue_error_text("Error: Playback failed");
    }
}

void ui_on_transcript_received(const char *text)
{
    if (!text) return;

    ui_evt_t e = {};
    e.type = UI_EVT_TRANSCRIPT_APPEND;
    snprintf(e.text, sizeof(e.text), "%s", text);
    enqueue_evt(&e);
}

// SDK callbacks (called from esp_websocket_client task context)
static void sdk_on_state(xai_voice_client_t client, xai_voice_state_t state, const char *detail, void *user_ctx)
{
    (void)client;
    (void)user_ctx;
    ui_evt_t e = {};
    e.type = UI_EVT_SDK_STATE;
    e.sdk_state = state;
    if (detail && detail[0]) {
        snprintf(e.text, sizeof(e.text), "%s", detail);
    }
    enqueue_evt(&e);
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
    ui_on_transcript_received(tmp); // enqueues
}

static void sdk_on_pcm16(xai_voice_client_t client, const int16_t *samples, size_t sample_count, int sample_rate_hz, void *user_ctx)
{
    (void)client;
    (void)user_ctx;
    ui_on_audio_received(samples, sample_count, sample_rate_hz);
}

void ui_events_process_lvgl(void)
{
    if (!s_ui_evtq) return;

    ui_evt_t e;
    // Drain all pending events (non-blocking).
    while (xQueueReceive(s_ui_evtq, &e, 0) == pdTRUE) {
        switch (e.type) {
        case UI_EVT_TRANSCRIPT_APPEND:
            if (e.text[0]) {
                ui_append_transcript(e.text);
            }
            break;
        case UI_EVT_ERROR_TEXT:
            // Treat ERROR_TEXT as a status update line (best-effort).
            if (e.text[0]) {
                // If it is a plain "disconnected" hint, show a consistent UX.
                if (strcmp(e.text, "disconnected") == 0) {
                    ui_set_button_state(BTN_STATE_DISCONNECTED);
                    ui_update_status_label("Disconnected\nTap to reconnect");
                } else {
                    ui_set_button_state(BTN_STATE_ERROR);
                    ui_update_status_label(e.text);
                }
            }
            break;
        case UI_EVT_SDK_STATE:
            switch (e.sdk_state) {
            case XAI_VOICE_STATE_CONNECTING:
                ui_set_button_state(BTN_STATE_CONNECTING);
                ui_update_status_label("Connecting to Grok...");
                break;
            case XAI_VOICE_STATE_CONNECTED:
                ui_set_button_state(BTN_STATE_CONNECTING);
                ui_update_status_label("Connected");
                break;
            case XAI_VOICE_STATE_SESSION_READY:
                ui_set_button_state(BTN_STATE_READY);
                ui_update_status_label("Ready");
                if (s_pending_turn) {
                    // Send the pending prompt now that the session is configured.
                    char *tmp = s_pending_turn;
                    s_pending_turn = NULL;
                    ui_clear_transcript();
                    (void)xai_voice_client_send_text_turn(s_voice, tmp);
                    free(tmp);
                }
                break;
            case XAI_VOICE_STATE_TURN_STARTED:
                ui_set_button_state(BTN_STATE_SPEAKING);
                ui_update_status_label("Speaking...");
                break;
            case XAI_VOICE_STATE_TURN_DONE:
                ui_set_button_state(BTN_STATE_READY);
                ui_update_status_label("Ready");
                break;
            case XAI_VOICE_STATE_DISCONNECTED:
                ui_set_button_state(BTN_STATE_DISCONNECTED);
                ui_update_status_label("Disconnected\nTap to reconnect");
                break;
            case XAI_VOICE_STATE_ERROR:
                ui_set_button_state(BTN_STATE_ERROR);
                if (e.text[0]) {
                    char buf[192];
                    // Truncate safely to avoid -Wformat-truncation (this project treats warnings as errors).
                    // Keep room for "error: " prefix and NUL.
                    const int max_detail = (int)sizeof(buf) - (int)sizeof("error: ") - 1;
                    snprintf(buf, sizeof(buf), "error: %.*s", max_detail, e.text);
                    ui_update_status_label(buf);
                } else {
                    ui_update_status_label("error: unknown");
                }
                break;
            default:
                break;
            }
            break;
        default:
            break;
        }
    }
}

