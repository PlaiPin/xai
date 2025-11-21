/**
 * @file xai_conversation.c
 * @brief Conversation management helper functions
 * 
 * Provides a convenient API for managing multi-turn conversations
 * with automatic message history management.
 */

#include "sdkconfig.h"

#ifdef CONFIG_XAI_ENABLE_CONVERSATION_HELPER

#include <string.h>
#include <stdlib.h>
#include "xai.h"
#include "xai_internal.h"
#include "esp_log.h"

static const char *TAG = "xai_conversation";

#define CONVERSATION_INITIAL_CAPACITY 8

/**
 * @brief Create conversation context
 */
xai_conversation_t xai_conversation_create(const char *system_prompt) {
    struct xai_conversation_s *conv = calloc(1, sizeof(struct xai_conversation_s));
    if (!conv) {
        ESP_LOGE(TAG, "Failed to allocate conversation");
        return NULL;
    }

    // Allocate message array
    conv->message_capacity = CONVERSATION_INITIAL_CAPACITY;
    conv->messages = calloc(conv->message_capacity, sizeof(xai_message_t));
    if (!conv->messages) {
        ESP_LOGE(TAG, "Failed to allocate message array");
        free(conv);
        return NULL;
    }

    conv->message_count = 0;

    // Add system prompt if provided
    if (system_prompt) {
        conv->system_prompt = strdup(system_prompt);
        if (!conv->system_prompt) {
            ESP_LOGE(TAG, "Failed to allocate system prompt");
            free(conv->messages);
            free(conv);
            return NULL;
        }

        // Add as first message
        conv->messages[0].role = XAI_ROLE_SYSTEM;
        conv->messages[0].content = conv->system_prompt;
        conv->messages[0].name = NULL;
        conv->messages[0].tool_call_id = NULL;
        conv->messages[0].tool_calls = NULL;
        conv->messages[0].tool_call_count = 0;
        conv->messages[0].images = NULL;
        conv->messages[0].image_count = 0;
        conv->message_count = 1;
    }

    ESP_LOGD(TAG, "Created conversation (system_prompt=%s)", system_prompt ? "yes" : "no");
    return (xai_conversation_t)conv;
}

/**
 * @brief Add user message to conversation
 */
void xai_conversation_add_user(xai_conversation_t conv, const char *message) {
    if (!conv || !message) {
        ESP_LOGE(TAG, "Invalid arguments");
        return;
    }

    struct xai_conversation_s *conv_impl = (struct xai_conversation_s *)conv;

    // Check if we need to resize message array
    if (conv_impl->message_count >= conv_impl->message_capacity) {
        size_t new_capacity = conv_impl->message_capacity * 2;
        xai_message_t *new_messages = realloc(
            conv_impl->messages,
            new_capacity * sizeof(xai_message_t)
        );
        if (!new_messages) {
            ESP_LOGE(TAG, "Failed to resize message array");
            return;
        }
        conv_impl->messages = new_messages;
        conv_impl->message_capacity = new_capacity;
    }

    // Add user message
    size_t idx = conv_impl->message_count++;
    conv_impl->messages[idx].role = XAI_ROLE_USER;
    conv_impl->messages[idx].content = strdup(message);
    conv_impl->messages[idx].name = NULL;
    conv_impl->messages[idx].tool_call_id = NULL;
    conv_impl->messages[idx].tool_calls = NULL;
    conv_impl->messages[idx].tool_call_count = 0;
    conv_impl->messages[idx].images = NULL;
    conv_impl->messages[idx].image_count = 0;

    ESP_LOGD(TAG, "Added user message (%zu total)", conv_impl->message_count);
}

/**
 * @brief Add assistant message to conversation
 */
void xai_conversation_add_assistant(xai_conversation_t conv, const char *message) {
    if (!conv || !message) {
        ESP_LOGE(TAG, "Invalid arguments");
        return;
    }

    struct xai_conversation_s *conv_impl = (struct xai_conversation_s *)conv;

    // Check if we need to resize message array
    if (conv_impl->message_count >= conv_impl->message_capacity) {
        size_t new_capacity = conv_impl->message_capacity * 2;
        xai_message_t *new_messages = realloc(
            conv_impl->messages,
            new_capacity * sizeof(xai_message_t)
        );
        if (!new_messages) {
            ESP_LOGE(TAG, "Failed to resize message array");
            return;
        }
        conv_impl->messages = new_messages;
        conv_impl->message_capacity = new_capacity;
    }

    // Add assistant message
    size_t idx = conv_impl->message_count++;
    conv_impl->messages[idx].role = XAI_ROLE_ASSISTANT;
    conv_impl->messages[idx].content = strdup(message);
    conv_impl->messages[idx].name = NULL;
    conv_impl->messages[idx].tool_call_id = NULL;
    conv_impl->messages[idx].tool_calls = NULL;
    conv_impl->messages[idx].tool_call_count = 0;
    conv_impl->messages[idx].images = NULL;
    conv_impl->messages[idx].image_count = 0;

    ESP_LOGD(TAG, "Added assistant message (%zu total)", conv_impl->message_count);
}

/**
 * @brief Complete conversation and get response
 */
xai_err_t xai_conversation_complete(
    xai_client_t client,
    xai_conversation_t conv,
    xai_response_t *response
) {
    if (!client || !conv || !response) {
        ESP_LOGE(TAG, "Invalid arguments");
        return XAI_ERR_INVALID_ARG;
    }

    struct xai_conversation_s *conv_impl = (struct xai_conversation_s *)conv;

    if (conv_impl->message_count == 0) {
        ESP_LOGE(TAG, "No messages in conversation");
        return XAI_ERR_INVALID_ARG;
    }

    // Call chat completion
    xai_err_t err = xai_chat_completion(
        client,
        conv_impl->messages,
        conv_impl->message_count,
        NULL,
        response
    );

    if (err != XAI_OK) {
        return err;
    }

    // Add assistant response to conversation history
    if (response->content) {
        xai_conversation_add_assistant(conv, response->content);
    }

    ESP_LOGD(TAG, "Conversation completed (%zu messages)", conv_impl->message_count);
    return XAI_OK;
}

/**
 * @brief Clear conversation history
 */
void xai_conversation_clear(xai_conversation_t conv) {
    if (!conv) {
        return;
    }

    struct xai_conversation_s *conv_impl = (struct xai_conversation_s *)conv;

    // Free all message contents (except system prompt)
    for (size_t i = 0; i < conv_impl->message_count; i++) {
        if (conv_impl->messages[i].role != XAI_ROLE_SYSTEM) {
            if (conv_impl->messages[i].content) {
                free((void*)conv_impl->messages[i].content);
            }
        }
    }

    // Reset to just system prompt if it exists
    if (conv_impl->system_prompt) {
        conv_impl->messages[0].role = XAI_ROLE_SYSTEM;
        conv_impl->messages[0].content = conv_impl->system_prompt;
        conv_impl->messages[0].name = NULL;
        conv_impl->messages[0].tool_call_id = NULL;
        conv_impl->messages[0].tool_calls = NULL;
        conv_impl->messages[0].tool_call_count = 0;
        conv_impl->messages[0].images = NULL;
        conv_impl->messages[0].image_count = 0;
        conv_impl->message_count = 1;
    } else {
        conv_impl->message_count = 0;
    }

    ESP_LOGD(TAG, "Cleared conversation");
}

/**
 * @brief Destroy conversation and free resources
 */
void xai_conversation_destroy(xai_conversation_t conv) {
    if (!conv) {
        return;
    }

    struct xai_conversation_s *conv_impl = (struct xai_conversation_s *)conv;

    // Free all message contents
    for (size_t i = 0; i < conv_impl->message_count; i++) {
        if (conv_impl->messages[i].role != XAI_ROLE_SYSTEM ||
            !conv_impl->system_prompt) {
            if (conv_impl->messages[i].content) {
                free((void*)conv_impl->messages[i].content);
            }
        }
    }

    // Free system prompt
    if (conv_impl->system_prompt) {
        free(conv_impl->system_prompt);
    }

    // Free message array
    if (conv_impl->messages) {
        free(conv_impl->messages);
    }

    free(conv_impl);
    ESP_LOGD(TAG, "Destroyed conversation");
}

#endif // CONFIG_XAI_ENABLE_CONVERSATION_HELPER

