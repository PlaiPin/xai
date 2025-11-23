# xAI ESP-IDF Component

[![Component Registry](https://components.espressif.com/components/plaipin/xai/badge.svg)](https://components.espressif.com/components/plaipin/xai)
[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](https://opensource.org/licenses/Apache-2.0)
[![IDF Version](https://img.shields.io/badge/IDF-v5.0%2B-green)](https://github.com/espressif/esp-idf)

Production-ready ESP-IDF component for integrating [xAI's Grok models](https://x.ai) into ESP32 applications. Access 25+ state-of-the-art LLMs with vision, real-time search, function calling, and image generation capabilities — optimized for embedded systems.

---

## Features

### Core Capabilities
- **25+ Grok Models** - From fast mini models to powerful grok-4 with reasoning
- **Chat Completions** - Synchronous and streaming (SSE) responses
- **Vision API** - Multi-modal image understanding with grok-2-vision
- **Image Generation** - Create images with grok-2-image models
- **Real-Time Search** - Web, X (Twitter), News, and RSS grounding with citations
- **Function Calling** - Client-side tool execution with parallel support
- **Responses API** - Server-side agentic tools (webSearch, xSearch, codeExecution)
- **Tokenization** - Count tokens and estimate memory usage
- **Conversation Helpers** - Simplified multi-turn dialogue management

### ESP32 Optimizations
- **Low Memory Footprint** - Configurable buffers (2KB-32KB)
- **Menuconfig Integration** - Full ESP-IDF configuration system
- **FreeRTOS Compatible** - Thread-safe buffer pooling
- **Modular Features** - Disable unused features to save flash (~10KB savings)
- **Automatic Retries** - Configurable retry logic for reliability
- **Debug Logging** - Comprehensive ESP-LOG integration

---

## Table of Contents

- [Installation](#-installation)
- [Quick Start](#-quick-start)
- [Hardware Requirements](#-hardware-requirements)
- [Configuration](#-configuration)
- [API Overview](#-api-overview)
- [Examples](#-examples)
- [Memory Management](#-memory-management)
- [Model Selection](#-model-selection)
- [Advanced Features](#-advanced-features)
- [Troubleshooting](#-troubleshooting)
- [Contributing](#-contributing)
- [License](#-license)

---

## Installation

### Method 1: IDF Component Manager (Recommended)

Add to your project's `idf_component.yml`:

```yaml
dependencies:
  plaipin/xai:
    version: "^0.1.0"
```

Or via command line:

```bash
idf.py add-dependency "plaipin/xai^0.1.0"
```

### Method 2: Manual Installation

Clone into your project's `components` directory:

```bash
cd your_project/components
git clone https://github.com/PlaiPin/xai.git
```

---

## Quick Start

### 1. Get Your API Key

Visit [console.x.ai](https://console.x.ai) to obtain your xAI API key.

### 2. Configure Your Project

Run `idf.py menuconfig` and navigate to:
- `xAI Configuration` → `Default xAI API Key` → Enter your API key
- `Component config` → `ESP System Settings` → `Main task stack size` → Set to `12288`

### 3. Basic Chat Example

```c
#include "xai.h"
#include <string.h>
#include <stdio.h>

void app_main(void)
{
    // Initialize WiFi first (not shown)
    
    // Create xAI client
    xai_client_t client = xai_create("xai-your-api-key");
    if (!client) {
        printf("Failed to create client\n");
        return;
    }
    
    // Send a message
    char *response = NULL;
    size_t response_len = 0;
    xai_err_t err = xai_text_completion(
        client,
        "Explain ESP32 in one sentence",
        &response,
        &response_len
    );
    
    if (err == XAI_OK) {
        printf("Grok: %s\n", response);
        free(response);
    } else {
        printf("Error: %s\n", xai_err_to_string(err));
    }
    
    xai_destroy(client);
}
```

### 4. Build and Flash

```bash
idf.py build
idf.py flash monitor
```

---

## Hardware Requirements

### Minimum Requirements
- **ESP32, ESP32-S2, ESP32-S3, or ESP32-C3**
- **240KB RAM** (for basic chat with grok-3-mini)
- **WiFi connectivity**
- **TLS/SSL support** (built into ESP-IDF)

### Recommended Configuration
- **ESP32-S3** with **8MB Flash** and **8MB PSRAM**
- **320KB+ RAM** available
- **Stable WiFi** with good signal strength

### Model-Specific Requirements

| Model | Recommended RAM | Best For |
|-------|----------------|----------|
| `grok-3-mini-latest` | 240KB | Resource-constrained devices |
| `grok-3-fast-latest` | 280KB | Low-latency responses |
| `grok-3-latest` | 320KB | General-purpose (balanced) |
| `grok-4-latest` | 400KB+ | Advanced reasoning tasks |
| `grok-2-vision-latest` | 320KB | Image understanding |

---

## Configuration

All settings are configurable via `idf.py menuconfig` under **xAI Configuration**.

### Essential Settings

```
xAI Configuration
├── Default xAI API Key              [Your API key]
├── xAI API Base URL                 [https://api.x.ai/v1]
├── Default Model                    [grok-3-latest]
│
├── Memory Configuration
│   ├── Maximum response size        [8192 bytes]
│   └── Buffer pool size             [2 buffers]
│
├── Feature Toggles
│   ├── Enable streaming             [Yes]
│   ├── Enable vision                [Yes]
│   ├── Enable tools                 [No]
│   ├── Enable search                [Yes]
│   └── Enable conversation helper   [Yes]
│
└── Network Settings
    ├── HTTP timeout                 [60000 ms]
    └── Max retries                  [3]
```

### Memory Optimization

For **ESP32 with limited RAM** (240KB-280KB):

```
Memory Configuration
├── Max response size: 2048-4096 bytes
├── Buffer pool: 1 buffer
Feature Toggles
├── Disable vision (saves ~1KB flash)
├── Disable tools (saves ~3KB flash)
└── Disable search (saves ~3KB flash)
```

For **ESP32-S3 with PSRAM** (512KB+):

```
Memory Configuration
├── Max response size: 16384-32768 bytes
├── Buffer pool: 3-4 buffers
Feature Toggles
├── Enable all features
```

---

## API Overview

### Client Management

```c
// Create with default configuration
xai_client_t client = xai_create(api_key);

// Create with custom configuration
xai_config_t config = xai_config_default();
config.default_model = "grok-3-mini-fast-latest";
config.timeout_ms = 30000;
config.max_tokens = 512;
xai_client_t client = xai_create_config(&config);

// Destroy when done
xai_destroy(client);
```

### Chat Completions

#### Simple Text Completion

```c
char *response = NULL;
xai_err_t err = xai_text_completion(client, "Hello!", &response, NULL);
if (err == XAI_OK) {
    printf("%s\n", response);
    free(response);
}
```

#### Advanced Chat with Options

```c
xai_message_t messages[] = {
    {.role = XAI_ROLE_SYSTEM, .content = "You are a helpful assistant."},
    {.role = XAI_ROLE_USER, .content = "What is FreeRTOS?"}
};

xai_options_t options = xai_options_default();
options.temperature = 0.8f;
options.max_tokens = 200;

xai_response_t response;
xai_err_t err = xai_chat_completion(client, messages, 2, &options, &response);

if (err == XAI_OK) {
    printf("Response: %s\n", response.content);
    printf("Tokens used: %u\n", response.total_tokens);
    xai_response_free(&response);
}
```

#### Streaming Chat

```c
void stream_callback(const char *chunk, size_t len, void *user_data) {
    if (chunk == NULL) {
        printf("\n[Stream complete]\n");
    } else {
        printf("%.*s", (int)len, chunk);
        fflush(stdout);
    }
}

xai_options_t options = xai_options_default();
options.stream = true;

xai_err_t err = xai_chat_completion_stream(
    client, messages, count, &options, stream_callback, NULL
);
```

### Vision

```c
xai_image_t image = {
    .url = "https://example.com/image.jpg",
    .detail = "auto"
};

xai_response_t response;
xai_err_t err = xai_vision_completion(
    client,
    "What's in this image?",
    &image,
    1,
    &response
);

if (err == XAI_OK) {
    printf("Analysis: %s\n", response.content);
    xai_response_free(&response);
}
```

### Web Search & Grounding

```c
// Simple web search
xai_response_t response;
xai_err_t err = xai_web_search(
    client,
    "What are the latest developments in ESP32?",
    XAI_SEARCH_AUTO,
    true,  // return citations
    &response
);

if (err == XAI_OK) {
    printf("Response: %s\n", response.content);
    
    // Display citations
    for (size_t i = 0; i < response.citation_count; i++) {
        printf("[%zu] %s\n", i+1, response.citations[i].url);
    }
    
    xai_response_free(&response);
}
```

#### Advanced Search Configuration

```c
// X (Twitter) search with filters
xai_search_params_t *params = xai_search_params_x(
    XAI_SEARCH_AUTO,
    true,  // return citations
    NULL   // all handles (or specify array)
);

xai_chat_completion_with_search(client, messages, count, params, &response);
xai_search_params_free(params);

// News search with country filter
params = xai_search_params_news(XAI_SEARCH_AUTO, true, "US");

// RSS search
params = xai_search_params_rss(XAI_SEARCH_AUTO, true, "https://feeds.example.com/rss");
```

### Function Calling (Tools)

```c
// Define tools
xai_tool_t tools[] = {
    {
        .name = "get_weather",
        .description = "Get current weather for a location",
        .parameters_json = 
            "{\"type\":\"object\","
            "\"properties\":{"
                "\"location\":{\"type\":\"string\"},"
                "\"units\":{\"type\":\"string\",\"enum\":[\"celsius\",\"fahrenheit\"]}"
            "},"
            "\"required\":[\"location\"]}"
    }
};

xai_response_t response;
xai_err_t err = xai_chat_completion_with_tools(
    client, messages, count, tools, 1, &response
);

if (err == XAI_OK && response.tool_call_count > 0) {
    // Execute tool calls
    for (size_t i = 0; i < response.tool_call_count; i++) {
        xai_tool_call_t *call = &response.tool_calls[i];
        printf("Tool: %s\n", call->name);
        printf("Args: %s\n", call->arguments);
        
        // Execute function and return result...
    }
}

xai_response_free(&response);
```

### Image Generation

```c
xai_image_request_t request = {
    .prompt = "A futuristic ESP32 microcontroller with glowing circuits",
    .model = "grok-2-image-latest",
    .n = 1,
    .response_format = "url"
};

xai_image_response_t response;
xai_err_t err = xai_generate_image(client, &request, &response);

if (err == XAI_OK) {
    for (size_t i = 0; i < response.image_count; i++) {
        printf("Image URL: %s\n", response.images[i].url);
    }
    xai_image_response_free(&response);
}
```

### Conversation Helper

```c
// Create conversation with system prompt
xai_conversation_t conv = xai_conversation_create(
    "You are an expert in embedded systems."
);

// Add messages
xai_conversation_add_user(conv, "What is MQTT?");

// Get response (automatically includes history)
xai_response_t response;
xai_err_t err = xai_conversation_complete(client, conv, &response);

if (err == XAI_OK) {
    // Add assistant response to history
    xai_conversation_add_assistant(conv, response.content);
    printf("Grok: %s\n", response.content);
    xai_response_free(&response);
}

// Continue conversation
xai_conversation_add_user(conv, "How do I implement it on ESP32?");
xai_conversation_complete(client, conv, &response);
// ...

// Clean up
xai_conversation_destroy(conv);
```

---

## Examples

All examples include complete WiFi setup and error handling.

| Example | Description | Key Features |
|---------|-------------|--------------|
| [basic_chat](examples/basic_chat) | Simple text completion | Minimal setup, quick start |
| [streaming_chat](examples/streaming_chat) | Real-time streaming | SSE callbacks, live output |
| [conversation](examples/conversation) | Multi-turn dialogue | History management, context |
| [vision](examples/vision) | Image understanding | Multi-modal, URL/base64 images |
| [web_search](examples/web_search) | Grounded responses | Web/X/News search, citations |
| [tools](examples/tools) | Function calling | Client-side tools, parallel execution |
| [image_generation](examples/image_generation) | Create images | Text-to-image, URL/base64 output |

### Running Examples

```bash
cd examples/basic_chat
idf.py menuconfig  # Configure WiFi and API key
idf.py build flash monitor
```

---

## Memory Management

### Buffer Configuration

The component uses a **buffer pool** for response handling:

```c
// Configured in menuconfig
CONFIG_XAI_MAX_RESPONSE_SIZE = 8192      // Per-buffer size
CONFIG_XAI_BUFFER_POOL_SIZE = 2          // Number of buffers
```

**Total heap usage**: `MAX_RESPONSE_SIZE × BUFFER_POOL_SIZE + ~20KB overhead`

### Memory Guidelines

| Application Type | Response Size | Pool Size | Total RAM |
|------------------|---------------|-----------|-----------|
| Simple Q&A | 2048-4096 | 1 | ~25KB |
| General chat | 8192 | 2 | ~36KB |
| Long-form content | 16384 | 2 | ~52KB |
| Multi-threaded | 8192 | 3-4 | ~44-60KB |

### Response Cleanup

**Always free responses** to prevent memory leaks:

```c
xai_response_t response;
xai_chat_completion(client, messages, count, NULL, &response);

// Use response...

xai_response_free(&response);  // Free all allocated memory
```

### Tokenization & Estimation

```c
// Count tokens before sending
uint32_t token_count;
xai_err_t err = xai_count_tokens(client, prompt, NULL, &token_count);

// Estimate memory needed
size_t estimated_bytes = xai_estimate_memory(token_count);
printf("Estimated memory: %u bytes\n", estimated_bytes);
```

---

## Model Selection

### Available Models

#### Grok-4 Family (Latest, Most Capable)
- **`grok-4-latest`** - Advanced reasoning with thinking process (128K context)
- **`grok-4-fast`** - Faster version with reasoning (128K context)
- **`grok-4-fast-non-reasoning`** - No reasoning output (128K context)

#### Grok-3 Family (Recommended for ESP32)
- **`grok-3-latest`** - Balanced performance (128K context) - **RECOMMENDED**
- **`grok-3-mini-latest`** - Efficient, low memory (128K context) - **BEST FOR ESP32**
- **`grok-3-fast-latest`** - Low latency (128K context)
- **`grok-3-mini-fast-latest`** - Minimal + fast (128K context)

#### Grok-2 Family (Stable)
- **`grok-2-1212`** - December 2024 release (131K context)
- **`grok-2-vision-1212`** - Vision capabilities (8K context)

#### Image Generation
- **`grok-2-image-latest`** - Text-to-image generation

### Selecting a Model

```c
// At client creation
xai_config_t config = xai_config_default();
config.default_model = "grok-3-mini-fast-latest";
xai_client_t client = xai_create_config(&config);

// Per request
xai_options_t options = xai_options_default();
options.model = "grok-4-latest";
xai_chat_completion(client, messages, count, &options, &response);
```

### Model Information API

```c
const xai_model_info_t *info = xai_get_model_info("grok-3-latest");
if (info) {
    printf("Model: %s\n", info->id);
    printf("Max tokens: %u\n", info->max_tokens);
    printf("Supports vision: %s\n", info->supports_vision ? "Yes" : "No");
    printf("Supports tools: %s\n", info->supports_tools ? "Yes" : "No");
}
```

---

## Advanced Features

### Reasoning Effort (Grok-4)

Control thinking depth for grok-4 models:

```c
xai_options_t options = xai_options_default();
options.model = "grok-4-latest";
options.reasoning_effort = "high";  // "low" or "high"

xai_chat_completion(client, messages, count, &options, &response);

// Access reasoning process
if (response.reasoning_content) {
    printf("Thinking: %s\n", response.reasoning_content);
}
```

### Parallel Tool Calling

Enable multiple simultaneous function calls:

```c
xai_options_t options = xai_options_default();
options.parallel_function_calling = true;
options.tools = tool_array;
options.tool_count = tool_count;

xai_chat_completion(client, messages, count, &options, &response);

// Process multiple tool calls
for (size_t i = 0; i < response.tool_call_count; i++) {
    execute_tool(&response.tool_calls[i]);
}
```

### Responses API (Server-Side Tools)

Let xAI execute tools on their servers:

```c
// Define server-side tools
xai_tool_t tools[] = {
    xai_tool_web_search(NULL, NULL, false),  // Web search
    xai_tool_x_search(NULL, NULL, NULL, NULL, false, false),  // X search
    xai_tool_code_execution()  // Python execution
};

xai_options_t options = xai_options_default();
options.model = "grok-4-latest";  // Requires grok-4
options.tools = tools;
options.tool_count = 3;

// xAI orchestrates tool execution automatically
xai_responses_completion(client, messages, count, tools, 3, &response);
```

**Note**: Responses API requires significant memory (~5-8KB additional) and only works with grok-4 models. Not recommended for standard ESP32 unless specifically needed.

### Custom Search Sources

```c
xai_search_source_t sources[2];

// Web source with domain filtering
sources[0].type = XAI_SOURCE_WEB;
sources[0].web.allowed_websites = (const char*[]){
    "espressif.com", "esp32.com", NULL
};
sources[0].web.safe_search = true;

// X source with engagement filters
sources[1].type = XAI_SOURCE_X;
sources[1].x.post_favorite_count_min = 100;  // Min 100 likes
sources[1].x.enable_image_understanding = true;

xai_search_params_t params = {
    .mode = XAI_SEARCH_AUTO,
    .return_citations = true,
    .sources = sources,
    .source_count = 2,
    .max_results = 10
};
```

### Date-Range Search

```c
xai_search_params_t *params = xai_search_params_news(XAI_SEARCH_ON, true, "US");
params->from_date = "2024-01-01";
params->to_date = "2024-12-31";

xai_chat_completion_with_search(client, messages, count, params, &response);
```

---

## Troubleshooting

### Common Issues

#### `XAI_ERR_NO_MEMORY`

**Cause**: Insufficient heap memory.

**Solutions**:
- Reduce `CONFIG_XAI_MAX_RESPONSE_SIZE` in menuconfig
- Decrease `CONFIG_XAI_BUFFER_POOL_SIZE`
- Use `grok-3-mini` or `grok-3-mini-fast` models
- Free unused resources before API calls
- Enable PSRAM on ESP32-S3

#### `XAI_ERR_HTTP_FAILED`

**Cause**: Network connectivity issues.

**Solutions**:
- Verify WiFi is connected: `esp_netif_is_netif_up()`
- Check internet access: `ping 8.8.8.8`
- Verify DNS resolution: `ping api.x.ai`
- Increase timeout: `CONFIG_XAI_HTTP_TIMEOUT_MS`
- Check firewall/proxy settings

#### `XAI_ERR_AUTH_FAILED`

**Cause**: Invalid or expired API key.

**Solutions**:
- Verify API key at [console.x.ai](https://console.x.ai)
- Check for typos in key (should start with `xai-`)
- Ensure key is correctly set in menuconfig or code
- Regenerate key if compromised

#### `XAI_ERR_RATE_LIMIT`

**Cause**: Too many requests.

**Solutions**:
- Implement exponential backoff
- Reduce request frequency
- Check rate limits at console.x.ai
- Use streaming to reduce perceived latency

#### `XAI_ERR_TIMEOUT`

**Cause**: Request took too long.

**Solutions**:
- Increase `CONFIG_XAI_HTTP_TIMEOUT_MS` (default: 60s)
- Use faster models (`grok-3-fast`, `grok-3-mini-fast`)
- Reduce `max_tokens` in options
- Check network latency

#### Stack Overflow / Guru Meditation

**Cause**: Task stack too small.

**Solutions**:
- Increase main task stack: `CONFIG_ESP_MAIN_TASK_STACK_SIZE = 12288`
- Increase custom task stack when creating tasks
- Reduce local variable sizes
- Use heap allocation for large buffers

### Debugging Tips

#### Enable Verbose Logging

```
menuconfig → xAI Configuration → Logging → Log level = 5 (Verbose)
```

#### Monitor Heap Usage

```c
printf("Free heap: %u bytes\n", esp_get_free_heap_size());
printf("Min free heap: %u bytes\n", esp_get_minimum_free_heap_size());

xai_chat_completion(client, messages, count, NULL, &response);

printf("After API call: %u bytes\n", esp_get_free_heap_size());
```

#### Test Network Connectivity

```c
esp_err_t err = esp_netif_init();
if (err != ESP_OK) {
    printf("Network init failed: %s\n", esp_err_to_name(err));
}
```

---

## Build Configuration

### Flash Optimization

To minimize flash usage when building:

```bash
# Optimize for size
idf.py menuconfig
# Compiler options → Optimization Level → Optimize for size (-Os)

# Disable unused features
menuconfig → xAI Configuration → Feature Toggles
# Disable vision, tools, search if not needed
```

### PSRAM Support (ESP32-S3)

For large responses or multi-threading:

```bash
menuconfig → Component config → ESP32S3-Specific
# Support for external, SPI-connected RAM → Yes
# SPI RAM config → Initialize SPI RAM during startup
```

---

## Contributing

Contributions are welcome! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

### Development Setup

```bash
git clone https://github.com/PlaiPin/xai.git
cd xai

# Run examples
cd examples/basic_chat
idf.py build
```

### Reporting Issues

Please include:
- ESP-IDF version (`idf.py --version`)
- ESP32 variant (ESP32, ESP32-S3, etc.)
- Component version
- Full error logs with verbose logging enabled
- Minimal reproducible example

Open issues at: [github.com/PlaiPin/xai/issues](https://github.com/PlaiPin/xai/issues)

---

## License

This project is licensed under the Apache License 2.0 - see the [LICENSE](LICENSE) file for details.

---

## Resources

- **xAI Documentation**: [docs.x.ai](https://docs.x.ai)
- **xAI Console**: [console.x.ai](https://console.x.ai)
- **ESP-IDF Documentation**: [docs.espressif.com/projects/esp-idf](https://docs.espressif.com/projects/esp-idf)
- **Component Registry**: [components.espressif.com](https://components.espressif.com)
- **GitHub Repository**: [github.com/PlaiPin/xai](https://github.com/PlaiPin/xai)

---

## Comparison with Other SDKs

This ESP-IDF component follows the design patterns of the [Vercel AI SDK](https://sdk.vercel.ai) but is specifically optimized for embedded systems:

| Feature | Vercel AI SDK | xAI ESP-IDF Component |
|---------|---------------|----------------------|
| **Platform** | Node.js / Edge Runtime | ESP32 / FreeRTOS |
| **Memory** | Unlimited (V8 heap) | Configurable (2KB-32KB) |
| **Streaming** | ReadableStream API | SSE callbacks |
| **Language** | TypeScript | C (C99) |
| **Tools** | Native JavaScript | JSON schema + callbacks |
| **Configuration** | Runtime only | menuconfig + runtime |
| **Transport** | fetch() / Node http | esp_http_client |

### Design Philosophy

Like the Vercel AI SDK, this component prioritizes:
- **Type safety** (via C structs with clear documentation)
- **Ease of use** (simple APIs with sensible defaults)
- **Provider flexibility** (easy to extend/modify)
- **Streaming first** (efficient SSE implementation)

But adapted for embedded constraints:
- **Compile-time optimization** (disable unused features)
- **Explicit memory management** (no garbage collection)
- **Low-level control** (direct buffer access, configurable pooling)
- **FreeRTOS integration** (task-safe, multi-threaded)

---

## API Compatibility Notes

### xAI vs OpenAI Differences

While xAI's API is OpenAI-compatible, some parameters are **not supported**:

#### NOT Supported by xAI
- `stop` (stop sequences)
- `presence_penalty`
- `frequency_penalty`
- `user` (user identification)
- `size` / `quality` / `style` (image generation)

These fields exist in the API structs but are **ignored** by the xAI API.

#### xAI-Specific Extensions
- `reasoning_effort` (grok-4 models)
- `parallel_function_calling` (tool execution)
- Search/grounding parameters (web, X, news, RSS)
- Responses API (server-side tools)

---

