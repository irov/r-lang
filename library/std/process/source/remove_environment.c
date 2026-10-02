#include "r_std_process.h"

#include "r_library_process_internal.h"

#include <stdint.h>

RStdProcessVoidResult r_std_process_remove_environment(RStdProcessCommand *command,
                                                       RStdStringView name) {
    RStdProcessVoidResult result = {0};
    RRuntimeString owned_name = {0};
    RRuntimeString removed_value = {0};
    RLibraryProcessValidation validation;
    _Bool did_remove;

    result.status = R_STD_PROCESS_CALL_SUCCESS;
    validation = r_library_internal_process_validate_environment_name(name);
    if (validation == R_LIBRARY_PROCESS_INVALID) {
        result.status = R_STD_PROCESS_CALL_ERROR;
        result.error =
            r_library_internal_process_error(R_STD_PROCESS_ERROR_INVALID_ARGUMENT, INT64_C(0));
        return result;
    }
    result.status = r_library_internal_process_copy_string(
        command->storage->allocator, name, &owned_name, &result.error);
    if (result.status != R_STD_PROCESS_CALL_SUCCESS) {
        return result;
    }
    did_remove = r_runtime_dict_remove(&command->storage->environment, &owned_name, &removed_value);
    if (did_remove) {
        r_runtime_string_destroy(&removed_value);
    }
    r_runtime_string_destroy(&owned_name);
    result.status = R_STD_PROCESS_CALL_SUCCESS;
    return result;
}
