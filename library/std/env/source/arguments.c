#include "r_std_env.h"

#include "r_library_environment_internal.h"
#include "r_runtime_0_1.h"
#include "r_runtime_array.h"
#include "r_runtime_string.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static RStdEnvCallStatus r_std_env_arguments_string_status(RRuntimeStringStatus status,
                                                           RStdEnvError *error) {
    if (status == R_RUNTIME_STRING_OK) {
        return R_STD_ENV_CALL_SUCCESS;
    }
    if ((status == R_RUNTIME_STRING_ALLOCATION_FAILED) ||
        (status == R_RUNTIME_STRING_SIZE_OVERFLOW) ||
        (status == R_RUNTIME_STRING_UNSUPPORTED_ALIGNMENT)) {
        error->code = R_STD_ENV_ERROR_ALLOCATION_FAILED;
        error->native_code = INT64_C(0);
        return R_STD_ENV_CALL_ERROR;
    }
    return R_STD_ENV_CALL_SUCCESS;
}

static RStdEnvCallStatus r_std_env_arguments_array_status(RRuntimeArrayStatus status,
                                                          RStdEnvError *error) {
    if (status == R_RUNTIME_ARRAY_OK) {
        return R_STD_ENV_CALL_SUCCESS;
    }
    if ((status == R_RUNTIME_ARRAY_ALLOCATION_FAILED) ||
        (status == R_RUNTIME_ARRAY_SIZE_OVERFLOW) ||
        (status == R_RUNTIME_ARRAY_UNSUPPORTED_ALIGNMENT)) {
        error->code = R_STD_ENV_ERROR_ALLOCATION_FAILED;
        error->native_code = INT64_C(0);
        return R_STD_ENV_CALL_ERROR;
    }
    return R_STD_ENV_CALL_SUCCESS;
}

RStdEnvArgumentsResult r_std_env_arguments(RRuntimeAllocator *allocator) {
    RStdEnvArgumentsResult result = {0};
    RRuntimeArgumentSnapshotView snapshot = {0};
    RRuntimeString staged = {0};
    RRuntimeArrayStatus array_status;
    size_t argument_index;
    size_t offset = 0U;
    _Bool locked = 0;
    _Bool staged_ready = 0;
    _Bool snapshot_ready;

    result.status = R_STD_ENV_CALL_CONTRACT_VIOLATION;
    if (!r_library_internal_environment_acquire()) {
        return result;
    }
    locked = 1;
    snapshot_ready = r_runtime_hosted_argument_snapshot(&snapshot);
    (void)snapshot_ready;
    array_status = r_runtime_array_with_capacity(
        &result.value, allocator, r_library_internal_environment_string_type(), snapshot.count);
    result.status = r_std_env_arguments_array_status(array_status, &result.error);
    if (result.status != R_STD_ENV_CALL_SUCCESS) {
        goto cleanup;
    }
    for (argument_index = 0U; argument_index < snapshot.count; ++argument_index) {
        const uint8_t *terminator;
        size_t remaining;
        size_t length;
        RRuntimeStringStatus string_status;

        remaining = snapshot.byte_length - offset;
        terminator = memchr(snapshot.data + offset, 0, remaining);
        length = (size_t)(terminator - (snapshot.data + offset));
        string_status =
            r_runtime_string_from_valid_utf8(&staged, allocator, snapshot.data + offset, length);
        result.status = r_std_env_arguments_string_status(string_status, &result.error);
        if (result.status != R_STD_ENV_CALL_SUCCESS) {
            goto cleanup;
        }
        staged_ready = 1;
        array_status = r_runtime_array_push(&result.value, &staged);
        result.status = r_std_env_arguments_array_status(array_status, &result.error);
        if (result.status != R_STD_ENV_CALL_SUCCESS) {
            goto cleanup;
        }
        staged_ready = 0;
        offset += length + 1U;
    }
    result.status = R_STD_ENV_CALL_SUCCESS;
    if (!r_library_internal_environment_release()) {
        result.status = R_STD_ENV_CALL_CONTRACT_VIOLATION;
        locked = 0;
        goto cleanup;
    }
    return result;

cleanup:
    if (staged_ready) {
        r_runtime_string_destroy(&staged);
    }
    r_runtime_array_destroy(&result.value);
    (void)memset(&result.value, 0, sizeof(result.value));
    if (locked && !r_library_internal_environment_release()) {
        result.status = R_STD_ENV_CALL_CONTRACT_VIOLATION;
    }
    return result;
}
