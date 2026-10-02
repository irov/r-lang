#include "r_library_environment_internal.h"

#include "r_runtime_dict.h"
#include "r_runtime_string.h"
#include "r_runtime_utf8.h"

#if defined(__APPLE__)
#include <crt_externs.h>
#endif

#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum RLibraryEnvironmentValidation {
    R_LIBRARY_ENVIRONMENT_VALID = 0,
    R_LIBRARY_ENVIRONMENT_INVALID = 1
} RLibraryEnvironmentValidation;

typedef enum RLibraryEnvironmentCopyStatus {
    R_LIBRARY_ENVIRONMENT_COPY_OK = 0,
    R_LIBRARY_ENVIRONMENT_COPY_ALLOCATION_FAILED = 1
} RLibraryEnvironmentCopyStatus;

static pthread_mutex_t r_library_environment_mutex = PTHREAD_MUTEX_INITIALIZER;

static RStdEnvError r_library_environment_error(RStdEnvErrorCode code, int native_code) {
    RStdEnvError error;

    error.code = code;
    error.native_code = (int64_t)native_code;
    return error;
}

static RStdEnvError r_library_environment_allocation_error(void) {
    return r_library_environment_error(R_STD_ENV_ERROR_ALLOCATION_FAILED, 0);
}

static RStdEnvError r_library_environment_native_error(int native_code,
                                                       _Bool invalid_name_context) {
    if (invalid_name_context && (native_code == EINVAL)) {
        return r_library_environment_error(R_STD_ENV_ERROR_INVALID_NAME, native_code);
    }
    if ((native_code == EACCES) || (native_code == EPERM)) {
        return r_library_environment_error(R_STD_ENV_ERROR_PERMISSION_DENIED, native_code);
    }
    if ((native_code == EAGAIN) || (native_code == ENOMEM) || (native_code == ENOSPC) ||
        (native_code == E2BIG)) {
        return r_library_environment_error(R_STD_ENV_ERROR_RESOURCE_EXHAUSTED, native_code);
    }
    return r_library_environment_error(R_STD_ENV_ERROR_OTHER, native_code);
}

static RLibraryEnvironmentValidation r_library_environment_validate_name(RStdStringView name) {
    size_t index;

    if (name.length == 0U) {
        return R_LIBRARY_ENVIRONMENT_INVALID;
    }
    for (index = 0U; index < name.length; ++index) {
        if ((name.data[index] == UINT8_C(0)) || (name.data[index] == UINT8_C(0x3d))) {
            return R_LIBRARY_ENVIRONMENT_INVALID;
        }
    }
    return R_LIBRARY_ENVIRONMENT_VALID;
}

static RLibraryEnvironmentValidation r_library_environment_validate_value(RStdStringView value) {
    size_t index;

    for (index = 0U; index < value.length; ++index) {
        if (value.data[index] == UINT8_C(0)) {
            return R_LIBRARY_ENVIRONMENT_INVALID;
        }
    }
    return R_LIBRARY_ENVIRONMENT_VALID;
}

static RLibraryEnvironmentCopyStatus r_library_environment_copy_c_string(
    RRuntimeAllocator *allocator, RStdStringView source, char **result) {
    RRuntimeAllocationStatus status;
    void *allocation = NULL;
    size_t size;

    *result = NULL;
    if (source.length == SIZE_MAX) {
        return R_LIBRARY_ENVIRONMENT_COPY_ALLOCATION_FAILED;
    }
    size = source.length + 1U;
    status = r_runtime_allocator_allocate(allocator, size, _Alignof(char), &allocation);
    if (status != R_RUNTIME_ALLOCATION_OK) {
        return R_LIBRARY_ENVIRONMENT_COPY_ALLOCATION_FAILED;
    }
    if (source.length != 0U) {
        (void)memcpy(allocation, source.data, source.length);
    }
    ((char *)allocation)[source.length] = '\0';
    *result = allocation;
    return R_LIBRARY_ENVIRONMENT_COPY_OK;
}

_Bool r_library_internal_environment_acquire(void) {
    return pthread_mutex_lock(&r_library_environment_mutex) == 0;
}

_Bool r_library_internal_environment_release(void) {
    return pthread_mutex_unlock(&r_library_environment_mutex) == 0;
}

static char **r_library_environment_native_entries(void) {
#if defined(__APPLE__)
    char ***environment = _NSGetEnviron();

    return environment == NULL ? NULL : *environment;
#else
    extern char **environ;

    return environ;
#endif
}

/* R-LIB-0020 (M24-2): the key contract of an owned string, which generated code computes too:
   FNV-1a over its UTF-8 bytes, like the JSON decoder and the contract of `str`. */
static uint64_t r_library_environment_hash_string(const void *value) {
    const RRuntimeString *string = value;
    const uint8_t *bytes = r_runtime_string_bytes(string);
    const size_t length = r_runtime_string_length(string);
    uint64_t hash = UINT64_C(14695981039346656037);

    for (size_t index = 0U; index < length; ++index) {
        hash = (hash ^ bytes[index]) * UINT64_C(1099511628211);
    }
    return hash;
}

static _Bool r_library_environment_equal_string(const void *left, const void *right) {
    const RRuntimeString *left_string = left;
    const RRuntimeString *right_string = right;
    const size_t left_length = r_runtime_string_length(left_string);
    const size_t right_length = r_runtime_string_length(right_string);

    if (left_length != right_length) {
        return 0;
    }
    if (left_length == 0U) {
        return 1;
    }
    return memcmp(r_runtime_string_bytes(left_string),
                  r_runtime_string_bytes(right_string),
                  left_length) == 0;
}

static void r_library_environment_move_string(void *destination, void *source) {
    RRuntimeString *destination_string = destination;
    RRuntimeString *source_string = source;

    *destination_string = *source_string;
    source_string->bytes.data = NULL;
    source_string->bytes.length = 0U;
    source_string->bytes.capacity = 0U;
}

static void r_library_environment_drop_string(void *value) {
    r_runtime_string_destroy(value);
}

RRuntimeTypeInfo r_library_internal_environment_string_type(void) {
    const RRuntimeTypeInfo type = {
        sizeof(RRuntimeString),
        _Alignof(RRuntimeString),
        r_library_environment_move_string,
        r_library_environment_drop_string,
    };

    return type;
}

static RRuntimeDictKeyInfo r_library_environment_string_key(void) {
    RRuntimeDictKeyInfo key;

    key.type = r_library_internal_environment_string_type();
    key.hash = r_library_environment_hash_string;
    key.equal = r_library_environment_equal_string;
    return key;
}

static RStdEnvCallStatus r_library_environment_string_from_native(RRuntimeAllocator *allocator,
                                                                  const uint8_t *bytes,
                                                                  size_t length,
                                                                  RStdEnvErrorCode invalid_code,
                                                                  RRuntimeString *result,
                                                                  RStdEnvError *error) {
    RRuntimeStringStatus status;

    if (!r_runtime_utf8_validate(bytes, length, NULL)) {
        *error = r_library_environment_error(invalid_code, 0);
        return R_STD_ENV_CALL_ERROR;
    }
    status = r_runtime_string_from_valid_utf8(result, allocator, bytes, length);
    if ((status == R_RUNTIME_STRING_ALLOCATION_FAILED) ||
        (status == R_RUNTIME_STRING_SIZE_OVERFLOW) ||
        (status == R_RUNTIME_STRING_UNSUPPORTED_ALIGNMENT)) {
        *error = r_library_environment_allocation_error();
        return R_STD_ENV_CALL_ERROR;
    }
    return R_STD_ENV_CALL_SUCCESS;
}

static RStdEnvCallStatus r_library_environment_dict_status(RRuntimeDictStatus status,
                                                           RStdEnvError *error) {
    if (status == R_RUNTIME_DICT_OK) {
        return R_STD_ENV_CALL_SUCCESS;
    }
    if ((status == R_RUNTIME_DICT_ALLOCATION_FAILED) || (status == R_RUNTIME_DICT_SIZE_OVERFLOW) ||
        (status == R_RUNTIME_DICT_UNSUPPORTED_ALIGNMENT)) {
        *error = r_library_environment_allocation_error();
        return R_STD_ENV_CALL_ERROR;
    }
    return R_STD_ENV_CALL_SUCCESS;
}

RStdEnvVariablesResult r_library_internal_environment_variables(RRuntimeAllocator *allocator,
                                                                uint64_t seed) {
    RStdEnvVariablesResult result = {0};
    RRuntimeString key = {0};
    RRuntimeString value = {0};
    RRuntimeString replaced = {0};
    char **entries;
    size_t entry_index;
    _Bool locked = 0;
    _Bool key_ready = 0;
    _Bool value_ready = 0;
    RRuntimeDictStatus dict_status;

    result.status = R_STD_ENV_CALL_CONTRACT_VIOLATION;
    (void)r_runtime_dict_initialize(&result.value,
                                    allocator,
                                    r_library_environment_string_key(),
                                    r_library_internal_environment_string_type(),
                                    seed);
    if (!r_library_internal_environment_acquire()) {
        goto cleanup;
    }
    locked = 1;
    entries = r_library_environment_native_entries();
    if (entries == NULL) {
        result.status = R_STD_ENV_CALL_ERROR;
        result.error = r_library_environment_error(R_STD_ENV_ERROR_OTHER, 0);
        goto cleanup;
    }
    for (entry_index = 0U; entries[entry_index] != NULL; ++entry_index) {
        const char *entry = entries[entry_index];
        const char *separator = strchr(entry, '=');
        size_t name_length;
        size_t value_length;
        _Bool did_replace = 0;

        if ((separator == NULL) || (separator == entry)) {
            result.status = R_STD_ENV_CALL_ERROR;
            result.error = r_library_environment_error(R_STD_ENV_ERROR_INVALID_NAME, 0);
            goto cleanup;
        }
        name_length = (size_t)(separator - entry);
        value_length = strlen(separator + 1);
        result.status = r_library_environment_string_from_native(allocator,
                                                                 (const uint8_t *)entry,
                                                                 name_length,
                                                                 R_STD_ENV_ERROR_INVALID_NAME,
                                                                 &key,
                                                                 &result.error);
        if (result.status != R_STD_ENV_CALL_SUCCESS) {
            goto cleanup;
        }
        key_ready = 1;
        result.status = r_library_environment_string_from_native(allocator,
                                                                 (const uint8_t *)(separator + 1),
                                                                 value_length,
                                                                 R_STD_ENV_ERROR_INVALID_VALUE,
                                                                 &value,
                                                                 &result.error);
        if (result.status != R_STD_ENV_CALL_SUCCESS) {
            goto cleanup;
        }
        value_ready = 1;
        dict_status = r_runtime_dict_insert(&result.value, &key, &value, &replaced, &did_replace);
        result.status = r_library_environment_dict_status(dict_status, &result.error);
        if (result.status != R_STD_ENV_CALL_SUCCESS) {
            goto cleanup;
        }
        key_ready = 0;
        value_ready = 0;
        if (did_replace) {
            r_runtime_string_destroy(&replaced);
        }
    }
    result.status = R_STD_ENV_CALL_SUCCESS;

cleanup:
    if (value_ready) {
        r_runtime_string_destroy(&value);
    }
    if (key_ready) {
        r_runtime_string_destroy(&key);
    }
    if (locked && !r_library_internal_environment_release()) {
        result.status = R_STD_ENV_CALL_CONTRACT_VIOLATION;
    }
    if (result.status != R_STD_ENV_CALL_SUCCESS) {
        r_runtime_dict_destroy(&result.value);
        (void)memset(&result.value, 0, sizeof(result.value));
    }
    return result;
}

RStdEnvGetResult r_library_internal_environment_get(RRuntimeAllocator *allocator,
                                                    RStdStringView name) {
    RStdEnvGetResult result = {0};
    RLibraryEnvironmentValidation validation;
    RLibraryEnvironmentCopyStatus copy_status;
    char *native_name = NULL;
    const char *native_value;
    _Bool locked = 0;

    result.status = R_STD_ENV_CALL_CONTRACT_VIOLATION;
    validation = r_library_environment_validate_name(name);
    if (validation == R_LIBRARY_ENVIRONMENT_INVALID) {
        result.status = R_STD_ENV_CALL_ERROR;
        result.error = r_library_environment_error(R_STD_ENV_ERROR_INVALID_NAME, 0);
        return result;
    }
    if (!r_library_internal_environment_acquire()) {
        goto cleanup;
    }
    locked = 1;
    copy_status = r_library_environment_copy_c_string(allocator, name, &native_name);
    if (copy_status == R_LIBRARY_ENVIRONMENT_COPY_ALLOCATION_FAILED) {
        result.status = R_STD_ENV_CALL_ERROR;
        result.error = r_library_environment_allocation_error();
        goto cleanup;
    }
    native_value = getenv(native_name);
    if (native_value == NULL) {
        result.status = R_STD_ENV_CALL_SUCCESS;
        goto cleanup;
    }
    result.status = r_library_environment_string_from_native(allocator,
                                                             (const uint8_t *)native_value,
                                                             strlen(native_value),
                                                             R_STD_ENV_ERROR_INVALID_VALUE,
                                                             &result.value,
                                                             &result.error);
    if (result.status == R_STD_ENV_CALL_SUCCESS) {
        result.has_value = 1;
    }

cleanup:
    r_runtime_allocator_deallocate(native_name, _Alignof(char));
    if (locked && !r_library_internal_environment_release()) {
        if (result.has_value) {
            r_runtime_string_destroy(&result.value);
            result.has_value = 0;
        }
        result.status = R_STD_ENV_CALL_CONTRACT_VIOLATION;
    }
    return result;
}

RStdEnvVoidResult r_library_internal_environment_set(RRuntimeAllocator *allocator,
                                                     RStdStringView name,
                                                     RStdStringView value) {
    RStdEnvVoidResult result = {0};
    RLibraryEnvironmentValidation validation;
    RLibraryEnvironmentCopyStatus copy_status;
    char *native_name = NULL;
    char *native_value = NULL;
    _Bool locked = 0;
    int native_status;
    int native_code = 0;

    result.status = R_STD_ENV_CALL_CONTRACT_VIOLATION;
    validation = r_library_environment_validate_name(name);
    if (validation == R_LIBRARY_ENVIRONMENT_INVALID) {
        result.status = R_STD_ENV_CALL_ERROR;
        result.error = r_library_environment_error(R_STD_ENV_ERROR_INVALID_NAME, 0);
        return result;
    }
    validation = r_library_environment_validate_value(value);
    if (validation == R_LIBRARY_ENVIRONMENT_INVALID) {
        result.status = R_STD_ENV_CALL_ERROR;
        result.error = r_library_environment_error(R_STD_ENV_ERROR_INVALID_VALUE, 0);
        return result;
    }
    if (!r_library_internal_environment_acquire()) {
        goto cleanup;
    }
    locked = 1;
    copy_status = r_library_environment_copy_c_string(allocator, name, &native_name);
    if (copy_status == R_LIBRARY_ENVIRONMENT_COPY_ALLOCATION_FAILED) {
        result.status = R_STD_ENV_CALL_ERROR;
        result.error = r_library_environment_allocation_error();
        goto cleanup;
    }
    copy_status = r_library_environment_copy_c_string(allocator, value, &native_value);
    if (copy_status == R_LIBRARY_ENVIRONMENT_COPY_ALLOCATION_FAILED) {
        result.status = R_STD_ENV_CALL_ERROR;
        result.error = r_library_environment_allocation_error();
        goto cleanup;
    }
    errno = 0;
    native_status = setenv(native_name, native_value, 1);
    if (native_status != 0) {
        native_code = errno;
        result.status = R_STD_ENV_CALL_ERROR;
        result.error = r_library_environment_native_error(native_code, 1);
    } else {
        result.status = R_STD_ENV_CALL_SUCCESS;
    }

cleanup:
    r_runtime_allocator_deallocate(native_value, _Alignof(char));
    r_runtime_allocator_deallocate(native_name, _Alignof(char));
    if (locked && !r_library_internal_environment_release()) {
        result.status = R_STD_ENV_CALL_CONTRACT_VIOLATION;
    }
    return result;
}

RStdEnvVoidResult r_library_internal_environment_remove(RRuntimeAllocator *allocator,
                                                        RStdStringView name) {
    RStdEnvVoidResult result = {0};
    RLibraryEnvironmentValidation validation;
    RLibraryEnvironmentCopyStatus copy_status;
    char *native_name = NULL;
    _Bool locked = 0;
    int native_status;
    int native_code = 0;

    result.status = R_STD_ENV_CALL_CONTRACT_VIOLATION;
    validation = r_library_environment_validate_name(name);
    if (validation == R_LIBRARY_ENVIRONMENT_INVALID) {
        result.status = R_STD_ENV_CALL_ERROR;
        result.error = r_library_environment_error(R_STD_ENV_ERROR_INVALID_NAME, 0);
        return result;
    }
    if (!r_library_internal_environment_acquire()) {
        goto cleanup;
    }
    locked = 1;
    copy_status = r_library_environment_copy_c_string(allocator, name, &native_name);
    if (copy_status == R_LIBRARY_ENVIRONMENT_COPY_ALLOCATION_FAILED) {
        result.status = R_STD_ENV_CALL_ERROR;
        result.error = r_library_environment_allocation_error();
        goto cleanup;
    }
    errno = 0;
    native_status = unsetenv(native_name);
    if (native_status != 0) {
        native_code = errno;
        result.status = R_STD_ENV_CALL_ERROR;
        result.error = r_library_environment_native_error(native_code, 1);
    } else {
        result.status = R_STD_ENV_CALL_SUCCESS;
    }

cleanup:
    r_runtime_allocator_deallocate(native_name, _Alignof(char));
    if (locked && !r_library_internal_environment_release()) {
        result.status = R_STD_ENV_CALL_CONTRACT_VIOLATION;
    }
    return result;
}
