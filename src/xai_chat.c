/**
 * @file xai_chat.c
 * @brief Chat completions API implementation
 * 
 * Implements both synchronous and streaming chat completions using the
 * xAI /v1/chat/completions endpoint.
 */

#include <string.h>
#include <stdlib.h>
#include "xai.h"
#include "xai_internal.h"
#include "esp_log.h"

static const char *TAG = "xai_chat";

/* ========================================================================
 * Synchronous Chat Completion
 * ======================================================================== */

xai_err_t xai_chat_completion(
    xai_client_t client,
    const xai_message_t *messages,
    size_t message_count,
    const xai_options_t *options,
    xai_response_t *response
) {
    if (!client || !messages || message_count == 0 || !response) {
        ESP_LOGE(TAG, "Invalid arguments");
        return XAI_ERR_INVALID_ARG;
    }

    struct xai_client_s *client_impl = (struct xai_client_s *)client;
    xai_err_t err = XAI_OK;

    // Acquire client mutex
    if (xSemaphoreTake(client_impl->mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex");
        return XAI_ERR_TIMEOUT;
    }

    // Allocate request buffer (16KB should be enough for most requests)
    char *request_buffer = malloc(16384);
    if (!request_buffer) {
        ESP_LOGE(TAG, "Failed to allocate request buffer");
        xSemaphoreGive(client_impl->mutex);
        return XAI_ERR_NO_MEMORY;
    }

    // Build JSON request
    size_t request_len = 0;
    err = xai_json_build_chat_request(
        request_buffer,
        16384,
        &request_len,
        messages,
        message_count,
        options,
        client_impl->default_model
    );

    if (err != XAI_OK) {
        ESP_LOGE(TAG, "Failed to build request: %d", err);
        free(request_buffer);
        xSemaphoreGive(client_impl->mutex);
        return err;
    }

    ESP_LOGI(TAG, "Sending chat completion request (%zu bytes)", request_len);
    ESP_LOGD(TAG, "Request JSON: %.*s", (int)request_len, request_buffer);

    // Send HTTP POST request
    char *response_data = NULL;
    size_t response_len = 0;
    err = xai_http_post(
        client_impl->http_client,
        "/chat/completions",
        request_buffer,
        request_len,
        &response_data,
        &response_len
    );

    free(request_buffer);

    if (err != XAI_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %d", err);
        xSemaphoreGive(client_impl->mutex);
        return err;
    }

    ESP_LOGI(TAG, "Received response (%zu bytes)", response_len);
    ESP_LOGD(TAG, "Response JSON: %.*s", (int)response_len, response_data);

    // Parse JSON response
    err = xai_json_parse_chat_response(response_data, response);

    // Free response buffer (parser makes copies of needed data)
    free(response_data);

    xSemaphoreGive(client_impl->mutex);

    if (err != XAI_OK) {
        ESP_LOGE(TAG, "Failed to parse response: %d", err);
        return err;
    }

    ESP_LOGI(TAG, "Chat completion successful (tokens: %u prompt + %u completion = %u total)",
             response->prompt_tokens, response->completion_tokens, response->total_tokens);

    return XAI_OK;
}

/* ========================================================================
 * Streaming Chat Completion
 * ======================================================================== */

xai_err_t xai_chat_completion_stream(
    xai_client_t client,
    const xai_message_t *messages,
    size_t message_count,
    const xai_options_t *options,
    xai_stream_callback_t callback,
    void *user_data
) {
    if (!client || !messages || message_count == 0 || !callback) {
        ESP_LOGE(TAG, "Invalid arguments");
        return XAI_ERR_INVALID_ARG;
    }

    struct xai_client_s *client_impl = (struct xai_client_s *)client;
    xai_err_t err = XAI_OK;

    // Acquire client mutex
    if (xSemaphoreTake(client_impl->mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex");
        return XAI_ERR_TIMEOUT;
    }

    // Create modified options with stream=true
    xai_options_t stream_options;
    if (options) {
        memcpy(&stream_options, options, sizeof(xai_options_t));
    } else {
        memset(&stream_options, 0, sizeof(xai_options_t));
    }
    stream_options.stream = true;

    // Allocate request buffer
    char *request_buffer = malloc(16384);
    if (!request_buffer) {
        ESP_LOGE(TAG, "Failed to allocate request buffer");
        xSemaphoreGive(client_impl->mutex);
        return XAI_ERR_NO_MEMORY;
    }

    // Build JSON request
    size_t request_len = 0;
    err = xai_json_build_chat_request(
        request_buffer,
        16384,
        &request_len,
        messages,
        message_count,
        &stream_options,
        client_impl->default_model
    );

    if (err != XAI_OK) {
        ESP_LOGE(TAG, "Failed to build request: %d", err);
        free(request_buffer);
        xSemaphoreGive(client_impl->mutex);
        return err;
    }

    ESP_LOGI(TAG, "Sending streaming chat completion request (%zu bytes)", request_len);
    ESP_LOGI(TAG, "Request body: %.*s", (int)request_len, request_buffer);  // Temporarily INFO for debugging

    // Send streaming HTTP POST request
    err = xai_http_post_stream(
        client_impl->http_client,
        "/chat/completions",
        request_buffer,
        request_len,
        callback,
        user_data
    );

    free(request_buffer);
    xSemaphoreGive(client_impl->mutex);

    if (err != XAI_OK) {
        ESP_LOGE(TAG, "Streaming request failed: %d", err);
        return err;
    }

    ESP_LOGI(TAG, "Streaming chat completion completed");
    return XAI_OK;
}

/* ========================================================================
 * Convenience: Simple Text Completion
 * ======================================================================== */

xai_err_t xai_text_completion(
    xai_client_t client,
    const char *prompt,
    char **response_text,
    size_t *response_len
) {
    if (!client || !prompt || !response_text) {
        ESP_LOGE(TAG, "Invalid arguments");
        return XAI_ERR_INVALID_ARG;
    }

    // Create simple user message
    xai_message_t message = {
        .role = XAI_ROLE_USER,
        .content = prompt,
        .name = NULL,
        .tool_call_id = NULL,
        .tool_calls = NULL,
        .tool_call_count = 0,
        .images = NULL,
        .image_count = 0
    };

    // Call chat completion
    xai_response_t response;
    xai_err_t err = xai_chat_completion(client, &message, 1, NULL, &response);
    if (err != XAI_OK) {
        return err;
    }

    // Extract content
    if (response.content) {
        *response_text = response.content;
        if (response_len) {
            *response_len = strlen(response.content);
        }
        
        // Transfer ownership of content string to caller
        response.content = NULL;
    } else {
        *response_text = NULL;
        if (response_len) {
            *response_len = 0;
        }
    }

    // Free rest of response
    xai_response_free(&response);

    return XAI_OK;
}

/* ========================================================================
 * Advanced: Chat Completion with Search
 * ======================================================================== */

#ifdef CONFIG_XAI_ENABLE_SEARCH

xai_err_t xai_chat_completion_with_search(
    xai_client_t client,
    const xai_message_t *messages,
    size_t message_count,
    const xai_search_params_t *search_params,
    xai_response_t *response
) {
    if (!client || !messages || message_count == 0 || !response) {
        ESP_LOGE(TAG, "Invalid arguments");
        return XAI_ERR_INVALID_ARG;
    }

    // Create options with search parameters
    xai_options_t options = {
        .model = NULL,
        .temperature = -1.0f,
        .max_tokens = 0,
        .stream = false,
        .stop = {NULL},
        .top_p = -1.0f,
        .presence_penalty = 0.0f,
        .frequency_penalty = 0.0f,
        .user_id = NULL,
        .search_params = (xai_search_params_t *)search_params,
        .reasoning_effort = NULL,
        .parallel_function_calling = false,
        .tools = NULL,
        .tool_count = 0
    };

    return xai_chat_completion(client, messages, message_count, &options, response);
}

/* ========================================================================
 * Advanced: Chat Completion with Tools
 * ======================================================================== */

#ifdef CONFIG_XAI_ENABLE_TOOLS

xai_err_t xai_chat_completion_with_tools(
    xai_client_t client,
    const xai_message_t *messages,
    size_t message_count,
    const xai_tool_t *tools,
    size_t tool_count,
    xai_response_t *response
) {
    if (!client || !messages || message_count == 0 || !tools || tool_count == 0 || !response) {
        ESP_LOGE(TAG, "Invalid arguments");
        return XAI_ERR_INVALID_ARG;
    }

    // Create options with tools
    xai_options_t options = {
        .model = NULL,
        .temperature = -1.0f,
        .max_tokens = 0,
        .stream = false,
        .stop = {NULL},
        .top_p = -1.0f,
        .presence_penalty = 0.0f,
        .frequency_penalty = 0.0f,
        .user_id = NULL,
        .search_params = NULL,
        .reasoning_effort = NULL,
        .parallel_function_calling = false,
        .tools = (xai_tool_t *)tools,
        .tool_count = tool_count
    };

    return xai_chat_completion(client, messages, message_count, &options, response);
}

#endif // CONFIG_XAI_ENABLE_TOOLS

/* ========================================================================
 * Convenience: Web Search
 * ======================================================================== */

xai_err_t xai_web_search(
    xai_client_t client,
    const char *prompt,
    xai_search_mode_t search_mode,
    bool return_citations,
    xai_response_t *response
) {
    if (!client || !prompt || !response) {
        ESP_LOGE(TAG, "Invalid arguments");
        return XAI_ERR_INVALID_ARG;
    }

    // Create simple user message
    xai_message_t message = {
        .role = XAI_ROLE_USER,
        .content = prompt,
        .name = NULL,
        .tool_call_id = NULL,
        .tool_calls = NULL,
        .tool_call_count = 0,
        .images = NULL,
        .image_count = 0
    };

    // Create search parameters for web source
    xai_search_source_t web_source = {
        .type = XAI_SOURCE_WEB,
        .web = {
            .allowed_websites = NULL,
            .excluded_websites = NULL,
            .safe_search = false
        }
    };

    xai_search_params_t search_params = {
        .mode = search_mode,
        .return_citations = return_citations,
        .max_results = 0,  // Use default
        .sources = &web_source,
        .source_count = 1
    };

    return xai_chat_completion_with_search(client, &message, 1, &search_params, response);
}

#endif // CONFIG_XAI_ENABLE_SEARCH

/* ========================================================================
 * Vision: Image Analysis
 * ======================================================================== */

#ifdef CONFIG_XAI_ENABLE_VISION

xai_err_t xai_vision_completion(
    xai_client_t client,
    const char *prompt,
    const xai_image_t *images,
    size_t image_count,
    xai_response_t *response
) {
    if (!client || !prompt || !images || image_count == 0 || !response) {
        ESP_LOGE(TAG, "Invalid arguments");
        return XAI_ERR_INVALID_ARG;
    }

    // Create user message with images
    xai_message_t message = {
        .role = XAI_ROLE_USER,
        .content = prompt,
        .name = NULL,
        .tool_call_id = NULL,
        .tool_calls = NULL,
        .tool_call_count = 0,
        .images = (xai_image_t *)images,
        .image_count = image_count
    };

    // Use vision model
    xai_options_t options = {
        .model = "grok-2-vision-latest",
        .temperature = -1.0f,
        .max_tokens = 0,
        .stream = false,
        .stop = {NULL},
        .top_p = -1.0f,
        .presence_penalty = 0.0f,
        .frequency_penalty = 0.0f,
        .user_id = NULL,
        .search_params = NULL,
        .reasoning_effort = NULL,
        .parallel_function_calling = false,
        .tools = NULL,
        .tool_count = 0
    };

    return xai_chat_completion(client, &message, 1, &options, response);
}

#endif // CONFIG_XAI_ENABLE_VISION

