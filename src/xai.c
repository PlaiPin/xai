/**
 * @file xai.c
 * @brief xAI ESP-IDF Component - Core Implementation
 * 
 * @copyright 2025
 * @license Apache-2.0
 */

#include "xai.h"
#include "xai_internal.h"
#include "esp_log.h"
#include "esp_system.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "xai";

// Default configuration values
#define XAI_DEFAULT_BASE_URL    "https://api.x.ai/v1"
#define XAI_DEFAULT_MODEL       "grok-3-latest"
#define XAI_DEFAULT_TIMEOUT_MS  60000
#define XAI_DEFAULT_MAX_RETRIES 3
#define XAI_DEFAULT_MAX_TOKENS  1024
#define XAI_DEFAULT_TEMPERATURE 1.0f

// From Kconfig
#ifndef CONFIG_XAI_MAX_RESPONSE_SIZE
#define CONFIG_XAI_MAX_RESPONSE_SIZE 8192
#endif

#ifndef CONFIG_XAI_BUFFER_POOL_SIZE
#define CONFIG_XAI_BUFFER_POOL_SIZE 2
#endif

// ============================================================================
// Configuration Helpers
// ============================================================================

xai_config_t xai_config_default(void) {
    xai_config_t config = {
        .api_key = NULL,
        .base_url = XAI_DEFAULT_BASE_URL,
        .default_model = XAI_DEFAULT_MODEL,
        .timeout_ms = XAI_DEFAULT_TIMEOUT_MS,
        .max_retries = XAI_DEFAULT_MAX_RETRIES,
        .max_tokens = XAI_DEFAULT_MAX_TOKENS,
        .temperature = XAI_DEFAULT_TEMPERATURE
    };
    return config;
}

xai_options_t xai_options_default(void) {
    xai_options_t options = {
        .model = NULL,
        .temperature = -1.0f,
        .max_tokens = 0,
        .stream = false,
        .stop = {NULL, NULL, NULL, NULL},
        .top_p = -1.0f,
        .presence_penalty = 0.0f,
        .frequency_penalty = 0.0f,
        .user_id = NULL,
        .search_params = NULL,
        .reasoning_effort = NULL,
        .parallel_function_calling = false,
        .tools = NULL,
        .tool_count = 0,
        .tool_choice = NULL
    };
    return options;
}

// ============================================================================
// Buffer Pool Implementation
// ============================================================================

xai_buffer_pool_t* xai_buffer_pool_create(size_t buffer_count, size_t buffer_size) {
    xai_buffer_pool_t *pool = calloc(1, sizeof(xai_buffer_pool_t));
    if (!pool) {
        ESP_LOGE(TAG, "Failed to allocate buffer pool");
        return NULL;
    }

    pool->buffers = calloc(buffer_count, sizeof(xai_buffer_t));
    if (!pool->buffers) {
        ESP_LOGE(TAG, "Failed to allocate buffers");
        free(pool);
        return NULL;
    }

    pool->count = buffer_count;
    pool->mutex = xSemaphoreCreateMutex();
    if (!pool->mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        free(pool->buffers);
        free(pool);
        return NULL;
    }

    // Allocate individual buffers
    for (size_t i = 0; i < buffer_count; i++) {
        pool->buffers[i].data = malloc(buffer_size);
        if (!pool->buffers[i].data) {
            ESP_LOGE(TAG, "Failed to allocate buffer %zu", i);
            // Cleanup already allocated buffers
            for (size_t j = 0; j < i; j++) {
                free(pool->buffers[j].data);
            }
            vSemaphoreDelete(pool->mutex);
            free(pool->buffers);
            free(pool);
            return NULL;
        }
        pool->buffers[i].capacity = buffer_size;
        pool->buffers[i].used = 0;
        pool->buffers[i].in_use = false;
    }

    ESP_LOGI(TAG, "Created buffer pool: %zu buffers of %zu bytes each", buffer_count, buffer_size);
    return pool;
}

xai_buffer_t* xai_buffer_pool_acquire(xai_buffer_pool_t *pool) {
    if (!pool) return NULL;

    xSemaphoreTake(pool->mutex, portMAX_DELAY);

    xai_buffer_t *buffer = NULL;
    for (size_t i = 0; i < pool->count; i++) {
        if (!pool->buffers[i].in_use) {
            pool->buffers[i].in_use = true;
            pool->buffers[i].used = 0;
            buffer = &pool->buffers[i];
            break;
        }
    }

    xSemaphoreGive(pool->mutex);

    if (!buffer) {
        ESP_LOGW(TAG, "No available buffers in pool");
    }

    return buffer;
}

void xai_buffer_pool_release(xai_buffer_pool_t *pool, xai_buffer_t *buffer) {
    if (!pool || !buffer) return;

    xSemaphoreTake(pool->mutex, portMAX_DELAY);

    // Find and release the buffer
    for (size_t i = 0; i < pool->count; i++) {
        if (&pool->buffers[i] == buffer) {
            pool->buffers[i].in_use = false;
            pool->buffers[i].used = 0;
            break;
        }
    }

    xSemaphoreGive(pool->mutex);
}

void xai_buffer_pool_destroy(xai_buffer_pool_t *pool) {
    if (!pool) return;

    if (pool->buffers) {
        for (size_t i = 0; i < pool->count; i++) {
            free(pool->buffers[i].data);
        }
        free(pool->buffers);
    }

    if (pool->mutex) {
        vSemaphoreDelete(pool->mutex);
    }

    free(pool);
    ESP_LOGI(TAG, "Destroyed buffer pool");
}

// ============================================================================
// Client Lifecycle
// ============================================================================

xai_client_t xai_create(const char *api_key) {
    if (!api_key || strlen(api_key) == 0) {
        ESP_LOGE(TAG, "API key is required");
        return NULL;
    }

    xai_config_t config = xai_config_default();
    config.api_key = api_key;
    
    return xai_create_config(&config);
}

xai_client_t xai_create_config(const xai_config_t *config) {
    if (!config) {
        ESP_LOGE(TAG, "Config is NULL");
        return NULL;
    }
    if (!config->api_key) {
        ESP_LOGE(TAG, "API key is NULL");
        return NULL;
    }

    ESP_LOGI(TAG, "Creating xAI client");

    // Allocate client structure
    struct xai_client_s *client = calloc(1, sizeof(struct xai_client_s));
    if (!client) {
        ESP_LOGE(TAG, "Failed to allocate client");
        return NULL;
    }

    // Copy configuration
    client->api_key = strdup(config->api_key);
    client->base_url = strdup(config->base_url ? config->base_url : XAI_DEFAULT_BASE_URL);
    client->default_model = strdup(config->default_model ? config->default_model : XAI_DEFAULT_MODEL);
    client->timeout_ms = config->timeout_ms ? config->timeout_ms : XAI_DEFAULT_TIMEOUT_MS;
    client->max_retries = config->max_retries;
    client->default_temperature = config->temperature;
    client->default_max_tokens = config->max_tokens ? config->max_tokens : XAI_DEFAULT_MAX_TOKENS;

    if (!client->api_key || !client->base_url || !client->default_model) {
        ESP_LOGE(TAG, "Failed to copy configuration strings");
        goto error;
    }

    // Create mutex
    client->mutex = xSemaphoreCreateMutex();
    if (!client->mutex) {
        ESP_LOGE(TAG, "Failed to create client mutex");
        goto error;
    }

    // Create buffer pool
    client->buffer_pool = xai_buffer_pool_create(
        CONFIG_XAI_BUFFER_POOL_SIZE,
        CONFIG_XAI_MAX_RESPONSE_SIZE
    );
    if (!client->buffer_pool) {
        ESP_LOGE(TAG, "Failed to create buffer pool");
        goto error;
    }

    // Create HTTP client
    client->http_client = xai_http_client_create(
        client->base_url,
        client->api_key,
        client->timeout_ms
    );
    if (!client->http_client) {
        ESP_LOGE(TAG, "Failed to create HTTP client");
        goto error;
    }

    ESP_LOGI(TAG, "xAI client created successfully (model: %s)", client->default_model);
    return (xai_client_t)client;

error:
    if (client->http_client) {
        xai_http_client_destroy(client->http_client);
    }
    if (client->buffer_pool) {
        xai_buffer_pool_destroy(client->buffer_pool);
    }
    if (client->mutex) {
        vSemaphoreDelete(client->mutex);
    }
    free(client->api_key);
    free(client->base_url);
    free(client->default_model);
    free(client);
    return NULL;
}

void xai_destroy(xai_client_t client) {
    if (!client) return;

    struct xai_client_s *impl = (struct xai_client_s*)client;

    ESP_LOGI(TAG, "Destroying xAI client");

    if (impl->http_client) {
        xai_http_client_destroy(impl->http_client);
    }

    if (impl->buffer_pool) {
        xai_buffer_pool_destroy(impl->buffer_pool);
    }

    if (impl->mutex) {
        vSemaphoreDelete(impl->mutex);
    }

    free(impl->api_key);
    free(impl->base_url);
    free(impl->default_model);
    free(impl);

    ESP_LOGI(TAG, "xAI client destroyed");
}

// ============================================================================
// Response Memory Management
// ============================================================================

void xai_response_free(xai_response_t *response) {
    if (!response) return;

    free(response->content);
    free(response->reasoning_content);
    free(response->model);
    free(response->finish_reason);

    // Free tool calls
    if (response->tool_calls) {
        for (size_t i = 0; i < response->tool_call_count; i++) {
            free(response->tool_calls[i].id);
            free(response->tool_calls[i].name);
            free(response->tool_calls[i].arguments);
        }
        free(response->tool_calls);
    }

    // Free citations
    if (response->citations) {
        for (size_t i = 0; i < response->citation_count; i++) {
            free(response->citations[i].source_type);
            free(response->citations[i].url);
            free(response->citations[i].title);
            free(response->citations[i].snippet);
            free(response->citations[i].author);
            free(response->citations[i].published_date);
        }
        free(response->citations);
    }

    // Clear structure
    memset(response, 0, sizeof(xai_response_t));
}

// ============================================================================
// Error Handling
// ============================================================================

const char* xai_err_to_string(xai_err_t err) {
    switch (err) {
        case XAI_OK:                return "Success";
        case XAI_ERR_INVALID_ARG:   return "Invalid argument";
        case XAI_ERR_NO_MEMORY:     return "Out of memory";
        case XAI_ERR_HTTP_FAILED:   return "HTTP request failed";
        case XAI_ERR_PARSE_FAILED:  return "JSON parsing failed";
        case XAI_ERR_AUTH_FAILED:   return "Authentication failed";
        case XAI_ERR_RATE_LIMIT:    return "Rate limit exceeded";
        case XAI_ERR_TIMEOUT:       return "Request timeout";
        case XAI_ERR_API_ERROR:     return "API error";
        case XAI_ERR_NOT_SUPPORTED: return "Feature not supported";
        default:                    return "Unknown error";
    }
}

// ============================================================================
// Simple Text Completion Wrapper
// ============================================================================
// NOTE: xai_text_completion() is implemented in xai_chat.c

