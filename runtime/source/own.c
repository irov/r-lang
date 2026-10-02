#include "r_runtime_own.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void r_runtime_own_move(RRuntimeTypeInfo type, void *destination, void *source) {
    if (type.move_initialize != NULL) {
        type.move_initialize(destination, source);
    } else {
        (void)memcpy(destination, source, type.size);
    }
}

RRuntimeOwnStatus r_runtime_own_create(RRuntimeAllocator *allocator,
                                       RRuntimeTypeInfo type,
                                       void *value,
                                       RRuntimeOwn *result) {
    void *memory = NULL;
    RRuntimeAllocationStatus status;

    *result = (RRuntimeOwn){0};
    status = r_runtime_allocator_allocate(allocator, type.size, type.alignment, &memory);
    if (status == R_RUNTIME_ALLOCATION_EXHAUSTED) {
        return R_RUNTIME_OWN_ALLOCATION_FAILED;
    }
    if (status == R_RUNTIME_ALLOCATION_SIZE_OVERFLOW) {
        return R_RUNTIME_OWN_SIZE_OVERFLOW;
    }
    if (status == R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT) {
        return R_RUNTIME_OWN_UNSUPPORTED_ALIGNMENT;
    }
    r_runtime_own_move(type, memory, value);
    result->allocation = memory;
    result->type = type;
    result->allocation_alignment = type.alignment;
    return R_RUNTIME_OWN_OK;
}

RRuntimeOwnStatus r_runtime_own_create_initialize(RRuntimeAllocator *allocator,
                                                  RRuntimeTypeInfo type,
                                                  RRuntimeOwnInitializeFn initialize,
                                                  const void *context,
                                                  RRuntimeOwn *result) {
    void *memory = NULL;
    RRuntimeAllocationStatus status;

    *result = (RRuntimeOwn){0};
    status = r_runtime_allocator_allocate(allocator, type.size, type.alignment, &memory);
    if (status == R_RUNTIME_ALLOCATION_EXHAUSTED) {
        return R_RUNTIME_OWN_ALLOCATION_FAILED;
    }
    if (status == R_RUNTIME_ALLOCATION_SIZE_OVERFLOW) {
        return R_RUNTIME_OWN_SIZE_OVERFLOW;
    }
    if (status == R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT) {
        return R_RUNTIME_OWN_UNSUPPORTED_ALIGNMENT;
    }
    initialize(memory, context);
    result->allocation = memory;
    result->type = type;
    result->allocation_alignment = type.alignment;
    return R_RUNTIME_OWN_OK;
}

RRuntimeOwnStatus
r_runtime_own_adopt(RRuntimeTypeInfo type, void *allocation, RRuntimeOwn *result) {
    *result = (RRuntimeOwn){0};
    if (type.size > R_RUNTIME_ALLOCATOR_MAX_OBJECT_SIZE) {
        return R_RUNTIME_OWN_SIZE_OVERFLOW;
    }
    if (type.alignment > R_RUNTIME_ALLOCATOR_MAX_ALIGNMENT) {
        return R_RUNTIME_OWN_UNSUPPORTED_ALIGNMENT;
    }
    result->allocation = allocation;
    result->type = type;
    result->allocation_alignment = type.alignment;
    return R_RUNTIME_OWN_OK;
}

const void *r_runtime_own_get(const RRuntimeOwn *owner) {
    return owner->allocation;
}

void *r_runtime_own_get_mut(RRuntimeOwn *owner) {
    return owner->allocation;
}

RRuntimeOwnStatus r_runtime_own_into_value(RRuntimeOwn *owner, void *result) {
    void *allocation;
    RRuntimeTypeInfo type;
    size_t alignment;

    allocation = owner->allocation;
    type = owner->type;
    alignment = owner->allocation_alignment;
    *owner = (RRuntimeOwn){0};
    r_runtime_own_move(type, result, allocation);
    r_runtime_allocator_deallocate(allocation, alignment);
    return R_RUNTIME_OWN_OK;
}

RRuntimeOwnStatus r_runtime_own_into_raw(RRuntimeOwn *owner, void **result) {
    void *allocation;

    allocation = owner->allocation;
    *owner = (RRuntimeOwn){0};
    *result = allocation;
    return R_RUNTIME_OWN_OK;
}

void r_runtime_own_release(RRuntimeOwn *owner) {
    void *allocation;
    RRuntimeDropFn drop;
    size_t alignment;

    if (owner->allocation == NULL) {
        return;
    }
    allocation = owner->allocation;
    drop = owner->type.drop;
    alignment = owner->allocation_alignment;
    *owner = (RRuntimeOwn){0};
    if (drop != NULL) {
        drop(allocation);
    }
    r_runtime_allocator_deallocate(allocation, alignment);
}
