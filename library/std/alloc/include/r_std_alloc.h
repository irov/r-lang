#ifndef R_STD_ALLOC_H
#define R_STD_ALLOC_H

#include "r_runtime_allocator.h"
#include "r_runtime_array.h"
#include "r_runtime_budget.h"
#include "r_runtime_own.h"
#include "r_runtime_type.h"

#include <stddef.h>
#include <stdint.h>

typedef enum RStdAllocError {
    R_STD_ALLOC_ERROR_OUT_OF_MEMORY = 0,
    R_STD_ALLOC_ERROR_SIZE_OVERFLOW = 1,
    R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT = 2,
    R_STD_ALLOC_ERROR_BUDGET_EXHAUSTED = 3
} RStdAllocError;

/* The alloc_error of a refusal of the allocator, read right after it on the same thread:
 * budget_exhausted when a budget refused (Core R-STMT-0020), out_of_memory otherwise. */
#define R_STD_ALLOC_REFUSAL()                                                                      \
    (r_runtime_allocation_refused_by_budget() ? R_STD_ALLOC_ERROR_BUDGET_EXHAUSTED                 \
                                              : R_STD_ALLOC_ERROR_OUT_OF_MEMORY)

/*
 * This status belongs to the compiler/runtime ABI, not to the public R outcome. A conforming
 * compiler wrapper raises the checked alloc_error effect for ALLOCATION_ERROR. Compiler-proven
 * preconditions are
 * internal assertions in checked builds and do not add a production status alternative.
 */
typedef enum RStdAllocCallStatus {
    R_STD_ALLOC_CALL_SUCCESS = 0,
    R_STD_ALLOC_CALL_ALLOCATION_ERROR = 1
} RStdAllocCallStatus;

typedef struct RStdAllocTryNewResult {
    RStdAllocCallStatus status;
    RStdAllocError error;
    RRuntimeOwn object;
} RStdAllocTryNewResult;

typedef struct RStdAllocBytesResult {
    RStdAllocCallStatus status;
    RStdAllocError error;
    RRuntimeArray value;
} RStdAllocBytesResult;

/*
 * Ownership: allocator and type are shared call-bounded runtime metadata. staged_value points to
 * one initialized T reserved exclusively by the compiler wrapper. Success move-initializes the
 * owned allocation and consumes that staged T. Allocation failure leaves staged_value initialized
 * and byte-for-byte untouched so the wrapper can construct new_error(T). Success retains only the
 * exact allocation base and the inline type/alignment metadata required for drop and deallocation;
 * it does not retain allocator object state.
 */
RStdAllocTryNewResult
r_std_alloc_try_new(RRuntimeAllocator *allocator, RRuntimeTypeInfo type, void *staged_value);

/*
 * Ownership: object is consumed and set empty on success. result_storage points to uninitialized,
 * suitably aligned storage for the same T. Success move-initializes it and releases the allocation
 * without invoking T drop. A conforming compiler passes a nonempty matching owner.
 */
RStdAllocCallStatus r_std_alloc_into_value(RRuntimeOwn *object, void *result_storage);

/*
 * Ownership: allocator is a shared runtime reference retained by the successful array. The result
 * owns its byte storage. An allocation error exposes no partial array.
 */
RStdAllocBytesResult r_std_alloc_bytes(RRuntimeAllocator *allocator, size_t length, uint8_t fill);

#endif
