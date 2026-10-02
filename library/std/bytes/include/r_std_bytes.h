#ifndef R_STD_BYTES_H
#define R_STD_BYTES_H

#include "r_runtime_array.h"
#include "r_std_alloc.h"

#include <stddef.h>
#include <stdint.h>

/* bytes is the C ABI spelling of the predefined transparent array(u8) alias. */
typedef RRuntimeArray RStdBytes;

/* A zero-length slice may carry a null data pointer; every nonzero slice names a valid region. */
typedef struct RStdBytesSlice {
    const uint8_t *data;
    size_t length;
} RStdBytesSlice;

typedef struct RStdBytesMutSlice {
    uint8_t *data;
    size_t length;
} RStdBytesMutSlice;

typedef enum RStdBytesError {
    R_STD_BYTES_ERROR_OUT_OF_BOUNDS = 0,
    R_STD_BYTES_ERROR_RANGE_OVERFLOW = 1
} RStdBytesError;

typedef struct RStdBytesSizeResult {
    _Bool is_ok;
    size_t value;
    RStdBytesError error;
} RStdBytesSizeResult;

typedef struct RStdBytesIndexOption {
    _Bool has_value;
    size_t value;
} RStdBytesIndexOption;

typedef struct RStdBytesArrayResult {
    RStdAllocCallStatus status;
    RStdAllocError error;
    RStdBytes value;
} RStdBytesArrayResult;

typedef struct RStdBytesAllocResult {
    RStdAllocCallStatus status;
    RStdAllocError error;
} RStdBytesAllocResult;

/*
 * Ownership: allocator is a shared runtime reference retained by the returned owner. Success
 * returns an empty byte array with at least capacity bytes of storage. Failure exposes no partial
 * allocation.
 */
RStdBytesArrayResult r_std_bytes_with_capacity(RRuntimeAllocator *allocator, size_t capacity);

/*
 * Ownership: target is an exclusive call-bounded borrow and source is a shared call-bounded
 * borrow. Allocation failure preserves target and source exactly. Neither borrow is retained.
 */
RStdBytesAllocResult r_std_bytes_append(RStdBytes *target, RStdBytesSlice source);

/* Ownership: target is exclusive. Allocation failure preserves target exactly. */
RStdBytesAllocResult r_std_bytes_append_u8(RStdBytes *target, uint8_t value);
RStdBytesAllocResult r_std_bytes_append_u16_le(RStdBytes *target, uint16_t value);
RStdBytesAllocResult r_std_bytes_append_u32_le(RStdBytes *target, uint32_t value);
RStdBytesAllocResult r_std_bytes_append_u64_le(RStdBytes *target, uint64_t value);

/*
 * Ownership: destination is an exclusive call-bounded borrow and source is a shared
 * call-bounded borrow. Neither is retained or consumed. A nonzero-length slice has a valid
 * data region of at least length bytes. The admitted R borrows are distinct.
 */
RStdBytesSizeResult r_std_bytes_copy(RStdBytesMutSlice destination, RStdBytesSlice source);

/*
 * Ownership: buffer is an exclusive call-bounded borrow and is not retained or consumed. A
 * nonzero-length slice has a valid data region of at least length bytes.
 */
RStdBytesSizeResult
r_std_bytes_copy_within(RStdBytesMutSlice buffer, size_t destination, size_t source, size_t length);

/* Ownership: destination is an exclusive call-bounded borrow and is not retained. */
void r_std_bytes_fill(RStdBytesMutSlice destination, uint8_t value);

/* Ownership: left and right are shared call-bounded borrows and are not retained. */
int32_t r_std_bytes_compare(RStdBytesSlice left, RStdBytesSlice right);

/* Ownership: left and right are shared call-bounded borrows and are not retained. */
_Bool r_std_bytes_equal(RStdBytesSlice left, RStdBytesSlice right);

/* Ownership: source is a shared call-bounded borrow and is not retained. */
RStdBytesIndexOption r_std_bytes_find(RStdBytesSlice source, uint8_t value);

/* Ownership: source and pattern are shared call-bounded borrows and are not retained. */
RStdBytesIndexOption r_std_bytes_find_slice(RStdBytesSlice source, RStdBytesSlice pattern);

/* Ownership: source and prefix are shared call-bounded borrows and are not retained. */
_Bool r_std_bytes_starts_with(RStdBytesSlice source, RStdBytesSlice prefix);

/* Ownership: source and suffix are shared call-bounded borrows and are not retained. */
_Bool r_std_bytes_ends_with(RStdBytesSlice source, RStdBytesSlice suffix);

#endif
