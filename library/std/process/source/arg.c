#include "r_std_process.h"

#include "r_library_process_internal.h"

#include <stdint.h>

RStdProcessVoidResult r_std_process_arg(RStdProcessCommand *command, RStdStringView value) {
    RStdProcessVoidResult result = {0};
    RRuntimeString argument = {0};
    RLibraryProcessValidation validation;
    RRuntimeArrayStatus array_status;

    result.status = R_STD_PROCESS_CALL_SUCCESS;
    validation = r_library_internal_process_validate_argument(value);
    if (validation == R_LIBRARY_PROCESS_INVALID) {
        result.status = R_STD_PROCESS_CALL_ERROR;
        result.error =
            r_library_internal_process_error(R_STD_PROCESS_ERROR_INVALID_ARGUMENT, INT64_C(0));
        return result;
    }
    result.status = r_library_internal_process_copy_string(
        command->storage->allocator, value, &argument, &result.error);
    if (result.status != R_STD_PROCESS_CALL_SUCCESS) {
        return result;
    }
    array_status = r_runtime_array_push(&command->storage->arguments, &argument);
    result.status = r_library_internal_process_array_status(array_status, &result.error);
    r_runtime_string_destroy(&argument);
    return result;
}
