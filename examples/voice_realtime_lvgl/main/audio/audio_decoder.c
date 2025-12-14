/**
 * @file audio_decoder.c
 * @brief Base64 audio decoder implementation
 */

#include "audio_decoder.h"
#include "esp_log.h"
#include "mbedtls/base64.h"
#include <string.h>

static const char *TAG = "audio_decoder";

int audio_decode_base64(const char *base64_data, size_t base64_len, 
                        int16_t *pcm_out, size_t pcm_out_size)
{
    if (!base64_data || base64_len == 0 || !pcm_out || pcm_out_size == 0) {
        ESP_LOGE(TAG, "Invalid input parameters");
        return -1;
    }
    
    // Validate base64 length (should be multiple of 4 when properly padded)
    if (base64_len % 4 != 0) {
        ESP_LOGW(TAG, "Base64 length %zu not multiple of 4 (may need padding)", base64_len);
    }
    
    // Log first/last few chars for debugging
    ESP_LOGD(TAG, "Decoding base64: first='%.20s...', last='...%.20s'", 
             base64_data, 
             base64_len > 20 ? base64_data + base64_len - 20 : base64_data);
    
    size_t decoded_len = 0;
    unsigned char *decoded_buffer = (unsigned char *)pcm_out;
    
    int ret = mbedtls_base64_decode(decoded_buffer, pcm_out_size * sizeof(int16_t), 
                                     &decoded_len, (const unsigned char *)base64_data, 
                                     base64_len);
    
    if (ret != 0) {
        ESP_LOGE(TAG, "Base64 decode failed: %d (len=%zu)", ret, base64_len);
        
        // Log details for debugging
        if (ret == -0x002A) {  // MBEDTLS_ERR_BASE64_INVALID_CHARACTER
            ESP_LOGE(TAG, "Invalid character in base64 string");
            
            // Check for common issues
            bool has_whitespace = false;
            bool has_newline = false;
            for (size_t i = 0; i < base64_len && i < 100; i++) {
                if (base64_data[i] == ' ' || base64_data[i] == '\t') has_whitespace = true;
                if (base64_data[i] == '\n' || base64_data[i] == '\r') has_newline = true;
            }
            if (has_whitespace) ESP_LOGE(TAG, "Base64 contains whitespace");
            if (has_newline) ESP_LOGE(TAG, "Base64 contains newlines");
        }
        return -1;
    }
    
    int num_samples = decoded_len / sizeof(int16_t);
    ESP_LOGD(TAG, "Decoded %zu bytes → %d PCM samples", base64_len, num_samples);
    
    return num_samples;
}

