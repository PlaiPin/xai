# Web Search Example

Demonstrates real-time web/X/news search grounding with citation support.

## Features

- Web search with citations
- X (Twitter) search
- News search with country filtering
- RSS feed search (if enabled)
- Citation parsing and display

## Configuration

Update WiFi and API key in `web_search_example.c`.

## Build

```bash
cd examples/web_search
idf.py build flash monitor
```

## What You'll Learn

- Using `xai_web_search()` for simple web grounding
- Creating search parameters with helper functions
- Parsing and displaying citations
- Filtering by source type (web/X/news/RSS)

## Key API Calls

```c
// Simple web search
xai_web_search(client, prompt, XAI_SEARCH_AUTO, true, &response);

// X search with helpers
xai_search_params_t *params = xai_search_params_x(XAI_SEARCH_AUTO, true, NULL);
xai_chat_completion_with_search(client, messages, count, params, &response);

// Display citations
for (size_t i = 0; i < response.citation_count; i++) {
    printf("Source: %s\n", response.citations[i].url);
}
```

## Configuration

Enable search in menuconfig:
```
Component config → xAI Configuration → Feature Toggles → Enable search/grounding support
```

## Related Examples

- `conversation` - Multi-turn with search
- `streaming_chat` - Real-time search results

