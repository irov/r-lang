#include "r_library_process_internal.h"

#include "r_library_fs_internal.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static _Bool add_size(size_t left, size_t right, size_t *result) {
    if (right > SIZE_MAX - left) {
        return 0;
    }
    *result = left + right;
    return 1;
}

static _Bool add_terminated_size(size_t current, size_t length, size_t *result) {
    size_t terminated;

    return add_size(length, 1U, &terminated) && add_size(current, terminated, result);
}

static RStdStringView string_view(const RRuntimeString *string) {
    return (RStdStringView){r_runtime_string_bytes(string), r_runtime_string_length(string)};
}

static _Bool native_text_size(const RStdProcessCommandStorage *storage,
                              size_t *pointer_count,
                              size_t *byte_count) {
    RRuntimeDictIterator iterator;
    RRuntimeDictEntryRef entry;
    size_t pointers;
    size_t bytes = 0U;
    size_t index;

    if (!add_size(storage->arguments.length, storage->environment.length, &pointers) ||
        !add_size(pointers, 2U, &pointers)) {
        return 0;
    }
    if (!add_terminated_size(
            bytes, r_library_internal_fs_path_length(&storage->executable), &bytes)) {
        return 0;
    }
    for (index = 0U; index < storage->arguments.length; ++index) {
        const RRuntimeString *argument = r_runtime_array_get(&storage->arguments, index);
        const RStdStringView view = string_view(argument);

        if (!add_terminated_size(bytes, view.length, &bytes)) {
            return 0;
        }
    }
    iterator = r_runtime_dict_iter(&storage->environment);
    while (r_runtime_dict_next(&iterator, &entry)) {
        const RStdStringView name = string_view(entry.key);
        const RStdStringView value = string_view(entry.value);

        if (!add_size(bytes, name.length, &bytes) || !add_size(bytes, 1U, &bytes) ||
            !add_terminated_size(bytes, value.length, &bytes)) {
            return 0;
        }
    }
    if (storage->has_working_directory &&
        !add_terminated_size(
            bytes, r_library_internal_fs_path_length(&storage->working_directory), &bytes)) {
        return 0;
    }
    *pointer_count = pointers;
    *byte_count = bytes;
    return 1;
}

static char *copy_terminated(char *destination, const uint8_t *source, size_t length) {
    if (length != 0U) {
        (void)memcpy(destination, source, length);
    }
    destination[length] = '\0';
    return destination + length + 1U;
}

RLibraryProcessNativeTextStatus
r_library_internal_process_native_text_create(const RStdProcessCommand *command,
                                              RLibraryProcessNativeText *result) {
    const RStdProcessCommandStorage *storage;
    RRuntimeAllocationStatus allocation_status;
    RRuntimeDictIterator iterator;
    RRuntimeDictEntryRef entry;
    size_t pointer_count;
    size_t pointer_bytes;
    size_t byte_count;
    size_t allocation_size;
    size_t argument_index = 0U;
    size_t environment_index = 0U;
    char *cursor;

    (void)memset(result, 0, sizeof(*result));
    storage = command->storage;
    if (!native_text_size(storage, &pointer_count, &byte_count) ||
        pointer_count > SIZE_MAX / sizeof(char *)) {
        return R_LIBRARY_PROCESS_NATIVE_TEXT_RESOURCE_EXHAUSTED;
    }
    pointer_bytes = pointer_count * sizeof(char *);
    if (!add_size(pointer_bytes, byte_count, &allocation_size)) {
        return R_LIBRARY_PROCESS_NATIVE_TEXT_RESOURCE_EXHAUSTED;
    }
    allocation_status = r_runtime_allocator_allocate(
        storage->allocator, allocation_size, _Alignof(char *), &result->allocation);
    if (allocation_status != R_RUNTIME_ALLOCATION_OK) {
        (void)memset(result, 0, sizeof(*result));
        return R_LIBRARY_PROCESS_NATIVE_TEXT_RESOURCE_EXHAUSTED;
    }

    result->allocator = storage->allocator;
    result->argument_count = storage->arguments.length;
    result->environment_count = storage->environment.length;
    result->arguments = result->allocation;
    result->environment = result->arguments + result->argument_count + 1U;
    cursor = (char *)result->allocation + pointer_bytes;
    result->executable = cursor;
    cursor = copy_terminated(cursor,
                             r_library_internal_fs_path_bytes(&storage->executable),
                             r_library_internal_fs_path_length(&storage->executable));

    for (argument_index = 0U; argument_index < result->argument_count; ++argument_index) {
        const RRuntimeString *argument = r_runtime_array_get(&storage->arguments, argument_index);
        const RStdStringView view = string_view(argument);

        result->arguments[argument_index] = cursor;
        cursor = copy_terminated(cursor, view.data, view.length);
    }
    result->arguments[result->argument_count] = NULL;

    iterator = r_runtime_dict_iter(&storage->environment);
    while (r_runtime_dict_next(&iterator, &entry)) {
        const RStdStringView name = string_view(entry.key);
        const RStdStringView value = string_view(entry.value);

        result->environment[environment_index] = cursor;
        if (name.length != 0U) {
            (void)memcpy(cursor, name.data, name.length);
        }
        cursor += name.length;
        *cursor = '=';
        cursor += 1U;
        cursor = copy_terminated(cursor, value.data, value.length);
        environment_index += 1U;
    }
    result->environment[result->environment_count] = NULL;

    if (storage->has_working_directory) {
        result->working_directory = cursor;
        (void)copy_terminated(cursor,
                              r_library_internal_fs_path_bytes(&storage->working_directory),
                              r_library_internal_fs_path_length(&storage->working_directory));
    }
    return R_LIBRARY_PROCESS_NATIVE_TEXT_OK;
}

void r_library_internal_process_native_text_destroy(RLibraryProcessNativeText *text) {
    r_runtime_allocator_deallocate(text->allocation, _Alignof(char *));
    (void)memset(text, 0, sizeof(*text));
}
