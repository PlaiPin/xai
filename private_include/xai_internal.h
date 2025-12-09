/**
 * @file xai_internal.h
 * @brief xAI ESP-IDF Component - Internal Structures and APIs
 * 
 * Private header file - not exposed to users.
 * 
 * @copyright 2025
 * @license Apache-2.0
 */

#pragma once

#include "xai.h"
#include "esp_http_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief HTTP client handle
 */
typedef struct {
    esp_http_client_handle_t client;
    char *base_url;                 /**< Stored base URL for reconstructing paths */
    char *response_buffer;
    size_t response_size;
    size_t response_capacity;
    xai_stream_callback_t stream_callback;
    void *stream_user_data;
} xai_http_client_t;

/**
 * @brief Buffer for memory management
 */
typedef struct {
    char *data;
    size_t capacity;
    size_t used;
    bool in_use;
} xai_buffer_t;

/**
 * @brief Buffer pool
 */
typedef struct {
    xai_buffer_t *buffers;
    size_t count;
    SemaphoreHandle_t mutex;
} xai_buffer_pool_t;

/**
 * @brief Client implementation structure
 */
struct xai_client_s {
    char *api_key;
    char *base_url;
    char *default_model;
    uint32_t timeout_ms;
    uint32_t max_retries;
    float default_temperature;
    size_t default_max_tokens;
    
    xai_http_client_t *http_client;
    xai_buffer_pool_t *buffer_pool;
    SemaphoreHandle_t mutex;
};

/**
 * @brief Conversation implementation structure
 */
struct xai_conversation_s {
    xai_message_t *messages;
    size_t message_count;
    size_t message_capacity;
    char *system_prompt;
};

/**
 * @brief SSE stream parser state
 */
typedef enum {
    SSE_STATE_IDLE,
    SSE_STATE_FIELD,
    SSE_STATE_VALUE,
    SSE_STATE_EOL
} sse_state_t;

/**
 * @brief SSE stream parser
 */
typedef struct {
    sse_state_t state;
    char field_buffer[32];
    xai_buffer_t *data_buffer;
    xai_stream_callback_t callback;
    void *user_data;
} xai_stream_parser_t;

// ============================================================================
// HTTP Client Functions (xai_http.c)
// ============================================================================

/**
 * @brief Create HTTP client
 */
xai_http_client_t* xai_http_client_create(
    const char *base_url,
    const char *api_key,
    uint32_t timeout_ms
);

/**
 * @brief Destroy HTTP client
 */
void xai_http_client_destroy(xai_http_client_t *client);

/**
 * @brief Perform POST request
 */
xai_err_t xai_http_post(
    xai_http_client_t *client,
    const char *path,
    const char *body,
    size_t body_len,
    char **response,
    size_t *response_len
);

/**
 * @brief Perform streaming POST request
 */
xai_err_t xai_http_post_stream(
    xai_http_client_t *client,
    const char *path,
    const char *body,
    size_t body_len,
    xai_stream_callback_t callback,
    void *user_data
);

/**
 * @brief Perform GET request
 */
xai_err_t xai_http_get(
    xai_http_client_t *client,
    const char *path,
    char **response,
    size_t *response_len
);


// ============================================================================
// JSON Functions (xai_json.c)
// ============================================================================

/**
 * @brief Build chat completion request JSON
 */
xai_err_t xai_json_build_chat_request(
    char *buffer,
    size_t buffer_size,
    size_t *bytes_written,
    const xai_message_t *messages,
    size_t message_count,
    const xai_options_t *options,
    const char *default_model
);

/**
 * @brief Parse chat completion response
 */
xai_err_t xai_json_parse_chat_response(
    const char *json_str,
    xai_response_t *response
);

/**
 * @brief Parse streaming chunk
 */
xai_err_t xai_json_parse_stream_chunk(
    const char *json_str,
    char **content_delta,
    bool *is_done
);

// ============================================================================
// Stream Parser Functions (xai_stream.c)
// ============================================================================

/**
 * @brief Create stream parser
 */
xai_stream_parser_t* xai_stream_parser_create(
    xai_stream_callback_t callback,
    void *user_data
);

/**
 * @brief Feed data to stream parser
 */
void xai_stream_parser_feed(
    const char *data,
    size_t len,
    void *user_data
);

/**
 * @brief Destroy stream parser
 */
void xai_stream_parser_destroy(xai_stream_parser_t *parser);

// ============================================================================
// Buffer Pool Functions (xai.c)
// ============================================================================

/**
 * @brief Create buffer pool
 */
xai_buffer_pool_t* xai_buffer_pool_create(size_t buffer_count, size_t buffer_size);

/**
 * @brief Acquire buffer from pool
 */
xai_buffer_t* xai_buffer_pool_acquire(xai_buffer_pool_t *pool);

/**
 * @brief Release buffer back to pool
 */
void xai_buffer_pool_release(xai_buffer_pool_t *pool, xai_buffer_t *buffer);

/**
 * @brief Destroy buffer pool
 */
void xai_buffer_pool_destroy(xai_buffer_pool_t *pool);

// ============================================================================
// Utility Macros
// ============================================================================

#define XAI_CHECK(condition, err_code, msg) \
    do { \
        if (!(condition)) { \
            ESP_LOGE(TAG, "%s (%s:%d)", msg, __FILE__, __LINE__); \
            return err_code; \
        } \
    } while(0)

#define XAI_CHECK_NULL(ptr, msg) XAI_CHECK(ptr != NULL, XAI_ERR_INVALID_ARG, msg)

#define XAI_LOCK(client) \
    xSemaphoreTake(client->mutex, portMAX_DELAY)

#define XAI_UNLOCK(client) \
    xSemaphoreGive(client->mutex)

#ifdef __cplusplus
}
#endif

