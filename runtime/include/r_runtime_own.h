#ifndef R_RUNTIME_OWN_H
#define R_RUNTIME_OWN_H

#include "r_runtime_allocator.h"
#include "r_runtime_type.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RRuntimeOwn {
    /* Exact base address of the owned T allocation, or NULL for an empty owner. */
    void *allocation;
    RRuntimeTypeInfo type;
    size_t allocation_alignment;
} RRuntimeOwn;

typedef enum RRuntimeOwnStatus {
    R_RUNTIME_OWN_OK = 0,
    R_RUNTIME_OWN_INVALID,
    R_RUNTIME_OWN_ALLOCATION_FAILED,
    R_RUNTIME_OWN_SIZE_OVERFLOW,
    R_RUNTIME_OWN_UNSUPPORTED_ALIGNMENT
} RRuntimeOwnStatus;

typedef void (*RRuntimeOwnInitializeFn)(void *destination, const void *context);

RRuntimeOwnStatus r_runtime_own_create(RRuntimeAllocator *allocator,
                                       RRuntimeTypeInfo type,
                                       void *value,
                                       RRuntimeOwn *result);
/*
 * Allocates storage for T and invokes initialize exactly once in that storage after the last
 * recoverable failure. Allocation or validation failure leaves result empty and does not invoke
 * initialize. The callback is infallible and shall completely initialize one valid T value before
 * returning; context remains owned by the caller.
 */
RRuntimeOwnStatus r_runtime_own_create_initialize(RRuntimeAllocator *allocator,
                                                  RRuntimeTypeInfo type,
                                                  RRuntimeOwnInitializeFn initialize,
                                                  const void *context,
                                                  RRuntimeOwn *result);
/*
 * Takes an exact base pointer produced by the compatible runtime allocator. This operation does
 * not allocate, move, copy, or drop T. The caller transfers both T's drop duty and the allocation
 * deallocation duty on success.
 */
RRuntimeOwnStatus r_runtime_own_adopt(RRuntimeTypeInfo type, void *allocation, RRuntimeOwn *result);
const void *r_runtime_own_get(const RRuntimeOwn *owner);
void *r_runtime_own_get_mut(RRuntimeOwn *owner);
RRuntimeOwnStatus r_runtime_own_into_value(RRuntimeOwn *owner, void *result);
/* Consumes owner without dropping or deallocating T and returns the same exact base pointer. */
RRuntimeOwnStatus r_runtime_own_into_raw(RRuntimeOwn *owner, void **result);
void r_runtime_own_release(RRuntimeOwn *owner);

#ifdef __cplusplus
}
#endif

#endif
