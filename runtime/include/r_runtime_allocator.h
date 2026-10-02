#ifndef R_RUNTIME_ALLOCATOR_H
#define R_RUNTIME_ALLOCATOR_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum RRuntimeAllocationStatus {
    R_RUNTIME_ALLOCATION_OK = 0,
    R_RUNTIME_ALLOCATION_INVALID,
    R_RUNTIME_ALLOCATION_SIZE_OVERFLOW,
    R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT,
    R_RUNTIME_ALLOCATION_EXHAUSTED
} RRuntimeAllocationStatus;

#define R_RUNTIME_ALLOCATOR_MAX_ALIGNMENT ((size_t)UINT32_C(1073741824))
#if SIZE_MAX < UINT64_C(2305843009213693951)
#error "runtime allocator maximum object size is not representable"
#endif
#define R_RUNTIME_ALLOCATOR_MAX_OBJECT_SIZE ((size_t)UINT64_C(2305843009213693951))

typedef struct RRuntimeAllocator {
    _Atomic uint64_t attempt_count;
    _Atomic uint64_t fail_at_attempt;
} RRuntimeAllocator;

void r_runtime_allocator_initialize(RRuntimeAllocator *allocator);
void r_runtime_allocator_set_failure(RRuntimeAllocator *allocator, uint64_t attempt);
uint64_t r_runtime_allocator_attempt_count(const RRuntimeAllocator *allocator);

RRuntimeAllocationStatus r_runtime_allocator_allocate(RRuntimeAllocator *allocator,
                                                      size_t size,
                                                      size_t alignment,
                                                      void **result);
RRuntimeAllocationStatus r_runtime_allocator_reallocate(RRuntimeAllocator *allocator,
                                                        void *allocation,
                                                        size_t old_size,
                                                        size_t new_size,
                                                        size_t alignment,
                                                        void **result);
void r_runtime_allocator_deallocate(void *allocation, size_t alignment);

#ifdef __cplusplus
}
#endif

#endif
