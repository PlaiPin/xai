/**
 * @file xai_voice_realtime.h
 * @brief xAI Grok Voice Realtime (WebSocket) client for ESP-IDF
 *
 * Device-agnostic SDK layer:
 * - Owns WebSocket connection to wss://api.x.ai/v1/realtime
 * - Reassembles fragmented WebSocket TEXT payloads
 * - Filters out WebSocket control frames (PING/PONG/CLOSE) so JSON parsing is never corrupted
 * - Parses voice events and decodes base64 audio deltas into PCM16 (little-endian, mono)
 *
 * This module deliberately does NOT handle audio playback (I2S/codec) or UI.
 */

#pragma once

#include "xai.h"

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/** Opaque realtime voice client handle */
typedef struct xai_voice_client_s* xai_voice_client_t;

/**
 * @brief Realtime client state
 */
typedef enum {
    XAI_VOICE_STATE_DISCONNECTED = 0,
    XAI_VOICE_STATE_CONNECTING,
    XAI_VOICE_STATE_CONNECTED,      /**< WebSocket connected (transport) */
    XAI_VOICE_STATE_SESSION_READY,  /**< session.updated received; safe to send turns */
    XAI_VOICE_STATE_TURN_STARTED,   /**< response.created */
    XAI_VOICE_STATE_TURN_DONE,      /**< response.done */
    XAI_VOICE_STATE_ERROR
} xai_voice_state_t;

/**
 * @brief Voice session configuration
 */
typedef struct {
    const char *voice;              /**< Voice name (e.g., "Ara") */
    const char *instructions;       /**< System instructions */
    int sample_rate_hz;             /**< Output/input sample rate for audio/pcm (e.g., 16000) */
    bool server_vad;                /**< If true, send turn_detection.type="server_vad"; else null (text turns) */
} xai_voice_session_t;

/**
 * @brief Realtime client configuration
 */
typedef struct {
    const char *uri;                /**< WebSocket URI (default: wss://api.x.ai/v1/realtime) */
    const char *api_key;            /**< xAI API key (required) */

    int network_timeout_ms;         /**< Network timeout */
    int reconnect_timeout_ms;       /**< Reconnect timeout */

    int ws_rx_buffer_size;          /**< esp_websocket_client rx buffer size */
    size_t max_message_size;        /**< Maximum single JSON message size (reassembly buffer bytes) */

    size_t pcm_buffer_bytes;        /**< PCM decode buffer size (bytes). Must fit decoded audio deltas. */
    bool prefer_psram;              /**< Prefer PSRAM for big buffers if available */

    xai_voice_session_t session;    /**< Session defaults to send after connect */

    bool queue_turn_before_ready;   /**< If true, queue one pending text turn until SESSION_READY */
} xai_voice_config_t;

/**
 * @brief Callbacks for realtime voice client
 *
 * Notes:
 * - Callbacks are invoked from the esp_websocket_client task context.
 * - For on_pcm16(): the pcm pointer is SDK-owned and only valid until the callback returns.
 */
typedef struct {
    void (*on_state)(xai_voice_client_t client, xai_voice_state_t state, const char *detail, void *user_ctx);
    void (*on_transcript_delta)(xai_voice_client_t client, const char *utf8, size_t len, void *user_ctx);
    void (*on_pcm16)(xai_voice_client_t client, const int16_t *samples, size_t sample_count, int sample_rate_hz, void *user_ctx);
    void (*on_event_json)(xai_voice_client_t client, const char *type, const char *json, size_t len, void *user_ctx); /**< Optional */
} xai_voice_callbacks_t;

/**
 * @brief Create a realtime voice client (does not connect).
 */
xai_voice_client_t xai_voice_client_create(const xai_voice_config_t *cfg,
                                          const xai_voice_callbacks_t *cbs,
                                          void *user_ctx);

/**
 * @brief Destroy a realtime voice client. Disconnects if necessary.
 */
void xai_voice_client_destroy(xai_voice_client_t client);

/**
 * @brief Connect WebSocket and send session.update.
 */
xai_err_t xai_voice_client_connect(xai_voice_client_t client);

/**
 * @brief Disconnect WebSocket.
 */
xai_err_t xai_voice_client_disconnect(xai_voice_client_t client);

/**
 * @brief Returns true if WebSocket transport is connected.
 */
bool xai_voice_client_is_connected(xai_voice_client_t client);

/**
 * @brief Returns true if session.updated has been received (safe to send turns).
 */
bool xai_voice_client_is_ready(xai_voice_client_t client);

/**
 * @brief Send a text turn (conversation.item.create + response.create).
 *
 * If not ready:
 * - If cfg.queue_turn_before_ready=true, one pending turn is queued and sent on SESSION_READY.
 * - Otherwise returns XAI_ERR_NOT_READY.
 */
xai_err_t xai_voice_client_send_text_turn(xai_voice_client_t client, const char *text);

#ifdef __cplusplus
}
#endif


