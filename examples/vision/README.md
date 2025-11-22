# Vision Example

Demonstrates image understanding with xAI Grok vision models.

## Features

- Image analysis from URLs
- Multi-image comparison
- Detailed/auto image resolution
- Multi-modal conversations

## Configuration

Update WiFi, API key, and image URLs in `vision_example.c`.

## Build

```bash
cd examples/vision
idf.py build flash monitor
```

## What You'll Learn

- Using `xai_vision_completion()` for image analysis
- Creating `xai_image_t` structures
- Multi-image messages
- Vision model selection (grok-2-vision-latest)

## Key API Calls

```c
// Single image
xai_image_t image = {
    .url = "https://example.com/image.jpg",
    .data = NULL,
    .data_len = 0,
    .detail = "auto"
};

xai_vision_completion(client, "What's in this image?", &image, 1, &response);

// Multiple images in message
xai_message_t message = {
    .role = XAI_ROLE_USER,
    .content = "Compare these images",
    .images = images,
    .image_count = 2
};
```

## Configuration

Enable vision in menuconfig:
```
Component config → xAI Configuration → Feature Toggles → Enable vision support
```

## Notes

- Requires grok-2-vision models
- Image URLs must be publicly accessible
- Base64 data encoding also supported

## Related Examples

- `conversation` - Multi-turn vision chat
- `web_search` - Vision + web grounding

