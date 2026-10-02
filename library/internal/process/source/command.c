#include "r_library_process_internal.h"

#include "r_library_fs_internal.h"
#include "r_runtime_utf8.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static void r_library_process_move_string(void *destination, void *source) {
    RRuntimeString *destination_string = destination;
    RRuntimeString *source_string = source;

    *destination_string = *source_string;
    source_string->bytes.data = NULL;
    source_string->bytes.length = 0U;
    source_string->bytes.capacity = 0U;
}

static void r_library_process_drop_string(void *value) {
    r_runtime_string_destroy(value);
}

static void r_library_process_drop_path(RStdFsPath *path) {
    if ((path != NULL) && (path->storage != NULL)) {
        r_runtime_allocator_deallocate(path->storage, _Alignof(max_align_t));
        path->storage = NULL;
    }
}

static _Bool r_library_process_view_equal(RStdStringView left, RStdStringView right) {
    return (left.length == right.length) &&
           ((left.length == 0U) || (memcmp(left.data, right.data, left.length) == 0));
}

static RStdStringView r_library_process_string_view(const RRuntimeString *string) {
    RStdStringView view;

    view.data = r_runtime_string_bytes(string);
    view.length = r_runtime_string_length(string);
    return view;
}

static uint64_t r_library_process_hash_bytes(uint64_t hash, const uint8_t *bytes, size_t length) {
    size_t index;

    hash ^= (uint64_t)length;
    hash *= UINT64_C(1099511628211);
    for (index = 0U; index < length; ++index) {
        hash ^= (uint64_t)bytes[index];
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t r_library_process_command_content_hash(const RStdProcessCommandStorage *storage) {
    uint64_t hash = UINT64_C(1469598103934665603);
    RRuntimeDictIterator iterator;
    RRuntimeDictEntryRef entry;
    size_t index;

    hash = r_library_process_hash_bytes(hash,
                                        r_library_internal_fs_path_bytes(&storage->executable),
                                        r_library_internal_fs_path_length(&storage->executable));
    for (index = 0U; index < storage->arguments.length; ++index) {
        const RRuntimeString *argument = r_runtime_array_get(&storage->arguments, index);

        hash = r_library_process_hash_bytes(
            hash, r_runtime_string_bytes(argument), r_runtime_string_length(argument));
    }
    iterator = r_runtime_dict_iter(&storage->environment);
    while (r_runtime_dict_next(&iterator, &entry)) {
        const RRuntimeString *name = entry.key;
        const RRuntimeString *value = entry.value;

        hash = r_library_process_hash_bytes(
            hash, r_runtime_string_bytes(name), r_runtime_string_length(name));
        hash = r_library_process_hash_bytes(
            hash, r_runtime_string_bytes(value), r_runtime_string_length(value));
    }
    if (storage->has_working_directory) {
        hash = r_library_process_hash_bytes(
            hash,
            r_library_internal_fs_path_bytes(&storage->working_directory),
            r_library_internal_fs_path_length(&storage->working_directory));
    }
    hash ^= (uint64_t)storage->stdio.input;
    hash *= UINT64_C(1099511628211);
    hash ^= (uint64_t)storage->stdio.output;
    hash *= UINT64_C(1099511628211);
    hash ^= (uint64_t)storage->stdio.error;
    return hash;
}

RStdProcessError r_library_internal_process_error(RStdProcessErrorCode code, int64_t native_code) {
    RStdProcessError error;

    error.code = code;
    error.native_code = native_code;
    return error;
}

RStdProcessError r_library_internal_process_allocation_error(void) {
    return r_library_internal_process_error(R_STD_PROCESS_ERROR_RESOURCE_EXHAUSTED, INT64_C(0));
}

RStdProcessCallStatus r_library_internal_process_string_status(RRuntimeStringStatus status,
                                                               RStdProcessError *error) {
    if (status == R_RUNTIME_STRING_OK) {
        return R_STD_PROCESS_CALL_SUCCESS;
    }
    *error = r_library_internal_process_allocation_error();
    return R_STD_PROCESS_CALL_ERROR;
}

RStdProcessCallStatus r_library_internal_process_array_status(RRuntimeArrayStatus status,
                                                              RStdProcessError *error) {
    if (status == R_RUNTIME_ARRAY_OK) {
        return R_STD_PROCESS_CALL_SUCCESS;
    }
    *error = r_library_internal_process_allocation_error();
    return R_STD_PROCESS_CALL_ERROR;
}

RStdProcessCallStatus r_library_internal_process_dict_status(RRuntimeDictStatus status,
                                                             RStdProcessError *error) {
    if (status == R_RUNTIME_DICT_OK) {
        return R_STD_PROCESS_CALL_SUCCESS;
    }
    *error = r_library_internal_process_allocation_error();
    return R_STD_PROCESS_CALL_ERROR;
}

RStdProcessCallStatus r_library_internal_process_copy_string(RRuntimeAllocator *allocator,
                                                             RStdStringView source,
                                                             RRuntimeString *result,
                                                             RStdProcessError *error) {
    RRuntimeStringStatus status;

    (void)memset(result, 0, sizeof(*result));
    status = r_runtime_string_from_valid_utf8(result, allocator, source.data, source.length);
    return r_library_internal_process_string_status(status, error);
}

RStdProcessCallStatus r_library_internal_process_copy_path(RRuntimeAllocator *allocator,
                                                           const RStdFsPath *source,
                                                           RStdFsPath *result,
                                                           RStdProcessError *error) {
    RStdAllocError allocation_error = R_STD_ALLOC_ERROR_OUT_OF_MEMORY;
    RStdFsCallStatus status;

    result->storage = NULL;
    status = r_library_internal_fs_path_create(
        allocator, source->storage->bytes, source->storage->length, result, &allocation_error);
    if (status == R_STD_FS_CALL_SUCCESS) {
        return R_STD_PROCESS_CALL_SUCCESS;
    }
    (void)allocation_error;
    *error = r_library_internal_process_allocation_error();
    return R_STD_PROCESS_CALL_ERROR;
}

RLibraryProcessValidation r_library_internal_process_validate_argument(RStdStringView value) {
    size_t index;

    for (index = 0U; index < value.length; ++index) {
        if (value.data[index] == UINT8_C(0)) {
            return R_LIBRARY_PROCESS_INVALID;
        }
    }
    return R_LIBRARY_PROCESS_VALID;
}

RLibraryProcessValidation
r_library_internal_process_validate_environment_name(RStdStringView name) {
    size_t index;

    if (name.length == 0U) {
        return R_LIBRARY_PROCESS_INVALID;
    }
    for (index = 0U; index < name.length; ++index) {
        if ((name.data[index] == UINT8_C(0)) || (name.data[index] == UINT8_C('='))) {
            return R_LIBRARY_PROCESS_INVALID;
        }
    }
    return R_LIBRARY_PROCESS_VALID;
}

RLibraryProcessValidation
r_library_internal_process_validate_environment_value(RStdStringView value) {
    size_t index;

    for (index = 0U; index < value.length; ++index) {
        if (value.data[index] == UINT8_C(0)) {
            return R_LIBRARY_PROCESS_INVALID;
        }
    }
    return R_LIBRARY_PROCESS_VALID;
}

RRuntimeTypeInfo r_library_internal_process_string_type(void) {
    const RRuntimeTypeInfo type = {
        sizeof(RRuntimeString),
        _Alignof(RRuntimeString),
        r_library_process_move_string,
        r_library_process_drop_string,
    };

    return type;
}

void r_library_internal_process_command_move(RStdProcessCommand *destination,
                                             RStdProcessCommand *source) {
    destination->storage = source->storage;
    source->storage = NULL;
}

void r_library_internal_process_command_destroy(RStdProcessCommand *command) {
    RStdProcessCommandStorage *storage;

    if (command->storage == NULL) {
        return;
    }
    storage = command->storage;
    if (storage->has_working_directory) {
        r_library_process_drop_path(&storage->working_directory);
    }
    if (storage->current_directory >= 0) {
        (void)close(storage->current_directory);
        storage->current_directory = -1;
    }
    r_runtime_dict_destroy(&storage->environment);
    r_runtime_array_destroy(&storage->arguments);
    r_library_process_drop_path(&storage->executable);
    r_runtime_allocator_deallocate(storage, _Alignof(RStdProcessCommandStorage));
    command->storage = NULL;
}

RLibraryProcessState r_library_internal_process_command_state(const RStdProcessCommand *command) {
    RLibraryProcessState state = {0};
    const RStdProcessCommandStorage *storage;

    storage = command->storage;
    state.storage = storage;
    state.arguments_data = storage->arguments.data;
    state.arguments_length = storage->arguments.length;
    state.arguments_capacity = storage->arguments.capacity;
    state.environment_slots = storage->environment.slots;
    state.environment_length = storage->environment.length;
    state.environment_capacity = storage->environment.capacity;
    state.working_directory_storage = storage->working_directory.storage;
    state.current_directory = storage->current_directory;
    state.has_working_directory = storage->has_working_directory;
    state.stdio = storage->stdio;
    state.content_hash = r_library_process_command_content_hash(storage);
    return state;
}

RStdStringView r_library_internal_process_executable(const RStdProcessCommand *command) {
    RStdStringView view;

    view.data = command->storage->executable.storage->bytes;
    view.length = command->storage->executable.storage->length;
    return view;
}

size_t r_library_internal_process_argument_count(const RStdProcessCommand *command) {
    return command->storage->arguments.length;
}

RStdStringView r_library_internal_process_argument(const RStdProcessCommand *command,
                                                   size_t index) {
    const RRuntimeString *argument;

    argument = r_runtime_array_get(&command->storage->arguments, index);
    return r_library_process_string_view(argument);
}

size_t r_library_internal_process_environment_count(const RStdProcessCommand *command) {
    return command->storage->environment.length;
}

_Bool r_library_internal_process_environment_value(const RStdProcessCommand *command,
                                                   RStdStringView name,
                                                   RStdStringView *value) {
    RRuntimeDictIterator iterator;
    RRuntimeDictEntryRef entry;

    iterator = r_runtime_dict_iter(&command->storage->environment);
    while (r_runtime_dict_next(&iterator, &entry)) {
        RStdStringView key_view = r_library_process_string_view(entry.key);

        if (r_library_process_view_equal(key_view, name)) {
            *value = r_library_process_string_view(entry.value);
            return 1;
        }
    }
    return 0;
}

int r_library_internal_process_current_directory(const RStdProcessCommand *command) {
    return command->storage->current_directory;
}

_Bool r_library_internal_process_working_directory(const RStdProcessCommand *command,
                                                   RStdStringView *path) {
    if (!command->storage->has_working_directory) {
        return 0;
    }
    path->data = command->storage->working_directory.storage->bytes;
    path->length = command->storage->working_directory.storage->length;
    return 1;
}

RStdProcessStdio r_library_internal_process_stdio(const RStdProcessCommand *command) {
    return command->storage->stdio;
}
