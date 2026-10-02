#include "r_library_fs_internal.h"

#include "r_runtime_darwin_fs_lane.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static RStdFsCallStatus r_library_internal_fs_allocation_status(RRuntimeAllocationStatus status,
                                                                RStdAllocError *error) {
    if (status == R_RUNTIME_ALLOCATION_OK) {
        return R_STD_FS_CALL_SUCCESS;
    }
    if (status == R_RUNTIME_ALLOCATION_EXHAUSTED) {
        *error = R_STD_ALLOC_REFUSAL();
        return R_STD_FS_CALL_ERROR;
    }
    if (status == R_RUNTIME_ALLOCATION_SIZE_OVERFLOW) {
        *error = R_STD_ALLOC_ERROR_SIZE_OVERFLOW;
        return R_STD_FS_CALL_ERROR;
    }
    *error = R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT;
    return R_STD_FS_CALL_ERROR;
}

RStdFsCallStatus r_library_internal_fs_path_create(RRuntimeAllocator *allocator,
                                                   const uint8_t *bytes,
                                                   size_t length,
                                                   RStdFsPath *result,
                                                   RStdAllocError *error) {
    RStdFsPathStorage *storage = NULL;
    RRuntimeAllocationStatus allocation_status;
    RStdFsCallStatus status;
    size_t allocation_size;

    result->storage = NULL;
    if (length > (SIZE_MAX - sizeof(*storage) - 1U)) {
        *error = R_STD_ALLOC_ERROR_SIZE_OVERFLOW;
        return R_STD_FS_CALL_ERROR;
    }
    allocation_size = sizeof(*storage) + length + 1U;
    allocation_status = r_runtime_allocator_allocate(
        allocator, allocation_size, _Alignof(max_align_t), (void **)&storage);
    status = r_library_internal_fs_allocation_status(allocation_status, error);
    if (status != R_STD_FS_CALL_SUCCESS) {
        return status;
    }
    storage->allocator = allocator;
    storage->length = length;
    if (length != 0U) {
        (void)memcpy(storage->bytes, bytes, length);
    }
    storage->bytes[length] = UINT8_C(0);
    result->storage = storage;
    return R_STD_FS_CALL_SUCCESS;
}

const uint8_t *r_library_internal_fs_path_bytes(const RStdFsPath *path) {
    return path->storage->bytes;
}

size_t r_library_internal_fs_path_length(const RStdFsPath *path) {
    return path->storage->length;
}

RRuntimeAllocator *r_library_internal_fs_path_allocator(const RStdFsPath *path) {
    return path->storage->allocator;
}

_Bool r_library_internal_fs_path_has_reserved_component(const RStdFsPath *path) {
    const uint8_t *bytes;
    size_t length;
    size_t component_start = 0U;
    size_t index;

    bytes = path->storage->bytes;
    length = path->storage->length;
    for (index = 0U; index <= length; ++index) {
        if ((index == length) || (bytes[index] == UINT8_C('/'))) {
            const size_t component_length = index - component_start;

            if (component_length >= sizeof(R_RUNTIME_DARWIN_FS_STAGING_PREFIX) - 1U &&
                memcmp(bytes + component_start,
                       R_RUNTIME_DARWIN_FS_STAGING_PREFIX,
                       sizeof(R_RUNTIME_DARWIN_FS_STAGING_PREFIX) - 1U) == 0) {
                return 1;
            }
            component_start = index + 1U;
        }
    }
    return 0;
}

size_t r_library_internal_fs_first_nul(const uint8_t *bytes, size_t length) {
    size_t index;

    for (index = 0U; index < length; ++index) {
        if (bytes[index] == UINT8_C(0)) {
            return index;
        }
    }
    return length;
}

RLibraryFsRelativePathStatus
r_library_internal_fs_validate_beneath_relative(const RStdFsPath *path) {
    const uint8_t *bytes;
    size_t length;
    size_t component_start = 0U;
    size_t index;

    bytes = path->storage->bytes;
    length = path->storage->length;
    if (length == 0U) {
        return R_LIBRARY_FS_RELATIVE_PATH_EMPTY;
    }
    if (bytes[0] == UINT8_C('/')) {
        return R_LIBRARY_FS_RELATIVE_PATH_ABSOLUTE;
    }
    for (index = 0U; index <= length; ++index) {
        if ((index == length) || (bytes[index] == UINT8_C('/'))) {
            size_t component_length = index - component_start;

            if (component_length == 0U) {
                return R_LIBRARY_FS_RELATIVE_PATH_EMPTY_COMPONENT;
            }
            if ((component_length == 1U) && (bytes[component_start] == UINT8_C('.'))) {
                return R_LIBRARY_FS_RELATIVE_PATH_CURRENT_COMPONENT;
            }
            if ((component_length == 2U) && (bytes[component_start] == UINT8_C('.')) &&
                (bytes[component_start + 1U] == UINT8_C('.'))) {
                return R_LIBRARY_FS_RELATIVE_PATH_PARENT_COMPONENT;
            }
            if (component_length >= sizeof(R_RUNTIME_DARWIN_FS_STAGING_PREFIX) - 1U &&
                memcmp(bytes + component_start,
                       R_RUNTIME_DARWIN_FS_STAGING_PREFIX,
                       sizeof(R_RUNTIME_DARWIN_FS_STAGING_PREFIX) - 1U) == 0) {
                return R_LIBRARY_FS_RELATIVE_PATH_RESERVED_COMPONENT;
            }
            component_start = index + 1U;
        }
    }
    return R_LIBRARY_FS_RELATIVE_PATH_VALID;
}
