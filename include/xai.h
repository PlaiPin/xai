/**
 * @file xai.h
 * @brief xAI ESP-IDF Component - Public API
 * 
 * ESP-IDF component for interfacing with the xAI REST API.
 * Supports Grok models with chat, vision, image generation, and search/grounding.
 * 
 * @copyright 2025
 * @license Apache-2.0
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * @defgroup xai_types Core Types
 * @{
 */

/** Opaque client handle */
typedef struct xai_client_s* xai_client_t;

/** Opaque conversation handle */
typedef struct xai_conversation_s* xai_conversation_t;

/**
 * @brief Error codes
 */
typedef enum {
    XAI_OK = 0,                     /**< Success */
    XAI_ERR_INVALID_ARG,            /**< Invalid argument */
    XAI_ERR_NO_MEMORY,              /**< Out of memory */
    XAI_ERR_HTTP_FAILED,            /**< HTTP request failed */
    XAI_ERR_PARSE_FAILED,           /**< JSON parsing failed */
    XAI_ERR_AUTH_FAILED,            /**< Authentication failed */
    XAI_ERR_RATE_LIMIT,             /**< Rate limit exceeded */
    XAI_ERR_TIMEOUT,                /**< Request timeout */
    XAI_ERR_API_ERROR,              /**< API error response */
    XAI_ERR_NOT_SUPPORTED           /**< Feature not supported */
} xai_err_t;

/**
 * @brief Message roles
 */
typedef enum {
    XAI_ROLE_SYSTEM,                /**< System message */
    XAI_ROLE_USER,                  /**< User message */
    XAI_ROLE_ASSISTANT,             /**< Assistant message */
    XAI_ROLE_TOOL                   /**< Tool result message */
} xai_message_role_t;

/**
 * @brief Search modes for grounding
 */
typedef enum {
    XAI_SEARCH_OFF,                 /**< Disable search */
    XAI_SEARCH_AUTO,                /**< Model decides when to search */
    XAI_SEARCH_ON                   /**< Always perform search */
} xai_search_mode_t;

/**
 * @brief Search source types
 */
typedef enum {
    XAI_SOURCE_WEB,                 /**< General web search */
    XAI_SOURCE_NEWS,                /**< News articles */
    XAI_SOURCE_X,                   /**< X (Twitter) posts */
    XAI_SOURCE_RSS                  /**< RSS feeds */
} xai_search_source_type_t;

/** @} */

/**
 * @defgroup xai_structs Data Structures
 * @{
 */

/**
 * @brief Client configuration
 */
typedef struct {
    const char *api_key;            /**< xAI API key (required) */
    const char *base_url;           /**< Base URL (default: https://api.x.ai/v1) */
    const char *default_model;      /**< Default model (default: grok-3-latest) */
    uint32_t timeout_ms;            /**< Request timeout in ms (default: 60000) */
    uint32_t max_retries;           /**< Max retry attempts (default: 3) */
    size_t max_tokens;              /**< Default max tokens (default: 1024) */
    float temperature;              /**< Default temperature (default: 1.0) */
} xai_config_t;

/**
 * @brief Image for vision models
 */
typedef struct {
    const char *url;                /**< Image URL (if remote) */
    const uint8_t *data;            /**< Image data (if local) */
    size_t data_len;                /**< Image data length */
    const char *detail;             /**< Detail level: "auto", "low", "high" */
} xai_image_t;

/**
 * @brief Tool call from model
 */
typedef struct {
    char *id;                       /**< Tool call ID */
    char *name;                     /**< Function name */
    char *arguments;                /**< JSON arguments */
} xai_tool_call_t;

/**
 * @brief Chat message
 */
typedef struct {
    xai_message_role_t role;        /**< Message role */
    const char *content;            /**< Message content */
    const char *name;               /**< Optional: function name for tool messages */
    const char *tool_call_id;       /**< Optional: tool call ID for tool responses */
    
    // Vision support (for user messages with grok-2-vision models)
    const xai_image_t *images;      /**< Optional: array of images for multi-modal messages */
    size_t image_count;             /**< Number of images */
    
    // Tool calls (for assistant messages in multi-turn conversations)
    const xai_tool_call_t *tool_calls; /**< Optional: array of tool calls from assistant */
    size_t tool_call_count;         /**< Number of tool calls */
} xai_message_t;

/**
 * @brief Search source configuration
 */
typedef struct {
    xai_search_source_type_t type;  /**< Source type */
    
    union {
        // Web source options
        struct {
            const char **allowed_websites;      /**< NULL-terminated array of allowed domains */
            const char **excluded_websites;     /**< NULL-terminated array of excluded domains */
            bool safe_search;                   /**< Enable safe search filtering */
        } web;
        
        // News source options  
        struct {
            const char *country;                /**< ISO country code (e.g., "US") */
            const char **excluded_websites;     /**< NULL-terminated array of excluded domains */
            bool safe_search;                   /**< Enable safe search filtering */
        } news;
        
        // X source options
        struct {
            const char **included_x_handles;    /**< NULL-terminated array of X handles to include */
            const char **excluded_x_handles;    /**< NULL-terminated array of X handles to exclude */
            uint32_t post_favorite_count_min;   /**< Minimum likes threshold (0 = no filter) */
            uint32_t post_view_count_min;       /**< Minimum views threshold (0 = no filter) */
            bool enable_image_understanding;    /**< Analyze images in posts */
            bool enable_video_understanding;    /**< Analyze videos in posts */
        } x;
        
        // RSS source options
        struct {
            const char **rss_links;             /**< NULL-terminated array of RSS URLs (max 1) */
        } rss;
    };
} xai_search_source_t;

/**
 * @brief Search parameters for grounding
 */
typedef struct {
    xai_search_mode_t mode;         /**< Search mode */
    bool return_citations;          /**< Include source citations */
    const char *from_date;          /**< Start date (ISO8601: YYYY-MM-DD) */
    const char *to_date;            /**< End date (ISO8601: YYYY-MM-DD) */
    uint32_t max_results;           /**< Max search results (0 = default: 20) */
    xai_search_source_t *sources;   /**< Array of search sources */
    size_t source_count;            /**< Number of sources (0 = default: web, x) */
} xai_search_params_t;

/**
 * @brief Citation from search results
 * 
 * Note: The xAI API currently returns citations as simple URL strings.
 * Only the 'url' and 'source_type' fields are populated by the API.
 * Other fields are reserved for future API enhancements.
 */
typedef struct {
    char *source_type;              /**< Source type (currently always "url") */
    char *url;                      /**< Source URL (primary field returned by API) */
    char *title;                    /**< Source title (reserved for future use) */
    char *snippet;                  /**< Relevant excerpt (reserved for future use) */
    char *author;                   /**< Author name or X handle (reserved for future use) */
    char *published_date;           /**< Publication date (reserved for future use) */
} xai_citation_t;

/**
 * @brief Tool definition for function calling
 */
typedef struct {
    const char *name;               /**< Tool name */
    const char *description;        /**< Tool description */
    const char *parameters_json;    /**< JSON schema for parameters */
} xai_tool_t;

/**
 * @brief Request options
 * 
 * NOTE: xAI's API does NOT support all OpenAI-compatible parameters.
 * The following parameters are defined but NOT sent to xAI (will be ignored):
 * - stop: Stop sequences
 * - presence_penalty: Presence penalty
 * - frequency_penalty: Frequency penalty
 * - user_id: User identification
 * 
 * Supported parameters:
 * - model, temperature, max_tokens, stream, top_p
 * - reasoning_effort, parallel_function_calling (xAI-specific)
 * - search_params (xAI-specific)
 * - tools, tool_count, tool_choice
 */
typedef struct {
    const char *model;              /**< Override default model */
    float temperature;              /**< Temperature (-1 = use default) */
    size_t max_tokens;              /**< Max tokens (0 = use default) */
    bool stream;                    /**< Enable streaming */
    const char *stop[4];            /**< NOT SUPPORTED by xAI - will be ignored */
    float top_p;                    /**< Top-p sampling (-1 = use default) */
    float presence_penalty;         /**< NOT SUPPORTED by xAI - will be ignored */
    float frequency_penalty;        /**< NOT SUPPORTED by xAI - will be ignored */
    const char *user_id;            /**< NOT SUPPORTED by xAI - will be ignored */
    
    // xAI-specific options
    xai_search_params_t *search_params;     /**< Search/grounding parameters */
    const char *reasoning_effort;           /**< "low" or "high" (grok-4 models) */
    bool parallel_function_calling;         /**< Allow parallel tool calls */
    xai_tool_t *tools;              /**< Array of available tools */
    size_t tool_count;              /**< Number of tools */
    const char *tool_choice;        /**< Tool choice: "auto", "none", or function name */
} xai_options_t;

/**
 * @brief API response
 */
typedef struct {
    char *content;                  /**< Response text (caller must free) */
    char *reasoning_content;        /**< Reasoning/thinking process (grok-4 models only) */
    char *model;                    /**< Model used */
    char *finish_reason;            /**< Finish reason: "stop", "length", "tool_calls" */
    
    // Usage statistics
    uint32_t prompt_tokens;         /**< Prompt tokens used */
    uint32_t completion_tokens;     /**< Completion tokens used */
    uint32_t total_tokens;          /**< Total tokens used */
    
    // Tool calls (if any)
    xai_tool_call_t *tool_calls;    /**< Array of tool calls */
    size_t tool_call_count;         /**< Number of tool calls */
    
    // Citations (if search enabled)
    xai_citation_t *citations;      /**< Array of citations */
    size_t citation_count;          /**< Number of citations */
    
    // Internal (do not access)
    void *_internal;                /**< Internal memory management */
} xai_response_t;

/**
 * @brief Model information
 */
typedef struct {
    const char *id;                 /**< Model ID */
    const char *description;        /**< Model description */
    uint32_t max_tokens;            /**< Maximum context tokens */
    bool supports_vision;           /**< Supports vision/images */
    bool supports_tools;            /**< Supports function calling */
    bool supports_reasoning;        /**< Supports reasoning effort */
    bool supports_search;           /**< Supports search/grounding */
} xai_model_info_t;

/**
 * @brief Image generation request
 * 
 * Note: xAI's image API does NOT support size, quality, or style parameters.
 * The grok-2-image model generates images at a fixed resolution.
 */
typedef struct {
    const char *prompt;             /**< Text prompt for image generation (REQUIRED) */
    const char *model;              /**< Model to use (default: grok-2-image-latest) */
    uint32_t n;                     /**< Number of images (1-10, default: 1) */
    const char *size;               /**< NOT SUPPORTED by xAI - will be ignored */
    const char *response_format;    /**< Response format: "url" or "b64_json" (default: url) */
    const char *quality;            /**< NOT SUPPORTED by xAI - will be ignored */
    const char *style;              /**< NOT SUPPORTED by xAI - will be ignored */
    const char *user_id;            /**< NOT SUPPORTED by xAI - will be ignored */
} xai_image_request_t;

/**
 * @brief Single image data in response
 */
typedef struct {
    char *url;                      /**< Image URL (if format is "url") */
    char *b64_json;                 /**< Base64 encoded image (if format is "b64_json") */
    char *revised_prompt;           /**< Revised prompt used for generation */
} xai_image_data_t;

/**
 * @brief Image generation response
 */
typedef struct {
    uint32_t created;               /**< Timestamp of creation */
    xai_image_data_t *images;       /**< Array of generated images */
    size_t image_count;             /**< Number of images */
} xai_image_response_t;

/**
 * @brief Stream callback function type
 * 
 * @param chunk Text chunk (NULL indicates end of stream)
 * @param length Chunk length
 * @param user_data User data from streaming call
 */
typedef void (*xai_stream_callback_t)(
    const char *chunk,
    size_t length,
    void *user_data
);

/** @} */

/**
 * @defgroup xai_init Initialization & Configuration
 * @{
 */

/**
 * @brief Get default configuration
 * 
 * @return xai_config_t Default configuration
 */
xai_config_t xai_config_default(void);

/**
 * @brief Get default options
 * 
 * @return xai_options_t Default request options
 */
xai_options_t xai_options_default(void);

/**
 * @brief Create xAI client with default configuration
 * 
 * @param api_key xAI API key
 * @return Client handle, or NULL on failure
 */
xai_client_t xai_create(const char *api_key);

/**
 * @brief Create xAI client with custom configuration
 * 
 * @param config Configuration structure
 * @return Client handle, or NULL on failure
 */
xai_client_t xai_create_config(const xai_config_t *config);

/**
 * @brief Destroy xAI client and free resources
 * 
 * @param client Client handle
 */
void xai_destroy(xai_client_t client);

/** @} */

/**
 * @defgroup xai_chat Chat Completions
 * @{
 */

/**
 * @brief Synchronous chat completion
 * 
 * @param client Client handle
 * @param messages Array of messages
 * @param message_count Number of messages
 * @param options Request options (NULL for defaults)
 * @param response Output response (caller must call xai_response_free)
 * @return Error code
 */
xai_err_t xai_chat_completion(
    xai_client_t client,
    const xai_message_t *messages,
    size_t message_count,
    const xai_options_t *options,
    xai_response_t *response
);

/**
 * @brief Streaming chat completion
 * 
 * @param client Client handle
 * @param messages Array of messages
 * @param message_count Number of messages
 * @param options Request options (NULL for defaults)
 * @param callback Stream callback function
 * @param user_data User data passed to callback
 * @return Error code
 */
xai_err_t xai_chat_completion_stream(
    xai_client_t client,
    const xai_message_t *messages,
    size_t message_count,
    const xai_options_t *options,
    xai_stream_callback_t callback,
    void *user_data
);

/**
 * @brief Simple text completion (single user message)
 * 
 * @param client Client handle
 * @param prompt User prompt
 * @param response Output string (caller must free)
 * @param response_len Output length (can be NULL)
 * @return Error code
 */
xai_err_t xai_text_completion(
    xai_client_t client,
    const char *prompt,
    char **response,
    size_t *response_len
);

/**
 * @brief Free response memory
 * 
 * @param response Response to free
 */
void xai_response_free(xai_response_t *response);

/** @} */

/**
 * @defgroup xai_search Search & Grounding
 * @{
 */

/**
 * @brief Chat completion with search grounding
 * 
 * @param client Client handle
 * @param messages Array of messages
 * @param message_count Number of messages
 * @param search_params Search configuration
 * @param response Output response with citations
 * @return Error code
 */
xai_err_t xai_chat_completion_with_search(
    xai_client_t client,
    const xai_message_t *messages,
    size_t message_count,
    const xai_search_params_t *search_params,
    xai_response_t *response
);

/**
 * @brief Simple web-grounded text completion
 * 
 * @param client Client handle
 * @param prompt User prompt
 * @param search_mode Search mode
 * @param return_citations Include citations
 * @param response Output response
 * @return Error code
 */
xai_err_t xai_web_search(
    xai_client_t client,
    const char *prompt,
    xai_search_mode_t search_mode,
    bool return_citations,
    xai_response_t *response
);

/**
 * @brief Create search parameters for web sources
 * 
 * @param mode Search mode
 * @param return_citations Include citations
 * @param allowed_websites NULL-terminated array of allowed domains
 * @return Allocated search params (caller must free)
 */
xai_search_params_t* xai_search_params_web(
    xai_search_mode_t mode,
    bool return_citations,
    const char **allowed_websites
);

/**
 * @brief Create search parameters for X sources
 * 
 * @param mode Search mode
 * @param return_citations Include citations
 * @param x_handles NULL-terminated array of X handles
 * @return Allocated search params (caller must free)
 */
xai_search_params_t* xai_search_params_x(
    xai_search_mode_t mode,
    bool return_citations,
    const char **x_handles
);

/**
 * @brief Create search parameters for news sources
 * 
 * @param mode Search mode
 * @param return_citations Include citations
 * @param country ISO country code (NULL for all)
 * @return Allocated search params (caller must free)
 */
xai_search_params_t* xai_search_params_news(
    xai_search_mode_t mode,
    bool return_citations,
    const char *country
);

/**
 * @brief Create search parameters for RSS sources
 * 
 * @param mode Search mode
 * @param return_citations Include citations
 * @param rss_url RSS feed URL
 * @return Allocated search params (caller must free)
 */
xai_search_params_t* xai_search_params_rss(
    xai_search_mode_t mode,
    bool return_citations,
    const char *rss_url
);

/**
 * @brief Free search parameters
 * 
 * @param params Search parameters to free
 */
void xai_search_params_free(xai_search_params_t *params);

/** @} */

/**
 * @defgroup xai_vision Vision
 * @{
 */

/**
 * @brief Vision-enabled completion
 * 
 * @param client Client handle
 * @param prompt Text prompt
 * @param images Array of images
 * @param image_count Number of images
 * @param response Output response
 * @return Error code
 */
xai_err_t xai_vision_completion(
    xai_client_t client,
    const char *prompt,
    const xai_image_t *images,
    size_t image_count,
    xai_response_t *response
);

/** @} */

/**
 * @defgroup xai_tools Tool Calling
 * @{
 */

/**
 * @brief Chat completion with client-side tool calling
 * 
 * @param client Client handle
 * @param messages Array of messages
 * @param message_count Number of messages
 * @param tools Array of tool definitions
 * @param tool_count Number of tools
 * @param response Output response (may contain tool calls)
 * @return Error code
 */
xai_err_t xai_chat_completion_with_tools(
    xai_client_t client,
    const xai_message_t *messages,
    size_t message_count,
    const xai_tool_t *tools,
    size_t tool_count,
    xai_response_t *response
);

/** @} */

/**
 * @defgroup xai_responses Responses API (Server-Side Tools)
 * @{
 */

/**
 * @brief Agentic completion with server-side tool execution
 * 
 * Only works with grok-4, grok-4-fast, grok-4-fast-non-reasoning models.
 * xAI executes tools on their servers and orchestrates multi-step reasoning.
 * 
 * @param client Client handle
 * @param messages Array of messages
 * @param message_count Number of messages
 * @param tools Array of server-side tool definitions
 * @param tool_count Number of tools
 * @param response Output response with final results
 * @return Error code
 */
xai_err_t xai_responses_completion(
    xai_client_t client,
    const xai_message_t *messages,
    size_t message_count,
    const xai_tool_t *tools,
    size_t tool_count,
    xai_response_t *response
);

/**
 * @brief Create server-side web search tool
 * 
 * @param allowed_domains NULL-terminated array of allowed domains (max 5)
 * @param excluded_domains NULL-terminated array of excluded domains (max 5)
 * @param enable_image_understanding Analyze images in search results
 * @return Tool definition
 */
xai_tool_t xai_tool_web_search(
    const char **allowed_domains,
    const char **excluded_domains,
    bool enable_image_understanding
);

/**
 * @brief Create server-side X search tool
 * 
 * @param allowed_handles NULL-terminated array of X handles (max 10)
 * @param excluded_handles NULL-terminated array of excluded handles (max 10)
 * @param from_date ISO8601 date string (YYYY-MM-DD)
 * @param to_date ISO8601 date string (YYYY-MM-DD)
 * @param enable_image_understanding Analyze images in posts
 * @param enable_video_understanding Analyze videos in posts
 * @return Tool definition
 */
xai_tool_t xai_tool_x_search(
    const char **allowed_handles,
    const char **excluded_handles,
    const char *from_date,
    const char *to_date,
    bool enable_image_understanding,
    bool enable_video_understanding
);

/**
 * @brief Create server-side code execution tool
 * 
 * Executes Python code on xAI servers.
 * 
 * @return Tool definition
 */
xai_tool_t xai_tool_code_execution(void);

/** @} */

/**
 * @defgroup xai_conversation Conversation Helpers
 * @{
 */

/**
 * @brief Create conversation context
 * 
 * @param system_prompt System prompt (can be NULL)
 * @return Conversation handle
 */
xai_conversation_t xai_conversation_create(const char *system_prompt);

/**
 * @brief Add user message to conversation
 * 
 * @param conv Conversation handle
 * @param message User message
 */
void xai_conversation_add_user(xai_conversation_t conv, const char *message);

/**
 * @brief Add assistant message to conversation
 * 
 * @param conv Conversation handle
 * @param message Assistant message
 */
void xai_conversation_add_assistant(xai_conversation_t conv, const char *message);

/**
 * @brief Complete conversation and get response
 * 
 * @param client Client handle
 * @param conv Conversation handle
 * @param response Output response
 * @return Error code
 */
xai_err_t xai_conversation_complete(
    xai_client_t client,
    xai_conversation_t conv,
    xai_response_t *response
);

/**
 * @brief Clear conversation history
 * 
 * @param conv Conversation handle
 */
void xai_conversation_clear(xai_conversation_t conv);

/**
 * @brief Destroy conversation and free resources
 * 
 * @param conv Conversation handle
 */
void xai_conversation_destroy(xai_conversation_t conv);

/** @} */

/**
 * @defgroup xai_models Models
 * @{
 */

/**
 * @brief List available models
 * 
 * @param client Client handle
 * @param model_ids Output array of model IDs (caller must free)
 * @param count Output number of models
 * @return Error code
 */
xai_err_t xai_list_models(
    xai_client_t client,
    xai_model_info_t **models,
    size_t *model_count
);

/**
 * @brief Get model information
 * 
 * @param model_id Model ID
 * @return Model info, or NULL if unknown
 */
const xai_model_info_t* xai_get_model_info(const char *model_id);

/** @} */

/**
 * @defgroup xai_images Image Generation
 * @{
 */

/**
 * @brief Generate image from text prompt
 * 
 * @param client Client handle
 * @param request Image generation request
 * @param response Output response (contains image URL or base64)
 * @return Error code
 */
xai_err_t xai_generate_image(
    xai_client_t client,
    const xai_image_request_t *request,
    xai_image_response_t *response
);

/**
 * @brief Free image response memory
 * 
 * @param response Image response to free
 */
void xai_image_response_free(xai_image_response_t *response);

/** @} */

/**
 * @defgroup xai_tokenize Tokenization
 * @{
 */

/**
 * @brief Count tokens in text
 * 
 * @param client Client handle
 * @param text Text to tokenize
 * @param model Model to use (NULL = default)
 * @param token_count Output token count
 * @return Error code
 */
xai_err_t xai_count_tokens(
    xai_client_t client,
    const char *text,
    const char *model,
    uint32_t *token_count
);

/**
 * @brief Count tokens in messages (conversation)
 * 
 * @param client Client handle
 * @param messages Array of messages
 * @param message_count Number of messages
 * @param model Model to use (NULL = default)
 * @param token_count Output token count
 * @return Error code
 */
xai_err_t xai_count_tokens_messages(
    xai_client_t client,
    const xai_message_t *messages,
    size_t message_count,
    const char *model,
    uint32_t *token_count
);

/**
 * @brief Estimate memory needed for response
 * 
 * @param token_count Expected token count
 * @return Estimated memory in bytes
 */
size_t xai_estimate_memory(uint32_t token_count);

/** @} */

/**
 * @defgroup xai_utils Utilities
 * @{
 */

/**
 * @brief Get error string from error code
 * 
 * @param err Error code
 * @return Error description
 */
const char* xai_err_to_string(xai_err_t err);

/** @} */

#ifdef __cplusplus
}
#endif
