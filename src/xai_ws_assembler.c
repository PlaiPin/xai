/**
 * @file xai_ws_assembler.c
 * @brief Internal helper: assemble fragmented WebSocket TEXT payloads into complete messages.
 */

#include "xai_ws_assembler.h"
#include <string.h>

void xai_ws_assembler_init(xai_ws_assembler_t *a, char *buf, size_t cap)
{
    if (!a) return;
    a->buf = buf;
    a->cap = cap;
    a->payload_len = 0;
    a->max_written = 0;
    a->in_progress = false;
}

void xai_ws_assembler_reset(xai_ws_assembler_t *a)
{
    if (!a) return;
    a->payload_len = 0;
    a->max_written = 0;
    a->in_progress = false;
}

bool xai_ws_assembler_feed_text(xai_ws_assembler_t *a,
                                int payload_len,
                                int payload_offset,
                                const char *data_ptr,
                                int data_len,
                                bool fin)
{
    if (!a || !a->buf || a->cap == 0 || !data_ptr || data_len <= 0) {
        return false;
    }
    if (payload_len <= 0) {
        return false;
    }
    if ((size_t)payload_len > a->cap) {
        // Oversize payload cannot fit; reset state.
        xai_ws_assembler_reset(a);
        return false;
    }

    // New message start
    if (payload_offset == 0) {
        a->payload_len = (size_t)payload_len;
        a->max_written = 0;
        a->in_progress = true;
    } else if (!a->in_progress) {
        // Fragment without a known start: drop
        return false;
    }

    if ((size_t)payload_offset + (size_t)data_len > a->cap) {
        xai_ws_assembler_reset(a);
        return false;
    }
    memcpy(a->buf + payload_offset, data_ptr, (size_t)data_len);
    size_t written_end = (size_t)payload_offset + (size_t)data_len;
    if (written_end > a->max_written) {
        a->max_written = written_end;
    }

    // Complete when we've written exactly up to payload_len and FIN is set
    if (fin && a->payload_len > 0 && a->max_written == a->payload_len) {
        a->in_progress = false;
        return true;
    }
    return false;
}


