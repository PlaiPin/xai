/**
 * @file xai_ws_assembler.h
 * @brief Internal helper: assemble fragmented WebSocket TEXT payloads into complete messages.
 */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

typedef struct {
    char *buf;
    size_t cap;
    size_t payload_len;    /**< expected total */
    size_t max_written;    /**< max offset+len written so far */
    bool in_progress;
} xai_ws_assembler_t;

void xai_ws_assembler_init(xai_ws_assembler_t *a, char *buf, size_t cap);
void xai_ws_assembler_reset(xai_ws_assembler_t *a);

/**
 * @brief Feed one websocket fragment.
 *
 * @return true if a complete message is now ready (caller can read a->buf with length a->payload_len)
 */
bool xai_ws_assembler_feed_text(xai_ws_assembler_t *a,
                                int payload_len,
                                int payload_offset,
                                const char *data_ptr,
                                int data_len,
                                bool fin);


