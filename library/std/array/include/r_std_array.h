#ifndef R_STD_ARRAY_H
#define R_STD_ARRAY_H

#include "r_runtime_array.h"
#include "r_std_alloc.h"

#include <stddef.h>

typedef RRuntimeArray RStdArray;

/*
 * Compiler wrapper contract:
 *
 * - T is represented by one immutable RRuntimeTypeInfo value emitted for the concrete type.
 * - typed wrappers stage Move T arguments in caller-owned storage and pass their address here;
 * - typed option wrappers allocate no storage and initialize T only when has_value is true;
 * - pointer and slice wrappers attach the originating array borrow to returned R references;
 * - wrappers derive array Send from T Send, array Sync from T Sync, and push_error(T)
 *   capabilities structurally from its T payload;
 * - push_error(T) is a compiler-generated tagged value containing reason and the still-initialized
 *   staged T after R_STD_ARRAY_CALL_ERROR. It has no standalone runtime function or symbol;
 * - generated drop glue calls r_runtime_array_destroy exactly once for every live array owner.
 */

typedef enum RStdArrayCallStatus {
    R_STD_ARRAY_CALL_SUCCESS = 0,
    R_STD_ARRAY_CALL_ERROR = 1,
    R_STD_ARRAY_CALL_CONTRACT_VIOLATION = 2
} RStdArrayCallStatus;

typedef struct RStdArrayCreateResult {
    RStdArrayCallStatus status;
    RStdArray value;
} RStdArrayCreateResult;

typedef struct RStdArrayAllocValueResult {
    RStdArrayCallStatus status;
    RStdAllocError error;
    RStdArray value;
} RStdArrayAllocValueResult;

typedef struct RStdArrayAllocResult {
    RStdArrayCallStatus status;
    RStdAllocError error;
} RStdArrayAllocResult;

typedef struct RStdArrayPushResult {
    RStdArrayCallStatus status;
    RStdAllocError reason;
} RStdArrayPushResult;

typedef struct RStdArrayValueOptionResult {
    RStdArrayCallStatus status;
    _Bool has_value;
} RStdArrayValueOptionResult;

typedef struct RStdArrayConstPointerOption {
    RStdArrayCallStatus status;
    _Bool has_value;
    const void *value;
} RStdArrayConstPointerOption;

typedef struct RStdArrayMutPointerOption {
    RStdArrayCallStatus status;
    _Bool has_value;
    void *value;
} RStdArrayMutPointerOption;

typedef struct RStdArrayConstSliceResult {
    RStdArrayCallStatus status;
    const void *data;
    size_t length;
} RStdArrayConstSliceResult;

typedef struct RStdArrayMutSliceResult {
    RStdArrayCallStatus status;
    void *data;
    size_t length;
} RStdArrayMutSliceResult;

/*
 * Ownership: allocator and type metadata are shared call-bounded inputs retained by the array
 * descriptor. Success returns the sole empty array owner. No allocation is performed.
 */
RStdArrayCreateResult r_std_array_create(RRuntimeAllocator *allocator, RRuntimeTypeInfo element);

/*
 * Ownership: allocator and type metadata are shared call-bounded inputs retained by the result.
 * Success returns the sole array owner; failure exposes no partial allocation.
 */
RStdArrayAllocValueResult
r_std_array_with_capacity(RRuntimeAllocator *allocator, RRuntimeTypeInfo element, size_t capacity);

/*
 * R-LIB-0019 (P4.2). Ownership: allocator and type metadata are shared call-bounded inputs
 * retained by the result; value points to one initialized Copy T that is only read. Success
 * returns the sole owner of length copies of it in one allocation; failure exposes no partial
 * allocation.
 */
RStdArrayAllocValueResult r_std_array_filled(RRuntimeAllocator *allocator,
                                             RRuntimeTypeInfo element,
                                             size_t length,
                                             const void *value);

/* Ownership: source is a shared call-bounded borrow and is never retained. */
size_t r_std_array_capacity(const RStdArray *source);

/*
 * Ownership: target is an exclusive call-bounded borrow. Allocation failure is transactional and
 * preserves its descriptor, allocation, length, capacity, and every element.
 */
RStdArrayAllocResult r_std_array_reserve(RStdArray *target, size_t additional);

/*
 * Ownership: target is exclusive. staged_value points to one initialized T owned by the compiler
 * wrapper. Success move-initializes the appended element and consumes staged_value. Failure leaves
 * target and staged_value unchanged so the wrapper can construct push_error(T).
 */
RStdArrayPushResult r_std_array_push(RStdArray *target, void *staged_value);

/*
 * Ownership: target is exclusive. result_storage points to uninitialized aligned T storage.
 * has_value true move-initializes that storage; false leaves it untouched.
 */
RStdArrayValueOptionResult r_std_array_pop(RStdArray *target, void *result_storage);
RStdArrayValueOptionResult
r_std_array_remove(RStdArray *target, size_t index, void *result_storage);

/* Ownership: returned pointers are call-produced borrows rooted in source and are not retained. */
RStdArrayConstPointerOption r_std_array_get(const RStdArray *source, size_t index);
RStdArrayMutPointerOption r_std_array_get_mut(RStdArray *source, size_t index);

/* Ownership: returned slices borrow the exact contiguous live-element range of source. */
RStdArrayConstSliceResult r_std_array_as_slice(const RStdArray *source);
RStdArrayMutSliceResult r_std_array_as_slice_mut(RStdArray *source);

/*
 * Ownership: target is exclusive. Every live element is dropped in reverse index order. The
 * backing allocation and capacity are retained and no allocation occurs.
 */
void r_std_array_clear(RStdArray *target);

#endif
