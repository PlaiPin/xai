/**
 * @file websocket_client.h
 * @brief xAI Grok Voice API WebSocket client
 */

#ifndef WEBSOCKET_CLIENT_H
#define WEBSOCKET_CLIENT_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback function for audio data
 * 
 * Called when decoded PCM16 audio is received from the server.
 * This callback is invoked from the WebSocket event loop.
 * 
 * @param pcm PCM16 little-endian mono samples
 * @param sample_count Number of int16 samples
 * @param sample_rate_hz Sample rate (Hz) configured for the session
 */
typedef void (*ws_audio_callback_t)(const int16_t *pcm, size_t sample_count, int sample_rate_hz);

/**
 * @brief Callback function for status updates
 * 
 * Called when connection status or response status changes.
 * Examples: "connected", "speaking", "done", "error: <message>"
 * 
 * @param status_msg Status message string
 */
typedef void (*ws_status_callback_t)(const char *status_msg);

/**
 * @brief Callback function for transcript text
 * 
 * Called when transcript delta is received (text of what's being spoken).
 * 
 * @param text Transcript text fragment
 */
typedef void (*ws_transcript_callback_t)(const char *text);

/**
 * @brief Initialize WebSocket client for xAI Grok Voice API
 * 
 * @param api_key xAI API key
 * @param audio_cb Callback for audio data (required)
 * @param status_cb Callback for status updates (optional)
 * @param transcript_cb Callback for transcript text (optional)
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ws_init(const char *api_key,
                  ws_audio_callback_t audio_cb,
                  ws_status_callback_t status_cb,
                  ws_transcript_callback_t transcript_cb);

/**
 * @brief Send a text message to Grok
 * 
 * This function sends a text message and requests a response.
 * Audio data will arrive via the audio callback.
 * 
 * @param message Text message to send
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ws_send_text_message(const char *message);

/**
 * @brief Check if WebSocket is connected
 * 
 * @return true if connected, false otherwise
 */
bool ws_is_connected(void);

/**
 * @brief Disconnect WebSocket
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t ws_disconnect(void);

/**
 * @brief Cleanup WebSocket client
 */
void ws_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // WEBSOCKET_CLIENT_H

