/**
 * @file xai_error.c
 * @brief Error handling and logging utilities
 * 
 * Provides error code to string conversion and error handling utilities.
 */

#include "xai.h"
#include "esp_log.h"

static const char *TAG = "xai_error";

/**
 * @brief Convert error code to human-readable string
 */
const char* xai_err_to_string(xai_err_t err) {
    switch (err) {
        case XAI_OK:
            return "Success";
        case XAI_ERR_INVALID_ARG:
            return "Invalid argument";
        case XAI_ERR_NO_MEMORY:
            return "Out of memory";
        case XAI_ERR_HTTP_FAILED:
            return "HTTP request failed";
        case XAI_ERR_PARSE_FAILED:
            return "JSON parse failed";
        case XAI_ERR_AUTH_FAILED:
            return "Authentication failed";
        case XAI_ERR_RATE_LIMIT:
            return "Rate limit exceeded";
        case XAI_ERR_TIMEOUT:
            return "Operation timed out";
        case XAI_ERR_API_ERROR:
            return "API error";
        case XAI_ERR_NOT_READY:
            return "Not ready";
        case XAI_ERR_WS_FAILED:
            return "WebSocket operation failed";
        case XAI_ERR_BUSY:
            return "Busy";
        default:
            return "Unknown error";
    }
}

/**
 * @brief Log error with details
 */
void xai_log_error(xai_err_t err, const char *context, const char *file, int line) {
    ESP_LOGE(TAG, "[%s] %s (error code: %d) at %s:%d",
             context ? context : "xAI",
             xai_err_to_string(err),
             err,
             file,
             line);
}

/**
 * @brief Check condition and log error if false
 */
bool xai_check_condition(
    bool condition,
    xai_err_t *err_out,
    xai_err_t err_code,
    const char *message,
    const char *file,
    int line
) {
    if (!condition) {
        if (err_out) {
            *err_out = err_code;
        }
        ESP_LOGE(TAG, "%s (error code: %d) at %s:%d",
                 message ? message : "Condition check failed",
                 err_code,
                 file,
                 line);
        return false;
    }
    return true;
}

