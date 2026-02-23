/**
 * @file basic.c
 * @brief Basic bip buffer usage: write, read, partial reserve, cancel.
 */

#include "bip_buffer.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    BUF_CAP = 4096,
    FILL_SIZE = 4000,
    CLAMP_REQ = 1024,
    CANCEL_REQ = 256,
};

static void demo_write_read(bip_buffer_t *buf) {
    const char *payload = "Hello, bip buffer!";
    size_t payload_len = strlen(payload);

    size_t reserved = 0;
    uint8_t *dst = buf->reserve(buf, payload_len, &reserved);
    assert(dst != NULL);
    assert(reserved == payload_len);
    memcpy(dst, payload, reserved);
    buf->commit(buf, reserved);

    size_t available = 0;
    const uint8_t *src = buf->peek(buf, &available);
    assert(src != NULL);
    assert(available == payload_len);
    assert(memcmp(src, "Hello, bip buffer!", payload_len) == 0);
    buf->consume(buf, available);

    src = buf->peek(buf, &available);
    assert(src == NULL);
    assert(available == 0);
}

static void demo_partial_reserve(bip_buffer_t *buf) {
    size_t reserved = 0;
    uint8_t *dst = buf->reserve(buf, FILL_SIZE, &reserved);
    assert(dst != NULL);
    memset(dst, 'X', reserved);
    buf->commit(buf, reserved);

    dst = buf->reserve(buf, CLAMP_REQ, &reserved);
    assert(dst != NULL);
    assert(reserved < CLAMP_REQ);
    buf->commit(buf, 0);
}

static void demo_cancel(bip_buffer_t *buf) {
    buf->clear(buf);

    size_t reserved = 0;
    const uint8_t *dst = buf->reserve(buf, CANCEL_REQ, &reserved);
    assert(dst != NULL);
    buf->commit(buf, 0);
    assert(buf->get_reservation_size(buf) == 0);
    assert(buf->get_committed_size(buf) == 0);
}

int main(void) {
    bip_buffer_t *buf = new_bip_buffer(BUF_CAP);
    assert(buf != NULL);

    demo_write_read(buf);
    demo_partial_reserve(buf);
    demo_cancel(buf);

    delete_bip_buffer(&buf);
    assert(buf == NULL);

    delete_bip_buffer(&buf);
    assert(buf == NULL);

    (void)fprintf(stdout, "basic: PASSED\n");
    return 0;
}
