/**
 * @file xai_responses.c
 * @brief Responses API - Server-side tool execution
 * 
 * The Responses API (/v1/responses) enables agentic behavior with server-side
 * tool execution. xAI executes tools on their servers and orchestrates multi-step
 * reasoning automatically.
 * 
 * Only works with: grok-4, grok-4-fast, grok-4-fast-non-reasoning models.
 */

#include "sdkconfig.h"

#ifdef CONFIG_XAI_ENABLE_RESPONSES_API

#include <string.h>
#include <stdlib.h>
#include "xai.h"
#include "xai_internal.h"
#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "xai_responses";

/**
 * @brief Agentic completion with server-side tool execution
 * 
 * Uses /v1/responses endpoint instead of /v1/chat/completions.
 * xAI orchestrates tool calls on the server side.
 */
xai_err_t xai_responses_completion(
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

    struct xai_client_s *client_impl = (struct xai_client_s *)client;
    xai_err_t err = XAI_OK;

    // Acquire client mutex
    if (xSemaphoreTake(client_impl->mutex, pdMS_TO_TICKS(5000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire mutex");
        return XAI_ERR_TIMEOUT;
    }

    // Build JSON request (similar to chat but for responses endpoint)
    char *request_buffer = malloc(16384);
    if (!request_buffer) {
        ESP_LOGE(TAG, "Failed to allocate request buffer");
        xSemaphoreGive(client_impl->mutex);
        return XAI_ERR_NO_MEMORY;
    }

    // Create options with tools for responses
    xai_options_t options = {
        .model = NULL,  // Defaults to grok-4
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

    size_t request_len = 0;
    err = xai_json_build_chat_request(
        request_buffer,
        16384,
        &request_len,
        messages,
        message_count,
        &options,
        "grok-4"  // Default model for responses API
    );

    if (err != XAI_OK) {
        ESP_LOGE(TAG, "Failed to build request: %d", err);
        free(request_buffer);
        xSemaphoreGive(client_impl->mutex);
        return err;
    }

    ESP_LOGI(TAG, "Sending responses API request (%zu bytes)", request_len);
    ESP_LOGD(TAG, "Request JSON: %.*s", (int)request_len, request_buffer);

    // Send HTTP POST to /responses endpoint
    char *response_data = NULL;
    size_t response_len = 0;
    err = xai_http_post(
        client_impl->http_client,
        "/responses",
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

    // Parse response (same format as chat completions)
    err = xai_json_parse_chat_response(response_data, response);
    free(response_data);

    xSemaphoreGive(client_impl->mutex);

    if (err != XAI_OK) {
        ESP_LOGE(TAG, "Failed to parse response: %d", err);
        return err;
    }

    ESP_LOGI(TAG, "Responses API completion successful");
    return XAI_OK;
}

/* ========================================================================
 * Pre-built Server-Side Tools
 * ======================================================================== */

/**
 * @brief Create web search tool definition for server-side execution
 */
xai_tool_t xai_tool_web_search(
    const char **allowed_domains,
    const char **excluded_domains,
    bool enable_image_understanding
) {
    // Build parameters JSON for web search tool
    cJSON *params = cJSON_CreateObject();
    cJSON *properties = cJSON_CreateObject();
    
    // Query parameter
    cJSON *query_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(query_prop, "type", "string");
    cJSON_AddStringToObject(query_prop, "description", "The search query");
    cJSON_AddItemToObject(properties, "query", query_prop);
    
    // Allowed domains
    if (allowed_domains) {
        cJSON *allowed_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(allowed_prop, "type", "array");
        cJSON_AddStringToObject(allowed_prop, "description", "Allowed domains to search");
        cJSON_AddItemToObject(properties, "allowed_domains", allowed_prop);
    }
    
    // Excluded domains
    if (excluded_domains) {
        cJSON *excluded_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(excluded_prop, "type", "array");
        cJSON_AddStringToObject(excluded_prop, "description", "Domains to exclude");
        cJSON_AddItemToObject(properties, "excluded_domains", excluded_prop);
    }
    
    // Image understanding
    if (enable_image_understanding) {
        cJSON *image_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(image_prop, "type", "boolean");
        cJSON_AddStringToObject(image_prop, "description", "Enable image understanding");
        cJSON_AddItemToObject(properties, "enable_image_understanding", image_prop);
    }
    
    cJSON_AddItemToObject(params, "properties", properties);
    cJSON_AddStringToObject(params, "type", "object");
    
    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("query"));
    cJSON_AddItemToObject(params, "required", required);
    
    char *params_json = cJSON_PrintUnformatted(params);
    cJSON_Delete(params);
    
    xai_tool_t tool = {
        .name = "web_search",
        .description = "Search the web for information",
        .parameters_json = params_json  // Note: caller must free this
    };
    
    return tool;
}

/**
 * @brief Create X/Twitter search tool definition for server-side execution
 */
xai_tool_t xai_tool_x_search(
    const char **allowed_handles,
    const char **excluded_handles,
    const char *from_date,
    const char *to_date,
    bool enable_image_understanding,
    bool enable_video_understanding
) {
    // Build parameters JSON for X search tool
    cJSON *params = cJSON_CreateObject();
    cJSON *properties = cJSON_CreateObject();
    
    // Query parameter
    cJSON *query_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(query_prop, "type", "string");
    cJSON_AddStringToObject(query_prop, "description", "The search query");
    cJSON_AddItemToObject(properties, "query", query_prop);
    
    // Allowed handles
    if (allowed_handles) {
        cJSON *allowed_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(allowed_prop, "type", "array");
        cJSON_AddStringToObject(allowed_prop, "description", "X handles to search");
        cJSON_AddItemToObject(properties, "allowed_handles", allowed_prop);
    }
    
    // Date range
    if (from_date) {
        cJSON *from_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(from_prop, "type", "string");
        cJSON_AddStringToObject(from_prop, "description", "Start date (YYYY-MM-DD)");
        cJSON_AddItemToObject(properties, "from_date", from_prop);
    }
    
    if (to_date) {
        cJSON *to_prop = cJSON_CreateObject();
        cJSON_AddStringToObject(to_prop, "type", "string");
        cJSON_AddStringToObject(to_prop, "description", "End date (YYYY-MM-DD)");
        cJSON_AddItemToObject(properties, "to_date", to_prop);
    }
    
    cJSON_AddItemToObject(params, "properties", properties);
    cJSON_AddStringToObject(params, "type", "object");
    
    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("query"));
    cJSON_AddItemToObject(params, "required", required);
    
    char *params_json = cJSON_PrintUnformatted(params);
    cJSON_Delete(params);
    
    xai_tool_t tool = {
        .name = "x_search",
        .description = "Search X (Twitter) for posts",
        .parameters_json = params_json  // Note: caller must free this
    };
    
    return tool;
}

/**
 * @brief Create code execution tool definition for server-side execution
 */
xai_tool_t xai_tool_code_execution(void) {
    // Build parameters JSON for code execution tool
    cJSON *params = cJSON_CreateObject();
    cJSON *properties = cJSON_CreateObject();
    
    // Code parameter
    cJSON *code_prop = cJSON_CreateObject();
    cJSON_AddStringToObject(code_prop, "type", "string");
    cJSON_AddStringToObject(code_prop, "description", "Python code to execute");
    cJSON_AddItemToObject(properties, "code", code_prop);
    
    cJSON_AddItemToObject(params, "properties", properties);
    cJSON_AddStringToObject(params, "type", "object");
    
    cJSON *required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("code"));
    cJSON_AddItemToObject(params, "required", required);
    
    char *params_json = cJSON_PrintUnformatted(params);
    cJSON_Delete(params);
    
    xai_tool_t tool = {
        .name = "code_execution",
        .description = "Execute Python code on the server",
        .parameters_json = params_json  // Note: caller must free this
    };
    
    return tool;
}

#endif // CONFIG_XAI_ENABLE_RESPONSES_API

