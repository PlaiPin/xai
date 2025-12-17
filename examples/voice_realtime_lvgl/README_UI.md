# xAI Grok Voice Demo with LVGL UI

A modular implementation of the xAI Grok Voice API with a touch-enabled LVGL user interface for the Waveshare ESP32-S3-Touch-AMOLED-1.75 board.

## Features

**Touch UI** - Beautiful AMOLED display (466x466) with capacitive touch  
**Voice AI** - Real-time conversation with xAI Grok using WebSocket  
**Audio Playback** - 16kHz mono PCM audio through ES8311 codec  
**Modular Architecture** - Clean separation of concerns for maintainability  
**Thread-Safe** - Proper mutex usage for multi-threaded operation  
**PSRAM Optimized** - Large buffers allocated in external PSRAM  

## Hardware

**Board:** Waveshare ESP32-S3-Touch-AMOLED-1.75

**Components:**
- **MCU:** ESP32-S3 with PSRAM
- **Display:** SH8601 466x466 QSPI AMOLED (round)
- **Touch:** CST92xx capacitive touch controller
- **Audio:** ES8311 codec with on-board speaker
- **Interfaces:** I2C (shared), I2S, SPI/QSPI

## Architecture

### File Structure

```
main/
├── main.c                    # Main application entry point (LVGL UI)
├── config/
│   └── app_config.h          # Centralized configuration
├── network/
│   ├── wifi_manager.c/h      # WiFi connection management
├── audio/
│   ├── audio_init.c/h        # ES8311 + I2S initialization
│   └── audio_playback.c/h    # PCM playback via I2S
└── ui/
    ├── ui_init.c/h           # LVGL + display + touch init
    ├── ui_screens.c/h        # Screen layouts
    └── ui_events.c/h         # Event handlers & coordination

components/
├── SensorLib/                # Touch controller driver
└── esp_lcd_sh8601/           # Display driver
```

### Module Overview

#### **config/app_config.h**
Centralized configuration for all hardware pins, WiFi credentials, API keys, and system parameters.

#### **network/**
- **wifi_manager**: Manages WiFi connection lifecycle (connect, status, disconnect)
- **xAI voice realtime SDK (`xai_voice_realtime`)**: Handles WebSocket + message reassembly + JSON parsing + base64→PCM16 decode
  - Device-agnostic and reusable across projects
  - Emits decoded PCM16 and transcript deltas via callbacks (consumed by `ui_events.cpp`)

#### **audio/**
- **audio_init**: Initializes ES8311 codec and I2S driver
  - Shared I2C bus with touch controller
  - Mono mode, 16kHz sample rate
  - Power amplifier control
- **audio_playback**: PCM16 playback through I2S DMA

#### **ui/**
- **ui_init**: Initializes display, touch, and LVGL
  - QSPI display setup (SH8601)
  - Touch controller (CST92xx on shared I2C)
  - LVGL task with mutex for thread safety
- **ui_screens**: Creates UI layouts
  - Main screen with status label, button, transcript
  - Button states: Ready, Connecting, Speaking, Error
  - Pulsing animation during speech
- **ui_events**: Coordinates UI with network/audio
  - Thread-safe callbacks from WebSocket
  - Audio decode and playback
  - UI updates with proper locking

### Data Flow

```
User taps button
    ↓
ui_on_button_clicked()
    ↓
xai_voice_client_send_text_turn(...)
    ↓
SDK WebSocket sends to xAI API
    ↓
Server responds with audio deltas
    ↓
SDK decodes base64 → PCM16
    ↓
audio_play_pcm() → I2S DMA → ES8311 → Speaker
    ↓
UI updates: "Speaking..." with pulsing button
    ↓
Server sends "response.done"
    ↓
ui_on_websocket_status("done")
    ↓
Button returns to "Ready" state
```

### Threading Model

| Thread | Purpose | Priority |
|--------|---------|----------|
| **Main Task** | Initialization, then monitoring | Default |
| **LVGL Task** | UI rendering + touch input | 2 |
| **WebSocket Task** | Network I/O (internal to esp-ws-client) | Managed by ESP-IDF |
| **WiFi Task** | Connection management (internal) | Managed by ESP-IDF |
| **I2S DMA** | Audio playback (hardware interrupts) | Hardware |

**Critical:** All LVGL API calls from non-LVGL tasks MUST be wrapped with `ui_lock()` / `ui_unlock()`.

### Memory Management

| Resource | Size | Location | Purpose |
|----------|------|----------|---------|
| WebSocket buffer | 128 KB | PSRAM | Message reassembly |
| PCM decode buffer | 102 KB | PSRAM | Audio decoding |
| LVGL draw buffer 1 | ~55 KB | DMA RAM | Display rendering |
| LVGL draw buffer 2 | ~55 KB | DMA RAM | Double buffering |
| Stack (LVGL task) | 4 KB | Internal | Task stack |

**Total PSRAM:** ~230 KB  
**Total Internal RAM:** ~120 KB (including TLS/WiFi)

## Configuration

Edit `main/config/app_config.h`:

```c
// WiFi
#define WIFI_SSID      "your-wifi-ssid"
#define WIFI_PASSWORD  "your-wifi-password"

// xAI API
#define XAI_API_KEY    "xai-your-api-key-here"

// Voice settings
#define VOICE_NAME      "Ara"  // or "Echo", "Beam", "Dust"
#define VOICE_DEFAULT_PROMPT "Hello! Tell me a short joke."
```

## Building

```bash
cd /path/to/xai/examples/voice_realtime_lvgl

# Configure (if needed)
idf.py menuconfig
# Enable: Component config → ESP PSRAM → Support for external SPIRAM

# Build
idf.py build

# Flash
idf.py -p /dev/ttyUSB0 flash monitor
```

## Usage

1. **Power on** - Device boots and connects to WiFi
2. **Wait for "Ready"** - Status label shows "Ready" when initialized
3. **Tap button** - Large circular button in center of screen
4. **Listen** - Grok responds with audio, button pulses green
5. **Read transcript** - Text appears at bottom of screen
6. **Repeat** - Button returns to blue "Ready" state

### Button States

| State | Color | Label | Enabled |
|-------|-------|-------|---------|
| **Ready** | Blue | "🔊 Tap to Ask" | ✅ Yes |
| **Connecting** | Gray | "📶 Connecting..." | ❌ No |
| **Speaking** | Green (pulsing) | "🔊 Speaking..." | ❌ No |
| **Error** | Red | "⚠️ Error" | ✅ Yes |

## Troubleshooting

### "Failed to allocate buffer in PSRAM"
**Solution:** Enable PSRAM in menuconfig:
```
Component config → ESP PSRAM → Support for external SPIRAM = Yes
```

### "I2C driver install failed"
**Cause:** I2C already initialized (shared between audio and touch)  
**Solution:** This is expected and handled gracefully in code

### "Touch not working"
**Check:** GPIO connections for touch controller (RST=40, INT=11)  
**Check:** I2C address (0x5A) matches your hardware

### "No audio output"
**Check:** Power amplifier GPIO (46) is high  
**Check:** I2S pins match your board (see app_config.h)  
**Check:** Volume level (default 80%)

### "WebSocket connection failed"
**Check:** WiFi credentials in app_config.h  
**Check:** xAI API key is valid  
**Check:** Internet connectivity  
**Check:** Certificate bundle is enabled in menuconfig

## API Reference

### UI Thread Safety

```c
// ALWAYS use this pattern for UI updates from other tasks:
if (ui_lock(1000)) {  // 1 second timeout
    ui_update_status_label("New status");
    ui_set_button_state(BTN_STATE_READY);
    ui_unlock();
} else {
    ESP_LOGW(TAG, "Failed to acquire UI lock");
}
```

### Adding Custom Messages

```c
// In ui_events.cpp, modify:
void ui_on_button_clicked(void)
{
    // Change this line:
    xai_voice_client_send_text_turn(..., "Your custom prompt here");
}
```

### Changing Voice

```c
// In config/app_config.h:
#define VOICE_NAME "Echo"  // Options: Ara, Echo, Beam, Dust
```

## Extending the Demo

### Add Voice Input (Microphone)
1. Enable microphone in `audio_init.c`:
   ```c
   es8311_microphone_config(es8311_handle, true);
   ```
2. Create `audio_record.c` for I2S RX
3. Send PCM to xAI using `conversation.item.create` with audio

### Add Settings Screen
1. Create new screen in `ui_screens.c`
2. Add button for screen navigation
3. Use LVGL dropdown for voice selection
4. Use LVGL slider for volume control

### Add Conversation History
1. Create scrollable list in `ui_screens.c`
2. Store messages in linked list
3. Update list on each response

### Add Visual Feedback
1. Use LVGL arc widget for volume meter
2. Animate waveform during playback
3. Add loading spinner during connection

## Performance

- **UI Refresh Rate:** 30-60 FPS (depends on complexity)
- **Audio Latency:** <100ms (decode + DMA buffer)
- **Touch Response:** <50ms (depends on LVGL task priority)
- **WebSocket Throughput:** ~500 KB/s (limited by network)

## License

See parent LICENSE file in xai repository.

## Contributing

See CONTRIBUTING.md in parent repository.

## Credits

- xAI for Grok Voice API
- Waveshare for ESP32-S3-Touch-AMOLED-1.75 hardware examples
- LVGL project for embedded GUI library
- Espressif for ESP-IDF framework

## Support

For issues related to:
- **xAI API:** console.x.ai
- **Hardware:** Waveshare support
- **ESP-IDF:** esp32.com forums
- **This demo:** Open an issue in the repository

---

**Happy voice hacking! 🎙️✨**

