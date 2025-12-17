# Voice Realtime (Headless) - WebSocket Audio Playback

This example demonstrates the **xAI Grok Voice API** using WebSocket for real-time voice interaction:

1. **Connect** to `wss://api.x.ai/v1/realtime` via WebSocket
2. **Send** text message to Grok
3. **Receive** PCM audio response in real-time (base64-encoded)
4. **Decode** and play through I2S speaker

## Features

- **WebSocket-based** realtime voice streaming
- **Direct PCM audio** - no MP3 decoding needed!
- **I2S audio output** - play through speaker/DAC
- **16kHz sample rate** - optimized for bandwidth and quality
- **Mono audio** - efficient I2S configuration for single-channel playback
- **Simple architecture** - WebSocket → Base64 Decode → I2S
- **Message fragmentation handling** - robust parsing of multi-frame WebSocket messages
- **Comprehensive logging** - detailed event tracking for debugging
- **Direct ES8311 initialization** - uses Waveshare's proven codec configuration

## Hardware Requirements

| Component | Required | Notes |
|-----------|----------|-------|
| Waveshare ESP32-S3-Touch-AMOLED-1.75 | Yes | Specific board required |
| ES8311 Audio Codec | Yes | Built-in on Waveshare board |
| WiFi | Yes | Internet connection required |

### I2S Audio Connections

**Waveshare ESP32-S3-Touch-AMOLED-1.75 (ES8311 Codec):**

This example is specifically configured for the Waveshare board which includes:
- **ES8311 DAC** (Digital-to-Analog Converter) for speaker output
- **ES7210 ADC** (Analog-to-Digital Converter) for microphone input (not used in this example)
- **Built-in speaker amplifier** with power control

**GPIO Configuration:**
```
Audio Function     ESP32-S3 GPIO
--------------     -------------
MCLK (Master)   -> GPIO 42 (required for ES8311)
BCK (Bit Clock) -> GPIO 9
WS (Word Select)-> GPIO 45
DOUT (Data Out) -> GPIO 8
I2C SDA         -> GPIO 15 (codec control)
I2C SCL         -> GPIO 14 (codec control)
PA Enable       -> GPIO 46 (power amplifier)
```

**⚠️ Important Notes:**
- This example is **NOT** compatible with simple I2S DACs (MAX98357A, PCM5102, etc.)
- The ES8311 codec requires I2C initialization before I2S will work
- Do not connect external speakers - the board has a built-in speaker
- For other boards, you'll need to modify the pin definitions and codec initialization

## Architecture

```
┌─────────────┐     WebSocket      ┌──────────┐
│  Grok Voice │ ◄──────────────────► │  ESP32   │
│     API     │   Base64 PCM Audio  │          │
└─────────────┘                     └────┬─────┘
                                         │ I2S
                                         ▼
                                    ┌─────────┐
                                    │ Speaker │
                                    └─────────┘
```

## Audio Format

- **Format**: PCM 16-bit, little-endian
- **Sample Rate**: 16,000 Hz (16kHz) - optimized for WebSocket bandwidth
- **Channels**: Mono (1 channel)
- **Encoding**: Base64 over WebSocket
- **Decoding**: mbedTLS base64 decoder (built-in)
- **Codec**: ES8311 (I2C-controlled DAC)

## Configuration (headless demo)

**Edit the credentials directly in `main/voice_demo_headless.c`:**

### 1. WiFi Credentials

```c
#define WIFI_SSID      "your_wifi_ssid"      // Change this
#define WIFI_PASSWORD  "your_wifi_password"  // Change this
```

### 2. xAI API Key

Get your API key from [console.x.ai](https://console.x.ai):

```c
#define XAI_API_KEY    "your_xai_api_key_here"  // Change this
```

### 3. I2S GPIO Pins (Pre-configured)

Pins are **pre-configured** for the **Waveshare ESP32-S3-Touch-AMOLED-1.75**:

```c
// I2S pins (do not change - board-specific)
#define I2S_MCLK_IO     (GPIO_NUM_42)  // Master clock (ES8311 required)
#define I2S_BCK_IO      (GPIO_NUM_9)   // Bit clock
#define I2S_WS_IO       (GPIO_NUM_45)  // Word select (LRCK)
#define I2S_DO_IO       (GPIO_NUM_8)   // Data out

// ES8311 codec control pins (do not change)
#define I2C_SDA_IO      (GPIO_NUM_15)  // I2C data
#define I2C_SCL_IO      (GPIO_NUM_14)  // I2C clock
#define PA_ENABLE_GPIO  (GPIO_NUM_46)  // Power amplifier enable
```

**⚠️ Do not modify these pins unless using a different board!**

## Building and Flashing

```bash
cd examples/voice_realtime_lvgl

# Set target to ESP32-S3 (only needed once)
idf.py set-target esp32s3

# Build
idf.py build

# Flash to ESP32-S3
idf.py -p /dev/ttyUSB0 flash monitor
```

## Selecting the entrypoint (UI vs headless)

By default this example builds the LVGL UI app (`main/main.c`).
To build the headless demo instead, edit `main/CMakeLists.txt` and swap:

- build `voice_demo_headless.c`
- do not build `main.c`

## How It Works

### 1. WebSocket Connection

The example connects to `wss://api.x.ai/v1/realtime` using the ESP-IDF WebSocket client with your API key for authentication.

### 2. Session Configuration

On connection, it sends a `session.update` message to configure:
- Voice: "Ara" (female, warm, friendly)
- Audio format: PCM, 16kHz (optimized for bandwidth)
- Turn detection: Server VAD (automatic)

```json
{
  "type": "session.update",
  "session": {
    "voice": "Ara",
    "audio": {
      "input": {
        "format": {"type": "audio/pcm", "rate": 16000}
      },
      "output": {
        "format": {"type": "audio/pcm", "rate": 16000}
      }
    },
    "turn_detection": {
      "type": "server_vad"
    }
  }
}
```

### 3. Sending Messages

The example sends a text message asking Grok to tell a joke:

```json
{
  "type": "conversation.item.create",
  "item": {
    "type": "message",
    "role": "user",
    "content": [{"type": "input_text", "text": "Hello! Tell me a short joke."}]
  }
}
```

### 4. Receiving Audio

Grok responds with `response.output_audio.delta` events containing base64-encoded PCM audio:

```json
{
  "type": "response.output_audio.delta",
  "delta": "AQIDBAUG..."  // Base64-encoded PCM16 samples
}
```

The example:
1. Decodes base64 to PCM16 samples
2. Writes samples directly to I2S
3. Speaker plays the audio in real-time

### 5. Event Flow

```
Client                          Server
  │                               │
  ├──── session.update ──────────>│
  │<──── session.updated ─────────┤
  │                               │
  ├──── conversation.item.create >│
  ├──── response.create ─────────>│
  │                               │
  │<──── response.created ────────┤
  │<──── response.output_audio.delta ──┤ (Base64 PCM audio chunk 1)
  │<──── response.output_audio.delta ──┤ (Base64 PCM audio chunk 2)
  │<──── response.output_audio.delta ──┤ (Base64 PCM audio chunk 3)
  │<──── ... ─────────────────────┤
  │<──── response.output_audio.done ───┤
  │<──── response.done ────────────┤
```

## Memory Usage

**RAM Usage (approximate):**
- WiFi + TCP/IP: ~40KB
- WebSocket client: ~10KB
- I2S driver: ~16KB (DMA buffers)
- Audio decode buffer: ~8KB
- **Total**: ~75KB RAM

**Flash Usage:**
- WebSocket client: ~50KB
- I2S driver: ~30KB
- mbedTLS (base64): ~5KB
- Example code: ~20KB
- **Total**: ~105KB flash

**Much more efficient than MP3 decoding!**

## Extending This Example

### Add Microphone Input

To create a full voice conversation loop, add I2S input:

```c
// Initialize I2S RX for microphone
i2s_channel_init_std_mode(rx_handle, &rx_std_cfg);

// Read audio from mic
i2s_channel_read(rx_handle, mic_buffer, buffer_size, &bytes_read, portMAX_DELAY);

// Encode to base64
mbedtls_base64_encode(base64_out, base64_len, &olen, mic_buffer, bytes_read);

// Send to Grok
char audio_msg[4096];
snprintf(audio_msg, sizeof(audio_msg), 
         "{\"type\":\"input_audio_buffer.append\",\"audio\":\"%s\"}", base64_out);
esp_websocket_client_send_text(client, audio_msg, strlen(audio_msg), portMAX_DELAY);
```

### Change Voice

Available voices: `Ara`, `Rex`, `Sal`, `Eve`, `Una`, `Leo`

Change in session config:
```c
"\"voice\":\"Rex\","  // Male, confident, clear
```

### Add Web Search

Enable web search in session config:
```json
{
  "tools": [
    {"type": "web_search"}
  ]
}
```

## Troubleshooting

### No Audio Output

**First, check the logs for these indicators:**

**Good signs** (should see these):
```
Event: session.updated
Session accepted config: {...}
Event: response.created
🔊 Received audio delta (base64 len: ...)
✓ Played X PCM samples (Y bytes written)
✓ Response complete!
```

**Bad signs** (should NOT see these):
```
Failed to parse JSON
Audio delta missing 'delta' field
I2S write failed
WebSocket buffer overflow
```

**Hardware checks for Waveshare ESP32-S3-Touch-AMOLED-1.75:**
1. **Board verification**: Ensure you're using the correct Waveshare board
2. **Built-in speaker**: The board has an integrated speaker - do not connect external speakers
3. **Power amplifier**: PA should auto-enable via GPIO 46
4. **ES8311 codec**: Initialized via I2C (GPIO 15/14)
5. **I2S pins**: Pre-configured for this board (MCLK=42, BCK=9, WS=45, DO=8)

** Common Issues:**
- **Using wrong board**: This example only works with the Waveshare AMOLED board
- **Simple I2S DAC won't work**: The code requires ES8311 codec initialization
- **No external speaker needed**: Use the built-in speaker only

**Software checks:**
1. Look for `response.output_audio_delta` events (not just transcript)
2. Check if base64 decoding succeeds (should show sample count)
3. Verify I2S channel is enabled (no errors during `i2s_init()`)
4. Ensure WiFi signal is strong (rssi > -70 dBm)

### WebSocket Connection Failed

**TLS/Certificate Issues:**
```
Error: esp-tls-mbedtls: No server verification option set
```
→ Make sure `esp_crt_bundle_attach` is configured (already done)

**Authentication Issues:**
```
esp_ws_handshake_status_code=403
```
→ Check API key is correct and active

**Timeout Issues:**
```
ESP_ERR_ESP_TLS_CONNECTION_TIMEOUT
```
→ Weak WiFi signal or slow internet. Move closer to router.

**General checks:**
1. Verify WiFi connection (check IP address is assigned)
2. Test API key with curl: `curl -H "Authorization: Bearer $KEY" https://api.x.ai/v1/chat/completions`
3. Ensure internet connectivity (ping 8.8.8.8)
4. Check firewall/proxy settings

### Getting Transcripts but No Audio

**This issue is now fully resolved!** If you still don't hear audio:

1. **Check logs for these success indicators:**
   ```
   I (1712) voice_demo: ES8311 codec initialized
   I (1712) voice_demo: I2S initialized and enabled - MONO, 16000 Hz
   I (6242) voice_demo: 🔊 Received audio delta (base64 len: 23896)
   I (6512) voice_demo: ✓ Played 8960 mono samples (17920 bytes written)
   ```

2. **Verify hardware:**
   - Confirm you're using the **Waveshare ESP32-S3-Touch-AMOLED-1.75** board
   - The built-in speaker should be working (test with the Waveshare `06_I2SCodec` example first)
   - PA enable GPIO 46 is correctly connected

3. **If audio is garbled/static:**
   - This was the symptom before the mono I2S fix
   - Make sure you're running the latest code (post Dec 9, 2025)
   - Check that `I2S_SLOT_MODE_MONO` is set (not STEREO)

### Buffer Overflow Warnings (Non-Critical)

**You may see warnings like:**
```
W (9002) voice_demo: Buffer overflow at 47280 bytes! Discarding.
```

**This is normal and acceptable:**
- The WebSocket reassembly buffer is 48KB (48,000 bytes)
- Most audio deltas are 24KB-41KB and fit fine
- Occasionally xAI sends larger chunks (47KB-49KB) that exceed the buffer
- These oversized messages are discarded, causing a brief audio skip (~0.3 seconds)
- **No crash** - system continues streaming normally

**Impact**: Minor audio skips every 30-60 seconds. Still very usable!

**To eliminate warnings (optional):**
- Increase `max_message_size` / `pcm_buffer_bytes` in the xAI SDK voice realtime config used by this demo.
- Trade-off: Uses more RAM (often PSRAM).

### Audio Glitches or Cutouts

1. **Increase DMA buffers:** Change `I2S_DMA_BUF_COUNT` to 8-12, `I2S_DMA_BUF_LEN` to 2048
2. **Reduce WiFi noise:** Disable other network tasks during audio playback
3. **Task priority:** Increase audio task priority to 5 or higher
4. **WiFi signal:** Move closer to router (weak signal causes packet loss)
5. **Sample rate mismatch:** Ensure both input and output audio configs use 16000 Hz

### Build Errors

1. Make sure ESP-IDF 5.x is installed: `idf.py --version`
2. Clean build: `rm -rf build sdkconfig && idf.py build`
3. Check all dependencies: `idf.py reconfigure`
4. Verify target: `idf.py set-target esp32s3`

### Binary Size Warning

```
Warning: The smallest app partition is nearly full (4% free space left)!
```
→ This is normal for this example. If you need more space, create a custom partition table with larger app partition.

## Next Steps

- See `voice_assistant_basic` for full duplex voice conversation
- See `../README.md` for other xAI SDK examples
- Read the [Grok Voice API docs](https://docs.x.ai/docs) for advanced features

## License

Apache 2.0
