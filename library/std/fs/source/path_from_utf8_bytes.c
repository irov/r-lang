#include "r_std_fs.h"

#include "r_library_fs_internal.h"
#include "r_runtime_utf8.h"

RStdFsPathResult r_std_fs_path_from_utf8_bytes(RRuntimeAllocator *allocator, RStdStringView bytes) {
    RStdFsPathResult result = {0};
    RStdAllocError allocation_error = R_STD_ALLOC_ERROR_OUT_OF_MEMORY;
    size_t invalid_index = 0U;
    size_t nul_index;
    _Bool valid_utf8;

    valid_utf8 = r_runtime_utf8_validate(bytes.data, bytes.length, &invalid_index);
    nul_index = r_library_internal_fs_first_nul(bytes.data, bytes.length);
    if (!valid_utf8 && ((nul_index == bytes.length) || (invalid_index < nul_index))) {
        result.status = R_STD_FS_CALL_ERROR;
        result.error.kind = R_STD_FS_PATH_ERROR_INVALID_UTF8;
        result.error.index = invalid_index;
        return result;
    }
    if (nul_index != bytes.length) {
        result.status = R_STD_FS_CALL_ERROR;
        result.error.kind = R_STD_FS_PATH_ERROR_EMBEDDED_NUL;
        result.error.index = nul_index;
        return result;
    }
    result.status = r_library_internal_fs_path_create(
        allocator, bytes.data, bytes.length, &result.value, &allocation_error);
    if (result.status == R_STD_FS_CALL_ERROR) {
        result.error.kind = R_STD_FS_PATH_ERROR_ALLOCATION_FAILED;
        result.error.allocation_error = allocation_error;
    }
    return result;
}
