#include "r_std_fs.h"

#include "r_library_fs_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static RStdFsCallStatus r_std_fs_path_join_allocate(RRuntimeAllocator *allocator,
                                                    const uint8_t *base,
                                                    size_t base_length,
                                                    size_t separator_length,
                                                    const uint8_t *component,
                                                    size_t component_length,
                                                    RStdFsPath *result,
                                                    RStdAllocError *error) {
    RStdFsPathStorage *storage = NULL;
    RRuntimeAllocationStatus allocation_status;
    size_t joined_length = base_length + separator_length + component_length;
    size_t allocation_size;

    if (joined_length > (SIZE_MAX - sizeof(*storage) - 1U)) {
        *error = R_STD_ALLOC_ERROR_SIZE_OVERFLOW;
        return R_STD_FS_CALL_ERROR;
    }
    allocation_size = sizeof(*storage) + joined_length + 1U;
    allocation_status = r_runtime_allocator_allocate(
        allocator, allocation_size, _Alignof(max_align_t), (void **)&storage);
    if (allocation_status == R_RUNTIME_ALLOCATION_EXHAUSTED) {
        *error = R_STD_ALLOC_REFUSAL();
        return R_STD_FS_CALL_ERROR;
    }
    if (allocation_status == R_RUNTIME_ALLOCATION_SIZE_OVERFLOW) {
        *error = R_STD_ALLOC_ERROR_SIZE_OVERFLOW;
        return R_STD_FS_CALL_ERROR;
    }
    if (allocation_status == R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT) {
        *error = R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT;
        return R_STD_FS_CALL_ERROR;
    }
    storage->allocator = allocator;
    storage->length = joined_length;
    if (base_length != 0U) {
        (void)memcpy(storage->bytes, base, base_length);
    }
    if (separator_length != 0U) {
        storage->bytes[base_length] = UINT8_C('/');
    }
    if (component_length != 0U) {
        (void)memcpy(storage->bytes + base_length + separator_length, component, component_length);
    }
    storage->bytes[joined_length] = UINT8_C(0);
    result->storage = storage;
    return R_STD_FS_CALL_SUCCESS;
}

RStdFsPathResult r_std_fs_path_join(const RStdFsPath *base, const RStdFsPath *component) {
    RStdFsPathResult result = {0};
    RStdAllocError allocation_error = R_STD_ALLOC_ERROR_OUT_OF_MEMORY;
    const uint8_t *base_bytes;
    const uint8_t *component_bytes;
    RRuntimeAllocator *allocator;
    size_t base_length;
    size_t component_length;
    size_t separator_length;

    base_bytes = base->storage->bytes;
    component_bytes = component->storage->bytes;
    base_length = base->storage->length;
    component_length = component->storage->length;
    if ((component_length != 0U) && (component_bytes[0] == UINT8_C('/'))) {
        result.status = R_STD_FS_CALL_ERROR;
        result.error.kind = R_STD_FS_PATH_ERROR_ABSOLUTE_COMPONENT;
        return result;
    }
    separator_length = ((base_length != 0U) && (component_length != 0U) &&
                        (base_bytes[base_length - 1U] != UINT8_C('/')))
                           ? 1U
                           : 0U;
    if ((component_length > (SIZE_MAX - base_length)) ||
        (separator_length > (SIZE_MAX - base_length - component_length))) {
        result.status = R_STD_FS_CALL_ERROR;
        result.error.kind = R_STD_FS_PATH_ERROR_ALLOCATION_FAILED;
        result.error.allocation_error = R_STD_ALLOC_ERROR_SIZE_OVERFLOW;
        return result;
    }
    allocator = ((base_length == 0U) && (component_length != 0U)) ? component->storage->allocator
                                                                  : base->storage->allocator;
    result.status = r_std_fs_path_join_allocate(allocator,
                                                base_bytes,
                                                base_length,
                                                separator_length,
                                                component_bytes,
                                                component_length,
                                                &result.value,
                                                &allocation_error);
    if (result.status == R_STD_FS_CALL_ERROR) {
        result.error.kind = R_STD_FS_PATH_ERROR_ALLOCATION_FAILED;
        result.error.allocation_error = allocation_error;
    }
    return result;
}
