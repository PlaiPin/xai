/**
 * @file voice_demo_ui.c
 * @brief Main application entry point for xAI Voice Demo with LVGL UI
 * 
 * This application demonstrates the xAI Grok Voice API with a touch UI:
 * 1. User taps button on AMOLED display
 * 2. App sends text message to Grok via WebSocket
 * 3. Grok responds with real-time audio (PCM, 16kHz)
 * 4. Audio is decoded and played through ES8311 codec
 * 5. Transcript is displayed on screen
 * 
 * Hardware: Waveshare ESP32-S3-Touch-AMOLED-1.75
 * - SH8601 466x466 QSPI AMOLED display
 * - CST92xx capacitive touch controller
 * - ES8311 audio codec with speaker
 * 
 * @copyright 2025
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

// Application modules
#include "config/app_config.h"
#include "i2c/i2c_manager.h"
#include "network/wifi_manager.h"
#include "network/websocket_client.h"
#include "audio/audio_init.h"
#include "audio/audio_playback.h"
#include "ui/ui_init.h"
#include "ui/ui_screens.h"
#include "ui/ui_events.h"

static const char *TAG = "voice_demo_ui";

void app_main(void)
{
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "xAI Grok Voice Demo with LVGL UI");
    ESP_LOGI(TAG, "=================================================");

    // ========================================================================
    // 1. Initialize NVS (required for WiFi)
    // ========================================================================
    ESP_LOGI(TAG, "[1/8] Initializing NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "✓ NVS initialized");

    // ========================================================================
    // 2. Initialize Shared I2C Bus (for touch + audio codec)
    // CRITICAL: Initialize BEFORE hardware modules that use I2C
    // ========================================================================
    ESP_LOGI(TAG, "[2/8] Initializing shared I2C bus...");
    ESP_ERROR_CHECK(i2c_shared_init());
    ESP_LOGI(TAG, "✓ I2C bus initialized (SDA=%d, SCL=%d, 100kHz)", I2C_SDA_IO, I2C_SCL_IO);

    // ========================================================================
    // 3. Initialize UI Hardware (LCD + Touch)
    // Note: Touch uses shared I2C bus initialized above
    // CRITICAL: Do this BEFORE WiFi/Audio to preserve internal RAM for LVGL buffers
    // ========================================================================
    ESP_LOGI(TAG, "[3/8] Initializing display and touch...");
    ESP_ERROR_CHECK(ui_hardware_init());
    ESP_LOGI(TAG, "✓ Display and touch initialized");

    // ========================================================================
    // 4. Initialize LVGL and Start UI Task
    // CRITICAL: Allocate LVGL buffers in PSRAM before WiFi/Audio
    // ========================================================================
    ESP_LOGI(TAG, "[4/8] Initializing LVGL...");
    ESP_ERROR_CHECK(ui_lvgl_init());
    ESP_LOGI(TAG, "✓ LVGL initialized, task started");

    // ========================================================================
    // 5. Create Initial UI Screen
    // Show "Initializing..." status while other subsystems start
    // ========================================================================
    ESP_LOGI(TAG, "[5/8] Creating UI screen...");
    if (ui_lock(-1)) {
        ui_create_main_screen(ui_on_button_clicked);
        ui_update_status_label("Initializing...\nConnecting to WiFi");
        ui_unlock();
        ESP_LOGI(TAG, "✓ UI screen created");
    } else {
        ESP_LOGE(TAG, "Failed to acquire UI lock!");
        return;
    }

    // ========================================================================
    // 6. Initialize WiFi (blocking until connected)
    // ========================================================================
    ESP_LOGI(TAG, "[6/8] Connecting to WiFi...");
    ESP_ERROR_CHECK(wifi_init_sta(WIFI_SSID, WIFI_PASSWORD));
    ESP_LOGI(TAG, "✓ WiFi connected");
    
    // Update UI
    if (ui_lock(1000)) {
        ui_update_status_label("WiFi Connected\nInitializing Audio...");
        ui_unlock();
    }

    // ========================================================================
    // 7. Initialize Audio Subsystem (ES8311 codec + I2S)
    // Note: Audio codec uses shared I2C bus initialized in step 2
    // ========================================================================
    ESP_LOGI(TAG, "[7/8] Initializing audio subsystem...");
    ESP_ERROR_CHECK(audio_init());
    ESP_LOGI(TAG, "✓ Audio ready (ES8311 + I2S, 16kHz mono)");
    
    // Update UI
    if (ui_lock(1000)) {
        ui_update_status_label("Audio Ready\nInitializing WebSocket...");
        ui_unlock();
    }

    // ========================================================================
    // 8. Initialize WebSocket Client
    // ========================================================================
    ESP_LOGI(TAG, "[8/8] Initializing WebSocket client...");
    ui_setup_event_handlers();  // Setup audio buffer and callbacks
    
    ESP_ERROR_CHECK(ws_init(XAI_API_KEY, 
                            ui_get_audio_callback(),
                            ui_get_status_callback(),
                            ui_get_transcript_callback()));
    ESP_LOGI(TAG, "✓ WebSocket client initialized");
    
    // Update UI to ready state
    if (ui_lock(1000)) {
        ui_update_status_label("Ready!\nTap button to talk to Grok");
        ui_unlock();
    }

    // ========================================================================
    // Initialization Complete
    // ========================================================================
    ESP_LOGI(TAG, "=================================================");
    ESP_LOGI(TAG, "✓ Initialization complete!");
    ESP_LOGI(TAG, "  Tap the button on screen to start conversation");
    ESP_LOGI(TAG, "=================================================");

    // Main task can now exit - everything runs in FreeRTOS tasks:
    // - LVGL task (rendering + touch input)
    // - WebSocket task (network I/O)
    // - WiFi task (connection management)
    // - Audio I2S DMA (hardware)
    
    // Keep main task alive for monitoring (optional)
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));  // 10 seconds
        
        // Print status
        ESP_LOGI(TAG, "Status: WiFi=%s, WebSocket=%s, Audio=%s",
                 wifi_is_connected() ? "connected" : "disconnected",
                 ws_is_connected() ? "connected" : "disconnected",
                 audio_is_playing() ? "playing" : "idle");
    }
}

