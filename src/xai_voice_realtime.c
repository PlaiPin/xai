/**
 * @file xai_voice_realtime.c
 * @brief xAI Grok Voice Realtime (WebSocket) client implementation for ESP-IDF
 */

#include "sdkconfig.h"

#ifdef CONFIG_XAI_ENABLE_VOICE_REALTIME

#include "xai_voice_realtime.h"
#include "xai_ws_assembler.h"

#include "esp_log.h"
#include "esp_websocket_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "mbedtls/base64.h"
#include "cJSON.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static const char *TAG = "xai_voice_rt";

#ifndef CONFIG_XAI_VOICE_WS_RX_BUFFER_SIZE
#define CONFIG_XAI_VOICE_WS_RX_BUFFER_SIZE 16384
#endif
#ifndef CONFIG_XAI_VOICE_WS_MAX_MESSAGE_SIZE
#define CONFIG_XAI_VOICE_WS_MAX_MESSAGE_SIZE (256 * 1024)
#endif
#ifndef CONFIG_XAI_VOICE_PCM_BUFFER_BYTES
#define CONFIG_XAI_VOICE_PCM_BUFFER_BYTES (64 * 1024)
#endif

#define XAI_VOICE_DEFAULT_URI "wss://api.x.ai/v1/realtime"

struct xai_voice_client_s {
    xai_voice_config_t cfg;
    xai_voice_callbacks_t cbs;
    void *user_ctx;

    esp_websocket_client_handle_t ws;
    bool connected;
    bool session_ready;
    bool in_turn;

    SemaphoreHandle_t mutex;

    // Buffers (SDK-owned)
    char *msg_buf;
    size_t msg_buf_cap;
    xai_ws_assembler_t assembler;

    int16_t *pcm_buf;
    size_t pcm_buf_bytes;

    // One pending text turn (optional)
    char *pending_text;
};

static void emit_state(xai_voice_client_t c, xai_voice_state_t st, const char *detail)
{
    if (c && c->cbs.on_state) {
        c->cbs.on_state(c, st, detail, c->user_ctx);
    }
}

static void lock_client(xai_voice_client_t c)
{
    if (c && c->mutex) {
        xSemaphoreTake(c->mutex, portMAX_DELAY);
    }
}

static void unlock_client(xai_voice_client_t c)
{
    if (c && c->mutex) {
        xSemaphoreGive(c->mutex);
    }
}

static void *xai_heap_malloc_prefer_psram(size_t bytes, bool prefer_psram)
{
    if (bytes == 0) return NULL;
    if (prefer_psram && heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0) {
        void *p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (p) return p;
    }
    return malloc(bytes);
}

static void xai_heap_free_any(void *p)
{
    if (!p) return;
    // heap_caps_malloc can be freed with free() in ESP-IDF, but heap_caps_free is safer.
    heap_caps_free(p);
}

static xai_err_t send_session_update_locked(xai_voice_client_t c)
{
    if (!c || !c->ws) return XAI_ERR_INVALID_ARG;

    const char *voice = c->cfg.session.voice ? c->cfg.session.voice : "Ara";
    const char *instructions = c->cfg.session.instructions ? c->cfg.session.instructions : "You are a helpful assistant.";
    int rate = c->cfg.session.sample_rate_hz > 0 ? c->cfg.session.sample_rate_hz : 24000;

    char msg[768];
    if (c->cfg.session.server_vad) {
        snprintf(msg, sizeof(msg),
                 "{"
                 "\"type\":\"session.update\","
                 "\"session\":{"
                 "\"voice\":\"%s\","
                 "\"instructions\":\"%s\","
                 "\"turn_detection\":{\"type\":\"server_vad\"},"
                 "\"audio\":{"
                 "\"input\":{\"format\":{\"type\":\"audio/pcm\",\"rate\":%d}},"
                 "\"output\":{\"format\":{\"type\":\"audio/pcm\",\"rate\":%d}}"
                 "}"
                 "}"
                 "}",
                 voice, instructions, rate, rate);
    } else {
        snprintf(msg, sizeof(msg),
                 "{"
                 "\"type\":\"session.update\","
                 "\"session\":{"
                 "\"voice\":\"%s\","
                 "\"instructions\":\"%s\","
                 "\"turn_detection\":null,"
                 "\"audio\":{"
                 "\"input\":{\"format\":{\"type\":\"audio/pcm\",\"rate\":%d}},"
                 "\"output\":{\"format\":{\"type\":\"audio/pcm\",\"rate\":%d}}"
                 "}"
                 "}"
                 "}",
                 voice, instructions, rate, rate);
    }

    int ret = esp_websocket_client_send_text(c->ws, msg, (int)strlen(msg), portMAX_DELAY);
    if (ret < 0) {
        return XAI_ERR_WS_FAILED;
    }
    return XAI_OK;
}

static xai_err_t send_text_turn_locked(xai_voice_client_t c, const char *text)
{
    if (!c || !c->ws || !text) return XAI_ERR_INVALID_ARG;
    if (!c->connected) return XAI_ERR_NOT_READY;
    if (!c->session_ready) return XAI_ERR_NOT_READY;
    if (c->in_turn) return XAI_ERR_BUSY;

    // conversation.item.create
    // NOTE: keep within a bounded buffer; escape quotes minimally (replace " with ')
    char safe_text[384];
    size_t j = 0;
    for (size_t i = 0; text[i] && j + 1 < sizeof(safe_text); i++) {
        char ch = text[i];
        if (ch == '\"') ch = '\'';
        safe_text[j++] = ch;
    }
    safe_text[j] = '\0';

    char msg[640];
    snprintf(msg, sizeof(msg),
             "{"
             "\"type\":\"conversation.item.create\","
             "\"item\":{"
             "\"type\":\"message\","
             "\"role\":\"user\","
             "\"content\":[{\"type\":\"input_text\",\"text\":\"%s\"}]"
             "}"
             "}",
             safe_text);

    int ret = esp_websocket_client_send_text(c->ws, msg, (int)strlen(msg), portMAX_DELAY);
    if (ret < 0) {
        return XAI_ERR_WS_FAILED;
    }

    // response.create
    const char *resp = "{\"type\":\"response.create\"}";
    ret = esp_websocket_client_send_text(c->ws, resp, (int)strlen(resp), portMAX_DELAY);
    if (ret < 0) {
        return XAI_ERR_WS_FAILED;
    }

    c->in_turn = true;
    return XAI_OK;
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

xai_voice_client_t xai_voice_client_create(const xai_voice_config_t *cfg,
                                          const xai_voice_callbacks_t *cbs,
                                          void *user_ctx)
{
    if (!cfg || !cfg->api_key || !cfg->api_key[0]) {
        return NULL;
    }

    xai_voice_client_t c = (xai_voice_client_t)calloc(1, sizeof(*c));
    if (!c) return NULL;

    c->cfg = *cfg;
    if (cbs) {
        c->cbs = *cbs;
    } else {
        memset(&c->cbs, 0, sizeof(c->cbs));
    }
    c->user_ctx = user_ctx;
    c->mutex = xSemaphoreCreateMutex();
    if (!c->mutex) {
        free(c);
        return NULL;
    }

    // Defaults
    if (!c->cfg.uri) c->cfg.uri = XAI_VOICE_DEFAULT_URI;
    if (c->cfg.ws_rx_buffer_size <= 0) c->cfg.ws_rx_buffer_size = CONFIG_XAI_VOICE_WS_RX_BUFFER_SIZE;
    if (c->cfg.max_message_size == 0) c->cfg.max_message_size = CONFIG_XAI_VOICE_WS_MAX_MESSAGE_SIZE;
    if (c->cfg.pcm_buffer_bytes == 0) c->cfg.pcm_buffer_bytes = CONFIG_XAI_VOICE_PCM_BUFFER_BYTES;
    if (c->cfg.network_timeout_ms <= 0) c->cfg.network_timeout_ms = 60000;
    if (c->cfg.reconnect_timeout_ms <= 0) c->cfg.reconnect_timeout_ms = 15000;

    c->msg_buf_cap = c->cfg.max_message_size;
    c->msg_buf = (char *)xai_heap_malloc_prefer_psram(c->msg_buf_cap, c->cfg.prefer_psram);
    if (!c->msg_buf) {
        vSemaphoreDelete(c->mutex);
        free(c);
        return NULL;
    }
    xai_ws_assembler_init(&c->assembler, c->msg_buf, c->msg_buf_cap);

    c->pcm_buf_bytes = c->cfg.pcm_buffer_bytes;
    c->pcm_buf = (int16_t *)xai_heap_malloc_prefer_psram(c->pcm_buf_bytes, c->cfg.prefer_psram);
    if (!c->pcm_buf) {
        xai_heap_free_any(c->msg_buf);
        vSemaphoreDelete(c->mutex);
        free(c);
        return NULL;
    }

    return c;
}

void xai_voice_client_destroy(xai_voice_client_t client)
{
    if (!client) return;
    xai_voice_client_disconnect(client);

    if (client->pending_text) {
        free(client->pending_text);
        client->pending_text = NULL;
    }

    if (client->pcm_buf) {
        xai_heap_free_any(client->pcm_buf);
        client->pcm_buf = NULL;
    }
    if (client->msg_buf) {
        xai_heap_free_any(client->msg_buf);
        client->msg_buf = NULL;
    }
    if (client->mutex) {
        vSemaphoreDelete(client->mutex);
        client->mutex = NULL;
    }
    free(client);
}

xai_err_t xai_voice_client_connect(xai_voice_client_t client)
{
    if (!client) return XAI_ERR_INVALID_ARG;

    lock_client(client);
    if (client->ws) {
        unlock_client(client);
        return XAI_OK;
    }
    emit_state(client, XAI_VOICE_STATE_CONNECTING, NULL);

    char auth_header[1024];
    snprintf(auth_header, sizeof(auth_header),
             "Authorization: Bearer %s\r\n"
             "Content-Type: application/json\r\n",
             client->cfg.api_key);

    esp_websocket_client_config_t websocket_cfg = {
        .uri = client->cfg.uri,
        .headers = auth_header,
        .buffer_size = client->cfg.ws_rx_buffer_size,
        .cert_pem = NULL,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .network_timeout_ms = client->cfg.network_timeout_ms,
        .reconnect_timeout_ms = client->cfg.reconnect_timeout_ms,
    };

    client->ws = esp_websocket_client_init(&websocket_cfg);
    if (!client->ws) {
        unlock_client(client);
        emit_state(client, XAI_VOICE_STATE_ERROR, "ws init failed");
        return XAI_ERR_WS_FAILED;
    }

    esp_websocket_register_events(client->ws, WEBSOCKET_EVENT_ANY, websocket_event_handler, client);
    esp_websocket_client_start(client->ws);
    unlock_client(client);
    return XAI_OK;
}

xai_err_t xai_voice_client_disconnect(xai_voice_client_t client)
{
    if (!client) return XAI_ERR_INVALID_ARG;
    lock_client(client);
    if (!client->ws) {
        unlock_client(client);
        return XAI_OK;
    }
    esp_websocket_client_stop(client->ws);
    esp_websocket_client_destroy(client->ws);
    client->ws = NULL;
    client->connected = false;
    client->session_ready = false;
    client->in_turn = false;
    xai_ws_assembler_reset(&client->assembler);
    unlock_client(client);
    emit_state(client, XAI_VOICE_STATE_DISCONNECTED, NULL);
    return XAI_OK;
}

bool xai_voice_client_is_connected(xai_voice_client_t client)
{
    if (!client) return false;
    lock_client(client);
    bool v = client->connected && client->ws != NULL;
    unlock_client(client);
    return v;
}

bool xai_voice_client_is_ready(xai_voice_client_t client)
{
    if (!client) return false;
    lock_client(client);
    bool v = client->session_ready;
    unlock_client(client);
    return v;
}

xai_err_t xai_voice_client_send_text_turn(xai_voice_client_t client, const char *text)
{
    if (!client || !text) return XAI_ERR_INVALID_ARG;
    lock_client(client);
    if (!client->session_ready) {
        if (client->cfg.queue_turn_before_ready) {
            free(client->pending_text);
            client->pending_text = strdup(text);
            unlock_client(client);
            return XAI_OK;
        }
        unlock_client(client);
        return XAI_ERR_NOT_READY;
    }
    xai_err_t err = send_text_turn_locked(client, text);
    unlock_client(client);
    return err;
}

static void handle_json_message(xai_voice_client_t client, const char *json, size_t len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        emit_state(client, XAI_VOICE_STATE_ERROR, "json parse failed");
        return;
    }

    cJSON *type = cJSON_GetObjectItem(root, "type");
    if (!type || !type->valuestring) {
        cJSON_Delete(root);
        return;
    }
    const char *event_type = type->valuestring;

    if (client->cbs.on_event_json) {
        client->cbs.on_event_json(client, event_type, json, len, client->user_ctx);
    }

    if (strcmp(event_type, "session.updated") == 0) {
        lock_client(client);
        client->session_ready = true;
        unlock_client(client);
        emit_state(client, XAI_VOICE_STATE_SESSION_READY, NULL);

        // Send queued turn if any
        lock_client(client);
        if (client->pending_text) {
            char *tmp = client->pending_text;
            client->pending_text = NULL;
            xai_err_t err = send_text_turn_locked(client, tmp);
            (void)err;
            free(tmp);
        }
        unlock_client(client);

    } else if (strcmp(event_type, "response.created") == 0) {
        emit_state(client, XAI_VOICE_STATE_TURN_STARTED, NULL);

    } else if (strcmp(event_type, "response.done") == 0) {
        lock_client(client);
        client->in_turn = false;
        unlock_client(client);
        emit_state(client, XAI_VOICE_STATE_TURN_DONE, NULL);

    } else if (strcmp(event_type, "response.output_audio_transcript.delta") == 0) {
        cJSON *delta = cJSON_GetObjectItem(root, "delta");
        if (delta && cJSON_IsString(delta) && client->cbs.on_transcript_delta) {
            const char *s = delta->valuestring;
            client->cbs.on_transcript_delta(client, s, strlen(s), client->user_ctx);
        }

    } else if (strcmp(event_type, "response.output_audio.delta") == 0) {
        cJSON *delta = cJSON_GetObjectItem(root, "delta");
        if (delta && cJSON_IsString(delta) && client->cbs.on_pcm16) {
            const char *b64 = delta->valuestring;
            size_t b64_len = strlen(b64);
            size_t out_len = 0;
            int ret = mbedtls_base64_decode((unsigned char *)client->pcm_buf,
                                            client->pcm_buf_bytes,
                                            &out_len,
                                            (const unsigned char *)b64,
                                            b64_len);
            if (ret != 0) {
                emit_state(client, XAI_VOICE_STATE_ERROR, "base64 decode failed");
                cJSON_Delete(root);
                return;
            }
            if (out_len % 2 != 0) {
                emit_state(client, XAI_VOICE_STATE_ERROR, "pcm16 odd bytecount");
                cJSON_Delete(root);
                return;
            }
            size_t samples = out_len / 2;
            int rate = client->cfg.session.sample_rate_hz > 0 ? client->cfg.session.sample_rate_hz : 24000;
            client->cbs.on_pcm16(client, client->pcm_buf, samples, rate, client->user_ctx);
        }
    }

    cJSON_Delete(root);
}

static void websocket_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;

    xai_voice_client_t client = (xai_voice_client_t)handler_args;
    esp_websocket_event_data_t *data = (esp_websocket_event_data_t *)event_data;

    if (!client) return;

    switch (event_id) {
    case WEBSOCKET_EVENT_CONNECTED:
        lock_client(client);
        client->connected = true;
        client->session_ready = false;
        client->in_turn = false;
        xai_ws_assembler_reset(&client->assembler);
        unlock_client(client);
        emit_state(client, XAI_VOICE_STATE_CONNECTED, NULL);

        lock_client(client);
        xai_err_t err = send_session_update_locked(client);
        unlock_client(client);
        if (err != XAI_OK) {
            emit_state(client, XAI_VOICE_STATE_ERROR, "session.update send failed");
        }
        break;

    case WEBSOCKET_EVENT_DISCONNECTED:
        lock_client(client);
        client->connected = false;
        client->session_ready = false;
        client->in_turn = false;
        xai_ws_assembler_reset(&client->assembler);
        unlock_client(client);
        emit_state(client, XAI_VOICE_STATE_DISCONNECTED, NULL);
        break;

    case WEBSOCKET_EVENT_ERROR:
        emit_state(client, XAI_VOICE_STATE_ERROR, "websocket error");
        break;

    case WEBSOCKET_EVENT_DATA:
        if (!data || data->data_len <= 0) break;

        // Only parse JSON from TEXT frames (opcode 0x1). Ignore control frames (PING/PONG/CLOSE).
        if (data->op_code != 0x01) {
            break;
        }

        bool complete = xai_ws_assembler_feed_text(&client->assembler,
                                                   data->payload_len,
                                                   data->payload_offset,
                                                   data->data_ptr,
                                                   data->data_len,
                                                   data->fin);
        if (complete) {
            handle_json_message(client, client->assembler.buf, client->assembler.payload_len);
        }
        break;
    default:
        break;
    }
}

#else
// If disabled, provide stubs to keep linker happy when header is included.
xai_voice_client_t xai_voice_client_create(const xai_voice_config_t *cfg,
                                          const xai_voice_callbacks_t *cbs,
                                          void *user_ctx)
{
    (void)cfg; (void)cbs; (void)user_ctx;
    return NULL;
}
void xai_voice_client_destroy(xai_voice_client_t client) { (void)client; }
xai_err_t xai_voice_client_connect(xai_voice_client_t client) { (void)client; return XAI_ERR_NOT_SUPPORTED; }
xai_err_t xai_voice_client_disconnect(xai_voice_client_t client) { (void)client; return XAI_ERR_NOT_SUPPORTED; }
bool xai_voice_client_is_connected(xai_voice_client_t client) { (void)client; return false; }
bool xai_voice_client_is_ready(xai_voice_client_t client) { (void)client; return false; }
xai_err_t xai_voice_client_send_text_turn(xai_voice_client_t client, const char *text)
{
    (void)client; (void)text;
    return XAI_ERR_NOT_SUPPORTED;
}
#endif // CONFIG_XAI_ENABLE_VOICE_REALTIME


