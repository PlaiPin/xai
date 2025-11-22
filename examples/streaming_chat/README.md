# Streaming Chat Example

This example demonstrates real-time streaming responses from xAI Grok models using Server-Sent Events (SSE).

## Features

- Real-time incremental response delivery
- Callback-based async processing
- Multi-turn streaming conversations
- Live output to console

## What You'll Learn

1. How to use `xai_chat_completion_stream()` for streaming responses
2. Implementing stream callback functions
3. Handling incremental content delivery
4. Multi-turn streaming conversations

## Hardware Required

- ESP32/ESP32-S2/ESP32-S3/ESP32-C3
- WiFi connection
- Minimum 320KB RAM recommended

## Configuration

1. Set your WiFi credentials in `streaming_chat_example.c`:
```c
#define WIFI_SSID      "your_wifi_ssid"
#define WIFI_PASSWORD  "your_wifi_password"
```

2. Set your xAI API key:
```c
#define XAI_API_KEY    "your_xai_api_key_here"
```

Get your API key from: https://console.x.ai

## Building and Running

```bash
cd examples/streaming_chat
idf.py build
idf.py -p (PORT) flash monitor
```

## Expected Output

```
I (1234) streaming_example: WiFi initialization complete
I (2345) streaming_example: Creating xAI client...

=== Example 1: Streaming Chat ===
User: Write a haiku about embedded systems programming on ESP32
Grok: Code flows through circuits,
ESP32 awakens bright,
Digital dreams light.
[Stream ended]

=== Example 2: Multi-turn Streaming ===
Grok: Here are three popular RTOS examples:

1. FreeRTOS - Open-source, widely used...
2. Zephyr - Linux Foundation project...
3. Azure RTOS - Microsoft's offering...
[Stream ended]
```

## Key Concepts

### Stream Callback Function

```c
static void stream_callback(const char *chunk, size_t length, void *user_data) {
    if (chunk == NULL) {
        // End of stream
        printf("\n[Stream ended]\n");
        return;
    }
    
    // Print chunk immediately
    printf("%.*s", (int)length, chunk);
    fflush(stdout);
}
```

The callback is invoked for each chunk of text:
- `chunk != NULL`: Text content to display
- `chunk == NULL`: Stream has ended

### Streaming Options

```c
xai_options_t options = xai_options_default();
options.stream = true;  // Enable streaming
options.temperature = 0.8f;
options.max_tokens = 150;
```

## Troubleshooting

**No output appears:**
- Check WiFi connection
- Verify API key is correct
- Increase HTTP timeout in menuconfig

**Choppy output:**
- Normal for network latency
- SSE delivers content as generated
- Consider buffering if needed

**Memory errors:**
- Increase task stack size (8192 minimum)
- Adjust `CONFIG_XAI_MAX_RESPONSE_SIZE` in menuconfig

## Next Steps

- Try different prompts and questions
- Experiment with temperature and max_tokens
- Implement buffering for smoother output
- Add conversation history tracking

## Related Examples

- `basic_chat` - Non-streaming chat completions
- `conversation` - Multi-turn conversation management
- `web_search` - Streaming with real-time search data

