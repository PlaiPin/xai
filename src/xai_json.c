/**
 * @file xai_json.c
 * @brief JSON request building and response parsing
 * 
 * This module handles all JSON serialization and deserialization for xAI API
 * requests and responses. It uses cJSON for parsing responses but builds
 * requests manually for memory efficiency.
 */

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "xai.h"
#include "xai_internal.h"
#include "cJSON.h"
#include "esp_log.h"

static const char *TAG = "xai_json";

/* ========================================================================
 * Request Building (Manual for efficiency)
 * ======================================================================== */

/**
 * @brief Build chat completion request JSON
 * 
 * Manually constructs JSON to avoid intermediate allocations.
 * Format:
 * {
 *   "model": "grok-2",
 *   "messages": [
 *     {"role": "user", "content": "Hello"}
 *   ],
 *   "temperature": 1.0,
 *   "max_tokens": 1024,
 *   "stream": false,
 *   ...
 * }
 */
xai_err_t xai_json_build_chat_request(
    char *buffer,
    size_t buffer_size,
    size_t *bytes_written,
    const xai_message_t *messages,
    size_t message_count,
    const xai_options_t *options,
    const char *default_model
) {
    if (!buffer || !bytes_written || !messages || message_count == 0) {
        return XAI_ERR_INVALID_ARG;
    }

    // Use cJSON for request building (simpler, more maintainable)
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        ESP_LOGE(TAG, "Failed to create JSON root object");
        return XAI_ERR_NO_MEMORY;
    }

    // Model
    const char *model = (options && options->model) ? options->model : default_model;
    cJSON_AddStringToObject(root, "model", model);

    // Messages array
    cJSON *messages_array = cJSON_CreateArray();
    if (!messages_array) {
        cJSON_Delete(root);
        return XAI_ERR_NO_MEMORY;
    }
    cJSON_AddItemToObject(root, "messages", messages_array);

    for (size_t i = 0; i < message_count; i++) {
        cJSON *msg = cJSON_CreateObject();
        if (!msg) {
            cJSON_Delete(root);
            return XAI_ERR_NO_MEMORY;
        }

        // Role
        const char *role_str = NULL;
        switch (messages[i].role) {
            case XAI_ROLE_SYSTEM:    role_str = "system"; break;
            case XAI_ROLE_USER:      role_str = "user"; break;
            case XAI_ROLE_ASSISTANT: role_str = "assistant"; break;
            case XAI_ROLE_TOOL:      role_str = "tool"; break;
            default:
                ESP_LOGE(TAG, "Invalid message role: %d", messages[i].role);
                cJSON_Delete(msg);
                cJSON_Delete(root);
                return XAI_ERR_INVALID_ARG;
        }
        cJSON_AddStringToObject(msg, "role", role_str);

        // Content (can be NULL for tool messages)
        if (messages[i].content) {
            // Check if this is a multi-modal message (vision)
            if (messages[i].images && messages[i].image_count > 0) {
                // Multi-modal content array
                cJSON *content_array = cJSON_CreateArray();
                if (!content_array) {
                    cJSON_Delete(msg);
                    cJSON_Delete(root);
                    return XAI_ERR_NO_MEMORY;
                }

                // Text content part
                cJSON *text_part = cJSON_CreateObject();
                cJSON_AddStringToObject(text_part, "type", "text");
                cJSON_AddStringToObject(text_part, "text", messages[i].content);
                cJSON_AddItemToArray(content_array, text_part);

                // Image content parts
                for (size_t j = 0; j < messages[i].image_count; j++) {
                    cJSON *image_part = cJSON_CreateObject();
                    cJSON_AddStringToObject(image_part, "type", "image_url");
                    
                    cJSON *image_url_obj = cJSON_CreateObject();
                    cJSON_AddStringToObject(image_url_obj, "url", messages[i].images[j].url);
                    if (messages[i].images[j].detail) {
                        cJSON_AddStringToObject(image_url_obj, "detail", messages[i].images[j].detail);
                    }
                    cJSON_AddItemToObject(image_part, "image_url", image_url_obj);
                    
                    cJSON_AddItemToArray(content_array, image_part);
                }

                cJSON_AddItemToObject(msg, "content", content_array);
            } else {
                // Simple text content
                cJSON_AddStringToObject(msg, "content", messages[i].content);
            }
        }

        // Optional fields
        if (messages[i].name) {
            cJSON_AddStringToObject(msg, "name", messages[i].name);
        }
        if (messages[i].tool_call_id) {
            cJSON_AddStringToObject(msg, "tool_call_id", messages[i].tool_call_id);
        }
        if (messages[i].tool_calls && messages[i].tool_call_count > 0) {
            cJSON *tool_calls_array = cJSON_CreateArray();
            for (size_t j = 0; j < messages[i].tool_call_count; j++) {
                cJSON *tool_call = cJSON_CreateObject();
                cJSON_AddStringToObject(tool_call, "id", messages[i].tool_calls[j].id);
                cJSON_AddStringToObject(tool_call, "type", "function");
                
                cJSON *function = cJSON_CreateObject();
                cJSON_AddStringToObject(function, "name", messages[i].tool_calls[j].name);
                cJSON_AddStringToObject(function, "arguments", messages[i].tool_calls[j].arguments);
                cJSON_AddItemToObject(tool_call, "function", function);
                
                cJSON_AddItemToArray(tool_calls_array, tool_call);
            }
            cJSON_AddItemToObject(msg, "tool_calls", tool_calls_array);
        }

        cJSON_AddItemToArray(messages_array, msg);
    }

    // Options
    if (options) {
        if (options->temperature >= 0) {
            cJSON_AddNumberToObject(root, "temperature", options->temperature);
        }
        if (options->max_tokens > 0) {
            cJSON_AddNumberToObject(root, "max_tokens", options->max_tokens);
        }
        if (options->stream) {
            cJSON_AddBoolToObject(root, "stream", true);
            
            // Add stream_options (required by xAI for streaming)
            cJSON *stream_opts = cJSON_CreateObject();
            if (stream_opts) {
                cJSON_AddBoolToObject(stream_opts, "include_usage", true);
                cJSON_AddItemToObject(root, "stream_options", stream_opts);
            }
        }
        if (options->top_p >= 0) {
            cJSON_AddNumberToObject(root, "top_p", options->top_p);
        }
        
        // NOTE: The following OpenAI-compatible parameters are NOT supported by xAI API
        // and have been commented out to prevent HTTP 400 errors:
        // - presence_penalty
        // - frequency_penalty
        // - stop sequences
        // - user (user_id)
        //
        // Based on Vercel AI SDK implementation and testing, xAI only supports:
        // - temperature, max_tokens, top_p, stream, seed
        // - reasoning_effort, parallel_function_calling (xAI-specific)
        // - search_parameters (xAI-specific)
        // - tools, tool_choice, response_format
        
        /*
        if (options->presence_penalty != 0.0f) {
            cJSON_AddNumberToObject(root, "presence_penalty", options->presence_penalty);
        }
        if (options->frequency_penalty != 0.0f) {
            cJSON_AddNumberToObject(root, "frequency_penalty", options->frequency_penalty);
        }
        if (options->user_id) {
            cJSON_AddStringToObject(root, "user", options->user_id);
        }

        // Stop sequences
        if (options->stop[0]) {
            cJSON *stop_array = cJSON_CreateArray();
            for (int i = 0; i < 4 && options->stop[i]; i++) {
                cJSON_AddItemToArray(stop_array, cJSON_CreateString(options->stop[i]));
            }
            cJSON_AddItemToObject(root, "stop", stop_array);
        }
        */

        // xAI-specific: Reasoning effort
        if (options->reasoning_effort) {
            cJSON_AddStringToObject(root, "reasoning_effort", options->reasoning_effort);
        }

        // xAI-specific: Parallel function calling
        if (options->parallel_function_calling) {
            cJSON_AddBoolToObject(root, "parallel_tool_calls", true);
        }

        // xAI-specific: Search parameters
        if (options->search_params) {
            xai_search_params_t *sp = options->search_params;
            
            // Only add if search is enabled
            if (sp->mode != XAI_SEARCH_OFF) {
                cJSON *search = cJSON_CreateObject();
                
                // Mode
                const char *mode_str = (sp->mode == XAI_SEARCH_AUTO) ? "auto" : "on";
                cJSON_AddStringToObject(search, "mode", mode_str);
                
                // Return citations
                if (sp->return_citations) {
                    cJSON_AddBoolToObject(search, "return_citations", true);
                }
                
                // Max results
                if (sp->max_results > 0) {
                    cJSON_AddNumberToObject(search, "max_results", sp->max_results);
                }
                
                // Sources
                if (sp->sources && sp->source_count > 0) {
                    cJSON *sources_array = cJSON_CreateArray();
                    for (size_t i = 0; i < sp->source_count; i++) {
                        cJSON *source = cJSON_CreateObject();
                        xai_search_source_t *src = &sp->sources[i];
                        
                        // Type
                        const char *type_str = NULL;
                        switch (src->type) {
                            case XAI_SOURCE_WEB:  type_str = "web"; break;
                            case XAI_SOURCE_NEWS: type_str = "news"; break;
                            case XAI_SOURCE_X:    type_str = "x"; break;
                            case XAI_SOURCE_RSS:  type_str = "rss"; break;
                        }
                        cJSON_AddStringToObject(source, "type", type_str);
                        
                        // Web-specific
                        if (src->type == XAI_SOURCE_WEB) {
                            if (src->web.allowed_websites) {
                                cJSON *allowed = cJSON_CreateArray();
                                for (int j = 0; src->web.allowed_websites[j]; j++) {
                                    cJSON_AddItemToArray(allowed, cJSON_CreateString(src->web.allowed_websites[j]));
                                }
                                cJSON_AddItemToObject(source, "allowed_websites", allowed);
                            }
                            if (src->web.excluded_websites) {
                                cJSON *excluded = cJSON_CreateArray();
                                for (int j = 0; src->web.excluded_websites[j]; j++) {
                                    cJSON_AddItemToArray(excluded, cJSON_CreateString(src->web.excluded_websites[j]));
                                }
                                cJSON_AddItemToObject(source, "excluded_websites", excluded);
                            }
                            if (src->web.safe_search) {
                                cJSON_AddBoolToObject(source, "safe_search", true);
                            }
                        }
                        
                        // News-specific
                        if (src->type == XAI_SOURCE_NEWS) {
                            if (src->news.country) {
                                cJSON_AddStringToObject(source, "country", src->news.country);
                            }
                            if (src->news.excluded_websites) {
                                cJSON *excluded = cJSON_CreateArray();
                                for (int j = 0; src->news.excluded_websites[j]; j++) {
                                    cJSON_AddItemToArray(excluded, cJSON_CreateString(src->news.excluded_websites[j]));
                                }
                                cJSON_AddItemToObject(source, "excluded_websites", excluded);
                            }
                            if (src->news.safe_search) {
                                cJSON_AddBoolToObject(source, "safe_search", true);
                            }
                        }
                        
                        // X-specific
                        if (src->type == XAI_SOURCE_X) {
                            if (src->x.included_x_handles) {
                                cJSON *included = cJSON_CreateArray();
                                for (int j = 0; src->x.included_x_handles[j]; j++) {
                                    cJSON_AddItemToArray(included, cJSON_CreateString(src->x.included_x_handles[j]));
                                }
                                cJSON_AddItemToObject(source, "included_x_handles", included);
                            }
                            if (src->x.excluded_x_handles) {
                                cJSON *excluded = cJSON_CreateArray();
                                for (int j = 0; src->x.excluded_x_handles[j]; j++) {
                                    cJSON_AddItemToArray(excluded, cJSON_CreateString(src->x.excluded_x_handles[j]));
                                }
                                cJSON_AddItemToObject(source, "excluded_x_handles", excluded);
                            }
                            if (src->x.post_favorite_count_min > 0) {
                                cJSON_AddNumberToObject(source, "post_favorite_count_min", src->x.post_favorite_count_min);
                            }
                            if (src->x.post_view_count_min > 0) {
                                cJSON_AddNumberToObject(source, "post_view_count_min", src->x.post_view_count_min);
                            }
                            if (src->x.enable_image_understanding) {
                                cJSON_AddBoolToObject(source, "enable_image_understanding", true);
                            }
                            if (src->x.enable_video_understanding) {
                                cJSON_AddBoolToObject(source, "enable_video_understanding", true);
                            }
                        }
                        
                        // RSS-specific
                        if (src->type == XAI_SOURCE_RSS) {
                            if (src->rss.rss_links) {
                                cJSON *rss_links = cJSON_CreateArray();
                                for (int j = 0; src->rss.rss_links[j]; j++) {
                                    cJSON_AddItemToArray(rss_links, cJSON_CreateString(src->rss.rss_links[j]));
                                }
                                cJSON_AddItemToObject(source, "rss_links", rss_links);
                            }
                        }
                        
                        cJSON_AddItemToArray(sources_array, source);
                    }
                    cJSON_AddItemToObject(search, "sources", sources_array);
                }
                
                cJSON_AddItemToObject(root, "search", search);
            }
        }

        // Tools (function calling)
        if (options->tools && options->tool_count > 0) {
            cJSON *tools_array = cJSON_CreateArray();
            for (size_t i = 0; i < options->tool_count; i++) {
                cJSON *tool = cJSON_CreateObject();
                cJSON_AddStringToObject(tool, "type", "function");
                
                cJSON *function = cJSON_CreateObject();
                cJSON_AddStringToObject(function, "name", options->tools[i].name);
                if (options->tools[i].description) {
                    cJSON_AddStringToObject(function, "description", options->tools[i].description);
                }
                if (options->tools[i].parameters_json) {
                    cJSON *params = cJSON_Parse(options->tools[i].parameters_json);
                    if (params) {
                        cJSON_AddItemToObject(function, "parameters", params);
                    }
                }
                
                cJSON_AddItemToObject(tool, "function", function);
                cJSON_AddItemToArray(tools_array, tool);
            }
            cJSON_AddItemToObject(root, "tools", tools_array);
        }
    }

    // Serialize to buffer
    char *json_str = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!json_str) {
        ESP_LOGE(TAG, "Failed to serialize JSON");
        return XAI_ERR_NO_MEMORY;
    }

    size_t json_len = strlen(json_str);
    if (json_len >= buffer_size) {
        ESP_LOGE(TAG, "JSON too large for buffer: %zu >= %zu", json_len, buffer_size);
        free(json_str);
        return XAI_ERR_NO_MEMORY;
    }

    memcpy(buffer, json_str, json_len);
    buffer[json_len] = '\0';
    *bytes_written = json_len;

    free(json_str);

    ESP_LOGD(TAG, "Built request JSON (%zu bytes)", json_len);
    return XAI_OK;
}

/* ========================================================================
 * Response Parsing (Using cJSON)
 * ======================================================================== */

/**
 * @brief Parse chat completion response
 * 
 * Parses JSON response and populates xai_response_t structure.
 * Format:
 * {
 *   "id": "chatcmpl-123",
 *   "object": "chat.completion",
 *   "created": 1234567890,
 *   "model": "grok-2",
 *   "choices": [
 *     {
 *       "index": 0,
 *       "message": {
 *         "role": "assistant",
 *         "content": "Hello! How can I help?"
 *       },
 *       "finish_reason": "stop"
 *     }
 *   ],
 *   "usage": {
 *     "prompt_tokens": 10,
 *     "completion_tokens": 20,
 *     "total_tokens": 30
 *   }
 * }
 */
xai_err_t xai_json_parse_chat_response(
    const char *json_str,
    xai_response_t *response
) {
    if (!json_str || !response) {
        return XAI_ERR_INVALID_ARG;
    }

    memset(response, 0, sizeof(xai_response_t));

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGE(TAG, "Failed to parse JSON response");
        return XAI_ERR_PARSE_FAILED;
    }

    // Check for error response
    cJSON *error = cJSON_GetObjectItem(root, "error");
    if (error) {
        cJSON *message = cJSON_GetObjectItem(error, "message");
        if (message && cJSON_IsString(message)) {
            ESP_LOGE(TAG, "API error: %s", message->valuestring);
        }
        
        // Map error type to error code
        cJSON *type = cJSON_GetObjectItem(error, "type");
        xai_err_t err_code = XAI_ERR_API_ERROR;
        if (type && cJSON_IsString(type)) {
            if (strcmp(type->valuestring, "invalid_request_error") == 0) {
                err_code = XAI_ERR_INVALID_ARG;
            } else if (strcmp(type->valuestring, "authentication_error") == 0) {
                err_code = XAI_ERR_AUTH_FAILED;
            } else if (strcmp(type->valuestring, "rate_limit_error") == 0) {
                err_code = XAI_ERR_RATE_LIMIT;
            }
        }
        
        cJSON_Delete(root);
        return err_code;
    }

    // Parse model
    cJSON *model = cJSON_GetObjectItem(root, "model");
    if (model && cJSON_IsString(model)) {
        response->model = strdup(model->valuestring);
    }

    // Parse choices
    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (!choices || !cJSON_IsArray(choices) || cJSON_GetArraySize(choices) == 0) {
        ESP_LOGE(TAG, "No choices in response");
        cJSON_Delete(root);
        return XAI_ERR_PARSE_FAILED;
    }

    cJSON *choice = cJSON_GetArrayItem(choices, 0);
    if (!choice) {
        ESP_LOGE(TAG, "Failed to get first choice");
        cJSON_Delete(root);
        return XAI_ERR_PARSE_FAILED;
    }

    // Parse message
    cJSON *message = cJSON_GetObjectItem(choice, "message");
    if (message) {
        cJSON *content = cJSON_GetObjectItem(message, "content");
        if (content && cJSON_IsString(content) && content->valuestring) {
            response->content = strdup(content->valuestring);
        }

        // Parse reasoning content (grok-4 models with reasoning)
        cJSON *reasoning_content = cJSON_GetObjectItem(message, "reasoning_content");
        if (reasoning_content && cJSON_IsString(reasoning_content) && reasoning_content->valuestring) {
            response->reasoning_content = strdup(reasoning_content->valuestring);
        }

        // Parse tool calls
        cJSON *tool_calls = cJSON_GetObjectItem(message, "tool_calls");
        if (tool_calls && cJSON_IsArray(tool_calls)) {
            response->tool_call_count = cJSON_GetArraySize(tool_calls);
            if (response->tool_call_count > 0) {
                response->tool_calls = calloc(response->tool_call_count, sizeof(xai_tool_call_t));
                if (response->tool_calls) {
                    for (size_t i = 0; i < response->tool_call_count; i++) {
                        cJSON *tc = cJSON_GetArrayItem(tool_calls, i);
                        cJSON *id = cJSON_GetObjectItem(tc, "id");
                        cJSON *function = cJSON_GetObjectItem(tc, "function");
                        
                        if (id && cJSON_IsString(id)) {
                            response->tool_calls[i].id = strdup(id->valuestring);
                        }
                        
                        if (function) {
                            cJSON *name = cJSON_GetObjectItem(function, "name");
                            cJSON *arguments = cJSON_GetObjectItem(function, "arguments");
                            
                            if (name && cJSON_IsString(name)) {
                                response->tool_calls[i].name = strdup(name->valuestring);
                            }
                            if (arguments && cJSON_IsString(arguments)) {
                                response->tool_calls[i].arguments = strdup(arguments->valuestring);
                            }
                        }
                    }
                }
            }
        }
    }

    // Parse finish reason
    cJSON *finish_reason = cJSON_GetObjectItem(choice, "finish_reason");
    if (finish_reason && cJSON_IsString(finish_reason)) {
        response->finish_reason = strdup(finish_reason->valuestring);
    }

    // Parse usage
    cJSON *usage = cJSON_GetObjectItem(root, "usage");
    if (usage) {
        cJSON *prompt_tokens = cJSON_GetObjectItem(usage, "prompt_tokens");
        cJSON *completion_tokens = cJSON_GetObjectItem(usage, "completion_tokens");
        cJSON *total_tokens = cJSON_GetObjectItem(usage, "total_tokens");
        
        if (prompt_tokens && cJSON_IsNumber(prompt_tokens)) {
            response->prompt_tokens = prompt_tokens->valueint;
        }
        if (completion_tokens && cJSON_IsNumber(completion_tokens)) {
            response->completion_tokens = completion_tokens->valueint;
        }
        if (total_tokens && cJSON_IsNumber(total_tokens)) {
            response->total_tokens = total_tokens->valueint;
        }
    }

    // Parse citations (xAI-specific)
    // Note: API returns citations as array of URL strings, not rich objects
    cJSON *citations = cJSON_GetObjectItem(root, "citations");
    if (citations && cJSON_IsArray(citations)) {
        response->citation_count = cJSON_GetArraySize(citations);
        if (response->citation_count > 0) {
            response->citations = calloc(response->citation_count, sizeof(xai_citation_t));
            if (response->citations) {
                for (size_t i = 0; i < response->citation_count; i++) {
                    cJSON *cit = cJSON_GetArrayItem(citations, i);
                    
                    // Citations are simple URL strings in the API response
                    if (cit && cJSON_IsString(cit)) {
                        response->citations[i].url = strdup(cit->valuestring);
                        response->citations[i].source_type = strdup("url");
                    }
                    // Legacy: also handle object format if API changes in future
                    else if (cit && cJSON_IsObject(cit)) {
                        cJSON *source_type = cJSON_GetObjectItem(cit, "source_type");
                        if (source_type && cJSON_IsString(source_type)) {
                            response->citations[i].source_type = strdup(source_type->valuestring);
                        }
                        
                        cJSON *url = cJSON_GetObjectItem(cit, "url");
                        if (url && cJSON_IsString(url)) {
                            response->citations[i].url = strdup(url->valuestring);
                        }
                        
                        cJSON *title = cJSON_GetObjectItem(cit, "title");
                        if (title && cJSON_IsString(title)) {
                            response->citations[i].title = strdup(title->valuestring);
                        }
                        
                        cJSON *snippet = cJSON_GetObjectItem(cit, "snippet");
                        if (snippet && cJSON_IsString(snippet)) {
                            response->citations[i].snippet = strdup(snippet->valuestring);
                        }
                        
                        cJSON *author = cJSON_GetObjectItem(cit, "author");
                        if (author && cJSON_IsString(author)) {
                            response->citations[i].author = strdup(author->valuestring);
                        }
                        
                        cJSON *published_date = cJSON_GetObjectItem(cit, "published_date");
                        if (published_date && cJSON_IsString(published_date)) {
                            response->citations[i].published_date = strdup(published_date->valuestring);
                        }
                    }
                }
            }
        }
    }

    cJSON_Delete(root);

    ESP_LOGD(TAG, "Parsed response: content_len=%zu, tokens=%u/%u/%u, citations=%zu, tool_calls=%zu",
             response->content ? strlen(response->content) : 0,
             response->prompt_tokens, response->completion_tokens, response->total_tokens,
             response->citation_count, response->tool_call_count);

    return XAI_OK;
}

/**
 * @brief Parse streaming chunk (SSE data line)
 * 
 * Streaming chunks come as SSE events:
 * data: {"choices":[{"delta":{"content":"Hello"}}]}
 * 
 * Special case:
 * data: [DONE]
 */
xai_err_t xai_json_parse_stream_chunk(
    const char *json_str,
    char **content_delta,
    bool *is_done
) {
    if (!json_str || !content_delta || !is_done) {
        return XAI_ERR_INVALID_ARG;
    }

    *content_delta = NULL;
    *is_done = false;

    // Check for [DONE] marker
    if (strcmp(json_str, "[DONE]") == 0) {
        *is_done = true;
        return XAI_OK;
    }

    cJSON *root = cJSON_Parse(json_str);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse stream chunk JSON");
        return XAI_ERR_PARSE_FAILED;
    }

    cJSON *choices = cJSON_GetObjectItem(root, "choices");
    if (choices && cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
        cJSON *choice = cJSON_GetArrayItem(choices, 0);
        cJSON *delta = cJSON_GetObjectItem(choice, "delta");
        
        if (delta) {
            cJSON *content = cJSON_GetObjectItem(delta, "content");
            if (content && cJSON_IsString(content) && content->valuestring) {
                *content_delta = strdup(content->valuestring);
            }
        }

        // Check finish reason
        cJSON *finish_reason = cJSON_GetObjectItem(choice, "finish_reason");
        if (finish_reason && cJSON_IsString(finish_reason)) {
            *is_done = true;
        }
    }

    cJSON_Delete(root);
    return XAI_OK;
}

// NOTE: xai_response_free() is implemented in xai.c

