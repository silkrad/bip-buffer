/**
 * @file bip_buffer.c
 * @brief Bip buffer (bipartite circular buffer) implementation.
 *
 * Region A holds the oldest committed data and is always read first.
 * Region B, when present, wraps around the beginning of the backing array.
 * A single reservation tracks in-progress writes that have not yet been
 * committed.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "bip_buffer.h"

/* Assert in debug, return error value in release. */
#ifdef NDEBUG
#define BB_ASSERT(cond, ret)                                                                                           \
    do {                                                                                                               \
        if (!(cond))                                                                                                   \
            return (ret);                                                                                              \
    } while (0)
#define BB_ASSERT_VOID(cond)                                                                                           \
    do {                                                                                                               \
        if (!(cond))                                                                                                   \
            return;                                                                                                    \
    } while (0)
#else
#define BB_ASSERT(cond, ret) assert(cond)
#define BB_ASSERT_VOID(cond) assert(cond)
#endif

/* ---- Forward declarations of private methods ---- */

static size_t bb_get_committed_size(const bip_buffer_t *bip_buff);
static size_t bb_get_reservation_size(const bip_buffer_t *bip_buff);
static size_t bb_get_buffer_size(const bip_buffer_t *bip_buff);
static size_t bb_get_space_after_a(const bip_buffer_t *bip_buff);
static size_t bb_get_b_free_space(const bip_buffer_t *bip_buff);
static uint8_t *bb_reserve_after_b(bip_buffer_t *bip_buff, size_t size, size_t *reserved);
static uint8_t *bb_reserve_no_b(bip_buffer_t *bip_buff, size_t size, size_t *reserved);
static uint8_t *bb_reserve(bip_buffer_t *bip_buff, size_t size, size_t *reserved);
static uint8_t *bb_peek(const bip_buffer_t *bip_buff, size_t *size);
static void bb_commit(bip_buffer_t *bip_buff, size_t size);
static void bb_consume(bip_buffer_t *bip_buff, size_t size);
static void bb_clear(bip_buffer_t *bip_buff);
static void bb_free_buffer(bip_buffer_t *bip_buff);

/* ---- Public API ---- */

/**
 * @brief Allocate and initialise a new bip buffer.
 * @param[in] size Capacity in bytes (must be > 0).
 * @return Pointer to the new bip buffer, or NULL on failure or if
 *         @p size is 0 (asserts in debug, returns NULL in release).
 */
bip_buffer_t *new_bip_buffer(const size_t size) {
    bip_buffer_t *bip_buff = NULL;

    BB_ASSERT(0 < size, NULL);

    bip_buff = calloc(1, sizeof *bip_buff);
    if (NULL == bip_buff) {
        (void)fprintf(stderr, "bip_buff calloc error\n");
        return NULL;
    }

    bip_buff->buffer = calloc(size, 1);
    if (NULL == bip_buff->buffer) {
        (void)fprintf(stderr, "bip_buff->buffer calloc error\n");
        free(bip_buff);
        return NULL;
    }

    bip_buff->buflen = size;

    bip_buff->commit = bb_commit;
    bip_buff->get_committed_size = bb_get_committed_size;
    bip_buff->get_reservation_size = bb_get_reservation_size;
    bip_buff->get_buffer_size = bb_get_buffer_size;
    bip_buff->reserve = bb_reserve;
    bip_buff->peek = bb_peek;
    bip_buff->consume = bb_consume;
    bip_buff->clear = bb_clear;

    return bip_buff;
}

/**
 * @brief Destroy a bip buffer and set the caller's pointer to NULL.
 *
 * Safe to call when @p *bip_buff is already NULL.  Passing a NULL
 * @p bip_buff pointer is a no-op in release (asserts in debug).
 *
 * @param[in,out] bip_buff Address of the bip buffer pointer.
 */
void delete_bip_buffer(bip_buffer_t **const bip_buff) {
    BB_ASSERT_VOID(NULL != bip_buff);

    if (NULL != *bip_buff) {
        bb_free_buffer(*bip_buff);
        free(*bip_buff);
        *bip_buff = NULL;
    }
}

/* ---- Queries ---- */

/**
 * @brief Return the total committed (readable) byte count.
 * @param[in] bip_buff Non-NULL bip buffer.
 * @return sza + szb.
 */
static size_t bb_get_committed_size(const bip_buffer_t *const bip_buff) {
    BB_ASSERT(NULL != bip_buff, 0);
    return bip_buff->sza + bip_buff->szb;
}

/**
 * @brief Return the size of the active reservation.
 * @param[in] bip_buff Non-NULL bip buffer.
 * @return Reservation size in bytes, or 0 if none.
 */
static size_t bb_get_reservation_size(const bip_buffer_t *const bip_buff) {
    BB_ASSERT(NULL != bip_buff, 0);
    return bip_buff->sz_reserve;
}

/**
 * @brief Return the total buffer capacity.
 * @param[in] bip_buff Non-NULL bip buffer.
 * @return Capacity in bytes.
 */
static size_t bb_get_buffer_size(const bip_buffer_t *const bip_buff) {
    BB_ASSERT(NULL != bip_buff, 0);
    return bip_buff->buflen;
}

/**
 * @brief Compute free bytes after region A (towards the end of the array).
 *
 * Internal helper -- the caller is responsible for NULL validation.
 *
 * @param[in] bip_buff Bip buffer (assumed non-NULL).
 * @return buflen - ixa - sza.
 */
static size_t bb_get_space_after_a(const bip_buffer_t *const bip_buff) {
    return bip_buff->buflen - bip_buff->ixa - bip_buff->sza;
}

/**
 * @brief Compute free bytes between the end of region B and the start of region A.
 *
 * Internal helper -- the caller is responsible for NULL validation.
 *
 * @param[in] bip_buff Bip buffer (assumed non-NULL).
 * @return ixa - ixb - szb.
 */
static size_t bb_get_b_free_space(const bip_buffer_t *const bip_buff) {
    return bip_buff->ixa - bip_buff->ixb - bip_buff->szb;
}

/* ---- Core operations ---- */

/**
 * @brief Reserve space after region B.
 *
 * Called when region B exists.  Allocates up to @p size bytes in the gap
 * between the end of B and the start of A.
 *
 * @param[in,out] bip_buff Bip buffer (assumed non-NULL).
 * @param[in]     size     Desired reservation in bytes.
 * @param[out]    reserved Actual reserved bytes (may be clamped).
 * @return Pointer to the reserved region, or NULL if no space remains.
 */
static uint8_t *bb_reserve_after_b(bip_buffer_t *bip_buff, size_t size, size_t *reserved) {
    size_t free_space = bb_get_b_free_space(bip_buff);

    if (size < free_space) {
        free_space = size;
    }

    if (0 == free_space) {
        return NULL;
    }

    bip_buff->sz_reserve = free_space;
    bip_buff->ix_reserve = bip_buff->ixb + bip_buff->szb;
    *reserved = free_space;
    return bip_buff->buffer + bip_buff->ix_reserve;
}

/**
 * @brief Reserve space when no region B exists.
 *
 * Chooses the larger of {space after A, space before A}.  If wrapping to
 * the front is selected, the reservation starts at index 0.
 *
 * @param[in,out] bip_buff Bip buffer (assumed non-NULL).
 * @param[in]     size     Desired reservation in bytes.
 * @param[out]    reserved Actual reserved bytes (may be clamped).
 * @return Pointer to the reserved region, or NULL if the buffer is full.
 */
static uint8_t *bb_reserve_no_b(bip_buffer_t *bip_buff, size_t size, size_t *reserved) {
    size_t free_space = bb_get_space_after_a(bip_buff);

    if (free_space >= bip_buff->ixa && 0 < free_space) {
        if (size < free_space) {
            free_space = size;
        }

        bip_buff->sz_reserve = free_space;
        bip_buff->ix_reserve = bip_buff->ixa + bip_buff->sza;
        *reserved = free_space;
        return bip_buff->buffer + bip_buff->ix_reserve;
    }

    if (0 < bip_buff->ixa) {
        if (bip_buff->ixa < size) {
            size = bip_buff->ixa;
        }

        bip_buff->sz_reserve = size;
        bip_buff->ix_reserve = 0;
        *reserved = size;
        return bip_buff->buffer;
    }

    return NULL;
}

/**
 * @brief Reserve writable space in the buffer.
 *
 * Delegates to bb_reserve_after_b() when region B exists, or
 * bb_reserve_no_b() otherwise.
 *
 * @param[in,out] bip_buff Non-NULL bip buffer.
 * @param[in]     size     Desired reservation in bytes.
 * @param[out]    reserved Actual reserved bytes (may be clamped).
 * @return Pointer to the reserved region, or NULL if the buffer is full.
 */
static uint8_t *bb_reserve(bip_buffer_t *bip_buff, size_t size, size_t *reserved) {
    BB_ASSERT(NULL != bip_buff, NULL);
    BB_ASSERT(NULL != reserved, NULL);

    *reserved = 0;

    if (bip_buff->szb) {
        return bb_reserve_after_b(bip_buff, size, reserved);
    }
    return bb_reserve_no_b(bip_buff, size, reserved);
}

/**
 * @brief Peek at the oldest committed data without consuming it.
 * @param[in]  bip_buff Non-NULL bip buffer.
 * @param[out] size     Contiguous readable byte count.
 * @return Pointer to region A data, or NULL if the buffer is empty.
 */
static uint8_t *bb_peek(const bip_buffer_t *bip_buff, size_t *size) {
    uint8_t *data = NULL;

    BB_ASSERT(NULL != bip_buff, NULL);
    BB_ASSERT(NULL != size, NULL);

    if (0 == bip_buff->sza) {
        *size = 0;
    } else {
        *size = bip_buff->sza;
        data = bip_buff->buffer + bip_buff->ixa;
    }

    return data;
}

/**
 * @brief Commit previously reserved bytes, making them readable.
 *
 * Committing 0 bytes cancels the active reservation.  When the buffer is
 * empty the committed region becomes A; otherwise the bytes extend A (if
 * contiguous) or B.
 *
 * @param[in,out] bip_buff Non-NULL bip buffer.
 * @param[in]     size     Bytes to commit (<= reservation size).
 */
static void bb_commit(bip_buffer_t *bip_buff, size_t size) {
    BB_ASSERT_VOID(NULL != bip_buff);
    BB_ASSERT_VOID(size <= bip_buff->sz_reserve);

    if (size == 0) {
        bip_buff->sz_reserve = 0;
        bip_buff->ix_reserve = 0;
    } else {
        if (0 == bip_buff->sza) {
            bip_buff->ixa = bip_buff->ix_reserve;
            bip_buff->sza = size;
        } else if (bip_buff->ix_reserve == bip_buff->sza + bip_buff->ixa) {
            bip_buff->sza += size;
        } else {
            bip_buff->szb += size;
        }

        bip_buff->ix_reserve = 0;
        bip_buff->sz_reserve = 0;
    }
}

/**
 * @brief Consume (discard) the oldest committed bytes.
 *
 * If @p size >= region A size, region B is promoted to region A.
 *
 * @param[in,out] bip_buff Non-NULL bip buffer.
 * @param[in]     size     Bytes to consume.
 */
static void bb_consume(bip_buffer_t *bip_buff, size_t size) {
    BB_ASSERT_VOID(NULL != bip_buff);

    if (size >= bip_buff->sza) {
        bip_buff->ixa = bip_buff->ixb;
        bip_buff->sza = bip_buff->szb;
        bip_buff->ixb = 0;
        bip_buff->szb = 0;
    } else {
        bip_buff->sza -= size;
        bip_buff->ixa += size;
    }
}

/**
 * @brief Reset the buffer to an empty state.
 *
 * All regions and the active reservation are zeroed.  The backing
 * memory is retained.
 *
 * @param[in,out] bip_buff Non-NULL bip buffer.
 */
static void bb_clear(bip_buffer_t *bip_buff) {
    BB_ASSERT_VOID(NULL != bip_buff);

    bip_buff->ixa = 0;
    bip_buff->sza = 0;
    bip_buff->ixb = 0;
    bip_buff->szb = 0;
    bip_buff->ix_reserve = 0;
    bip_buff->sz_reserve = 0;
}

/**
 * @brief Free the backing buffer and reset all fields.
 *
 * No-op if the backing buffer is already NULL.
 * Internal helper -- the caller is responsible for NULL validation.
 *
 * @param[in,out] bip_buff Bip buffer (assumed non-NULL).
 */
static void bb_free_buffer(bip_buffer_t *bip_buff) {
    if (NULL != bip_buff->buffer) {
        bb_clear(bip_buff);
        free(bip_buff->buffer);
        bip_buff->buffer = NULL;
        bip_buff->buflen = 0;
    }
}
