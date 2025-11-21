/**
 * @file xai_images.c
 * @brief Image generation endpoint implementation
 * 
 * Provides text-to-image generation using xAI's image models.
 * POST /v1/images/generations
 */

#include <string.h>
#include <stdlib.h>
#include "xai.h"
#include "xai_internal.h"
#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "xai_images";

/**
 * @brief Generate image from text prompt
 * 
 * Request format:
 * {
 *   "model": "grok-2-image-latest",
 *   "prompt": "A futuristic ESP32 microcontroller",
 *   "n": 1,
 *   "size": "1024x1024",
 *   "response_format": "url"
 * }
 * 
 * Response format:
 * {
 *   "created": 1234567890,
 *   "data": [
 *     {
 *       "url": "https://...",
 *       "revised_prompt": "..."
 *     }
 *   ]
 * }
 */
xai_err_t xai_generate_image(
    xai_client_t client,
    const xai_image_request_t *request,
    xai_image_response_t *response
) {
    if (!client || !request || !request->prompt || !response) {
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

    memset(response, 0, sizeof(xai_image_response_t));

    // Build JSON request
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        xSemaphoreGive(client_impl->mutex);
        return XAI_ERR_NO_MEMORY;
    }

    // Model (default to grok-2-image-latest)
    const char *model = request->model ? request->model : "grok-2-image-latest";
    cJSON_AddStringToObject(root, "model", model);

    // Prompt
    cJSON_AddStringToObject(root, "prompt", request->prompt);

    // Number of images (1-10)
    uint32_t n = request->n > 0 ? request->n : 1;
    if (n > 10) n = 10;  // xAI maximum is 10
    cJSON_AddNumberToObject(root, "n", n);

    // Response format (url or b64_json)
    const char *format = request->response_format ? request->response_format : "url";
    cJSON_AddStringToObject(root, "response_format", format);

    // NOTE: xAI does NOT support size, quality, style, or user parameters
    // These are silently ignored if provided in the request struct

    char *request_json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!request_json) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        xSemaphoreGive(client_impl->mutex);
        return XAI_ERR_NO_MEMORY;
    }

    size_t request_len = strlen(request_json);
    ESP_LOGI(TAG, "Generating %u image(s): \"%s\" (model: %s, format: %s)", 
             n, request->prompt, model, format);

    // Send HTTP POST request
    char *response_data = NULL;
    size_t response_len = 0;
    err = xai_http_post(
        client_impl->http_client,
        "/images/generations",
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
    cJSON *resp_json = cJSON_Parse(response_data);
    free(response_data);

    if (!resp_json) {
        ESP_LOGE(TAG, "Failed to parse response JSON");
        xSemaphoreGive(client_impl->mutex);
        return XAI_ERR_PARSE_FAILED;
    }

    // Check for error
    cJSON *error = cJSON_GetObjectItem(resp_json, "error");
    if (error) {
        cJSON *message = cJSON_GetObjectItem(error, "message");
        if (message && cJSON_IsString(message)) {
            ESP_LOGE(TAG, "API error: %s", message->valuestring);
        }
        cJSON_Delete(resp_json);
        xSemaphoreGive(client_impl->mutex);
        return XAI_ERR_API_ERROR;
    }

    // Parse created timestamp
    cJSON *created = cJSON_GetObjectItem(resp_json, "created");
    if (created && cJSON_IsNumber(created)) {
        response->created = created->valueint;
    }

    // Parse data array
    cJSON *data = cJSON_GetObjectItem(resp_json, "data");
    if (!data || !cJSON_IsArray(data)) {
        ESP_LOGE(TAG, "Missing or invalid data array in response");
        cJSON_Delete(resp_json);
        xSemaphoreGive(client_impl->mutex);
        return XAI_ERR_PARSE_FAILED;
    }

    response->image_count = cJSON_GetArraySize(data);
    if (response->image_count == 0) {
        ESP_LOGE(TAG, "Empty data array in response");
        cJSON_Delete(resp_json);
        xSemaphoreGive(client_impl->mutex);
        return XAI_ERR_PARSE_FAILED;
    }

    // Allocate image data array
    response->images = calloc(response->image_count, sizeof(xai_image_data_t));
    if (!response->images) {
        ESP_LOGE(TAG, "Failed to allocate image data array");
        cJSON_Delete(resp_json);
        xSemaphoreGive(client_impl->mutex);
        return XAI_ERR_NO_MEMORY;
    }

    // Parse each image
    for (size_t i = 0; i < response->image_count; i++) {
        cJSON *item = cJSON_GetArrayItem(data, i);
        
        // URL or b64_json
        cJSON *url = cJSON_GetObjectItem(item, "url");
        if (url && cJSON_IsString(url)) {
            response->images[i].url = strdup(url->valuestring);
        }
        
        cJSON *b64_json = cJSON_GetObjectItem(item, "b64_json");
        if (b64_json && cJSON_IsString(b64_json)) {
            response->images[i].b64_json = strdup(b64_json->valuestring);
        }
        
        // Revised prompt
        cJSON *revised_prompt = cJSON_GetObjectItem(item, "revised_prompt");
        if (revised_prompt && cJSON_IsString(revised_prompt)) {
            response->images[i].revised_prompt = strdup(revised_prompt->valuestring);
        }
    }

    cJSON_Delete(resp_json);
    xSemaphoreGive(client_impl->mutex);

    ESP_LOGI(TAG, "Image generation successful (%zu images)", response->image_count);
    return XAI_OK;
}

/**
 * @brief Free image response memory
 */
void xai_image_response_free(xai_image_response_t *response) {
    if (!response) {
        return;
    }

    if (response->images) {
        for (size_t i = 0; i < response->image_count; i++) {
            if (response->images[i].url) {
                free(response->images[i].url);
            }
            if (response->images[i].b64_json) {
                free(response->images[i].b64_json);
            }
            if (response->images[i].revised_prompt) {
                free(response->images[i].revised_prompt);
            }
        }
        free(response->images);
        response->images = NULL;
    }

    response->image_count = 0;
    response->created = 0;
}

