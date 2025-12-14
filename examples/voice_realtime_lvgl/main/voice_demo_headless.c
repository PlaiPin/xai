/**
 * @file voice_demo_headless.c
 * @brief WebSocket Realtime Voice Demo with I2S Audio Playback (SDK-backed)
 * 
 * This example demonstrates the xAI Grok Voice Realtime API using the xAI SDK:
 * 1. Connect to wss://api.x.ai/v1/realtime (SDK owns WebSocket + parsing)
 * 2. Send a text turn to Grok
 * 3. Receive decoded PCM16 audio in real-time via callback
 * 4. Play PCM16 through I2S speaker
 * 
 * Hardware Requirements:
 * - Waveshare ESP32-S3-Touch-AMOLED-1.75
 * - ES8311 audio codec (on-board)
 * - WiFi connection
 * 
 * Note: This example is configured specifically for the Waveshare board.
 * For other boards, adjust the GPIO pin definitions and codec initialization.
 *
 * Build note:
 * This file is an alternate entrypoint and is NOT compiled by default in this example project
 * (the LVGL UI app `main.c` is used instead). To build this file, update
 * `examples/voice_realtime_lvgl/main/CMakeLists.txt` to include `voice_demo_headless.c`
 * and exclude `main.c` (you can't compile both because they each define `app_main`).
 * 
 * @copyright 2025
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_netif.h"
#include "driver/i2s_std.h"
#include "driver/i2c_master.h"
#include "driver/gpio.h"

// ESP Codec Device Framework
#include "driver/i2c.h"
#include "es8311.h"

// xAI SDK (Voice Realtime)
#include "xai_voice_realtime.h"
#include "xai.h"

static const char *TAG = "voice_demo";

// WiFi credentials - EDIT THESE!
#define WIFI_SSID      "WIFI_SSID"
#define WIFI_PASSWORD  "WIFI_PASSWORD"

// xAI API key - EDIT THIS! (get yours from console.x.ai)
#define XAI_API_KEY    "xai-add-your-api-key-here"
#define WEBSOCKET_URI  "wss://api.x.ai/v1/realtime"

// I2S Configuration - Waveshare ESP32-S3-Touch-AMOLED-1.75 (from their 06_I2SCodec example)
#define I2S_NUM         (I2S_NUM_0)
#define I2S_MCLK_IO     (GPIO_NUM_42)  // Master clock (required for ES8311)
#define I2S_BCK_IO      (GPIO_NUM_9)   // Bit clock
#define I2S_WS_IO       (GPIO_NUM_45)  // Word select (LRCK) 
#define I2S_DO_IO       (GPIO_NUM_8)   // Data out (to ES8311)
#define I2S_DI_IO       (GPIO_NUM_10)  // Data in (from ES8311, for mic if needed)
#define I2S_SAMPLE_RATE (16000)        // 16kHz - matches xAI voice API
#define I2S_MCLK_MULTIPLE (384)        // From Waveshare example - critical for ES8311
#define I2S_MCLK_FREQ_HZ (I2S_SAMPLE_RATE * I2S_MCLK_MULTIPLE)
#define I2S_DMA_BUF_COUNT (6)          // From Waveshare example
#define I2S_DMA_BUF_LEN   (1200)       // From Waveshare example

// ES8311 Audio Codec Configuration
#define I2C_NUM         (I2C_NUM_0)
#define I2C_SDA_IO      (GPIO_NUM_15)
#define I2C_SCL_IO      (GPIO_NUM_14)
#define I2C_FREQ_HZ     (100000)
#define ES8311_ADDR     (0x18)
#define PA_ENABLE_GPIO  (GPIO_NUM_46)  // Power amplifier enable


// Audio buffer configuration (for I2S writes)
// Note: decoded PCM is provided by the SDK; we do not allocate base64 decode buffers here.

// Event group bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static EventGroupHandle_t s_wifi_event_group;

// I2S and ES8311 handles (using Waveshare's direct approach)
static i2s_chan_handle_t tx_handle = NULL;  // I2S TX channel for audio output
static es8311_handle_t es8311_handle = NULL;  // ES8311 codec handle
static xai_voice_client_t s_voice = NULL;

// ============================================================================
// WiFi Setup
// ============================================================================

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "Retry connecting to WiFi...");
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASSWORD,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi initialization finished.");

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi SSID:%s", WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
    }
}

// ============================================================================
// ES8311 Audio Codec Initialization (Waveshare 06_I2SCodec approach)
// ============================================================================

static void codec_init(void)
{
    ESP_LOGI(TAG, "Initializing audio codec (Waveshare method)...");

    // 1. Enable PA (Power Amplifier) - must be done first
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << PA_ENABLE_GPIO),
        .pull_down_en = 0,
        .pull_up_en = 0,
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    gpio_set_level(PA_ENABLE_GPIO, 1);  // Enable PA
    ESP_LOGI(TAG, "PA enabled on GPIO %d", PA_ENABLE_GPIO);

    // 2. Initialize I2C (legacy driver, matching Waveshare 06_I2SCodec)
    i2c_config_t es_i2c_cfg = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = I2C_SDA_IO,
        .scl_io_num = I2C_SCL_IO,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    ESP_ERROR_CHECK(i2c_param_config(I2C_NUM, &es_i2c_cfg));
    ESP_ERROR_CHECK(i2c_driver_install(I2C_NUM, I2C_MODE_MASTER, 0, 0, 0));
    ESP_LOGI(TAG, "I2C initialized (SDA=%d, SCL=%d)", I2C_SDA_IO, I2C_SCL_IO);

    // 3. Create and initialize ES8311 codec
    es8311_handle = es8311_create(I2C_NUM, ES8311_ADDRRES_0);
    if (!es8311_handle) {
        ESP_LOGE(TAG, "Failed to create ES8311 handle");
        return;
    }

    es8311_clock_config_t es_clk = {
        .mclk_inverted = false,
        .sclk_inverted = false,
        .mclk_from_mclk_pin = true,
        .mclk_frequency = I2S_MCLK_FREQ_HZ,
        .sample_frequency = I2S_SAMPLE_RATE,
    };
    ESP_ERROR_CHECK(es8311_init(es8311_handle, &es_clk, ES8311_RESOLUTION_16, ES8311_RESOLUTION_16));
    ESP_ERROR_CHECK(es8311_sample_frequency_config(es8311_handle, I2S_SAMPLE_RATE * I2S_MCLK_MULTIPLE, I2S_SAMPLE_RATE));
    ESP_ERROR_CHECK(es8311_voice_volume_set(es8311_handle, 80, NULL));  // 80% volume
    ESP_ERROR_CHECK(es8311_microphone_config(es8311_handle, false));  // Disable mic
    ESP_LOGI(TAG, "ES8311 codec initialized");

    // 4. Initialize I2S driver (matching Waveshare 06_I2SCodec)
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
    ESP_LOGI(TAG, "I2S initialized and enabled - MONO, %d Hz, MCLK multiple=%d", I2S_SAMPLE_RATE, I2S_MCLK_MULTIPLE);
    
    ESP_LOGI(TAG, "Audio codec ready for playback (mono mode)");
}

// ============================================================================
// xAI SDK Realtime Voice callbacks
// ============================================================================

static void voice_on_state(xai_voice_client_t client, xai_voice_state_t state, const char *detail, void *user_ctx)
{
    (void)client;
    (void)user_ctx;
    if (detail && detail[0]) {
        ESP_LOGI(TAG, "Voice state: %d (%s)", (int)state, detail);
    } else {
        ESP_LOGI(TAG, "Voice state: %d", (int)state);
    }

    if (state == XAI_VOICE_STATE_SESSION_READY) {
        const char *prompt = "Hello! Tell me a short joke.";
        xai_err_t err = xai_voice_client_send_text_turn(s_voice, prompt);
        if (err != XAI_OK) {
            ESP_LOGE(TAG, "Failed to send text turn: %s", xai_err_to_string(err));
        } else {
            ESP_LOGI(TAG, "Sent text turn: %s", prompt);
        }
    }
}

static void voice_on_transcript_delta(xai_voice_client_t client, const char *utf8, size_t len, void *user_ctx)
{
    (void)client;
    (void)user_ctx;
    if (!utf8 || len == 0) return;
    fwrite(utf8, 1, len, stdout);
    fflush(stdout);
}

static void voice_on_pcm16(xai_voice_client_t client, const int16_t *samples, size_t sample_count, int sample_rate_hz, void *user_ctx)
{
    (void)client;
    (void)user_ctx;
    (void)sample_rate_hz; // This demo uses fixed 16kHz I2S config.
    if (!samples || sample_count == 0 || !tx_handle) return;

    size_t bytes_written = 0;
    esp_err_t err = i2s_channel_write(tx_handle,
                                      samples,
                                      sample_count * sizeof(int16_t),
                                      &bytes_written,
                                      portMAX_DELAY);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2S write failed: %s", esp_err_to_name(err));
    }
}

// ============================================================================
// Main Application
// ============================================================================

void app_main(void)
{
    ESP_LOGI(TAG, "xAI Grok Voice WebSocket Demo");
    ESP_LOGI(TAG, "================================");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize WiFi
    wifi_init_sta();

    // Initialize audio codec using ESP Codec Dev framework
    codec_init();    // Handles I2C, I2S, ES8311, PA, and mono→stereo conversion

    // Start xAI Voice Realtime client (SDK owns WebSocket, parsing, and base64->PCM16 decode)
    xai_voice_callbacks_t cbs = {
        .on_state = voice_on_state,
        .on_transcript_delta = voice_on_transcript_delta,
        .on_pcm16 = voice_on_pcm16,
        .on_event_json = NULL,
    };
    xai_voice_config_t cfg = {
        .uri = WEBSOCKET_URI,
        .api_key = XAI_API_KEY,
        .network_timeout_ms = 60000,
        .reconnect_timeout_ms = 15000,
        .ws_rx_buffer_size = 16384,
        .max_message_size = 256 * 1024,
        .pcm_buffer_bytes = 128 * 1024,
        .prefer_psram = true,
        .queue_turn_before_ready = true,
        .session = {
            .voice = "Ara",
            .instructions = "You are a helpful AI assistant. Be concise.",
            .sample_rate_hz = 16000,
            .server_vad = true,
        },
    };
    s_voice = xai_voice_client_create(&cfg, &cbs, NULL);
    if (!s_voice) {
        ESP_LOGE(TAG, "Failed to create voice client");
        return;
    }
    xai_err_t xerr = xai_voice_client_connect(s_voice);
    if (xerr != XAI_OK) {
        ESP_LOGE(TAG, "Failed to connect voice client: %s", xai_err_to_string(xerr));
        return;
    }
    ESP_LOGI(TAG, "Voice client started (SDK). Waiting for session.updated...");
    
    // Keep running
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
