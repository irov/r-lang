#include "r_std_process.h"

#include "r_library_fs_internal.h"
#include "r_library_process_internal.h"

#include <stdint.h>

RStdProcessVoidResult r_std_process_working_directory(RStdProcessCommand *command,
                                                      const RStdFsPath *path) {
    RStdProcessVoidResult result = {0};
    RStdFsPath replacement = {0};

    result.status = R_STD_PROCESS_CALL_SUCCESS;
    if (path->storage->length == 0U) {
        result.status = R_STD_PROCESS_CALL_ERROR;
        result.error =
            r_library_internal_process_error(R_STD_PROCESS_ERROR_INVALID_COMMAND, INT64_C(0));
        return result;
    }
    result.status = r_library_internal_process_copy_path(
        command->storage->allocator, path, &replacement, &result.error);
    if (result.status != R_STD_PROCESS_CALL_SUCCESS) {
        return result;
    }
    if (command->storage->has_working_directory) {
        r_std_fs_path_destroy(&command->storage->working_directory);
    }
    command->storage->working_directory = replacement;
    command->storage->has_working_directory = 1;
    return result;
}
