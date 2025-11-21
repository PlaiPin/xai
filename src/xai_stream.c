/**
 * @file xai_stream.c
 * @brief Server-Sent Events (SSE) stream parser
 * 
 * Implements a state machine parser for SSE format used by xAI's streaming API.
 * 
 * SSE Format:
 * data: {"choices":[{"delta":{"content":"Hello"}}]}
 * 
 * data: {"choices":[{"delta":{"content":" world"}}]}
 * 
 * data: [DONE]
 */

#include "sdkconfig.h"

#ifdef CONFIG_XAI_ENABLE_STREAMING

#include <string.h>
#include <stdlib.h>
#include "xai_internal.h"
#include "esp_log.h"

static const char *TAG = "xai_stream";

/**
 * @brief Create SSE stream parser
 */
xai_stream_parser_t* xai_stream_parser_create(
    xai_stream_callback_t callback,
    void *user_data
) {
    if (!callback) {
        ESP_LOGE(TAG, "Callback is required");
        return NULL;
    }

    xai_stream_parser_t *parser = calloc(1, sizeof(xai_stream_parser_t));
    if (!parser) {
        ESP_LOGE(TAG, "Failed to allocate parser");
        return NULL;
    }

    parser->state = SSE_STATE_IDLE;
    parser->callback = callback;
    parser->user_data = user_data;

    // Allocate data buffer for accumulating JSON
    parser->data_buffer = calloc(1, sizeof(xai_buffer_t));
    if (!parser->data_buffer) {
        ESP_LOGE(TAG, "Failed to allocate data buffer");
        free(parser);
        return NULL;
    }

    parser->data_buffer->capacity = 8192;  // 8KB buffer for streaming chunks
    parser->data_buffer->data = malloc(parser->data_buffer->capacity);
    if (!parser->data_buffer->data) {
        ESP_LOGE(TAG, "Failed to allocate data buffer memory");
        free(parser->data_buffer);
        free(parser);
        return NULL;
    }

    parser->data_buffer->used = 0;
    parser->data_buffer->in_use = true;

    ESP_LOGD(TAG, "Created stream parser");
    return parser;
}

/**
 * @brief Feed data to stream parser
 * 
 * Parses SSE format line by line:
 * - Lines starting with "data: " contain JSON chunks
 * - Empty lines trigger event delivery
 * - "data: [DONE]" signals end of stream
 */
void xai_stream_parser_feed(
    const char *data,
    size_t len,
    void *user_data
) {
    xai_stream_parser_t *parser = (xai_stream_parser_t *)user_data;
    
    if (!parser || !data || len == 0) {
        return;
    }

    for (size_t i = 0; i < len; i++) {
        char c = data[i];

        switch (parser->state) {
            case SSE_STATE_IDLE:
                if (c == 'd') {
                    // Start of potential "data:" field
                    parser->state = SSE_STATE_FIELD;
                    parser->field_buffer[0] = c;
                    parser->field_buffer[1] = '\0';
                } else if (c == '\n' || c == '\r') {
                    // Empty line, stay idle
                } else {
                    // Unknown field, skip
                    parser->state = SSE_STATE_FIELD;
                    parser->field_buffer[0] = c;
                    parser->field_buffer[1] = '\0';
                }
                break;

            case SSE_STATE_FIELD:
                if (c == ':') {
                    // End of field name
                    parser->state = SSE_STATE_VALUE;
                    
                    // Skip space after colon if present
                    if (i + 1 < len && data[i + 1] == ' ') {
                        i++;
                    }
                    
                    // Check if this is a "data" field
                    if (strcmp(parser->field_buffer, "data") == 0) {
                        // Reset data buffer
                        parser->data_buffer->used = 0;
                    }
                } else if (c == '\n' || c == '\r') {
                    // End of line without colon, reset
                    parser->state = SSE_STATE_IDLE;
                } else {
                    // Accumulate field name
                    size_t field_len = strlen(parser->field_buffer);
                    if (field_len < sizeof(parser->field_buffer) - 1) {
                        parser->field_buffer[field_len] = c;
                        parser->field_buffer[field_len + 1] = '\0';
                    }
                }
                break;

            case SSE_STATE_VALUE:
                if (c == '\n' || c == '\r') {
                    // End of value
                    parser->state = SSE_STATE_EOL;
                    
                    // Process "data" field
                    if (strcmp(parser->field_buffer, "data") == 0) {
                        // Null-terminate data
                        if (parser->data_buffer->used < parser->data_buffer->capacity) {
                            parser->data_buffer->data[parser->data_buffer->used] = '\0';
                        }
                        
                        const char *json_str = parser->data_buffer->data;
                        ESP_LOGD(TAG, "Received data: %s", json_str);
                        
                        // Check for [DONE] marker
                        if (strcmp(json_str, "[DONE]") == 0) {
                            ESP_LOGD(TAG, "Stream completed");
                            // Signal end of stream with NULL content
                            parser->callback(NULL, 0, parser->user_data);
                        } else {
                            // Parse JSON chunk and extract content delta
                            char *content_delta = NULL;
                            bool is_done = false;
                            
                            xai_err_t err = xai_json_parse_stream_chunk(
                                json_str,
                                &content_delta,
                                &is_done
                            );
                            
                            if (err == XAI_OK && content_delta) {
                                // Deliver content to callback
                                parser->callback(content_delta, strlen(content_delta), parser->user_data);
                                free(content_delta);
                            }
                            
                            if (is_done) {
                                // Signal end of stream
                                parser->callback(NULL, 0, parser->user_data);
                            }
                        }
                        
                        // Reset data buffer
                        parser->data_buffer->used = 0;
                    }
                } else {
                    // Accumulate value (for "data" field, this is JSON)
                    if (strcmp(parser->field_buffer, "data") == 0) {
                        if (parser->data_buffer->used < parser->data_buffer->capacity - 1) {
                            parser->data_buffer->data[parser->data_buffer->used++] = c;
                        } else {
                            ESP_LOGW(TAG, "Data buffer overflow");
                        }
                    }
                }
                break;

            case SSE_STATE_EOL:
                if (c == '\n' || c == '\r') {
                    // Another newline, stay in EOL state
                } else {
                    // Start of new field
                    parser->state = SSE_STATE_IDLE;
                    i--;  // Reprocess this character
                }
                break;
        }
    }
}

/**
 * @brief Destroy stream parser
 */
void xai_stream_parser_destroy(xai_stream_parser_t *parser) {
    if (!parser) {
        return;
    }

    if (parser->data_buffer) {
        if (parser->data_buffer->data) {
            free(parser->data_buffer->data);
        }
        free(parser->data_buffer);
    }

    free(parser);
    ESP_LOGD(TAG, "Destroyed stream parser");
}

#endif // CONFIG_XAI_ENABLE_STREAMING

