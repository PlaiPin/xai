/**
 * @file xai_search.c
 * @brief Search parameter helper functions
 * 
 * Provides convenient helper functions for creating search parameters
 * for xAI's search/grounding features (web, X, news, RSS sources).
 */

#include "sdkconfig.h"

#ifdef CONFIG_XAI_ENABLE_SEARCH

#include <string.h>
#include <stdlib.h>
#include "xai.h"
#include "esp_log.h"

static const char *TAG = "xai_search";

/**
 * @brief Helper: Create search parameters for web sources
 */
xai_search_params_t* xai_search_params_web(
    xai_search_mode_t mode,
    bool return_citations,
    const char **allowed_websites
) {
    xai_search_params_t *params = calloc(1, sizeof(xai_search_params_t));
    if (!params) {
        ESP_LOGE(TAG, "Failed to allocate search params");
        return NULL;
    }

    params->mode = mode;
    params->return_citations = return_citations;
    params->max_results = 0;  // Use default

    // Create web source
    params->sources = calloc(1, sizeof(xai_search_source_t));
    if (!params->sources) {
        ESP_LOGE(TAG, "Failed to allocate source");
        free(params);
        return NULL;
    }

    params->source_count = 1;
    params->sources[0].type = XAI_SOURCE_WEB;
    params->sources[0].web.allowed_websites = allowed_websites;
    params->sources[0].web.excluded_websites = NULL;
    params->sources[0].web.safe_search = false;

    ESP_LOGD(TAG, "Created web search params (mode=%d, citations=%d)", mode, return_citations);
    return params;
}

/**
 * @brief Helper: Create search parameters for X (Twitter) sources
 */
xai_search_params_t* xai_search_params_x(
    xai_search_mode_t mode,
    bool return_citations,
    const char **x_handles
) {
    xai_search_params_t *params = calloc(1, sizeof(xai_search_params_t));
    if (!params) {
        ESP_LOGE(TAG, "Failed to allocate search params");
        return NULL;
    }

    params->mode = mode;
    params->return_citations = return_citations;
    params->max_results = 0;

    // Create X source
    params->sources = calloc(1, sizeof(xai_search_source_t));
    if (!params->sources) {
        ESP_LOGE(TAG, "Failed to allocate source");
        free(params);
        return NULL;
    }

    params->source_count = 1;
    params->sources[0].type = XAI_SOURCE_X;
    params->sources[0].x.included_x_handles = x_handles;
    params->sources[0].x.excluded_x_handles = NULL;
    params->sources[0].x.post_favorite_count_min = 0;
    params->sources[0].x.post_view_count_min = 0;
    params->sources[0].x.enable_image_understanding = false;
    params->sources[0].x.enable_video_understanding = false;

    ESP_LOGD(TAG, "Created X search params (mode=%d, citations=%d)", mode, return_citations);
    return params;
}

/**
 * @brief Helper: Create search parameters for news sources
 */
xai_search_params_t* xai_search_params_news(
    xai_search_mode_t mode,
    bool return_citations,
    const char *country
) {
    xai_search_params_t *params = calloc(1, sizeof(xai_search_params_t));
    if (!params) {
        ESP_LOGE(TAG, "Failed to allocate search params");
        return NULL;
    }

    params->mode = mode;
    params->return_citations = return_citations;
    params->max_results = 0;

    // Create news source
    params->sources = calloc(1, sizeof(xai_search_source_t));
    if (!params->sources) {
        ESP_LOGE(TAG, "Failed to allocate source");
        free(params);
        return NULL;
    }

    params->source_count = 1;
    params->sources[0].type = XAI_SOURCE_NEWS;
    params->sources[0].news.country = country;
    params->sources[0].news.excluded_websites = NULL;
    params->sources[0].news.safe_search = false;

    ESP_LOGD(TAG, "Created news search params (mode=%d, citations=%d, country=%s)",
             mode, return_citations, country ? country : "all");
    return params;
}

/**
 * @brief Helper: Create search parameters for RSS feed sources
 */
xai_search_params_t* xai_search_params_rss(
    xai_search_mode_t mode,
    bool return_citations,
    const char *rss_url
) {
    if (!rss_url) {
        ESP_LOGE(TAG, "RSS URL is required");
        return NULL;
    }

    xai_search_params_t *params = calloc(1, sizeof(xai_search_params_t));
    if (!params) {
        ESP_LOGE(TAG, "Failed to allocate search params");
        return NULL;
    }

    params->mode = mode;
    params->return_citations = return_citations;
    params->max_results = 0;

    // Create RSS source
    params->sources = calloc(1, sizeof(xai_search_source_t));
    if (!params->sources) {
        ESP_LOGE(TAG, "Failed to allocate source");
        free(params);
        return NULL;
    }

    // Allocate RSS links array (currently supports 1 feed)
    const char **rss_links = calloc(2, sizeof(char*));  // NULL-terminated
    if (!rss_links) {
        ESP_LOGE(TAG, "Failed to allocate RSS links");
        free(params->sources);
        free(params);
        return NULL;
    }
    rss_links[0] = rss_url;
    rss_links[1] = NULL;

    params->source_count = 1;
    params->sources[0].type = XAI_SOURCE_RSS;
    params->sources[0].rss.rss_links = rss_links;

    ESP_LOGD(TAG, "Created RSS search params (mode=%d, citations=%d, url=%s)",
             mode, return_citations, rss_url);
    return params;
}

/**
 * @brief Free search parameters
 */
void xai_search_params_free(xai_search_params_t *params) {
    if (!params) {
        return;
    }

    if (params->sources) {
        // Free RSS links if allocated
        for (size_t i = 0; i < params->source_count; i++) {
            if (params->sources[i].type == XAI_SOURCE_RSS &&
                params->sources[i].rss.rss_links) {
                free((void*)params->sources[i].rss.rss_links);
            }
        }
        free(params->sources);
    }

    free(params);
    ESP_LOGD(TAG, "Freed search params");
}

#endif // CONFIG_XAI_ENABLE_SEARCH

