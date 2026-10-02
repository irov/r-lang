#ifndef R_STD_SECRET_H
#define R_STD_SECRET_H

#include "r_runtime_array.h"
#include "r_std_alloc.h"

#include <stddef.h>
#include <stdint.h>

/*
 * buffer is a Move-only, Send, non-Sync owner of one contiguous byte allocation. Every byte of
 * the allocation, including unused capacity, is erased through volatile stores before release.
 */
typedef RRuntimeArray RStdSecretBuffer;

/* A zero-length slice may carry a null data pointer; every nonzero slice names a valid region. */
typedef struct RStdSecretSlice {
    const uint8_t *data;
    size_t length;
} RStdSecretSlice;

typedef struct RStdSecretMutSlice {
    uint8_t *data;
    size_t length;
} RStdSecretMutSlice;

typedef struct RStdSecretBufferResult {
    RStdAllocCallStatus status;
    RStdAllocError error;
    RStdSecretBuffer value;
} RStdSecretBufferResult;

/*
 * Ownership: allocator is a shared runtime reference retained by the returned owner. Success
 * returns a buffer of exactly length zero bytes. Failure exposes no partial allocation.
 */
RStdSecretBufferResult r_std_secret_with_length(RRuntimeAllocator *allocator, size_t length);

/*
 * Ownership: source is consumed. Its allocation, length and capacity move into the returned
 * buffer without copying; the consumed source owns nothing afterwards.
 */
RStdSecretBuffer r_std_secret_from_bytes(RRuntimeArray *source);

/* Ownership: source is a shared call-bounded borrow that is not retained. */
size_t r_std_secret_len(const RStdSecretBuffer *source);
RStdSecretSlice r_std_secret_as_slice(const RStdSecretBuffer *source);

/* Ownership: source is an exclusive call-bounded borrow that is not retained. */
RStdSecretMutSlice r_std_secret_as_slice_mut(RStdSecretBuffer *source);

/* Ownership: target is an exclusive call-bounded borrow. Every byte is erased through volatile
 * stores. */
void r_std_secret_zeroize(RStdSecretMutSlice target);

/*
 * Ownership: both operands are shared call-bounded borrows. Equal lengths examine every byte
 * without a data-dependent branch; differing lengths compare the lengths only.
 */
_Bool r_std_secret_constant_time_equal(RStdSecretSlice left, RStdSecretSlice right);

/* R-SLIB-SECRET-0003: the stores are volatile so the erasure cannot be elided before release. */
static inline void r_std_secret_erase(void *data, size_t length) {
    volatile unsigned char *bytes = (volatile unsigned char *)data;
    size_t index;

    for (index = 0U; index < length; index += 1U) {
        bytes[index] = 0U;
    }
}

static inline void r_std_secret_buffer_move_initialize(RStdSecretBuffer *destination,
                                                       RStdSecretBuffer *source) {
    *destination = *source;
    *source = (RStdSecretBuffer){0};
}

static inline void r_std_secret_buffer_destroy(RStdSecretBuffer *buffer) {
    if (buffer->data != NULL) {
        r_std_secret_erase(buffer->data, buffer->capacity);
    }
    r_runtime_array_destroy(buffer);
}

#endif
