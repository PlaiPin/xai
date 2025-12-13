/**
 * @file audio_playback.h
 * @brief PCM audio playback via I2S
 */

#ifndef AUDIO_PLAYBACK_H
#define AUDIO_PLAYBACK_H

#include "esp_err.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Play PCM audio samples (blocking)
 * 
 * This function writes PCM samples to the I2S driver. It blocks until
 * all samples are written to the DMA buffer.
 * 
 * @param pcm Pointer to PCM samples (int16_t, mono, 16kHz)
 * @param num_samples Number of samples to play
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t audio_play_pcm(const int16_t *pcm, size_t num_samples);

/**
 * @brief Check if audio is currently playing
 * 
 * Note: This only checks if the last write completed, not if DMA is done.
 * For more accurate detection, check I2S driver state.
 * 
 * @return true if playing, false otherwise
 */
bool audio_is_playing(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_PLAYBACK_H

