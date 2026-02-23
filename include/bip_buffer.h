/* SPDX-License-Identifier: Apache-2.0
 * Copyright 2023-2026 Ricardo Rivera */

/**
 * @file bip_buffer.h
 * @brief Bip buffer (bipartite circular buffer) interface.
 *
 * A bip buffer maintains two contiguous regions (A and B) within a single
 * flat allocation, enabling zero-copy reads of committed data.  Callers
 * reserve writable space, commit written bytes, peek at readable data,
 * and consume bytes that have been processed.
 *
 * @note This implementation is @b not thread-safe.  External synchronisation
 *       is required when a bip buffer is shared between threads.
 */

#ifndef BIP_BUFFER_H
#define BIP_BUFFER_H

#include <stddef.h>
#include <stdint.h>

/** @brief Forward declaration of the bip buffer type. */
typedef struct bip_buffer bip_buffer_t;

/**
 * @brief Bip buffer instance.
 *
 * All data fields and function pointers are initialised by new_bip_buffer().
 * Callers interact through the function-pointer members; direct field access
 * is permitted but not required.
 */
struct bip_buffer {
    uint8_t *buffer;   /**< Backing byte array. */
    size_t ixa;        /**< Start index of region A. */
    size_t sza;        /**< Byte count of region A. */
    size_t ixb;        /**< Start index of region B. */
    size_t szb;        /**< Byte count of region B. */
    size_t buflen;     /**< Total capacity in bytes. */
    size_t ix_reserve; /**< Start index of the current reservation. */
    size_t sz_reserve; /**< Byte count of the current reservation. */

    /**
     * @brief Return the total committed (readable) bytes.
     * @param[in] bip_buff Non-NULL bip buffer.
     * @return Sum of region A and region B sizes, or 0 if @p bip_buff is NULL
     *         (release builds only; asserts in debug).
     */
    size_t (*get_committed_size)(const bip_buffer_t *bip_buff);

    /**
     * @brief Return the size of the current reservation.
     * @param[in] bip_buff Non-NULL bip buffer.
     * @return Reservation size in bytes, or 0 if none is active or @p bip_buff
     *         is NULL (release builds only; asserts in debug).
     */
    size_t (*get_reservation_size)(const bip_buffer_t *bip_buff);

    /**
     * @brief Return the total buffer capacity.
     * @param[in] bip_buff Non-NULL bip buffer.
     * @return Capacity in bytes passed to new_bip_buffer(), or 0 if @p bip_buff
     *         is NULL (release builds only; asserts in debug).
     */
    size_t (*get_buffer_size)(const bip_buffer_t *bip_buff);

    /**
     * @brief Reserve writable space in the buffer.
     *
     * The returned pointer is valid until the next call to reserve, commit,
     * consume, or clear.  If the requested size cannot be fully satisfied the
     * reservation is clamped to the largest contiguous free region.
     *
     * Passing @p size == 0 is a no-op that returns NULL with @p *reserved == 0.
     *
     * @param[in,out] bip_buff Non-NULL bip buffer.
     * @param[in]     size     Desired reservation in bytes.
     * @param[out]    reserved Actual reserved bytes (may be less than @p size).
     * @return Pointer to the reserved region, or NULL if the buffer is full or
     *         a NULL argument is passed (release builds only; asserts in debug).
     */
    uint8_t *(*reserve)(bip_buffer_t *bip_buff, size_t size, size_t *reserved);

    /**
     * @brief Peek at the oldest committed data without consuming it.
     * @param[in]  bip_buff Non-NULL bip buffer.
     * @param[out] size     Number of contiguous readable bytes.
     * @return Pointer to the data, or NULL if the buffer is empty or a NULL
     *         argument is passed (release builds only; asserts in debug).
     */
    uint8_t *(*peek)(const bip_buffer_t *bip_buff, size_t *size);

    /**
     * @brief Commit previously reserved bytes, making them readable.
     *
     * @p size must be less than or equal to the current reservation size.
     * A commit of 0 cancels the reservation.  In release builds, passing a
     * NULL @p bip_buff or a @p size exceeding the reservation is a no-op
     * (asserts in debug).
     *
     * @param[in,out] bip_buff Non-NULL bip buffer.
     * @param[in]     size     Bytes to commit (<= reservation size).
     */
    void (*commit)(bip_buffer_t *bip_buff, size_t size);

    /**
     * @brief Consume (discard) the oldest committed bytes.
     *
     * If @p size is greater than or equal to region A, region B is promoted
     * to region A.  In release builds, passing a NULL @p bip_buff is a no-op
     * (asserts in debug).
     *
     * @param[in,out] bip_buff Non-NULL bip buffer.
     * @param[in]     size     Bytes to consume.
     */
    void (*consume)(bip_buffer_t *bip_buff, size_t size);

    /**
     * @brief Reset the buffer to an empty state.
     *
     * All regions and the active reservation are cleared.  The backing
     * memory is retained.  In release builds, passing a NULL @p bip_buff
     * is a no-op (asserts in debug).
     *
     * @param[in,out] bip_buff Non-NULL bip buffer.
     */
    void (*clear)(bip_buffer_t *bip_buff);
};

/**
 * @brief Allocate and initialise a new bip buffer.
 * @param[in] size Capacity in bytes (must be > 0).
 * @return Pointer to the new bip buffer, or NULL on allocation failure or if
 *         @p size is 0 (release builds only; asserts in debug).
 */
bip_buffer_t *new_bip_buffer(size_t size);

/**
 * @brief Destroy a bip buffer and set the caller's pointer to NULL.
 *
 * Safe to call when @p *bip_buff is already NULL.  In release builds,
 * passing a NULL @p bip_buff pointer is a no-op (asserts in debug).
 *
 * @param[in,out] bip_buff Address of the bip buffer pointer.
 */
void delete_bip_buffer(bip_buffer_t **bip_buff);

#endif
