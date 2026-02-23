/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2023-2026 Ricardo Rivera */

/**
 * @file wrap.c
 * @brief Demonstrates wrap-around behavior and Region B promotion.
 */

#include "bip_buffer.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

enum {
    BUF_CAP = 32,
    PHASE1_WRITE = 24,
    PHASE2_CONSUME = 16,
    PHASE3_WRITE = 12,
    REGION_A_AFTER_CONSUME = 8,
    COMMITTED_AFTER_WRAP = 20, /* 8 + 12 */
};

/* Phase 1: Write 24 bytes -- fills most of the buffer.
 *
 *   |AAAAAAAABBBBBBBBCCCCCCCC........|
 *    <------ A (24) ------><-free(8)->
 */
static void phase_fill(bip_buffer_t *buf) {
    size_t reserved = 0;
    uint8_t *dst = buf->reserve(buf, PHASE1_WRITE, &reserved);
    assert(reserved == PHASE1_WRITE);
    memcpy(dst, "AAAAAAAABBBBBBBBCCCCCCCC", PHASE1_WRITE);
    buf->commit(buf, PHASE1_WRITE);

    assert(buf->get_committed_size(buf) == PHASE1_WRITE);
    assert(buf->get_buffer_size(buf) == BUF_CAP);
}

/* Phase 2: Consume 16 bytes -- A shrinks, space opens at front.
 *
 *   |................CCCCCCCC........|
 *    <-- free (16) --><-A(8)-><f.(8)>
 */
static void phase_consume(bip_buffer_t *buf) {
    buf->consume(buf, PHASE2_CONSUME);
    assert(buf->get_committed_size(buf) == REGION_A_AFTER_CONSUME);
}

/* Phase 3: Reserve 12 -- wraps to front (16 before A > 8 after A).
 *
 *   |rrrrrrrrrrrr....CCCCCCCC........|
 *    <-reserved(12)->><-A(8)-><      >
 *                free(4)       unused
 */
static void phase_wrap(bip_buffer_t *buf) {
    size_t reserved = 0;
    uint8_t *dst = buf->reserve(buf, PHASE3_WRITE, &reserved);
    assert(reserved == PHASE3_WRITE);
    assert(dst == buf->buffer);
    memcpy(dst, "DDDDDDDDEEEE", PHASE3_WRITE);
    buf->commit(buf, PHASE3_WRITE);

    assert(buf->get_committed_size(buf) == COMMITTED_AFTER_WRAP);
}

/* Phase 4-5: Read A, then B (promoted to A). */
static void phase_read_and_promote(bip_buffer_t *buf) {
    size_t available = 0;
    const uint8_t *src = buf->peek(buf, &available);
    assert(available == REGION_A_AFTER_CONSUME);
    assert(memcmp(src, "CCCCCCCC", REGION_A_AFTER_CONSUME) == 0);
    buf->consume(buf, REGION_A_AFTER_CONSUME);

    /*
     *   |DDDDDDDDEEEE....................|
     *    <-- A (12) --><--- free (20) --->
     */
    src = buf->peek(buf, &available);
    assert(available == PHASE3_WRITE);
    assert(memcmp(src, "DDDDDDDDEEEE", PHASE3_WRITE) == 0);
    buf->consume(buf, PHASE3_WRITE);

    src = buf->peek(buf, &available);
    assert(src == NULL);
    assert(available == 0);
}

int main(void) {
    bip_buffer_t *buf = new_bip_buffer(BUF_CAP);
    assert(buf != NULL);

    phase_fill(buf);
    phase_consume(buf);
    phase_wrap(buf);
    phase_read_and_promote(buf);

    delete_bip_buffer(&buf);

    (void)fprintf(stdout, "wrap: PASSED\n");
    return 0;
}
