#include "r_std_process.h"

#include "r_library_fs_internal.h"
#include "r_library_process_internal.h"
#include "r_std_env.h"

#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static RStdProcessError r_std_process_environment_error(RStdEnvError error) {
    switch (error.code) {
    case R_STD_ENV_ERROR_INVALID_NAME:
    case R_STD_ENV_ERROR_INVALID_VALUE:
        return r_library_internal_process_error(R_STD_PROCESS_ERROR_INVALID_ARGUMENT,
                                                error.native_code);
    case R_STD_ENV_ERROR_ALLOCATION_FAILED:
        return r_library_internal_process_allocation_error();
    case R_STD_ENV_ERROR_PERMISSION_DENIED:
        return r_library_internal_process_error(R_STD_PROCESS_ERROR_PERMISSION_DENIED,
                                                error.native_code);
    case R_STD_ENV_ERROR_RESOURCE_EXHAUSTED:
        return r_library_internal_process_error(R_STD_PROCESS_ERROR_RESOURCE_EXHAUSTED,
                                                error.native_code);
    case R_STD_ENV_ERROR_OTHER:
        return r_library_internal_process_error(R_STD_PROCESS_ERROR_OTHER, error.native_code);
    }
    return r_library_internal_process_error(R_STD_PROCESS_ERROR_OTHER, error.native_code);
}

static RStdProcessError r_std_process_current_directory_error(int native_code) {
    if ((native_code == EACCES) || (native_code == EPERM)) {
        return r_library_internal_process_error(R_STD_PROCESS_ERROR_PERMISSION_DENIED,
                                                (int64_t)native_code);
    }
    if ((native_code == EAGAIN) || (native_code == EMFILE) || (native_code == ENFILE) ||
        (native_code == ENOMEM) || (native_code == ENOSPC)) {
        return r_library_internal_process_error(R_STD_PROCESS_ERROR_RESOURCE_EXHAUSTED,
                                                (int64_t)native_code);
    }
    if ((native_code == ENOENT) || (native_code == ENOTDIR)) {
        return r_library_internal_process_error(R_STD_PROCESS_ERROR_NOT_FOUND,
                                                (int64_t)native_code);
    }
    return r_library_internal_process_error(R_STD_PROCESS_ERROR_OTHER, (int64_t)native_code);
}

static RStdProcessCallStatus r_std_process_capture_current_directory(int *result,
                                                                     RStdProcessError *error) {
    int descriptor;
    int descriptor_flags;
    int native_code;

    descriptor = open(".", O_RDONLY | O_DIRECTORY);
    if (descriptor < 0) {
        native_code = errno;
        *error = r_std_process_current_directory_error(native_code);
        return R_STD_PROCESS_CALL_ERROR;
    }
    descriptor_flags = fcntl(descriptor, F_GETFD);
    if (descriptor_flags < 0) {
        native_code = errno;
        (void)close(descriptor);
        *error = r_std_process_current_directory_error(native_code);
        return R_STD_PROCESS_CALL_ERROR;
    }
    if (fcntl(descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) < 0) {
        native_code = errno;
        (void)close(descriptor);
        *error = r_std_process_current_directory_error(native_code);
        return R_STD_PROCESS_CALL_ERROR;
    }
    *result = descriptor;
    return R_STD_PROCESS_CALL_SUCCESS;
}

RStdProcessCommandResult r_std_process_command_create(RRuntimeAllocator *allocator,
                                                      const RStdFsPath *executable) {
    RStdProcessCommandResult result = {0};
    RStdProcessCommandStorage *storage = NULL;
    RRuntimeString first_argument = {0};
    RStdEnvVariablesResult environment_snapshot = {0};
    RRuntimeAllocationStatus allocation_status;
    RRuntimeArrayStatus array_status;
    RStdStringView executable_view;

    result.status = R_STD_PROCESS_CALL_SUCCESS;
    executable_view.data = executable->storage->bytes;
    executable_view.length = executable->storage->length;
    if (executable_view.length == 0U) {
        result.status = R_STD_PROCESS_CALL_ERROR;
        result.error =
            r_library_internal_process_error(R_STD_PROCESS_ERROR_INVALID_COMMAND, INT64_C(0));
        return result;
    }
    allocation_status = r_runtime_allocator_allocate(
        allocator, sizeof(*storage), _Alignof(RStdProcessCommandStorage), (void **)&storage);
    if (allocation_status == R_RUNTIME_ALLOCATION_EXHAUSTED) {
        result.status = R_STD_PROCESS_CALL_ERROR;
        result.error = r_library_internal_process_allocation_error();
        return result;
    }
    (void)memset(storage, 0, sizeof(*storage));
    storage->allocator = allocator;
    storage->current_directory = -1;
    storage->stdio.input = R_STD_PROCESS_PIPE_INHERIT;
    storage->stdio.output = R_STD_PROCESS_PIPE_INHERIT;
    storage->stdio.error = R_STD_PROCESS_PIPE_INHERIT;
    r_runtime_array_initialize(
        &storage->arguments, allocator, r_library_internal_process_string_type());

    result.status = r_library_internal_process_copy_path(
        allocator, executable, &storage->executable, &result.error);
    if (result.status != R_STD_PROCESS_CALL_SUCCESS) {
        goto cleanup;
    }
    result.status = r_library_internal_process_copy_string(
        allocator, executable_view, &first_argument, &result.error);
    if (result.status != R_STD_PROCESS_CALL_SUCCESS) {
        goto cleanup;
    }
    array_status = r_runtime_array_push(&storage->arguments, &first_argument);
    result.status = r_library_internal_process_array_status(array_status, &result.error);
    if (result.status != R_STD_PROCESS_CALL_SUCCESS) {
        goto cleanup;
    }

    environment_snapshot = r_std_env_variables(allocator, UINT64_C(0x72a1d4b39e5c086f));
    if (environment_snapshot.status == R_STD_ENV_CALL_ERROR) {
        result.status = R_STD_PROCESS_CALL_ERROR;
        result.error = r_std_process_environment_error(environment_snapshot.error);
        goto cleanup;
    }
    storage->environment = environment_snapshot.value;
    (void)memset(&environment_snapshot.value, 0, sizeof(environment_snapshot.value));

    result.status =
        r_std_process_capture_current_directory(&storage->current_directory, &result.error);
    if (result.status != R_STD_PROCESS_CALL_SUCCESS) {
        goto cleanup;
    }
    result.value.storage = storage;
    storage = NULL;

cleanup:
    r_runtime_string_destroy(&first_argument);
    if (storage != NULL) {
        RStdProcessCommand partial = {storage};

        r_library_internal_process_command_destroy(&partial);
    }
    return result;
}
