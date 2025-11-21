/**
 * @file xai_tokenize.c
 * @brief Tokenization endpoint implementation
 * 
 * Provides token counting functionality for pre-flight resource estimation.
 * Useful for ESP32 memory planning and rate limit management.
 */

#include <string.h>
#include <stdlib.h>
#include "xai.h"
#include "xai_internal.h"
#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "xai_tokenize";

/**
 * @brief Count tokens in text
 * 
 * Calls POST /v1/tokenize-text endpoint
 */
xai_err_t xai_count_tokens(
    xai_client_t client,
    const char *text,
    const char *model,
    uint32_t *token_count
) {
    if (!client || !text || !token_count) {
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

    // Build JSON request
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        xSemaphoreGive(client_impl->mutex);
        return XAI_ERR_NO_MEMORY;
    }

    cJSON_AddStringToObject(root, "text", text);
    
    const char *model_to_use = model ? model : client_impl->default_model;
    cJSON_AddStringToObject(root, "model", model_to_use);

    char *request_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!request_json) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        xSemaphoreGive(client_impl->mutex);
        return XAI_ERR_NO_MEMORY;
    }

    size_t request_len = strlen(request_json);
    ESP_LOGI(TAG, "Counting tokens for text (%zu chars)", strlen(text));

    // Send HTTP POST request
    char *response_data = NULL;
    size_t response_len = 0;
    err = xai_http_post(
        client_impl->http_client,
        "/tokenize-text",
        request_json,
        request_len,
        &response_data,
        &response_len
    );

    free(request_json);

    if (err != XAI_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %d", err);
        xSemaphoreGive(client_impl->mutex);
        return err;
    }

    ESP_LOGD(TAG, "Response: %.*s", (int)response_len, response_data);

    // Parse response
    cJSON *response = cJSON_Parse(response_data);
    free(response_data);

    if (!response) {
        ESP_LOGE(TAG, "Failed to parse response JSON");
        xSemaphoreGive(client_impl->mutex);
        return XAI_ERR_PARSE_FAILED;
    }

    // Check for error
    cJSON *error = cJSON_GetObjectItem(response, "error");
    if (error) {
        cJSON *message = cJSON_GetObjectItem(error, "message");
        if (message && cJSON_IsString(message)) {
            ESP_LOGE(TAG, "API error: %s", message->valuestring);
        }
        cJSON_Delete(response);
        xSemaphoreGive(client_impl->mutex);
        return XAI_ERR_API_ERROR;
    }

    // Extract token count
    // Response format: {"token_count": 42}
    cJSON *count = cJSON_GetObjectItem(response, "token_count");
    if (!count || !cJSON_IsNumber(count)) {
        ESP_LOGE(TAG, "Missing or invalid token_count in response");
        cJSON_Delete(response);
        xSemaphoreGive(client_impl->mutex);
        return XAI_ERR_PARSE_FAILED;
    }

    *token_count = count->valueint;
    cJSON_Delete(response);
    xSemaphoreGive(client_impl->mutex);

    ESP_LOGI(TAG, "Token count: %u", *token_count);
    return XAI_OK;
}

/**
 * @brief Count tokens in messages (conversation)
 * 
 * Concatenates all message contents and counts tokens.
 * Note: This is an approximation as it doesn't account for message structure overhead.
 */
xai_err_t xai_count_tokens_messages(
    xai_client_t client,
    const xai_message_t *messages,
    size_t message_count,
    const char *model,
    uint32_t *token_count
) {
    if (!client || !messages || message_count == 0 || !token_count) {
        ESP_LOGE(TAG, "Invalid arguments");
        return XAI_ERR_INVALID_ARG;
    }

    // Concatenate all message contents
    size_t total_len = 0;
    for (size_t i = 0; i < message_count; i++) {
        if (messages[i].content) {
            total_len += strlen(messages[i].content) + 1;  // +1 for newline
        }
    }

    char *combined_text = malloc(total_len + 1);
    if (!combined_text) {
        ESP_LOGE(TAG, "Failed to allocate combined text buffer");
        return XAI_ERR_NO_MEMORY;
    }

    size_t offset = 0;
    for (size_t i = 0; i < message_count; i++) {
        if (messages[i].content) {
            size_t len = strlen(messages[i].content);
            memcpy(combined_text + offset, messages[i].content, len);
            offset += len;
            combined_text[offset++] = '\n';
        }
    }
    combined_text[offset] = '\0';

    // Count tokens
    xai_err_t err = xai_count_tokens(client, combined_text, model, token_count);
    free(combined_text);

    if (err == XAI_OK) {
        ESP_LOGI(TAG, "Message token count: %u (approximate)", *token_count);
    }

    return err;
}

/**
 * @brief Estimate memory needed for response
 * 
 * Rough estimation: 1 token ≈ 4 bytes for English text
 */
size_t xai_estimate_memory(uint32_t token_count) {
    // Conservative estimate: 4 bytes per token + overhead
    return (token_count * 4) + 1024;  // +1KB overhead for JSON structure
}

