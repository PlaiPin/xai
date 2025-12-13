/**
 * @file audio_decoder.h
 * @brief Base64 audio decoder for xAI voice responses
 */

#ifndef AUDIO_DECODER_H
#define AUDIO_DECODER_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Decode base64 audio data to PCM samples
 * 
 * @param base64_data Base64-encoded audio string
 * @param base64_len Length of base64 string
 * @param pcm_out Output buffer for PCM samples (int16_t, mono, 16kHz)
 * @param pcm_out_size Size of output buffer in number of samples
 * 
 * @return Number of PCM samples decoded, or -1 on error
 */
int audio_decode_base64(const char *base64_data, size_t base64_len, 
                        int16_t *pcm_out, size_t pcm_out_size);

#ifdef __cplusplus
}
#endif

#endif // AUDIO_DECODER_H

