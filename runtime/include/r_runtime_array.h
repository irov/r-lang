#ifndef R_RUNTIME_ARRAY_H
#define R_RUNTIME_ARRAY_H

#include "r_runtime_allocator.h"
#include "r_runtime_type.h"

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RRuntimeArrayStatus {
    R_RUNTIME_ARRAY_OK = 0,
    R_RUNTIME_ARRAY_INVALID,
    R_RUNTIME_ARRAY_ALLOCATION_FAILED,
    R_RUNTIME_ARRAY_SIZE_OVERFLOW,
    R_RUNTIME_ARRAY_UNSUPPORTED_ALIGNMENT
} RRuntimeArrayStatus;

typedef struct RRuntimeArray {
    RRuntimeAllocator *allocator;
    RRuntimeTypeInfo element;
    void *data;
    size_t length;
    size_t capacity;
} RRuntimeArray;

void r_runtime_array_initialize(RRuntimeArray *array,
                                RRuntimeAllocator *allocator,
                                RRuntimeTypeInfo element);
RRuntimeArrayStatus r_runtime_array_with_capacity(RRuntimeArray *array,
                                                  RRuntimeAllocator *allocator,
                                                  RRuntimeTypeInfo element,
                                                  size_t capacity);
RRuntimeArrayStatus r_runtime_array_reserve(RRuntimeArray *array, size_t additional);
RRuntimeArrayStatus r_runtime_array_push(RRuntimeArray *array, void *value);
_Bool r_runtime_array_pop(RRuntimeArray *array, void *result);
_Bool r_runtime_array_remove(RRuntimeArray *array, size_t index, void *result);
const void *r_runtime_array_get(const RRuntimeArray *array, size_t index);
void *r_runtime_array_get_mut(RRuntimeArray *array, size_t index);
void r_runtime_array_clear(RRuntimeArray *array);
void r_runtime_array_destroy(RRuntimeArray *array);

/*
 * Iterative-destruction protocol used by r_runtime_drop_iterative. Elements are destroyed by the
 * caller from the last index down; stride is the element size taken from the descriptor, since
 * the header's element size and alignment words serve as traversal scratch (scratch) between
 * the first element and finish. locate maps an element address back to its index. finish
 * releases the buffer with the given alignment and leaves the array empty.
 * Ownership: the caller destroys every element before finish.
 */
size_t r_runtime_array_destroy_count(const RRuntimeArray *array);
void *r_runtime_array_destroy_element(RRuntimeArray *array, size_t index, size_t stride);
_Bool r_runtime_array_destroy_locate(const RRuntimeArray *array,
                                     const void *element,
                                     size_t stride,
                                     size_t *index);
RRuntimeTypeInfo *r_runtime_array_destroy_scratch(RRuntimeArray *array);
void r_runtime_array_destroy_finish(RRuntimeArray *array, size_t alignment);

#ifdef __cplusplus
}
#endif

#endif
