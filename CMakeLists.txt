idf_component_register(
    SRCS "src/xai.c"
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
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "private_include"
    REQUIRES esp_http_client 
             json
             esp-tls
             mbedtls
             freertos
)
