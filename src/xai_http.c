/**
 * @file xai_http.c
 * @brief xAI ESP-IDF Component - HTTP Client Wrapper
 * 
 * @copyright 2025
 * @license Apache-2.0
 */

#include "xai_internal.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "xai_http";

// HTTP event handler
static esp_err_t http_event_handler(esp_http_client_event_t *evt) {
    xai_http_client_t *client = (xai_http_client_t*)evt->user_data;
    
    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (!esp_http_client_is_chunked_response(evt->client)) {
                // Non-chunked response
                if (client->response_buffer) {
                    size_t new_size = client->response_size + evt->data_len;
                    if (new_size > client->response_capacity) {
                        ESP_LOGE(TAG, "Response too large: %zu > %zu", 
                                 new_size, client->response_capacity);
                        return ESP_FAIL;
                    }
                    memcpy(client->response_buffer + client->response_size,
                           evt->data, evt->data_len);
                    client->response_size += evt->data_len;
                }
            } else {
                // Streaming response
                if (client->stream_callback) {
                    client->stream_callback(evt->data, evt->data_len,
                                          client->stream_user_data);
                }
            }
            break;

        case HTTP_EVENT_ON_FINISH:
            if (client->response_buffer) {
                client->response_buffer[client->response_size] = '\0';
            }
            break;

        case HTTP_EVENT_ERROR:
            ESP_LOGE(TAG, "HTTP error");
            break;

        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "HTTP disconnected");
            break;

        default:
            break;
    }

    return ESP_OK;
}

xai_http_client_t* xai_http_client_create(
    const char *base_url,
    const char *api_key,
    uint32_t timeout_ms
) {
    if (!base_url || !api_key) {
        ESP_LOGE(TAG, "Invalid parameters");
        return NULL;
    }

    xai_http_client_t *client = calloc(1, sizeof(xai_http_client_t));
    if (!client) {
        ESP_LOGE(TAG, "Failed to allocate HTTP client");
        return NULL;
    }

    // Store base URL for proper path reconstruction
    client->base_url = strdup(base_url);
    if (!client->base_url) {
        ESP_LOGE(TAG, "Failed to allocate base URL");
        free(client);
        return NULL;
    }

    // Allocate response buffer
    client->response_capacity = 16384; // 16KB initial buffer
    client->response_buffer = malloc(client->response_capacity);
    if (!client->response_buffer) {
        ESP_LOGE(TAG, "Failed to allocate response buffer");
        free(client->base_url);
        free(client);
        return NULL;
    }

    // Prepare authorization header
    char auth_header[256];
    snprintf(auth_header, sizeof(auth_header), "Bearer %s", api_key);

    // Configure HTTP client
    esp_http_client_config_t config = {
        .url = base_url,
        .event_handler = http_event_handler,
        .user_data = client,
        .timeout_ms = timeout_ms,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
        .is_async = false,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    client->client = esp_http_client_init(&config);
    if (!client->client) {
        ESP_LOGE(TAG, "Failed to initialize HTTP client");
        free(client->response_buffer);
        free(client->base_url);
        free(client);
        return NULL;
    }

    // Set default headers
    esp_http_client_set_header(client->client, "Authorization", auth_header);
    esp_http_client_set_header(client->client, "Content-Type", "application/json");
    esp_http_client_set_header(client->client, "User-Agent", "xai-esp-idf/1.0");

    ESP_LOGI(TAG, "HTTP client created");
    return client;
}

void xai_http_client_destroy(xai_http_client_t *client) {
    if (!client) return;

    if (client->client) {
        esp_http_client_cleanup(client->client);
    }

    free(client->base_url);
    free(client->response_buffer);
    free(client);

    ESP_LOGI(TAG, "HTTP client destroyed");
}

xai_err_t xai_http_post(
    xai_http_client_t *client,
    const char *path,
    const char *body,
    size_t body_len,
    char **response,
    size_t *response_len
) {
    if (!client || !path || !body || !response) {
        ESP_LOGE(TAG, "Invalid parameters");
        return XAI_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "POST %s (%zu bytes)", path, body_len);

    // Reset response buffer
    client->response_size = 0;
    client->stream_callback = NULL;
    client->stream_user_data = NULL;

    // Construct full URL from base URL + path
    char full_url[512];
    snprintf(full_url, sizeof(full_url), "%s%s", client->base_url, path);
    esp_http_client_set_url(client->client, full_url);

    // Set method and body
    esp_http_client_set_method(client->client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client->client, body, body_len);

    // Perform request
    esp_err_t err = esp_http_client_perform(client->client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP request failed: %s", esp_err_to_name(err));
        return XAI_ERR_HTTP_FAILED;
    }

    // Check status code
    int status_code = esp_http_client_get_status_code(client->client);
    ESP_LOGI(TAG, "HTTP Status: %d, Response: %zu bytes", status_code, client->response_size);

    if (status_code < 200 || status_code >= 300) {
        ESP_LOGW(TAG, "HTTP error status: %d", status_code);
        
        // Parse error response if available
        if (client->response_size > 0) {
            ESP_LOGW(TAG, "Error response: %s", client->response_buffer);
        }
        
        if (status_code == 401) {
            return XAI_ERR_AUTH_FAILED;
        } else if (status_code == 429) {
            return XAI_ERR_RATE_LIMIT;
        } else {
            return XAI_ERR_API_ERROR;
        }
    }

    // Copy response
    *response = malloc(client->response_size + 1);
    if (!*response) {
        ESP_LOGE(TAG, "Failed to allocate response");
        return XAI_ERR_NO_MEMORY;
    }

    memcpy(*response, client->response_buffer, client->response_size);
    (*response)[client->response_size] = '\0';
    
    if (response_len) {
        *response_len = client->response_size;
    }

    return XAI_OK;
}

xai_err_t xai_http_post_stream(
    xai_http_client_t *client,
    const char *path,
    const char *body,
    size_t body_len,
    xai_stream_callback_t callback,
    void *user_data
) {
    if (!client || !path || !body || !callback) {
        ESP_LOGE(TAG, "Invalid parameters");
        return XAI_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "POST (stream) %s (%zu bytes)", path, body_len);

    // Create SSE parser
    xai_stream_parser_t *parser = xai_stream_parser_create(callback, user_data);
    if (!parser) {
        ESP_LOGE(TAG, "Failed to create SSE parser");
        return XAI_ERR_NO_MEMORY;
    }

    // Set streaming mode with SSE parsing
    client->response_size = 0;
    client->stream_callback = xai_stream_parser_feed;
    client->stream_user_data = parser;

    // Construct full URL from base URL + path
    char full_url[512];
    snprintf(full_url, sizeof(full_url), "%s%s", client->base_url, path);
    esp_http_client_set_url(client->client, full_url);

    // Set method and body
    esp_http_client_set_method(client->client, HTTP_METHOD_POST);
    esp_http_client_set_post_field(client->client, body, body_len);

    // Perform request (streaming)
    esp_err_t err = esp_http_client_perform(client->client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP streaming request failed: %s", esp_err_to_name(err));
        return XAI_ERR_HTTP_FAILED;
    }

    // Check status code
    int status_code = esp_http_client_get_status_code(client->client);
    ESP_LOGI(TAG, "HTTP Status: %d (streaming)", status_code);

    if (status_code < 200 || status_code >= 300) {
        ESP_LOGW(TAG, "HTTP error status: %d", status_code);
        
        // Try to read and log the error response body
        char error_buffer[512];
        int read_len = esp_http_client_read(client->client, error_buffer, sizeof(error_buffer) - 1);
        if (read_len > 0) {
            error_buffer[read_len] = '\0';
            ESP_LOGE(TAG, "Error response: %s", error_buffer);
        }
        
        if (status_code == 401) {
            return XAI_ERR_AUTH_FAILED;
        } else if (status_code == 429) {
            return XAI_ERR_RATE_LIMIT;
        } else {
            return XAI_ERR_API_ERROR;
        }
    }

    // Clean up parser
    xai_stream_parser_destroy(parser);

    return XAI_OK;
}

xai_err_t xai_http_get(
    xai_http_client_t *client,
    const char *path,
    char **response,
    size_t *response_len
) {
    if (!client || !path || !response) {
        ESP_LOGE(TAG, "Invalid parameters");
        return XAI_ERR_INVALID_ARG;
    }

    ESP_LOGD(TAG, "GET %s", path);

    // Reset response buffer
    client->response_size = 0;
    client->stream_callback = NULL;
    client->stream_user_data = NULL;

    // Construct full URL from base URL + path
    char full_url[512];
    snprintf(full_url, sizeof(full_url), "%s%s", client->base_url, path);
    esp_http_client_set_url(client->client, full_url);

    // Set method
    esp_http_client_set_method(client->client, HTTP_METHOD_GET);

    // Perform request
    esp_err_t err = esp_http_client_perform(client->client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP GET failed: %s", esp_err_to_name(err));
        return XAI_ERR_HTTP_FAILED;
    }

    // Check status code
    int status_code = esp_http_client_get_status_code(client->client);
    ESP_LOGI(TAG, "HTTP Status: %d, Response: %zu bytes", status_code, client->response_size);

    if (status_code < 200 || status_code >= 300) {
        ESP_LOGW(TAG, "HTTP error status: %d", status_code);
        
        if (status_code == 401) {
            return XAI_ERR_AUTH_FAILED;
        } else if (status_code == 429) {
            return XAI_ERR_RATE_LIMIT;
        } else {
            return XAI_ERR_API_ERROR;
        }
    }

    // Copy response
    *response = malloc(client->response_size + 1);
    if (!*response) {
        ESP_LOGE(TAG, "Failed to allocate response");
        return XAI_ERR_NO_MEMORY;
    }

    memcpy(*response, client->response_buffer, client->response_size);
    (*response)[client->response_size] = '\0';
    
    if (response_len) {
        *response_len = client->response_size;
    }

    return XAI_OK;
}

