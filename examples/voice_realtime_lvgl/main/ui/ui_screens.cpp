/**
 * @file ui_screens.c
 * @brief UI screen layouts implementation
 */

#include "ui_screens.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "ui_screens";

// UI objects (global for updating from other modules)
static lv_obj_t *status_label = NULL;
static lv_obj_t *main_button = NULL;
static lv_obj_t *button_label = NULL;
static lv_obj_t *transcript_label = NULL;
static lv_anim_t button_anim;
static button_state_t current_state = BTN_STATE_CONNECTING;

// Button callback storage
static button_click_callback_t button_callback = NULL;

// Button animation callback
static void button_anim_cb(void *var, int32_t value)
{
    lv_obj_set_style_bg_opa((lv_obj_t *)var, value, 0);
}

// Button click event handler
static void button_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    // Log the full press lifecycle so we can see if touch input is producing
    // PRESSED/RELEASED but never CLICKED (common when input is flaky).
    if (code == LV_EVENT_PRESSED) {
        ESP_LOGI(TAG, "Button event: PRESSED (state=%d)", current_state);
    } else if (code == LV_EVENT_RELEASED) {
        ESP_LOGI(TAG, "Button event: RELEASED (state=%d)", current_state);
    } else if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Button event: CLICKED (state=%d)", current_state);
        if (button_callback && current_state == BTN_STATE_READY) {
            button_callback();
        } else {
            ESP_LOGW(TAG, "Button not ready (state=%d, cb=%p)", current_state, button_callback);
        }
    }
}

lv_obj_t* ui_create_main_screen(button_click_callback_t btn_cb)
{
    ESP_LOGI(TAG, "Creating main screen...");
    
    // Store callback
    button_callback = btn_cb;
    
    // Create root screen
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x000000), 0);  // Black background
    
    // Status label (top)
    status_label = lv_label_create(screen);
    lv_label_set_text(status_label, "Starting...");
    lv_obj_set_style_text_color(status_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(status_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 30);
    
    // Main button (center)
    main_button = lv_btn_create(screen);
    lv_obj_set_size(main_button, 200, 200);  // Large circular button
    lv_obj_set_style_radius(main_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(main_button, lv_color_hex(0x2196F3), 0);  // Blue
    lv_obj_center(main_button);
    lv_obj_add_event_cb(main_button, button_event_cb, LV_EVENT_ALL, NULL);
    
    // Button icon/label
    button_label = lv_label_create(main_button);
    lv_label_set_text(button_label, LV_SYMBOL_AUDIO "\nTap to Ask");
    lv_obj_set_style_text_color(button_label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(button_label, LV_FONT_DEFAULT, 0);
    lv_obj_set_style_text_align(button_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(button_label);
    
    // Transcript label (bottom, scrollable)
    transcript_label = lv_label_create(screen);
    lv_label_set_text(transcript_label, "");
    lv_label_set_long_mode(transcript_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(transcript_label, 400);
    lv_obj_set_style_text_color(transcript_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_set_style_text_font(transcript_label, LV_FONT_DEFAULT, 0);
    lv_obj_align(transcript_label, LV_ALIGN_BOTTOM_MID, 0, -20);
    
    // Load screen
    lv_scr_load(screen);

    // Default to disabled until WebSocket is connected + session.updated received.
    ui_set_button_state(BTN_STATE_CONNECTING);
    
    ESP_LOGI(TAG, "Main screen created");
    return screen;
}

void ui_update_status_label(const char *text)
{
    if (status_label && text) {
        lv_label_set_text(status_label, text);
        ESP_LOGI(TAG, "Status updated: %s", text);
    }
}

void ui_set_button_state(button_state_t state)
{
    if (!main_button || !button_label) {
        ESP_LOGW(TAG, "Button not initialized");
        return;
    }
    
    current_state = state;
    
    // Stop any existing animation
    lv_anim_del(main_button, button_anim_cb);
    
    switch (state) {
    case BTN_STATE_READY:
        lv_obj_set_style_bg_color(main_button, lv_color_hex(0x2196F3), 0);  // Blue
        lv_obj_set_style_bg_opa(main_button, LV_OPA_COVER, 0);
        lv_label_set_text(button_label, LV_SYMBOL_AUDIO "\nTap to Ask");
        lv_obj_clear_state(main_button, LV_STATE_DISABLED);
        ESP_LOGI(TAG, "Button state: READY");
        break;
        
    case BTN_STATE_CONNECTING:
        lv_obj_set_style_bg_color(main_button, lv_color_hex(0x757575), 0);  // Gray
        lv_obj_set_style_bg_opa(main_button, LV_OPA_COVER, 0);
        lv_label_set_text(button_label, LV_SYMBOL_WIFI "\nConnecting...");
        lv_obj_add_state(main_button, LV_STATE_DISABLED);
        ESP_LOGI(TAG, "Button state: CONNECTING");
        break;
        
    case BTN_STATE_SPEAKING:
        lv_obj_set_style_bg_color(main_button, lv_color_hex(0x4CAF50), 0);  // Green
        lv_label_set_text(button_label, LV_SYMBOL_VOLUME_MAX "\nSpeaking...");
        lv_obj_add_state(main_button, LV_STATE_DISABLED);
        
        // Start pulsing animation
        lv_anim_init(&button_anim);
        lv_anim_set_var(&button_anim, main_button);
        lv_anim_set_exec_cb(&button_anim, button_anim_cb);
        lv_anim_set_values(&button_anim, LV_OPA_COVER, LV_OPA_50);
        lv_anim_set_time(&button_anim, 1000);
        lv_anim_set_playback_time(&button_anim, 1000);
        lv_anim_set_repeat_count(&button_anim, LV_ANIM_REPEAT_INFINITE);
        lv_anim_start(&button_anim);
        
        ESP_LOGI(TAG, "Button state: SPEAKING (pulsing)");
        break;
        
    case BTN_STATE_ERROR:
        lv_obj_set_style_bg_color(main_button, lv_color_hex(0xF44336), 0);  // Red
        lv_obj_set_style_bg_opa(main_button, LV_OPA_COVER, 0);
        lv_label_set_text(button_label, LV_SYMBOL_WARNING "\nError");
        lv_obj_clear_state(main_button, LV_STATE_DISABLED);
        ESP_LOGI(TAG, "Button state: ERROR");
        break;
    }
}

void ui_clear_transcript(void)
{
    if (transcript_label) {
        lv_label_set_text(transcript_label, "");
    }
}

void ui_append_transcript(const char *text)
{
    if (transcript_label && text) {
        const char *current = lv_label_get_text(transcript_label);
        char new_text[512];
        
        // Append to existing text
        snprintf(new_text, sizeof(new_text), "%s%s", current, text);
        lv_label_set_text(transcript_label, new_text);
    }
}

