#include "r_std_process.h"

#include "r_library_process_internal.h"

#include <stdint.h>

RStdProcessVoidResult
r_std_process_environment(RStdProcessCommand *command, RStdStringView name, RStdStringView value) {
    RStdProcessVoidResult result = {0};
    RRuntimeString owned_name = {0};
    RRuntimeString owned_value = {0};
    RRuntimeString replaced_value = {0};
    RLibraryProcessValidation validation;
    RRuntimeDictStatus dict_status;
    _Bool did_replace = 0;

    result.status = R_STD_PROCESS_CALL_SUCCESS;
    validation = r_library_internal_process_validate_environment_name(name);
    if (validation == R_LIBRARY_PROCESS_INVALID) {
        result.status = R_STD_PROCESS_CALL_ERROR;
        result.error =
            r_library_internal_process_error(R_STD_PROCESS_ERROR_INVALID_ARGUMENT, INT64_C(0));
        return result;
    }
    validation = r_library_internal_process_validate_environment_value(value);
    if (validation == R_LIBRARY_PROCESS_INVALID) {
        result.status = R_STD_PROCESS_CALL_ERROR;
        result.error =
            r_library_internal_process_error(R_STD_PROCESS_ERROR_INVALID_ARGUMENT, INT64_C(0));
        return result;
    }

    result.status = r_library_internal_process_copy_string(
        command->storage->allocator, name, &owned_name, &result.error);
    if (result.status != R_STD_PROCESS_CALL_SUCCESS) {
        goto cleanup;
    }
    result.status = r_library_internal_process_copy_string(
        command->storage->allocator, value, &owned_value, &result.error);
    if (result.status != R_STD_PROCESS_CALL_SUCCESS) {
        goto cleanup;
    }
    dict_status = r_runtime_dict_insert(
        &command->storage->environment, &owned_name, &owned_value, &replaced_value, &did_replace);
    result.status = r_library_internal_process_dict_status(dict_status, &result.error);

cleanup:
    if (did_replace) {
        r_runtime_string_destroy(&replaced_value);
    }
    r_runtime_string_destroy(&owned_value);
    r_runtime_string_destroy(&owned_name);
    return result;
}
