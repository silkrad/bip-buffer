#ifndef NDEBUG
#define NDEBUG /* Disable assert() so graceful return paths execute. */
#endif

#include "unity.h"
#include "bip_buffer.h"
#include "mock_calloc.h"
#include <stdlib.h>
#include <string.h>

enum {
    BUF_CAP = 64,
    SMALL_CAP = 16,
    FILL_12 = 12,
    CONSUME_8 = 8,
    WRITE_10 = 10,
    WRITE_3 = 3,
};

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

/* ================================================================
 * Error-path tests (BB_ASSERT condition false -> graceful return)
 * ================================================================ */

/* ---------- new_bip_buffer ---------- */

void test_new_bip_buffer_zero_size(void) {
    bb = new_bip_buffer(0);
    TEST_ASSERT_NULL(bb);
}

void test_new_bip_buffer_struct_alloc_fails(void) {
    mock_calloc_fail_on_call(1);
    bb = new_bip_buffer(BUF_CAP);
    TEST_ASSERT_NULL(bb);
}

void test_new_bip_buffer_buffer_alloc_fails(void) {
    mock_calloc_fail_on_call(2);
    bb = new_bip_buffer(BUF_CAP);
    TEST_ASSERT_NULL(bb);
}

/* ---------- delete_bip_buffer ---------- */

void test_delete_bip_buffer_null_ptr(void) {
    delete_bip_buffer(NULL);
    /* No crash -- void return. */
}

void test_delete_bip_buffer_already_null(void) {
    bip_buffer_t *p = NULL;
    delete_bip_buffer(&p);
    TEST_ASSERT_NULL(p);
}

/* ---------- reserve ---------- */

void test_reserve_null_reserved(void) {
    bb = new_bip_buffer(BUF_CAP);
    TEST_ASSERT_NOT_NULL(bb);

    const uint8_t *ptr = bb->reserve(bb, WRITE_10, NULL);
    TEST_ASSERT_NULL(ptr);
}

/* ---------- peek ---------- */

void test_peek_null_size(void) {
    bb = new_bip_buffer(BUF_CAP);
    TEST_ASSERT_NOT_NULL(bb);

    size_t reserved;
    uint8_t *ptr = bb->reserve(bb, WRITE_10, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    memset(ptr, 0xAA, reserved);
    bb->commit(bb, reserved);

    const uint8_t *data = bb->peek(bb, NULL);
    TEST_ASSERT_NULL(data);
}

/* ---------- commit ---------- */

void test_commit_oversized(void) {
    bb = new_bip_buffer(BUF_CAP);
    TEST_ASSERT_NOT_NULL(bb);

    size_t reserved;
    const uint8_t *ptr = bb->reserve(bb, WRITE_10, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(WRITE_10, reserved);

    /* Commit more than reserved -- should be a no-op in release. */
    bb->commit(bb, reserved + 1);

    /* Reservation should still be intact. */
    TEST_ASSERT_EQUAL_size_t(WRITE_10, bb->get_reservation_size(bb));
}

/* ---------- query functions with NULL ---------- */

void test_get_committed_size_null(void) {
    bb = new_bip_buffer(BUF_CAP);
    TEST_ASSERT_NOT_NULL(bb);

    size_t (*fn)(const bip_buffer_t *) = bb->get_committed_size;
    TEST_ASSERT_EQUAL_size_t(0, fn(NULL));
}

void test_get_reservation_size_null(void) {
    bb = new_bip_buffer(BUF_CAP);
    TEST_ASSERT_NOT_NULL(bb);

    size_t (*fn)(const bip_buffer_t *) = bb->get_reservation_size;
    TEST_ASSERT_EQUAL_size_t(0, fn(NULL));
}

void test_get_buffer_size_null(void) {
    bb = new_bip_buffer(BUF_CAP);
    TEST_ASSERT_NOT_NULL(bb);

    size_t (*fn)(const bip_buffer_t *) = bb->get_buffer_size;
    TEST_ASSERT_EQUAL_size_t(0, fn(NULL));
}

void test_reserve_null_self(void) {
    bb = new_bip_buffer(BUF_CAP);
    TEST_ASSERT_NOT_NULL(bb);

    uint8_t *(*fn)(bip_buffer_t *, size_t, size_t *) = bb->reserve;
    size_t reserved;
    TEST_ASSERT_NULL(fn(NULL, WRITE_10, &reserved));
}

void test_peek_null_self(void) {
    bb = new_bip_buffer(BUF_CAP);
    TEST_ASSERT_NOT_NULL(bb);

    uint8_t *(*fn)(const bip_buffer_t *, size_t *) = bb->peek;
    size_t size;
    TEST_ASSERT_NULL(fn(NULL, &size));
}

void test_commit_null_self(void) {
    bb = new_bip_buffer(BUF_CAP);
    TEST_ASSERT_NOT_NULL(bb);

    void (*fn)(bip_buffer_t *, size_t) = bb->commit;
    fn(NULL, 0); /* No crash -- void return. */
}

void test_consume_null_self(void) {
    bb = new_bip_buffer(BUF_CAP);
    TEST_ASSERT_NOT_NULL(bb);

    void (*fn)(bip_buffer_t *, size_t) = bb->consume;
    fn(NULL, 0); /* No crash -- void return. */
}

void test_clear_null_self(void) {
    bb = new_bip_buffer(BUF_CAP);
    TEST_ASSERT_NOT_NULL(bb);

    void (*fn)(bip_buffer_t *) = bb->clear;
    fn(NULL); /* No crash -- void return. */
}

/* ================================================================
 * Happy-path tests (BB_ASSERT condition true -> normal flow)
 *
 * These mirror the main test suite so that every branch in
 * bip_buffer.c is taken at least once in the NDEBUG build.
 * ================================================================ */

/* ---------- queries (valid self pointer) ---------- */

void test_get_committed_size_valid(void) {
    bb = new_bip_buffer(BUF_CAP);
    TEST_ASSERT_EQUAL_size_t(0, bb->get_committed_size(bb));
}

void test_get_reservation_size_valid(void) {
    bb = new_bip_buffer(BUF_CAP);
    TEST_ASSERT_EQUAL_size_t(0, bb->get_reservation_size(bb));
}

void test_get_buffer_size_valid(void) {
    bb = new_bip_buffer(BUF_CAP);
    TEST_ASSERT_EQUAL_size_t(BUF_CAP, bb->get_buffer_size(bb));
}

/* ---------- reserve (no B, fits after A) ---------- */

void test_reserve_empty_fits(void) {
    bb = new_bip_buffer(BUF_CAP);
    size_t reserved;
    uint8_t *ptr = bb->reserve(bb, WRITE_10, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(WRITE_10, reserved);
}

/* ---------- reserve (no B, clamps to available) ---------- */

void test_reserve_clamps(void) {
    bb = new_bip_buffer(BUF_CAP);
    size_t reserved;
    uint8_t *ptr = bb->reserve(bb, BUF_CAP * 2, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(BUF_CAP, reserved);
}

/* ---------- reserve (no B, wraps to front) ---------- */

void test_reserve_wraps_to_front(void) {
    bb = new_bip_buffer(SMALL_CAP);
    size_t reserved;

    /* Fill 12 of 16, consume 8 -> ixa=8, sza=4, space_after=4, ixa=8 */
    uint8_t *ptr = bb->reserve(bb, FILL_12, &reserved);
    memset(ptr, 0xAA, reserved);
    bb->commit(bb, reserved);
    bb->consume(bb, CONSUME_8);

    /* space_after_a(4) < ixa(8) -> wraps to front */
    ptr = bb->reserve(bb, CONSUME_8, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_PTR(bb->buffer, ptr);
    TEST_ASSERT_EQUAL_size_t(CONSUME_8, reserved);
}

/* ---------- reserve (no B, wraps, clamps to ixa) ---------- */

void test_reserve_wrap_clamps(void) {
    bb = new_bip_buffer(SMALL_CAP);
    size_t reserved;

    uint8_t *ptr = bb->reserve(bb, FILL_12, &reserved);
    memset(ptr, 0xAA, reserved);
    bb->commit(bb, reserved);
    bb->consume(bb, CONSUME_8);

    /* Request more than ixa(8) -> clamped */
    ptr = bb->reserve(bb, BUF_CAP, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(CONSUME_8, reserved);
}

/* ---------- reserve (no B, full -> NULL) ---------- */

void test_reserve_full_returns_null(void) {
    bb = new_bip_buffer(SMALL_CAP);
    size_t reserved;

    uint8_t *ptr = bb->reserve(bb, SMALL_CAP, &reserved);
    memset(ptr, 0xCC, reserved);
    bb->commit(bb, reserved);

    ptr = bb->reserve(bb, 1, &reserved);
    TEST_ASSERT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(0, reserved);
}

/* ---------- reserve (B exists, fits) ---------- */

void test_reserve_szb_fits(void) {
    bb = new_bip_buffer(SMALL_CAP);
    size_t reserved;

    /* Create A, consume some, wrap to create B */
    uint8_t *ptr = bb->reserve(bb, FILL_12, &reserved);
    memset(ptr, 0xAA, reserved);
    bb->commit(bb, reserved);
    bb->consume(bb, CONSUME_8);

    ptr = bb->reserve(bb, WRITE_3, &reserved);
    memset(ptr, 0xBB, reserved);
    bb->commit(bb, reserved);
    /* ixa=8, sza=4, ixb=0, szb=3. b_free=8-0-3=5 */

    ptr = bb->reserve(bb, 2, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(2, reserved);
}

/* ---------- reserve (B exists, clamps) ---------- */

void test_reserve_szb_clamps(void) {
    bb = new_bip_buffer(SMALL_CAP);
    size_t reserved;

    uint8_t *ptr = bb->reserve(bb, FILL_12, &reserved);
    memset(ptr, 0xAA, reserved);
    bb->commit(bb, reserved);
    bb->consume(bb, CONSUME_8);

    ptr = bb->reserve(bb, WRITE_3, &reserved);
    memset(ptr, 0xBB, reserved);
    bb->commit(bb, reserved);

    /* Request more than b_free(5) -> clamped */
    ptr = bb->reserve(bb, BUF_CAP, &reserved);
    TEST_ASSERT_NOT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(5, reserved);
}

/* ---------- reserve (B exists, full -> NULL) ---------- */

void test_reserve_szb_full(void) {
    bb = new_bip_buffer(SMALL_CAP);
    size_t reserved;

    uint8_t *ptr = bb->reserve(bb, FILL_12, &reserved);
    memset(ptr, 0xAA, reserved);
    bb->commit(bb, reserved);
    bb->consume(bb, CONSUME_8);

    /* Fill all of b_free: ixa(8) bytes */
    ptr = bb->reserve(bb, CONSUME_8, &reserved);
    memset(ptr, 0xBB, reserved);
    bb->commit(bb, reserved);

    ptr = bb->reserve(bb, 1, &reserved);
    TEST_ASSERT_NULL(ptr);
    TEST_ASSERT_EQUAL_size_t(0, reserved);
}

/* ---------- peek ---------- */

void test_peek_empty(void) {
    bb = new_bip_buffer(BUF_CAP);
    size_t size;
    const uint8_t *data = bb->peek(bb, &size);
    TEST_ASSERT_NULL(data);
    TEST_ASSERT_EQUAL_size_t(0, size);
}

void test_peek_has_data(void) {
    bb = new_bip_buffer(BUF_CAP);
    size_t reserved;
    uint8_t *ptr = bb->reserve(bb, WRITE_10, &reserved);
    memset(ptr, 0x42, reserved);
    bb->commit(bb, reserved);

    size_t size;
    const uint8_t *data = bb->peek(bb, &size);
    TEST_ASSERT_NOT_NULL(data);
    TEST_ASSERT_EQUAL_size_t(WRITE_10, size);
}

/* ---------- commit ---------- */

void test_commit_zero_cancels(void) {
    bb = new_bip_buffer(BUF_CAP);
    size_t reserved;
    bb->reserve(bb, WRITE_10, &reserved);
    bb->commit(bb, 0);
    TEST_ASSERT_EQUAL_size_t(0, bb->get_committed_size(bb));
}

void test_commit_to_empty(void) {
    bb = new_bip_buffer(BUF_CAP);
    size_t reserved;
    uint8_t *ptr = bb->reserve(bb, WRITE_10, &reserved);
    memset(ptr, 0xDD, reserved);
    bb->commit(bb, WRITE_10);
    TEST_ASSERT_EQUAL_size_t(WRITE_10, bb->get_committed_size(bb));
}

void test_commit_extend_a(void) {
    bb = new_bip_buffer(BUF_CAP);
    size_t reserved;

    uint8_t *ptr = bb->reserve(bb, WRITE_10, &reserved);
    memset(ptr, 0xAA, reserved);
    bb->commit(bb, WRITE_10);

    ptr = bb->reserve(bb, 5, &reserved);
    memset(ptr, 0xBB, reserved);
    bb->commit(bb, 5);
    TEST_ASSERT_EQUAL_size_t(15, bb->get_committed_size(bb));
}

void test_commit_extend_b(void) {
    bb = new_bip_buffer(SMALL_CAP);
    size_t reserved;

    uint8_t *ptr = bb->reserve(bb, FILL_12, &reserved);
    memset(ptr, 0xAA, reserved);
    bb->commit(bb, reserved);
    bb->consume(bb, CONSUME_8);

    /* Wrap to create B */
    ptr = bb->reserve(bb, WRITE_3, &reserved);
    memset(ptr, 0xBB, reserved);
    bb->commit(bb, WRITE_3);
    TEST_ASSERT_EQUAL_size_t(4 + WRITE_3, bb->get_committed_size(bb));
}

/* ---------- consume ---------- */

void test_consume_partial(void) {
    bb = new_bip_buffer(BUF_CAP);
    size_t reserved;
    uint8_t *ptr = bb->reserve(bb, WRITE_10, &reserved);
    memset(ptr, 0xFF, reserved);
    bb->commit(bb, WRITE_10);

    bb->consume(bb, 5);
    TEST_ASSERT_EQUAL_size_t(5, bb->get_committed_size(bb));
}

void test_consume_promotes_b(void) {
    bb = new_bip_buffer(SMALL_CAP);
    size_t reserved;

    uint8_t *ptr = bb->reserve(bb, FILL_12, &reserved);
    memset(ptr, 0xAA, reserved);
    bb->commit(bb, reserved);
    bb->consume(bb, CONSUME_8);

    ptr = bb->reserve(bb, WRITE_3, &reserved);
    memset(ptr, 0xBB, reserved);
    bb->commit(bb, WRITE_3);

    /* Consume all of A -> B promoted */
    bb->consume(bb, 4);
    TEST_ASSERT_EQUAL_size_t(WRITE_3, bb->get_committed_size(bb));
}

/* ---------- clear ---------- */

void test_clear(void) {
    bb = new_bip_buffer(BUF_CAP);
    size_t reserved;
    uint8_t *ptr = bb->reserve(bb, WRITE_10, &reserved);
    memset(ptr, 0x11, reserved);
    bb->commit(bb, reserved);

    bb->clear(bb);
    TEST_ASSERT_EQUAL_size_t(0, bb->get_committed_size(bb));
}

/* ---------- free_buffer (NULL inner buffer) ---------- */

void test_free_buffer_null_inner(void) {
    bb = new_bip_buffer(BUF_CAP);
    TEST_ASSERT_NOT_NULL(bb);
    free(bb->buffer);
    bb->buffer = NULL;
    delete_bip_buffer(&bb);
    TEST_ASSERT_NULL(bb);
}
