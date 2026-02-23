/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2023-2026 Ricardo Rivera */

#include "unity.h"
#include "bip_buffer.h"
#include "mock_calloc.h"
#include <stdlib.h>
#include <string.h>

static bip_buffer_t *bb;

void setUp(void) {
    mock_calloc_reset();
    bb = NULL;
}

void tearDown(void) {
    if (bb != NULL) {
        delete_bip_buffer(&bb);
    }
}

/* ---------- new_bip_buffer ---------- */

void test_new_bip_buffer_success(void) {
    bb = new_bip_buffer(64);
    TEST_ASSERT_NOT_NULL(bb);
    TEST_ASSERT_EQUAL_size_t(64, bb->get_buffer_size(bb));
}

void test_new_bip_buffer_struct_alloc_fails(void) {
    mock_calloc_fail_on_call(1);
    bb = new_bip_buffer(64);
    TEST_ASSERT_NULL(bb);
}

void test_new_bip_buffer_buffer_alloc_fails(void) {
    mock_calloc_fail_on_call(2);
    bb = new_bip_buffer(64);
    TEST_ASSERT_NULL(bb);
}

/* ---------- delete_bip_buffer ---------- */

void test_delete_bip_buffer_valid(void) {
    bb = new_bip_buffer(64);
    TEST_ASSERT_NOT_NULL(bb);
    delete_bip_buffer(&bb);
    TEST_ASSERT_NULL(bb);
}

void test_delete_bip_buffer_already_null(void) {
    bip_buffer_t *p = NULL;
    delete_bip_buffer(&p);
    TEST_ASSERT_NULL(p);
}

/* ---------- bb_free_buffer (buffer == NULL path) ---------- */

void test_free_buffer_null_buffer(void) {
    bb = new_bip_buffer(64);
    TEST_ASSERT_NOT_NULL(bb);
    /* Manually free and NULL the inner buffer to hit the NULL guard */
    free(bb->buffer);
    bb->buffer = NULL;
    /* delete_bip_buffer calls bb_free_buffer; the NULL guard skips the free */
    delete_bip_buffer(&bb);
    TEST_ASSERT_NULL(bb);
}

/* ---------- get_committed_size ---------- */

void test_get_committed_size_empty(void) {
    bb = new_bip_buffer(64);
    TEST_ASSERT_EQUAL_size_t(0, bb->get_committed_size(bb));
}

void test_get_committed_size_with_data(void) {
    bb = new_bip_buffer(64);
    size_t reserved;
    uint8_t *ptr = bb->reserve(bb, 10, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    memset(ptr, 0xAA, reserved);
    bb->commit(bb, reserved);
    TEST_ASSERT_EQUAL_size_t(10, bb->get_committed_size(bb));
}

/* ---------- get_reservation_size ---------- */

void test_get_reservation_size_none(void) {
    bb = new_bip_buffer(64);
    TEST_ASSERT_EQUAL_size_t(0, bb->get_reservation_size(bb));
}

void test_get_reservation_size_active(void) {
    bb = new_bip_buffer(64);
    size_t reserved;
    bb->reserve(bb, 10, &reserved);
    TEST_ASSERT_EQUAL_size_t(10, bb->get_reservation_size(bb));
}

/* ---------- get_buffer_size ---------- */

void test_get_buffer_size(void) {
    bb = new_bip_buffer(128);
    TEST_ASSERT_EQUAL_size_t(128, bb->get_buffer_size(bb));
}

/* ---------- reserve (szb == 0 branch) ---------- */

void test_reserve_empty_fits(void) {
    bb = new_bip_buffer(64);
    size_t reserved;
    uint8_t *ptr = bb->reserve(bb, 32, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(32, reserved);
}

void test_reserve_empty_clamps_to_available(void) {
    bb = new_bip_buffer(64);
    size_t reserved;
    uint8_t *ptr = bb->reserve(bb, 128, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(64, reserved);
}

void test_reserve_wraps_to_front(void) {
    /* Fill the buffer to create region A, consume some from front,
       then reserve should wrap to front when space_after_a < ixa */
    bb = new_bip_buffer(16);
    size_t reserved;

    /* Reserve and commit 12 bytes starting at offset 0 */
    uint8_t *ptr = bb->reserve(bb, 12, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(12, reserved);
    memset(ptr, 0xAA, reserved);
    bb->commit(bb, reserved);

    /* Consume 8 bytes -> ixa=8, sza=4, space_after_a=4, ixa=8 */
    bb->consume(bb, 8);

    /* Now space_after_a=4, ixa=8. 4 < 8 so we wrap to front.
       Request 5 but only ixa=8 available at front */
    ptr = bb->reserve(bb, 5, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(5, reserved);
    /* Reservation should be at offset 0 (wrapped) */
    TEST_ASSERT_EQUAL_PTR(bb->buffer, ptr);
}

void test_reserve_wrap_clamps_to_ixa(void) {
    bb = new_bip_buffer(16);
    size_t reserved;

    /* Reserve and commit 12 bytes */
    uint8_t *ptr = bb->reserve(bb, 12, &reserved);
    memset(ptr, 0xBB, reserved);
    bb->commit(bb, reserved);

    /* Consume 8 -> ixa=8, sza=4, space_after_a=4 */
    bb->consume(bb, 8);

    /* Wrap: request 100, but only ixa=8 bytes at front */
    ptr = bb->reserve(bb, 100, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(8, reserved);
}

void test_reserve_full_returns_null(void) {
    bb = new_bip_buffer(16);
    size_t reserved;

    /* Fill entirely */
    uint8_t *ptr = bb->reserve(bb, 16, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    memset(ptr, 0xCC, reserved);
    bb->commit(bb, reserved);

    /* No space left */
    ptr = bb->reserve(bb, 1, &reserved);
    TEST_ASSERT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(0, reserved);
}

/* ---------- reserve (szb > 0 branch) ---------- */

void test_reserve_szb_fits(void) {
    bb = new_bip_buffer(16);
    size_t reserved;

    /* Create region A at offset 0 */
    uint8_t *ptr = bb->reserve(bb, 12, &reserved);
    memset(ptr, 0xAA, reserved);
    bb->commit(bb, reserved);

    /* Consume 8 -> ixa=8, sza=4 */
    bb->consume(bb, 8);

    /* Wrap to front to create region B */
    ptr = bb->reserve(bb, 3, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    memset(ptr, 0xBB, reserved);
    bb->commit(bb, reserved);
    /* Now: ixa=8, sza=4, ixb=0, szb=3 */

    /* Reserve in B-free space: ixa - ixb - szb = 8 - 0 - 3 = 5 */
    ptr = bb->reserve(bb, 2, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(2, reserved);
}

void test_reserve_szb_clamps(void) {
    bb = new_bip_buffer(16);
    size_t reserved;

    /* Create region A */
    uint8_t *ptr = bb->reserve(bb, 12, &reserved);
    memset(ptr, 0xAA, reserved);
    bb->commit(bb, reserved);

    /* Consume 8 -> ixa=8, sza=4 */
    bb->consume(bb, 8);

    /* Wrap to create B */
    ptr = bb->reserve(bb, 3, &reserved);
    memset(ptr, 0xBB, reserved);
    bb->commit(bb, reserved);
    /* ixa=8, sza=4, ixb=0, szb=3. B-free = 5 */

    /* Request more than available */
    ptr = bb->reserve(bb, 100, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(5, reserved);
}

void test_reserve_szb_full(void) {
    bb = new_bip_buffer(16);
    size_t reserved;

    /* Create region A at offset 0 */
    uint8_t *ptr = bb->reserve(bb, 12, &reserved);
    memset(ptr, 0xAA, reserved);
    bb->commit(bb, reserved);

    /* Consume 8 -> ixa=8, sza=4 */
    bb->consume(bb, 8);

    /* Wrap and fill all B-free space: ixa - 0 - 0 = 8 bytes at front */
    ptr = bb->reserve(bb, 8, &reserved);
    memset(ptr, 0xBB, reserved);
    bb->commit(bb, reserved);
    /* ixa=8, sza=4, ixb=0, szb=8. B-free = 8-0-8 = 0 */

    /* No space left */
    ptr = bb->reserve(bb, 1, &reserved);
    TEST_ASSERT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(0, reserved);
}

/* ---------- peek ---------- */

void test_peek_empty(void) {
    bb = new_bip_buffer(64);
    size_t size;
    uint8_t *data = bb->peek(bb, &size);
    TEST_ASSERT_NULL(data);
    TEST_ASSERT_EQUAL_size_t(0, size);
}

void test_peek_has_data(void) {
    bb = new_bip_buffer(64);
    size_t reserved;
    uint8_t *ptr = bb->reserve(bb, 10, &reserved);
    memset(ptr, 0x42, reserved);
    bb->commit(bb, reserved);

    size_t size;
    uint8_t *data = bb->peek(bb, &size);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_size_t(10, size);
    TEST_ASSERT_EQUAL_UINT8(0x42, data[0]);
}

/* ---------- commit ---------- */

void test_commit_zero(void) {
    bb = new_bip_buffer(64);
    size_t reserved;
    bb->reserve(bb, 10, &reserved);
    TEST_ASSERT_EQUAL_size_t(10, bb->get_reservation_size(bb));
    bb->commit(bb, 0);
    TEST_ASSERT_EQUAL_size_t(0, bb->get_reservation_size(bb));
    TEST_ASSERT_EQUAL_size_t(0, bb->get_committed_size(bb));
}

void test_commit_first_to_empty(void) {
    bb = new_bip_buffer(64);
    size_t reserved;
    uint8_t *ptr = bb->reserve(bb, 10, &reserved);
    memset(ptr, 0xDD, reserved);
    bb->commit(bb, 10);
    TEST_ASSERT_EQUAL_size_t(10, bb->get_committed_size(bb));
    TEST_ASSERT_EQUAL_size_t(0, bb->get_reservation_size(bb));
}

void test_commit_extend_a(void) {
    bb = new_bip_buffer(64);
    size_t reserved;

    /* First commit creates region A */
    uint8_t *ptr = bb->reserve(bb, 10, &reserved);
    memset(ptr, 0xAA, reserved);
    bb->commit(bb, 10);

    /* Second reserve after A -> extends A */
    ptr = bb->reserve(bb, 5, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    memset(ptr, 0xBB, reserved);
    bb->commit(bb, 5);
    TEST_ASSERT_EQUAL_size_t(15, bb->get_committed_size(bb));
}

void test_commit_extend_b(void) {
    bb = new_bip_buffer(16);
    size_t reserved;

    /* Create region A */
    uint8_t *ptr = bb->reserve(bb, 12, &reserved);
    memset(ptr, 0xAA, reserved);
    bb->commit(bb, reserved);

    /* Consume some to make room at front */
    bb->consume(bb, 8);

    /* Reserve wraps to front -> creates region B */
    ptr = bb->reserve(bb, 3, &reserved);
    memset(ptr, 0xBB, reserved);
    bb->commit(bb, 3);

    TEST_ASSERT_EQUAL_size_t(4 + 3, bb->get_committed_size(bb));
}

void test_commit_partial(void) {
    bb = new_bip_buffer(64);
    size_t reserved;
    uint8_t *ptr = bb->reserve(bb, 10, &reserved);
    TEST_ASSERT_EQUAL_size_t(10, reserved);
    memset(ptr, 0xEE, reserved);
    bb->commit(bb, 5);
    TEST_ASSERT_EQUAL_size_t(5, bb->get_committed_size(bb));
}

/* ---------- consume ---------- */

void test_consume_partial(void) {
    bb = new_bip_buffer(64);
    size_t reserved;
    uint8_t *ptr = bb->reserve(bb, 20, &reserved);
    memset(ptr, 0xFF, reserved);
    bb->commit(bb, 20);

    bb->consume(bb, 5);
    TEST_ASSERT_EQUAL_size_t(15, bb->get_committed_size(bb));
}

void test_consume_full_promotes_b(void) {
    bb = new_bip_buffer(16);
    size_t reserved;

    /* Create A */
    uint8_t *ptr = bb->reserve(bb, 12, &reserved);
    memset(ptr, 0xAA, reserved);
    bb->commit(bb, reserved);

    /* Consume some */
    bb->consume(bb, 8);

    /* Create B at front */
    ptr = bb->reserve(bb, 3, &reserved);
    memset(ptr, 0xBB, reserved);
    bb->commit(bb, 3);

    /* Consume all of A -> B becomes A */
    bb->consume(bb, 4);
    TEST_ASSERT_EQUAL_size_t(3, bb->get_committed_size(bb));

    /* Verify we can peek at what was B (now A) */
    size_t size;
    uint8_t *data = bb->peek(bb, &size);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_size_t(3, size);
    TEST_ASSERT_EQUAL_UINT8(0xBB, data[0]);
}

/* ---------- clear ---------- */

void test_clear(void) {
    bb = new_bip_buffer(64);
    size_t reserved;
    uint8_t *ptr = bb->reserve(bb, 32, &reserved);
    memset(ptr, 0x11, reserved);
    bb->commit(bb, reserved);

    bb->clear(bb);
    TEST_ASSERT_EQUAL_size_t(0, bb->get_committed_size(bb));
    TEST_ASSERT_EQUAL_size_t(0, bb->get_reservation_size(bb));

    /* Buffer should be fully usable again */
    ptr = bb->reserve(bb, 64, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(64, reserved);
}
