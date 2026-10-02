#include "r_std_fs.h"

#include "r_library_fs_internal.h"
#include "r_runtime_utf8.h"

RStdFsPathResult r_std_fs_path_from_utf8(RRuntimeAllocator *allocator, RStdStringView text) {
    RStdFsPathResult result = {0};
    RStdAllocError allocation_error = R_STD_ALLOC_ERROR_OUT_OF_MEMORY;
    size_t nul_index;

    nul_index = r_library_internal_fs_first_nul(text.data, text.length);
    if (nul_index != text.length) {
        result.status = R_STD_FS_CALL_ERROR;
        result.error.kind = R_STD_FS_PATH_ERROR_EMBEDDED_NUL;
        result.error.index = nul_index;
        return result;
    }
    result.status = r_library_internal_fs_path_create(
        allocator, text.data, text.length, &result.value, &allocation_error);
    if (result.status == R_STD_FS_CALL_ERROR) {
        result.error.kind = R_STD_FS_PATH_ERROR_ALLOCATION_FAILED;
        result.error.allocation_error = allocation_error;
    }
    return result;
}
