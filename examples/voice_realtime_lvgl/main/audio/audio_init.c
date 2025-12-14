/**
 * @file audio_init.c
 * @brief ES8311 audio codec and I2S initialization implementation
 */

#include "audio_init.h"
#include "config/app_config.h"
#include "i2c/i2c_manager.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <string.h>

static const char *TAG = "audio_init";

static i2s_chan_handle_t tx_handle = NULL;
static es8311_handle_t es8311_handle = NULL;
static bool audio_initialized = false;

esp_err_t audio_init(void)
{
    if (audio_initialized) {
        ESP_LOGW(TAG, "Audio already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing audio subsystem...");

    // 1. Enable Power Amplifier (PA) - must be done first
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << PA_ENABLE_GPIO),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(PA_ENABLE_GPIO, 1);  // Enable PA
    ESP_LOGI(TAG, "Power amplifier enabled on GPIO %d", PA_ENABLE_GPIO);

    // 2. Verify I2C bus is initialized (shared resource managed by i2c_manager)
    // I2C should be initialized by main app before audio init
    if (!i2c_shared_is_initialized()) {
        ESP_LOGE(TAG, "I2C bus not initialized! Call i2c_shared_init() first");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGI(TAG, "Using shared I2C bus (I2C_NUM_%d) for ES8311 codec", i2c_shared_get_port());

    // 3. Create and initialize ES8311 codec
    es8311_handle = es8311_create(I2C_NUM, ES8311_ADDRRES_0);
    if (!es8311_handle) {
        ESP_LOGE(TAG, "Failed to create ES8311 handle");
        return ESP_FAIL;
    }

    es8311_clock_config_t es_clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = I2S_MCLK_FREQ_HZ,
        .sample_frequency = I2S_SAMPLE_RATE,
    };

    ESP_ERROR_CHECK(es8311_init(es8311_handle, &es_clk, 
                                ES8311_RESOLUTION_16, ES8311_RESOLUTION_16));
    ESP_ERROR_CHECK(es8311_sample_frequency_config(es8311_handle, 
                                                    I2S_SAMPLE_RATE * I2S_MCLK_MULTIPLE, 
                                                    I2S_SAMPLE_RATE));
    ESP_ERROR_CHECK(es8311_voice_volume_set(es8311_handle, 80, NULL));  // 80% volume
    ESP_ERROR_CHECK(es8311_microphone_config(es8311_handle, false));  // Disable mic
    ESP_LOGI(TAG, "ES8311 codec initialized");

    // 4. Initialize I2S driver (matching Waveshare configuration)
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;  // Auto clear legacy data in DMA buffer
    chan_cfg.dma_desc_num = I2S_DMA_BUF_COUNT;
    chan_cfg.dma_frame_num = I2S_DMA_BUF_LEN;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle, NULL));  // TX only

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(I2S_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_MCLK_IO,
            .bclk = I2S_BCK_IO,
            .ws = I2S_WS_IO,
            .dout = I2S_DO_IO,
            .din = I2S_GPIO_UNUSED,  // No mic input
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };
    std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE;  // Override with 384

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(tx_handle));
    ESP_LOGI(TAG, "I2S initialized - MONO, %d Hz, MCLK multiple=%d", 
             I2S_SAMPLE_RATE, I2S_MCLK_MULTIPLE);
    
    audio_initialized = true;
    ESP_LOGI(TAG, "Audio subsystem ready");
    return ESP_OK;
}

i2s_chan_handle_t audio_get_i2s_handle(void)
{
    return tx_handle;
}

esp_err_t audio_set_volume(uint8_t volume)
{
    if (!audio_initialized || !es8311_handle) {
        ESP_LOGE(TAG, "Audio not initialized");
        return ESP_FAIL;
    }

    if (volume > 100) {
        volume = 100;
    }

    esp_err_t ret = es8311_voice_volume_set(es8311_handle, volume, NULL);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Volume set to %d%%", volume);
    } else {
        ESP_LOGE(TAG, "Failed to set volume: %s", esp_err_to_name(ret));
    }
    return ret;
}

void audio_deinit(void)
{
    if (tx_handle) {
        i2s_channel_disable(tx_handle);
        i2s_del_channel(tx_handle);
        tx_handle = NULL;
    }
    
    if (es8311_handle) {
        es8311_delete(es8311_handle);
        es8311_handle = NULL;
    }
    
    gpio_set_level(PA_ENABLE_GPIO, 0);  // Disable PA
    audio_initialized = false;
    ESP_LOGI(TAG, "Audio subsystem deinitialized");
}

