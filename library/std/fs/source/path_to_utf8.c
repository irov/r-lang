#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsStringResult r_std_fs_path_to_utf8(const RStdFsPath *source) {
    RStdFsStringResult result = {0};
    RStdStringAllocValueResult converted;

    converted =
        r_std_string_from_str(source->storage->allocator,
                              (RStdStringView){source->storage->bytes, source->storage->length});
    if (converted.status == R_STD_STRING_CALL_SUCCESS) {
        result.status = R_STD_FS_CALL_SUCCESS;
        result.value = converted.value;
        return result;
    }
    result.status = R_STD_FS_CALL_ERROR;
    result.error.kind = R_STD_FS_PATH_ERROR_ALLOCATION_FAILED;
    result.error.allocation_error = converted.error;
    return result;
}
