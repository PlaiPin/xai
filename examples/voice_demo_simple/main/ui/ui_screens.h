/**
 * @file ui_screens.h
 * @brief UI screen layouts for voice demo
 */

#ifndef UI_SCREENS_H
#define UI_SCREENS_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Button state enumeration
 */
typedef enum {
    BTN_STATE_READY,      // Ready to send request (blue)
    BTN_STATE_CONNECTING, // WebSocket connecting (gray, disabled)
    BTN_STATE_SPEAKING,   // Audio playing (green, pulsing)
    BTN_STATE_ERROR       // Error occurred (red)
} button_state_t;

/**
 * @brief Callback function for button click
 */
typedef void (*button_click_callback_t)(void);

/**
 * @brief Create main screen with button
 * 
 * Creates a screen with:
 * - Status label at top
 * - Large circular button in center
 * - Button label
 * 
 * @param btn_cb Callback function for button click
 * @return Root screen object
 */
lv_obj_t* ui_create_main_screen(button_click_callback_t btn_cb);

/**
 * @brief Update status label text
 * 
 * Thread-safe: Must be called with ui_lock() held!
 * 
 * @param text Status text to display
 */
void ui_update_status_label(const char *text);

/**
 * @brief Set button state (changes color, enable/disable, animation)
 * 
 * Thread-safe: Must be called with ui_lock() held!
 * 
 * @param state New button state
 */
void ui_set_button_state(button_state_t state);

/**
 * @brief Clear transcript label
 *
 * Thread-safe: Must be called with ui_lock() held!
 */
void ui_clear_transcript(void);

/**
 * @brief Append text to transcript label
 * 
 * Thread-safe: Must be called with ui_lock() held!
 * 
 * @param text Text fragment to append
 */
void ui_append_transcript(const char *text);

#ifdef __cplusplus
}
#endif

#endif // UI_SCREENS_H

