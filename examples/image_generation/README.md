# Image Generation Example

Demonstrates AI-powered text-to-image generation with xAI Grok.

## Features

- Text-to-image generation with grok-2-image model
- Generate multiple image variations (up to 10)
- URL and Base64 response formats
- Automatic prompt revision and enhancement
- Simple API with minimal parameters

## Important Notes

⚠️ **xAI's image API does NOT support:**
- Custom image sizes (`size` parameter)
- Quality settings (`quality` parameter)  
- Style preferences (`style` parameter)

The grok-2-image model generates images at a fixed resolution and automatically optimizes quality.

## Configuration

Update WiFi and API key in `image_gen_example.c`.

## Build

```bash
cd examples/image_generation
idf.py build flash monitor
```

## What You'll Learn

- Using `xai_generate_image()` for text-to-image
- Generating multiple image variations
- Handling URL vs Base64 responses
- Working with automatic prompt revision

## Key API Calls

```c
// Create request (only 4 parameters matter!)
xai_image_request_t request = {
    .prompt = "A futuristic ESP32 board with RGB LEDs",
    .model = NULL,  // Use default (grok-2-image-latest)
    .n = 1,         // Number of images (1-10)
    .response_format = "url",  // "url" or "b64_json"
    // size, quality, style, user_id are NOT supported
};

// Generate
xai_image_response_t response;
xai_generate_image(client, &request, &response);

// Access URLs or Base64
for (size_t i = 0; i < response.image_count; i++) {
    if (response.images[i].url) {
        printf("URL: %s\n", response.images[i].url);
    } else if (response.images[i].b64_json) {
        // Decode and save to flash/SD
    }
    
    // xAI revises prompts to improve quality
    if (response.images[i].revised_prompt) {
        printf("Revised: %s\n", response.images[i].revised_prompt);
    }
}

xai_image_response_free(&response);
```

## Response Formats

- **URL**: Image hosted by xAI (temporary) - **Recommended for ESP32**
- **Base64**: Raw image data for local storage - **Currently disabled due to buffer size (17KB+ exceeds 16KB limit)**

## Memory Considerations

- Base64 responses are ~17KB+ (exceeds default 16KB HTTP buffer)
- Example 4 (base64) is commented out - reference only
- URL format requires minimal memory (~1KB)
- Ensure sufficient heap (12KB+ stack for this example)
- Image resolution is fixed by the model (not configurable)

## Practical Usage on ESP32

For displaying images on ESP32:
- **Generate URL**: Use `response_format: "url"` (default)
- **Send to dashboard**: Forward URL via MQTT/HTTP to mobile/web app
- **QR code**: Encode URL as QR code for scanning
- **Server processing**: Have your backend download, resize, and serve processed image

Base64 format is impractical for ESP32 due to:
- Buffer size limitations (17KB+ response)
- Cannot directly display without extensive decoding
- Limited utility for embedded applications

## Related Examples

- `vision` - Analyze generated images
- `web_search` - Generate based on search results

