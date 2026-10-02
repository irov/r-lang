#ifndef R_STD_STRING_H
#define R_STD_STRING_H

#include "r_runtime_array.h"
#include "r_runtime_string.h"
#include "r_std_alloc.h"

#include <stddef.h>
#include <stdint.h>

typedef RRuntimeString RStdString;

/* A zero-length view may carry a null data pointer. The view never owns its storage. */
typedef struct RStdStringView {
    const uint8_t *data;
    size_t length;
} RStdStringView;

typedef enum RStdStringErrorKind {
    R_STD_STRING_ERROR_INVALID_UTF8 = 0,
    R_STD_STRING_ERROR_ALLOCATION_FAILED = 1
} RStdStringErrorKind;

typedef struct RStdStringError {
    RStdStringErrorKind kind;
    size_t invalid_index;
    RStdAllocError allocation_error;
} RStdStringError;

typedef enum RStdStringBoundaryError {
    R_STD_STRING_BOUNDARY_ERROR_OUT_OF_BOUNDS = 0,
    R_STD_STRING_BOUNDARY_ERROR_NOT_SCALAR_BOUNDARY = 1
} RStdStringBoundaryError;

/* Internal compiler/runtime ABI status; CONTRACT_VIOLATION is not an R error variant. */
typedef enum RStdStringCallStatus {
    R_STD_STRING_CALL_SUCCESS = 0,
    R_STD_STRING_CALL_ERROR = 1,
    R_STD_STRING_CALL_CONTRACT_VIOLATION = 2
} RStdStringCallStatus;

typedef struct RStdStringCreateResult {
    RStdStringCallStatus status;
    RStdString value;
} RStdStringCreateResult;

typedef struct RStdStringAllocValueResult {
    RStdStringCallStatus status;
    RStdAllocError error;
    RStdString value;
} RStdStringAllocValueResult;

typedef struct RStdStringUtf8ValueResult {
    RStdStringCallStatus status;
    RStdStringError error;
    RStdString value;
} RStdStringUtf8ValueResult;

typedef enum RStdStringFromBytesKind {
    R_STD_STRING_FROM_BYTES_VALID = 0,
    R_STD_STRING_FROM_BYTES_INVALID = 1
} RStdStringFromBytesKind;

typedef struct RStdStringFromBytesResult {
    RStdStringFromBytesKind kind;
    RStdString valid;
    RRuntimeArray invalid_bytes;
    size_t invalid_index;
} RStdStringFromBytesResult;

typedef struct RStdStringFromBytesCallResult {
    RStdStringCallStatus status;
    RStdStringFromBytesResult outcome;
} RStdStringFromBytesCallResult;

typedef struct RStdStringAllocResult {
    RStdStringCallStatus status;
    RStdAllocError error;
} RStdStringAllocResult;

typedef struct RStdStringErrorResult {
    RStdStringCallStatus status;
    RStdStringError error;
} RStdStringErrorResult;

typedef struct RStdStringBoundaryResult {
    RStdStringCallStatus status;
    RStdStringBoundaryError error;
} RStdStringBoundaryResult;

/*
 * Ownership: allocator is a shared runtime reference retained by value. No byte allocation is
 * performed. The successful result owns an empty string.
 */
RStdStringCreateResult r_std_string_create(RRuntimeAllocator *allocator);

/*
 * Ownership: allocator is a shared runtime reference retained by value. Success returns the sole
 * owner of the allocation. Failure exposes no partial string.
 */
RStdStringAllocValueResult r_std_string_with_capacity(RRuntimeAllocator *allocator,
                                                      size_t capacity);

/*
 * Ownership: source is a valid UTF-8 call-bounded borrow and is never retained. Success returns a
 * byte-for-byte owned copy; failure exposes no partial string.
 */
RStdStringAllocValueResult r_std_string_from_str(RRuntimeAllocator *allocator,
                                                 RStdStringView source);

/*
 * Ownership: source is a call-bounded byte borrow and is never retained. Complete UTF-8
 * validation precedes allocation. Success returns an owned copy.
 */
RStdStringUtf8ValueResult r_std_string_from_utf8(RRuntimeAllocator *allocator,
                                                 RStdStringView source);

/*
 * Ownership: source is consumed on every public outcome. Valid input transfers its allocation to
 * value. Invalid input transfers the identical array owner to invalid_bytes. No allocation occurs.
 */
RStdStringFromBytesCallResult r_std_string_from_bytes(RRuntimeArray *source);

/* Ownership: source is a shared call-bounded borrow and is never retained. */
size_t r_std_string_len(const RStdString *source);
size_t r_std_string_capacity(const RStdString *source);
RStdStringView r_std_string_as_str(const RStdString *source);
RStdStringView r_std_string_as_bytes(const RStdString *source);

/*
 * Ownership: target is an exclusive call-bounded borrow. Allocation failure is transactional and
 * preserves bytes, length, capacity, and ownership.
 */
RStdStringAllocResult r_std_string_reserve(RStdString *target, size_t additional);

/*
 * Ownership: target is exclusive and suffix is a valid UTF-8 shared call-bounded borrow. Neither
 * borrow is retained. Failure preserves target exactly.
 */
RStdStringAllocResult r_std_string_append_str(RStdString *target, RStdStringView suffix);

/*
 * Ownership: target is exclusive and suffix is a shared call-bounded byte borrow. Complete UTF-8
 * validation precedes allocation or mutation. Failure preserves target exactly.
 */
RStdStringErrorResult r_std_string_append_utf8(RStdString *target, RStdStringView suffix);

/* Ownership: target is an exclusive call-bounded borrow. Failure preserves target exactly. */
RStdStringAllocResult r_std_string_push_scalar(RStdString *target, uint32_t value);

/*
 * Ownership: target is an exclusive call-bounded borrow. Either boundary error preserves bytes
 * and capacity; success retains the existing allocation.
 */
RStdStringBoundaryResult r_std_string_truncate(RStdString *target, size_t byte_length);

/* Ownership: target is exclusive. Storage is retained and no allocation occurs. */
void r_std_string_clear(RStdString *target);

/*
 * Ownership: source is consumed and set empty on success; result becomes the sole owner of the
 * same allocation and exact bytes. No allocation or copying occurs.
 */
RStdStringCallStatus r_std_string_into_bytes(RStdString *source, RRuntimeArray *result);

/* Compiler ownership glue; the moved-from value is safe to destroy. */
static inline void r_std_string_move_initialize(RStdString *destination, RStdString *source) {
    *destination = *source;
    *source = (RStdString){0};
}

static inline void r_std_string_destroy(RStdString *source) {
    r_runtime_string_destroy(source);
}

static inline void
r_std_string_from_bytes_result_move_initialize(RStdStringFromBytesResult *destination,
                                               RStdStringFromBytesResult *source) {
    *destination = *source;
    *source = (RStdStringFromBytesResult){0};
}

static inline void r_std_string_from_bytes_result_destroy(RStdStringFromBytesResult *result) {
    if (result->kind == R_STD_STRING_FROM_BYTES_VALID) {
        r_std_string_destroy(&result->valid);
    } else {
        r_runtime_array_destroy(&result->invalid_bytes);
    }
    *result = (RStdStringFromBytesResult){0};
}

#endif
