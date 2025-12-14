# Base source files
set(COMPONENT_SRCS
    "src/xai.c"
         "src/xai_http.c"
         "src/xai_json.c"
         "src/xai_chat.c"
         "src/xai_stream.c"
         "src/xai_search.c"
         "src/xai_conversation.c"
         "src/xai_models.c"
         "src/xai_tokenize.c"
         "src/xai_images.c"
         "src/xai_responses.c"
         "src/xai_error.c"
)

# Optional: Grok Voice Realtime (WebSocket)
if(CONFIG_XAI_ENABLE_VOICE_REALTIME)
    list(APPEND COMPONENT_SRCS
        "src/xai_ws_assembler.c"
        "src/xai_voice_realtime.c"
    )
endif()

# Component registration
# - INCLUDE_DIRS: Public headers (include/xai.h) exposed to users
# - PRIV_INCLUDE_DIRS: Private headers (private_include/xai_internal.h) for internal use only
set(XAI_REQUIRES
    esp_http_client
    json
    esp-tls
    mbedtls
    freertos
)

if(CONFIG_XAI_ENABLE_VOICE_REALTIME)
    list(APPEND XAI_REQUIRES
        esp_event
        esp_crt_bundle
        espressif__esp_websocket_client
        espressif__cjson
    )
endif()

idf_component_register(
    SRCS ${COMPONENT_SRCS}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES ${XAI_REQUIRES}
)
