/**
 * @file audio_playback.c
 * @brief PCM audio playback implementation
 */

#include "audio_playback.h"
#include "audio_init.h"
#include "esp_log.h"
#include "driver/i2s_std.h"

static const char *TAG = "audio_playback";
static bool s_is_playing = false;

esp_err_t audio_play_pcm(const int16_t *pcm, size_t num_samples)
{
    if (!pcm || num_samples == 0) {
        ESP_LOGE(TAG, "Invalid parameters");
        return ESP_ERR_INVALID_ARG;
    }

    i2s_chan_handle_t tx_handle = audio_get_i2s_handle();
    if (!tx_handle) {
        ESP_LOGE(TAG, "I2S not initialized");
        return ESP_FAIL;
    }

    s_is_playing = true;
    
    size_t bytes_to_write = num_samples * sizeof(int16_t);
    size_t bytes_written = 0;
    
    esp_err_t err = i2s_channel_write(tx_handle, pcm, bytes_to_write,
                                       &bytes_written, portMAX_DELAY);
    
    s_is_playing = false;
    
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "✓ Played %zu mono samples (%zu bytes)", 
                 num_samples, bytes_written);
        return ESP_OK;
    } else {
        ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(err));
        return err;
    }
}

bool audio_is_playing(void)
{
    return s_is_playing;
}

