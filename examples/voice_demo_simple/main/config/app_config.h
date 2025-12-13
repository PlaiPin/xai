/**
 * @file app_config.h
 * @brief Centralized configuration for xAI Voice Demo with LVGL UI
 * 
 * All hardware pin definitions, WiFi credentials, API keys, and
 * system parameters are defined here for easy configuration.
 */

#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/i2s_std.h"

// ============================================================================
// WiFi Configuration
// ============================================================================
#define WIFI_SSID      "WIFI_SSID"
#define WIFI_PASSWORD  "WIFI_PASSWORD"

// ============================================================================
// xAI API Configuration
// ============================================================================
#define XAI_API_KEY    "xai-add-your-api-key-here"
#define WEBSOCKET_URI  "wss://api.x.ai/v1/realtime"

// ============================================================================
// I2C Configuration (Shared between ES8311 audio codec and touch controller)
// ============================================================================
#define I2C_NUM         (I2C_NUM_0)
#define I2C_SDA_IO      (GPIO_NUM_15)
#define I2C_SCL_IO      (GPIO_NUM_14)
#define I2C_FREQ_HZ     (100000)

// ES8311 Audio Codec Address
#define ES8311_ADDR     (0x18)

// CST92xx Touch Controller Configuration
#define TOUCH_ADDR      (0x5A)
#define TOUCH_RST_IO    (GPIO_NUM_40)
#define TOUCH_INT_IO    (GPIO_NUM_11)

// ============================================================================
// I2S Audio Configuration (Waveshare ESP32-S3-Touch-AMOLED-1.75)
// ============================================================================
#define I2S_NUM         (I2S_NUM_0)
#define I2S_MCLK_IO     (GPIO_NUM_42)  // Master clock (required for ES8311)
#define I2S_BCK_IO      (GPIO_NUM_9)   // Bit clock
#define I2S_WS_IO       (GPIO_NUM_45)  // Word select (LRCK) 
#define I2S_DO_IO       (GPIO_NUM_8)   // Data out (to ES8311)
#define I2S_DI_IO       (GPIO_NUM_10)  // Data in (from ES8311, unused)
#define I2S_SAMPLE_RATE (16000)        // 16kHz - matches xAI voice API
#define I2S_MCLK_MULTIPLE (384)        // Critical for ES8311 - from Waveshare
#define I2S_MCLK_FREQ_HZ (I2S_SAMPLE_RATE * I2S_MCLK_MULTIPLE)
#define I2S_DMA_BUF_COUNT (6)          // From Waveshare example
#define I2S_DMA_BUF_LEN   (1200)       // From Waveshare example

// Power Amplifier Enable Pin
#define PA_ENABLE_GPIO  (GPIO_NUM_46)

// ============================================================================
// LCD Display Configuration (SH8601 QSPI AMOLED)
// ============================================================================
#define LCD_HOST        (SPI2_HOST)
#define LCD_H_RES       (466)
#define LCD_V_RES       (466)

// LCD Pins (QSPI Mode)
#define LCD_PIN_CS      (GPIO_NUM_12)
#define LCD_PIN_PCLK    (GPIO_NUM_38)
#define LCD_PIN_DATA0   (GPIO_NUM_4)
#define LCD_PIN_DATA1   (GPIO_NUM_5)
#define LCD_PIN_DATA2   (GPIO_NUM_6)
#define LCD_PIN_DATA3   (GPIO_NUM_7)
#define LCD_PIN_RST     (GPIO_NUM_39)
#define LCD_PIN_BK      (-1)  // No backlight control

// LCD Configuration
#if CONFIG_LV_COLOR_DEPTH == 32
#define LCD_BIT_PER_PIXEL (24)
#elif CONFIG_LV_COLOR_DEPTH == 16
#define LCD_BIT_PER_PIXEL (16)
#else
#define LCD_BIT_PER_PIXEL (16)  // Default
#endif

// ============================================================================
// LVGL Configuration
// ============================================================================
// LVGL draw buffer height in lines.
// IMPORTANT: We keep this relatively small so we can allocate the draw buffers in
// internal DMA-capable RAM (MALLOC_CAP_DMA). If the draw buffer lives in PSRAM,
// the SPI driver may need to allocate an internal DMA "bounce buffer" per flush,
// which can fail at runtime and block touch/click handling.
#define LVGL_BUF_HEIGHT       (60)  // lines (tuned to fit DMA RAM for this app)
#define LVGL_TICK_PERIOD_MS   (2)
#define LVGL_TASK_STACK_SIZE  (4 * 1024)
#define LVGL_TASK_PRIORITY    (2)
#define LVGL_TASK_MAX_DELAY_MS (500)
#define LVGL_TASK_MIN_DELAY_MS (1)

// ============================================================================
// Audio Buffer Configuration (PSRAM)
// ============================================================================
// 51200 samples = 102.4KB buffer for decoded PCM audio
// With PSRAM we can handle even the largest xAI audio deltas (~45KB PCM)
#define AUDIO_BUFFER_SIZE (51200)

// ============================================================================
// WebSocket Configuration (PSRAM)
// ============================================================================
// 128KB - handles all xAI audio deltas with reassembly
#define WS_REASSEMBLY_SIZE (131072)
#define WS_BUFFER_SIZE     (16384)  // 16KB - for TCP chunks

// ============================================================================
// Voice API Configuration
// ============================================================================
#define VOICE_NAME      "Ara"
#define VOICE_DEFAULT_PROMPT "Hello! Tell me a short joke."

#endif // APP_CONFIG_H

