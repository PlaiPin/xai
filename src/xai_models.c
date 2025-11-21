/**
 * @file xai_models.c
 * @brief Model information and listing
 * 
 * Provides model information database and API functions for listing
 * and retrieving model details. Includes all 25+ xAI models.
 */

#include <string.h>
#include <stdlib.h>
#include "xai.h"
#include "xai_internal.h"
#include "esp_log.h"

static const char *TAG = "xai_models";

/**
 * @brief Model information database
 * 
 * Based on Vercel AI SDK xAI provider source code.
 * Models ending in -latest auto-update to newest version.
 * Dated models (e.g., -1212) are pinned to specific releases.
 */
static const xai_model_info_t MODEL_DATABASE[] = {
    // Grok-4 Series (Latest, with reasoning)
    {
        .id = "grok-4",
        .description = "Grok-4 full capability model",
        .max_tokens = 131072,
        .supports_vision = false,
        .supports_tools = true,
        .supports_reasoning = true,
        .supports_search = true
    },
    {
        .id = "grok-4-latest",
        .description = "Auto-updated to latest grok-4",
        .max_tokens = 131072,
        .supports_vision = false,
        .supports_tools = true,
        .supports_reasoning = true,
        .supports_search = true
    },
    {
        .id = "grok-4-0709",
        .description = "Grok-4 dated release (2024-07-09)",
        .max_tokens = 131072,
        .supports_vision = false,
        .supports_tools = true,
        .supports_reasoning = true,
        .supports_search = true
    },
    {
        .id = "grok-4-fast-reasoning",
        .description = "Fast grok-4 with thinking capability",
        .max_tokens = 131072,
        .supports_vision = false,
        .supports_tools = true,
        .supports_reasoning = true,
        .supports_search = true
    },
    {
        .id = "grok-4-fast-non-reasoning",
        .description = "Fast grok-4 without reasoning overhead",
        .max_tokens = 131072,
        .supports_vision = false,
        .supports_tools = true,
        .supports_reasoning = false,
        .supports_search = true
    },
    {
        .id = "grok-code-fast-1",
        .description = "Code-specialized fast model",
        .max_tokens = 131072,
        .supports_vision = false,
        .supports_tools = true,
        .supports_reasoning = false,
        .supports_search = true
    },
    
    // Grok-3 Series
    {
        .id = "grok-3",
        .description = "Grok-3 current generation",
        .max_tokens = 131072,
        .supports_vision = false,
        .supports_tools = true,
        .supports_reasoning = false,
        .supports_search = true
    },
    {
        .id = "grok-3-latest",
        .description = "Auto-updated to latest grok-3",
        .max_tokens = 131072,
        .supports_vision = false,
        .supports_tools = true,
        .supports_reasoning = false,
        .supports_search = true
    },
    {
        .id = "grok-3-fast",
        .description = "Grok-3 with lower latency",
        .max_tokens = 131072,
        .supports_vision = false,
        .supports_tools = true,
        .supports_reasoning = false,
        .supports_search = true
    },
    {
        .id = "grok-3-fast-latest",
        .description = "Auto-updated grok-3-fast",
        .max_tokens = 131072,
        .supports_vision = false,
        .supports_tools = true,
        .supports_reasoning = false,
        .supports_search = true
    },
    {
        .id = "grok-3-mini",
        .description = "Efficient small grok-3 model (best for ESP32)",
        .max_tokens = 131072,
        .supports_vision = false,
        .supports_tools = true,
        .supports_reasoning = false,
        .supports_search = true
    },
    {
        .id = "grok-3-mini-latest",
        .description = "Auto-updated grok-3-mini",
        .max_tokens = 131072,
        .supports_vision = false,
        .supports_tools = true,
        .supports_reasoning = false,
        .supports_search = true
    },
    {
        .id = "grok-3-mini-fast",
        .description = "Smallest/fastest grok-3",
        .max_tokens = 131072,
        .supports_vision = false,
        .supports_tools = true,
        .supports_reasoning = false,
        .supports_search = true
    },
    {
        .id = "grok-3-mini-fast-latest",
        .description = "Auto-updated grok-3-mini-fast",
        .max_tokens = 131072,
        .supports_vision = false,
        .supports_tools = true,
        .supports_reasoning = false,
        .supports_search = true
    },
    
    // Grok-2 Series
    {
        .id = "grok-2",
        .description = "Grok-2 previous generation",
        .max_tokens = 131072,
        .supports_vision = false,
        .supports_tools = true,
        .supports_reasoning = false,
        .supports_search = true
    },
    {
        .id = "grok-2-latest",
        .description = "Auto-updated grok-2",
        .max_tokens = 131072,
        .supports_vision = false,
        .supports_tools = true,
        .supports_reasoning = false,
        .supports_search = true
    },
    {
        .id = "grok-2-1212",
        .description = "Grok-2 dated release (2024-12-12)",
        .max_tokens = 131072,
        .supports_vision = false,
        .supports_tools = true,
        .supports_reasoning = false,
        .supports_search = true
    },
    {
        .id = "grok-2-vision",
        .description = "Grok-2 with vision capabilities",
        .max_tokens = 131072,
        .supports_vision = true,
        .supports_tools = true,
        .supports_reasoning = false,
        .supports_search = true
    },
    {
        .id = "grok-2-vision-latest",
        .description = "Auto-updated grok-2-vision",
        .max_tokens = 131072,
        .supports_vision = true,
        .supports_tools = true,
        .supports_reasoning = false,
        .supports_search = true
    },
    {
        .id = "grok-2-vision-1212",
        .description = "Grok-2-vision dated release (2024-12-12)",
        .max_tokens = 131072,
        .supports_vision = true,
        .supports_tools = true,
        .supports_reasoning = false,
        .supports_search = true
    },
    {
        .id = "grok-2-image",
        .description = "Grok-2 image model",
        .max_tokens = 131072,
        .supports_vision = true,
        .supports_tools = false,
        .supports_reasoning = false,
        .supports_search = false
    },
    {
        .id = "grok-2-image-latest",
        .description = "Auto-updated grok-2-image",
        .max_tokens = 131072,
        .supports_vision = true,
        .supports_tools = false,
        .supports_reasoning = false,
        .supports_search = false
    },
    {
        .id = "grok-2-image-1212",
        .description = "Grok-2-image dated release (2024-12-12)",
        .max_tokens = 131072,
        .supports_vision = true,
        .supports_tools = false,
        .supports_reasoning = false,
        .supports_search = false
    },
    
    // Legacy
    {
        .id = "grok-beta",
        .description = "Legacy grok beta (128K context)",
        .max_tokens = 131072,
        .supports_vision = false,
        .supports_tools = true,
        .supports_reasoning = false,
        .supports_search = true
    },
    {
        .id = "grok-vision-beta",
        .description = "Legacy grok vision beta",
        .max_tokens = 8192,
        .supports_vision = true,
        .supports_tools = false,
        .supports_reasoning = false,
        .supports_search = false
    }
};

static const size_t MODEL_DATABASE_SIZE = sizeof(MODEL_DATABASE) / sizeof(MODEL_DATABASE[0]);

/**
 * @brief Get model information by ID
 */
const xai_model_info_t* xai_get_model_info(const char *model_id) {
    if (!model_id) {
        return NULL;
    }

    for (size_t i = 0; i < MODEL_DATABASE_SIZE; i++) {
        if (strcmp(MODEL_DATABASE[i].id, model_id) == 0) {
            return &MODEL_DATABASE[i];
        }
    }

    ESP_LOGW(TAG, "Model not found: %s", model_id);
    return NULL;
}

/**
 * @brief List available models (API endpoint)
 */
xai_err_t xai_list_models(
    xai_client_t client,
    xai_model_info_t **models,
    size_t *model_count
) {
    if (!client || !models || !model_count) {
        ESP_LOGE(TAG, "Invalid arguments");
        return XAI_ERR_INVALID_ARG;
    }

    struct xai_client_s *client_impl = (struct xai_client_s *)client;

    // Call API endpoint GET /v1/models
    char *response_data = NULL;
    size_t response_len = 0;
    xai_err_t err = xai_http_get(
        client_impl->http_client,
        "/models",
        &response_data,
        &response_len
    );

    if (err != XAI_OK) {
        ESP_LOGE(TAG, "Failed to list models: %d", err);
        return err;
    }

    // Parse response (for now, just return local database)
    // TODO: Parse actual API response
    free(response_data);

    // Return local database
    *models = (xai_model_info_t *)MODEL_DATABASE;
    *model_count = MODEL_DATABASE_SIZE;

    ESP_LOGI(TAG, "Listed %zu models", MODEL_DATABASE_SIZE);
    return XAI_OK;
}

/**
 * @brief Get local model database
 */
const xai_model_info_t* xai_get_model_database(size_t *count) {
    if (count) {
        *count = MODEL_DATABASE_SIZE;
    }
    return MODEL_DATABASE;
}

/**
 * @brief Get recommended model for ESP32
 */
const char* xai_get_recommended_model(void) {
    // grok-3-mini-fast is most efficient for ESP32
    return "grok-3-mini-fast-latest";
}

