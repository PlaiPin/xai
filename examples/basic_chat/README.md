# Basic Chat Example

This example demonstrates the simplest way to use the xAI ESP32 SDK to send a message to Grok and receive a response.

## What This Example Does

1. Connects to WiFi
2. Creates an xAI client with your API key
3. Sends a simple text prompt to Grok
4. Prints the response
5. Cleans up and restarts

## Hardware Required

- ESP32 development board
- WiFi network connection

## Configuration

### 1. Get Your xAI API Key

Visit [console.x.ai](https://console.x.ai) to get your API key.

### 2. Configure WiFi and API Key

Run `idf.py menuconfig` and set:
- **WiFi SSID**: `Example Configuration` → `WiFi SSID`
- **WiFi Password**: `Example Configuration` → `WiFi Password`
- **xAI API Key**: `xAI Configuration` → `Default xAI API Key`

Alternatively, edit the values directly in `main.c`:

```c
#define EXAMPLE_ESP_WIFI_SSID      "YourWiFiSSID"
#define EXAMPLE_ESP_WIFI_PASS      "YourWiFiPassword"
#define XAI_API_KEY                "xai-your-api-key-here"
```

### 3. Increase main task stack size
Run `idf.py menuconfig` and set:
Component config → ESP System Settings → Main task stack size to 12288

## Build and Flash

```bash
idf.py build
idf.py flash monitor
```

## Expected Output

```
I (12345) xai_example: === xAI ESP32 SDK - Basic Chat Example ===
I (12350) xai_example: WiFi init complete. Connecting to YourWiFi...
I (13000) xai_example: Got IP:192.168.1.100
I (13100) xai_example: Starting xAI chat example...
I (13120) xai_example: xAI client created successfully
I (15890) xai_example: === Response ===
I (15895) xai_example: The ESP32 is a powerful microcontroller with built-in WiFi and Bluetooth capabilities, making it perfect for IoT projects!
I (15900) xai_example: ================
I (15910) xai_example: xAI client destroyed
I (15915) xai_example: Example complete. Restarting in 10 seconds...
```

## Troubleshooting

### "Failed to create xAI client"
- Check that your API key is valid
- Ensure you're connected to WiFi before creating the client

### "Chat completion failed: Authentication failed"
- Your API key is invalid or expired
- Get a new API key from console.x.ai

### "Chat completion failed: HTTP request failed"
- Check your internet connection
- Verify DNS is working (try pinging api.x.ai)
- Check firewall settings

### Memory Issues
- Reduce `XAI_MAX_RESPONSE_SIZE` in menuconfig if needed
- Use a smaller model like `grok-3-mini-fast-latest`

## Next Steps

- Try the **streaming example** to see real-time responses
- Try the **conversation example** for multi-turn dialogues
- Try the **web_search example** for grounded responses with citations

## Code Overview

The key functions used:

```c
// Create client (simple version)
xai_client_t client = xai_create(api_key);

// Send message and get response
xai_err_t err = xai_text_completion(
    client,
    "Your prompt here",
    &response_text,
    &response_len
);

// Check for errors
if (err != XAI_OK) {
    ESP_LOGE(TAG, "Error: %s", xai_err_to_string(err));
}

// Clean up
free(response_text);
xai_destroy(client);
```

## More Information

See the main [README.md](../../README.md) for full API documentation.

