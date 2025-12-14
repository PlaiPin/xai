/**
 * @file ui_events.h
 * @brief UI event handlers and coordination
 */

#ifndef UI_EVENTS_H
#define UI_EVENTS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Setup event handlers
 * 
 * This function registers callbacks with the WebSocket client
 * to handle audio, status, and transcript updates.
 */
void ui_setup_event_handlers(void);

/**
 * @brief Handle button click event
 * 
 * Called when the user taps the main button.
 * Sends a message to the xAI API.
 */
void ui_on_button_clicked(void);

/**
 * @brief Handle WebSocket status update
 * 
 * Called from WebSocket event loop when status changes.
 * Updates UI status label and button state.
 * 
 * @param status Status message (e.g., "connected", "speaking", "done")
 */
void ui_on_websocket_status(const char *status);

/**
 * @brief Handle audio data received
 * 
 * Called from WebSocket event loop when audio delta arrives.
 * Plays the decoded PCM16 audio.
 * 
 * @param pcm PCM16 mono samples
 * @param sample_count Number of samples
 * @param sample_rate_hz Sample rate (Hz)
 */
void ui_on_audio_received(const int16_t *pcm, size_t sample_count, int sample_rate_hz);

/**
 * @brief Handle transcript text received
 * 
 * Called from WebSocket event loop when transcript delta arrives.
 * Updates transcript label.
 * 
 * @param text Transcript text fragment
 */
void ui_on_transcript_received(const char *text);

// Forward declarations from websocket_client.h (kept here to avoid including networking headers)
typedef void (*ws_audio_callback_t)(const int16_t *pcm, size_t sample_count, int sample_rate_hz);
typedef void (*ws_status_callback_t)(const char *status_msg);
typedef void (*ws_transcript_callback_t)(const char *text);

/**
 * @brief Get audio callback for WebSocket client
 * 
 * @return Audio callback function pointer
 */
ws_audio_callback_t ui_get_audio_callback(void);

/**
 * @brief Get status callback for WebSocket client
 * 
 * @return Status callback function pointer
 */
ws_status_callback_t ui_get_status_callback(void);

/**
 * @brief Get transcript callback for WebSocket client
 * 
 * @return Transcript callback function pointer
 */
ws_transcript_callback_t ui_get_transcript_callback(void);

#ifdef __cplusplus
}
#endif

#endif // UI_EVENTS_H

