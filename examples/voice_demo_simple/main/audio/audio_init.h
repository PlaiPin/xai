/**
 * @file audio_init.h
 * @brief ES8311 audio codec and I2S initialization
 */

#ifndef AUDIO_INIT_H
#define AUDIO_INIT_H

#include "esp_err.h"
#include "driver/i2s_std.h"
#include "es8311.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize audio subsystem (I2C, ES8311 codec, I2S driver)
 * 
 * This function performs complete audio initialization:
 * 1. Enable power amplifier GPIO
 * 2. Initialize I2C bus (shared with touch controller)
 * 3. Create and configure ES8311 codec
 * 4. Initialize I2S driver in mono mode, 16kHz
 * 
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t audio_init(void);

/**
 * @brief Get the I2S TX channel handle for audio playback
 * 
 * @return I2S channel handle (NULL if not initialized)
 */
i2s_chan_handle_t audio_get_i2s_handle(void);

/**
 * @brief Set audio output volume
 * 
 * @param volume Volume level (0-100)
 * @return ESP_OK on success, error code otherwise
 */
esp_err_t audio_set_volume(uint8_t volume);

/**
 * @brief Cleanup audio subsystem
 */
void audio_deinit(void);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_INIT_H

