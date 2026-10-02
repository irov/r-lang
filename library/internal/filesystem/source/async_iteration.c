#include "r_library_fs_internal.h"

#include "r_library_clock_internal.h"
#include "r_library_duration_internal.h"
#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_darwin_fs_lane.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"
#include "r_runtime_utf8.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/attr.h>
#include <sys/vnode.h>

#define R_LIBRARY_FS_ENUMERATION_BUFFER_SIZE ((size_t)4096U)

typedef enum RLibraryFsIterationMode {
    R_LIBRARY_FS_ITERATE = 0,
    R_LIBRARY_FS_NEXT
} RLibraryFsIterationMode;

typedef enum RLibraryFsIterationDeadlineStatus {
    R_LIBRARY_FS_ITERATION_DEADLINE_READY = 0,
    R_LIBRARY_FS_ITERATION_DEADLINE_IMMEDIATE,
    R_LIBRARY_FS_ITERATION_DEADLINE_ERROR
} RLibraryFsIterationDeadlineStatus;

enum {
    R_LIBRARY_FS_ITERATION_CANCEL_REPORTED = 1U,
    R_LIBRARY_FS_ITERATION_CALLBACK_RELEASED = 2U,
    R_LIBRARY_FS_ITERATION_COMPLETION_REQUIRED = 4U
};

typedef struct RLibraryFsNativeEntry {
    const uint8_t *name;
    size_t name_length;
    size_t record_length;
    RStdFsFileKind kind;
    int native_error;
} RLibraryFsNativeEntry;

typedef struct RLibraryFsIterationPayload {
    RLibraryFsIterationMode mode;
    RStdFsDirectoryIter *staged_iterator;
    RStdFsDirectoryIterStorage *iterator_storage;
    RLibraryFsHandleStorage *handle_storage;
    RLibraryFsOperationRegistration registration;
    RStdFsDeadline deadline;
    RStdFsError immediate_error;
    uint64_t immediate_event_sequence;
    RRuntimeDarwinFsResult native_result;
    RRuntimeDarwinFsPreparedRequest *prepared;
    RRuntimeDarwinFsRequest *request;
    RRuntimeTaskExternalExecution *execution;
    void *result;
    _Atomic unsigned int cancellation_state;
    _Bool immediate;
} RLibraryFsIterationPayload;

_Noreturn static void iteration_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                    (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
}

static RStdFsError fs_error(RStdFsErrorCode code, int64_t native_code) {
    return (RStdFsError){code, native_code};
}

static int compare_instant(RStdTimeInstant left, RStdTimeInstant right) {
    if (left.storage_seconds != right.storage_seconds) {
        return left.storage_seconds < right.storage_seconds ? -1 : 1;
    }
    if (left.storage_nanoseconds != right.storage_nanoseconds) {
        return left.storage_nanoseconds < right.storage_nanoseconds ? -1 : 1;
    }
    return 0;
}

static RLibraryFsIterationDeadlineStatus
deadline_timeout(RStdFsDeadline deadline, uint64_t *timeout_nanoseconds, RStdFsError *error) {
    const uint64_t nanoseconds_per_second = UINT64_C(1000000000);
    RStdTimeInstantResult now;
    RStdTimeDurationTimeResult remaining;
    uint64_t seconds;

    *timeout_nanoseconds = 0U;
    if (!deadline.has_value) {
        return R_LIBRARY_FS_ITERATION_DEADLINE_READY;
    }
    if (deadline.value.storage_seconds < 0 ||
        deadline.value.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
        *error = fs_error(R_STD_FS_ERROR_INVALID_OPERATION, INT64_C(0));
        return R_LIBRARY_FS_ITERATION_DEADLINE_ERROR;
    }
    now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
    if (!now.is_ok) {
        *error = fs_error(R_STD_FS_ERROR_OTHER, now.error.native_code);
        return R_LIBRARY_FS_ITERATION_DEADLINE_ERROR;
    }
    if (compare_instant(deadline.value, now.value) <= 0) {
        *error = fs_error(R_STD_FS_ERROR_TIMED_OUT, INT64_C(0));
        return R_LIBRARY_FS_ITERATION_DEADLINE_IMMEDIATE;
    }
    remaining = r_library_internal_time_instant_duration(deadline.value, now.value);
    if (!remaining.is_ok || remaining.value.seconds < 0) {
        *error = fs_error(R_STD_FS_ERROR_OTHER, INT64_C(0));
        return R_LIBRARY_FS_ITERATION_DEADLINE_ERROR;
    }
    seconds = (uint64_t)remaining.value.seconds;
    if (seconds > ((uint64_t)INT64_MAX / nanoseconds_per_second)) {
        *error = fs_error(R_STD_FS_ERROR_UNSUPPORTED, INT64_C(0));
        return R_LIBRARY_FS_ITERATION_DEADLINE_ERROR;
    }
    *timeout_nanoseconds = seconds * nanoseconds_per_second;
    if ((uint64_t)remaining.value.nanoseconds > ((uint64_t)INT64_MAX - *timeout_nanoseconds)) {
        *error = fs_error(R_STD_FS_ERROR_UNSUPPORTED, INT64_C(0));
        return R_LIBRARY_FS_ITERATION_DEADLINE_ERROR;
    }
    *timeout_nanoseconds += (uint64_t)remaining.value.nanoseconds;
    return R_LIBRARY_FS_ITERATION_DEADLINE_READY;
}

static RStdFsErrorCode classify_native_error(int native_error) {
    if (native_error == EBADF) {
        return R_STD_FS_ERROR_CLOSED;
    }
    if (native_error == EINVAL || native_error == ENOTDIR || native_error == ENXIO) {
        return R_STD_FS_ERROR_INVALID_OPERATION;
    }
    if (native_error == EACCES || native_error == EPERM) {
        return R_STD_FS_ERROR_PERMISSION_DENIED;
    }
    if (native_error == ENOMEM || native_error == EMFILE || native_error == ENFILE ||
        native_error == ENOBUFS || native_error == EAGAIN || native_error == ERANGE) {
        return R_STD_FS_ERROR_RESOURCE_EXHAUSTED;
    }
#if defined(ENOTSUP)
    if (native_error == ENOTSUP) {
        return R_STD_FS_ERROR_UNSUPPORTED;
    }
#endif
    return R_STD_FS_ERROR_OTHER;
}

static RStdFsError native_result_error(RRuntimeDarwinFsResult native_result) {
    if (native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_CANCELLED ||
        native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_STOPPING) {
        return fs_error(R_STD_FS_ERROR_CANCELLED, (int64_t)native_result.native_error);
    }
    if (native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_TIMED_OUT) {
        return fs_error(R_STD_FS_ERROR_TIMED_OUT, (int64_t)native_result.native_error);
    }
    return fs_error(classify_native_error(native_result.native_error),
                    (int64_t)native_result.native_error);
}

static void iterator_result_move(void *destination_pointer, void *source_pointer) {
    RStdFsDirectoryIterResult *destination = destination_pointer;
    RStdFsDirectoryIterResult *source = source_pointer;

    *destination = *source;
    if (source->r_tag == UINT32_C(0))
        source->r_payload.r_ok.storage = NULL;
}

static void iterator_result_drop(void *value) {
    RStdFsDirectoryIterResult *result = value;

    if (result->r_tag == UINT32_C(0))
        r_library_internal_fs_directory_iter_drop(&result->r_payload.r_ok);
}

static void next_result_move(void *destination_pointer, void *source_pointer) {
    RStdFsDirectoryNextResult *destination = destination_pointer;
    RStdFsDirectoryNextResult *source = source_pointer;

    r_library_internal_fs_directory_next_result_move(destination, source);
}

static void next_result_drop(void *value) {
    RStdFsDirectoryNextResult *result = value;

    r_library_internal_fs_directory_next_result_drop(result);
}

static RRuntimeTypeInfo result_type(RLibraryFsIterationMode mode) {
    if (mode == R_LIBRARY_FS_ITERATE) {
        return (RRuntimeTypeInfo){sizeof(RStdFsDirectoryIterResult),
                                  _Alignof(RStdFsDirectoryIterResult),
                                  iterator_result_move,
                                  iterator_result_drop};
    }
    return (RRuntimeTypeInfo){sizeof(RStdFsDirectoryNextResult),
                              _Alignof(RStdFsDirectoryNextResult),
                              next_result_move,
                              next_result_drop};
}

static void payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryFsIterationPayload *destination = destination_pointer;
    RLibraryFsIterationPayload *source = source_pointer;

    (void)memset(destination, 0, sizeof(*destination));
    destination->mode = source->mode;
    destination->deadline = source->deadline;
    destination->immediate_error = source->immediate_error;
    destination->immediate_event_sequence = source->immediate_event_sequence;
    destination->prepared = source->prepared;
    destination->iterator_storage = source->iterator_storage;
    destination->handle_storage = source->handle_storage;
    destination->immediate = source->immediate;
    atomic_init(&destination->cancellation_state, 0U);
    source->prepared = NULL;
    source->iterator_storage = NULL;
    source->handle_storage = NULL;
    if (source->mode == R_LIBRARY_FS_NEXT) {
        if (source->staged_iterator == NULL || source->staged_iterator->storage == NULL ||
            destination->iterator_storage != source->staged_iterator->storage) {
            iteration_panic();
        }
        source->staged_iterator->storage = NULL;
    }
}

static void release_tracking(RLibraryFsIterationPayload *payload) {
    if (payload->registration.registered) {
        r_library_internal_fs_operation_unregister(payload->handle_storage, &payload->registration);
        payload->handle_storage = NULL;
    } else if (payload->handle_storage != NULL) {
        r_library_internal_fs_handle_release(payload->handle_storage);
        payload->handle_storage = NULL;
    }
}

static void payload_drop(void *value) {
    RLibraryFsIterationPayload *payload = value;

    if (payload->prepared != NULL) {
        r_runtime_darwin_fs_prepared_abort(&payload->prepared);
    }
    if (payload->request != NULL) {
        iteration_panic();
    }
    release_tracking(payload);
    if (payload->iterator_storage != NULL) {
        r_library_internal_fs_directory_iter_storage_release(payload->iterator_storage);
        payload->iterator_storage = NULL;
    }
}

static RStdFsFileKind native_file_kind(fsobj_type_t value) {
    switch (value) {
    case VREG:
        return R_STD_FS_FILE_KIND_REGULAR;
    case VDIR:
        return R_STD_FS_FILE_KIND_DIRECTORY;
    case VLNK:
        return R_STD_FS_FILE_KIND_SYMLINK;
    case VCHR:
        return R_STD_FS_FILE_KIND_CHARACTER_DEVICE;
    case VBLK:
        return R_STD_FS_FILE_KIND_BLOCK_DEVICE;
    case VFIFO:
        return R_STD_FS_FILE_KIND_FIFO;
    case VSOCK:
        return R_STD_FS_FILE_KIND_SOCKET;
    case VNON:
    case VBAD:
    case VSTR:
    case VCPLX:
        return R_STD_FS_FILE_KIND_OTHER;
    }
    return R_STD_FS_FILE_KIND_OTHER;
}

static _Bool
read_bytes(const uint8_t *buffer, size_t capacity, size_t offset, void *destination, size_t size) {
    if (offset > capacity || size > capacity - offset) {
        return 0;
    }
    (void)memcpy(destination, buffer + offset, size);
    return 1;
}

static _Bool parse_native_entry(const RStdFsDirectoryIterStorage *storage,
                                RLibraryFsNativeEntry *entry) {
    const size_t length_offset = 0U;
    const size_t returned_offset = sizeof(uint32_t);
    const size_t error_offset = returned_offset + sizeof(attribute_set_t);
    const size_t name_reference_offset = error_offset + sizeof(uint32_t);
    const size_t object_type_offset = name_reference_offset + sizeof(attrreference_t);
    const size_t fixed_size = object_type_offset + sizeof(fsobj_type_t);
    const uint8_t *buffer;
    attribute_set_t returned;
    attrreference_t name_reference;
    fsobj_type_t object_type;
    uint32_t record_length;
    uint32_t entry_error;
    int64_t name_start;
    size_t base;
    size_t index;

    (void)memset(entry, 0, sizeof(*entry));
    if (storage == NULL || storage->cache.data == NULL || storage->cache.entry_count <= 0 ||
        storage->cache_index < 0 || storage->cache_index >= storage->cache.entry_count) {
        return 0;
    }
    buffer = storage->cache.data;
    base = storage->cache_offset;
    if (!read_bytes(buffer,
                    storage->cache.capacity,
                    base + length_offset,
                    &record_length,
                    sizeof(record_length)) ||
        record_length < fixed_size || record_length > storage->cache.capacity - base ||
        !read_bytes(
            buffer, storage->cache.capacity, base + returned_offset, &returned, sizeof(returned)) ||
        !read_bytes(buffer,
                    storage->cache.capacity,
                    base + error_offset,
                    &entry_error,
                    sizeof(entry_error)) ||
        !read_bytes(buffer,
                    storage->cache.capacity,
                    base + name_reference_offset,
                    &name_reference,
                    sizeof(name_reference)) ||
        !read_bytes(buffer,
                    storage->cache.capacity,
                    base + object_type_offset,
                    &object_type,
                    sizeof(object_type))) {
        return 0;
    }
    if ((returned.commonattr & (ATTR_CMN_NAME | ATTR_CMN_OBJTYPE)) !=
        (ATTR_CMN_NAME | ATTR_CMN_OBJTYPE)) {
        return 0;
    }
    name_start =
        (int64_t)base + (int64_t)name_reference_offset + (int64_t)name_reference.attr_dataoffset;
    if (name_reference.attr_length < 2U || name_start < (int64_t)base ||
        (uint64_t)name_start > (uint64_t)(base + record_length) ||
        (uint64_t)name_reference.attr_length >
            (uint64_t)(base + record_length) - (uint64_t)name_start) {
        return 0;
    }
    entry->name = buffer + (size_t)name_start;
    entry->name_length = (size_t)name_reference.attr_length - 1U;
    if (entry->name[entry->name_length] != UINT8_C(0) || entry->name_length == 0U ||
        (entry->name_length == 1U && entry->name[0] == UINT8_C('.')) ||
        (entry->name_length == 2U && entry->name[0] == UINT8_C('.') &&
         entry->name[1] == UINT8_C('.'))) {
        return 0;
    }
    for (index = 0U; index < entry->name_length; ++index) {
        if (entry->name[index] == UINT8_C(0) || entry->name[index] == UINT8_C('/')) {
            return 0;
        }
    }
    if (!r_runtime_utf8_validate(entry->name, entry->name_length, NULL)) {
        return 0;
    }
    entry->record_length = (size_t)record_length;
    entry->kind = native_file_kind(object_type);
    if ((returned.commonattr & ATTR_CMN_ERROR) != 0U) {
        entry->native_error = (int)entry_error;
    }
    return 1;
}

static _Bool materialize_cached_entry(RStdFsDirectoryIterStorage *storage,
                                      RStdFsDirectoryEntry *entry,
                                      RStdFsError *error) {
    RLibraryFsNativeEntry native_entry;
    RStdAllocError allocation_error;
    RStdFsCallStatus path_status;

    (void)memset(entry, 0, sizeof(*entry));
    if (!parse_native_entry(storage, &native_entry)) {
        *error = fs_error(R_STD_FS_ERROR_OTHER, INT64_C(0));
        return 0;
    }
    if (native_entry.native_error != 0) {
        *error = fs_error(classify_native_error(native_entry.native_error),
                          (int64_t)native_entry.native_error);
        return 0;
    }
    path_status = r_library_internal_fs_path_create(storage->allocator,
                                                    native_entry.name,
                                                    native_entry.name_length,
                                                    &entry->name,
                                                    &allocation_error);
    if (path_status != R_STD_FS_CALL_SUCCESS) {
        *error = fs_error(R_STD_FS_ERROR_RESOURCE_EXHAUSTED, INT64_C(0));
        return 0;
    }
    entry->kind = native_entry.kind;
    storage->cache_offset += native_entry.record_length;
    storage->cache_index += 1;
    if (storage->cache_index == storage->cache.entry_count) {
        r_runtime_darwin_fs_enumeration_buffer_release(&storage->cache);
        storage->cache_offset = 0U;
        storage->cache_index = 0;
    }
    return 1;
}

#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
_Bool r_library_internal_fs_testing_parse_directory_entry(RStdFsDirectoryIterStorage *storage,
                                                          RStdFsDirectoryEntry *entry,
                                                          RStdFsError *error) {
    return materialize_cached_entry(storage, entry, error);
}
#endif

static void install_enumeration_cache(RLibraryFsIterationPayload *payload) {
    RRuntimeDarwinFsEnumerationBuffer cache;

    if (payload->mode != R_LIBRARY_FS_NEXT || payload->iterator_storage == NULL ||
        payload->iterator_storage->cache.data != NULL) {
        return;
    }
    cache = r_runtime_darwin_fs_request_take_enumeration_buffer(payload->request);
    if (cache.data != NULL) {
        payload->iterator_storage->cache = cache;
        payload->iterator_storage->cache_offset = 0U;
        payload->iterator_storage->cache_index = 0;
    }
}

static void fill_next_failure(RLibraryFsIterationPayload *payload, RStdFsError error) {
    RStdFsDirectoryNextResult *result = payload->result;

    (void)memset(result, 0, sizeof(*result));
    result->kind = R_STD_FS_DIRECTORY_NEXT_FAILED;
    result->error = error;
    result->iterator.storage = payload->iterator_storage;
    payload->iterator_storage = NULL;
}

static void fill_immediate_result(RLibraryFsIterationPayload *payload) {
    if (payload->mode == R_LIBRARY_FS_ITERATE) {
        RStdFsDirectoryIterResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->r_tag = UINT32_C(1);
        result->r_payload.r_err = payload->immediate_error;
    } else {
        fill_next_failure(payload, payload->immediate_error);
    }
}

static void fill_native_result(RLibraryFsIterationPayload *payload,
                               RRuntimeDarwinFsResult native_result) {
    const _Bool success = native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE &&
                          native_result.native_error == 0;

    if (payload->mode == R_LIBRARY_FS_ITERATE) {
        RStdFsDirectoryIterResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->r_tag = success ? UINT32_C(0) : UINT32_C(1);
        if (success) {
            int descriptor = r_runtime_darwin_fs_request_take_opened_fd(payload->request);

            if (descriptor < 0 || payload->iterator_storage == NULL) {
                iteration_panic();
            }
            r_library_internal_fs_directory_iter_publish(payload->iterator_storage, descriptor);
            result->r_payload.r_ok.storage = payload->iterator_storage;
            payload->iterator_storage = NULL;
        } else {
            result->r_payload.r_err = native_result_error(native_result);
        }
        return;
    }

    install_enumeration_cache(payload);
    if (!success) {
        fill_next_failure(payload, native_result_error(native_result));
    } else if (native_result.native_return_value == 0) {
        RStdFsDirectoryNextResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->kind = R_STD_FS_DIRECTORY_NEXT_END;
        r_library_internal_fs_directory_iter_storage_release(payload->iterator_storage);
        payload->iterator_storage = NULL;
    } else {
        RStdFsDirectoryNextResult *result = payload->result;
        RStdFsError error;

        (void)memset(result, 0, sizeof(*result));
        if (!materialize_cached_entry(payload->iterator_storage, &result->entry, &error)) {
            result->kind = R_STD_FS_DIRECTORY_NEXT_FAILED;
            result->error = error;
        } else {
            result->kind = R_STD_FS_DIRECTORY_NEXT_ENTRY;
        }
        result->iterator.storage = payload->iterator_storage;
        payload->iterator_storage = NULL;
    }
}

static void release_native_request(RLibraryFsIterationPayload *payload) {
    RRuntimeDarwinFsRequest *request = payload->request;

    if (request == NULL) {
        iteration_panic();
    }
    release_tracking(payload);
    payload->request = NULL;
    r_runtime_darwin_fs_request_release(request);
}

static void finalize_cancellation(RLibraryFsIterationPayload *payload) {
    if (payload->request != NULL) {
        release_native_request(payload);
    } else {
        release_tracking(payload);
    }
    r_runtime_task_external_acknowledge(payload->execution);
}

static void finalize_terminal_completion(RLibraryFsIterationPayload *payload) {
    if (!r_runtime_task_external_select_terminal_completion(payload->execution)) {
        iteration_panic();
    }
    fill_native_result(payload, payload->native_result);
    release_native_request(payload);
    r_runtime_task_external_acknowledge(payload->execution);
}

static void finalize_after_cancellation(RLibraryFsIterationPayload *payload, unsigned int state) {
    if ((state & R_LIBRARY_FS_ITERATION_COMPLETION_REQUIRED) != 0U) {
        finalize_terminal_completion(payload);
    } else {
        finalize_cancellation(payload);
    }
}

static void native_completed(RRuntimeDarwinFsRequest *request, void *context) {
    RLibraryFsIterationPayload *payload = context;
    RRuntimeDarwinFsResult result;
    unsigned int previous_state;
    unsigned int published_state;

    if (payload->request != request) {
        iteration_panic();
    }
    result = r_runtime_darwin_fs_request_wait(request);
    if (r_runtime_task_external_try_select_completion_at(payload->execution,
                                                         result.terminal_event_sequence)) {
        fill_native_result(payload, result);
        release_native_request(payload);
        r_runtime_task_external_acknowledge(payload->execution);
        return;
    }
    payload->native_result = result;
    published_state = R_LIBRARY_FS_ITERATION_CALLBACK_RELEASED;
    if (payload->mode == R_LIBRARY_FS_NEXT &&
        result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE && result.native_error == 0 &&
        result.native_return_value == 0) {
        published_state |= R_LIBRARY_FS_ITERATION_COMPLETION_REQUIRED;
    }
    previous_state = atomic_fetch_or_explicit(
        &payload->cancellation_state, published_state, memory_order_acq_rel);
    if ((previous_state & R_LIBRARY_FS_ITERATION_CANCEL_REPORTED) != 0U) {
        finalize_after_cancellation(payload, previous_state | published_state);
    }
}

static void external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryFsIterationPayload *payload = payload_pointer;
    unsigned int previous_state;
    unsigned int published_state;

    if (payload->execution != execution) {
        iteration_panic();
    }
    if (payload->request == NULL) {
        finalize_cancellation(payload);
        return;
    }
    (void)r_runtime_darwin_fs_request_cancel(payload->request);
    published_state = R_LIBRARY_FS_ITERATION_CANCEL_REPORTED;
    previous_state = atomic_fetch_or_explicit(
        &payload->cancellation_state, published_state, memory_order_acq_rel);
    if ((previous_state & R_LIBRARY_FS_ITERATION_CALLBACK_RELEASED) != 0U) {
        finalize_after_cancellation(payload, previous_state | published_state);
    }
}

static void complete_immediately(RLibraryFsIterationPayload *payload,
                                 RRuntimeTaskExternalExecution *execution) {
    _Bool selected = 0;

    if (payload->immediate) {
        if (payload->immediate_event_sequence == UINT64_C(0)) {
            iteration_panic();
        }
        selected = r_runtime_task_external_try_select_completion_at(
            execution, payload->immediate_event_sequence);
    }
    release_tracking(payload);
    r_runtime_task_external_start_ready(execution);
    if (selected) {
        fill_immediate_result(payload);
        r_runtime_task_external_acknowledge(execution);
    }
}

static void complete_cached(RLibraryFsIterationPayload *payload,
                            RRuntimeTaskExternalExecution *execution) {
    RStdFsDirectoryNextResult *result = payload->result;
    RStdFsError error;
    _Bool selected = r_runtime_task_external_try_select_completion(execution);

    r_runtime_task_external_start_ready(execution);
    if (!selected) {
        return;
    }
    (void)memset(result, 0, sizeof(*result));
    if (materialize_cached_entry(payload->iterator_storage, &result->entry, &error)) {
        result->kind = R_STD_FS_DIRECTORY_NEXT_ENTRY;
    } else {
        result->kind = R_STD_FS_DIRECTORY_NEXT_FAILED;
        result->error = error;
    }
    result->iterator.storage = payload->iterator_storage;
    payload->iterator_storage = NULL;
    r_runtime_task_external_acknowledge(execution);
}

static void external_start(RRuntimeTaskExternalExecution *execution,
                           void *payload_pointer,
                           void *result_pointer) {
    RLibraryFsIterationPayload *payload = payload_pointer;
    RRuntimeDarwinFsSubmitResult submission;
    RLibraryFsIterationDeadlineStatus deadline_status;
    RStdFsError deadline_error;
    uint64_t timeout_nanoseconds;

    payload->execution = execution;
    payload->result = result_pointer;
    if (!payload->immediate && payload->deadline.has_value) {
        deadline_status =
            deadline_timeout(payload->deadline, &timeout_nanoseconds, &deadline_error);
        if (deadline_status != R_LIBRARY_FS_ITERATION_DEADLINE_READY) {
            payload->immediate = 1;
            payload->immediate_error = deadline_error;
            payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        }
    }
    if (payload->immediate || r_runtime_task_external_cancel_requested(execution)) {
        if (payload->prepared != NULL) {
            r_runtime_darwin_fs_prepared_abort(&payload->prepared);
        }
        complete_immediately(payload, execution);
        return;
    }
    if (payload->mode == R_LIBRARY_FS_NEXT && payload->iterator_storage->cache.data != NULL) {
        complete_cached(payload, execution);
        return;
    }
    submission = r_runtime_darwin_fs_prepared_activate(&payload->prepared);
    if (submission.status != R_RUNTIME_DARWIN_FS_START_OK || submission.request == NULL ||
        payload->prepared != NULL) {
        iteration_panic();
    }
    payload->request = submission.request;
    if (payload->mode == R_LIBRARY_FS_ITERATE &&
        !r_library_internal_fs_operation_register(
            payload->handle_storage, &payload->registration, payload->request)) {
        iteration_panic();
    }
    r_runtime_task_external_start_ready(execution);
    if (!r_runtime_darwin_fs_request_set_completion(payload->request, native_completed, payload)) {
        iteration_panic();
    }
}

static RStdFsTaskStartResult task_start_failure(RRuntimeTaskStartStatus status) {
    RStdFsTaskStartResult result = {0};

    if (status == R_RUNTIME_TASK_START_ALLOCATION_FAILED) {
        result.error = R_STD_ASYNC_START_REFUSAL();
        return result;
    }
    if (status == R_RUNTIME_TASK_START_RUNTIME_STOPPING) {
        result.error = R_STD_ASYNC_START_RUNTIME_STOPPING;
        return result;
    }
    iteration_panic();
}

static RRuntimeTaskStartStatus native_prepare_status(RRuntimeDarwinFsPrepareResult preparation) {
    switch (preparation.status) {
    case R_RUNTIME_DARWIN_FS_START_OK:
        return R_RUNTIME_TASK_START_OK;
    case R_RUNTIME_DARWIN_FS_START_RUNTIME_STOPPING:
        return R_RUNTIME_TASK_START_RUNTIME_STOPPING;
    case R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED:
    case R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED:
    case R_RUNTIME_DARWIN_FS_START_QUEUE_FULL:
        return R_RUNTIME_TASK_START_ALLOCATION_FAILED;
    case R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT:
        iteration_panic();
    }
    iteration_panic();
}

static RStdFsTaskStartResult start_task(RLibraryFsIterationPayload *payload) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryFsIterationPayload),
        _Alignof(RLibraryFsIterationPayload),
        payload_move,
        payload_drop,
    };
    RRuntimeTaskPrepareResult preparation = r_runtime_task_external_start_prepare(
        payload_type, result_type(payload->mode), external_start, external_cancel);
    RRuntimeTaskStartResult started;
    RStdFsTaskStartResult result = {0};

    if (preparation.status != R_RUNTIME_TASK_START_OK) {
        if (payload->prepared != NULL) {
            r_runtime_darwin_fs_prepared_abort(&payload->prepared);
        }
        release_tracking(payload);
        if (payload->mode == R_LIBRARY_FS_ITERATE && payload->iterator_storage != NULL) {
            r_library_internal_fs_directory_iter_storage_release(payload->iterator_storage);
            payload->iterator_storage = NULL;
        }
        return task_start_failure(preparation.status);
    }
    started = r_runtime_task_start_commit(&preparation.transaction, payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        if (payload->prepared != NULL) {
            r_runtime_darwin_fs_prepared_abort(&payload->prepared);
        }
        release_tracking(payload);
        if (payload->mode == R_LIBRARY_FS_ITERATE && payload->iterator_storage != NULL) {
            r_library_internal_fs_directory_iter_storage_release(payload->iterator_storage);
            payload->iterator_storage = NULL;
        }
        return task_start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

RStdFsTaskStartResult r_library_internal_fs_iterate(const RStdFsDirectory *directory,
                                                    RStdFsDeadline deadline) {
    RLibraryFsIterationPayload payload;
    RRuntimeDarwinFsPrepareResult native_preparation;
    RRuntimeTaskStartStatus preparation_status;
    RLibraryFsIterationDeadlineStatus deadline_status;
    RRuntimeAllocator *allocator;
    uint64_t timeout_nanoseconds = 0U;
    int descriptor;

    (void)memset(&payload, 0, sizeof(payload));
    payload.mode = R_LIBRARY_FS_ITERATE;
    payload.deadline = deadline;
    atomic_init(&payload.cancellation_state, 0U);
    payload.handle_storage = r_library_internal_fs_directory_handle_storage(directory);
    if (payload.handle_storage == NULL) {
        iteration_panic();
    }
    deadline_status = deadline_timeout(deadline, &timeout_nanoseconds, &payload.immediate_error);
    payload.immediate = deadline_status != R_LIBRARY_FS_ITERATION_DEADLINE_READY;
    if (payload.immediate) {
        payload.immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        payload.handle_storage = NULL;
        return start_task(&payload);
    }
    if (!r_library_internal_fs_handle_retain(payload.handle_storage)) {
        iteration_panic();
    }
    allocator = payload.handle_storage->allocator;
    payload.iterator_storage = r_library_internal_fs_directory_iter_reserve(allocator);
    if (payload.iterator_storage == NULL) {
        release_tracking(&payload);
        return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    descriptor = r_library_internal_fs_directory_descriptor(directory);
    if (descriptor < 0) {
        release_tracking(&payload);
        r_library_internal_fs_directory_iter_storage_release(payload.iterator_storage);
        iteration_panic();
    }
    native_preparation = r_runtime_darwin_fs_service_prepare_open_beneath(
        descriptor, ".", O_RDONLY | O_DIRECTORY, 0, timeout_nanoseconds);
    if (native_preparation.status == R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED &&
        native_preparation.native_error == EBADF) {
        payload.immediate = 1;
        payload.immediate_error =
            fs_error(R_STD_FS_ERROR_CLOSED, (int64_t)native_preparation.native_error);
        payload.immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return start_task(&payload);
    }
    preparation_status = native_prepare_status(native_preparation);
    if (preparation_status != R_RUNTIME_TASK_START_OK) {
        release_tracking(&payload);
        r_library_internal_fs_directory_iter_storage_release(payload.iterator_storage);
        return task_start_failure(preparation_status);
    }
    payload.prepared = native_preparation.prepared;
    return start_task(&payload);
}

RStdFsTaskStartResult r_library_internal_fs_next(RStdFsDirectoryIter *iterator,
                                                 RStdFsDeadline deadline) {
    RLibraryFsIterationPayload payload;
    RRuntimeDarwinFsPrepareResult native_preparation;
    RRuntimeTaskStartStatus preparation_status;
    RLibraryFsIterationDeadlineStatus deadline_status;
    struct attrlist attributes;
    uint64_t timeout_nanoseconds = 0U;

    if (iterator == NULL || iterator->storage == NULL || iterator->storage->descriptor < 0) {
        iteration_panic();
    }
    (void)memset(&payload, 0, sizeof(payload));
    payload.mode = R_LIBRARY_FS_NEXT;
    payload.staged_iterator = iterator;
    payload.iterator_storage = iterator->storage;
    payload.deadline = deadline;
    atomic_init(&payload.cancellation_state, 0U);
    deadline_status = deadline_timeout(deadline, &timeout_nanoseconds, &payload.immediate_error);
    payload.immediate = deadline_status != R_LIBRARY_FS_ITERATION_DEADLINE_READY;
    if (payload.immediate) {
        payload.immediate_event_sequence = r_runtime_darwin_event_sequence_next();
    }
    if (!payload.immediate && payload.iterator_storage->cache.data == NULL) {
        (void)memset(&attributes, 0, sizeof(attributes));
        attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
        attributes.commonattr =
            ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME | ATTR_CMN_ERROR | ATTR_CMN_OBJTYPE;
        native_preparation =
            r_runtime_darwin_fs_service_prepare_enumerate(payload.iterator_storage->descriptor,
                                                          &attributes,
                                                          R_LIBRARY_FS_ENUMERATION_BUFFER_SIZE,
                                                          timeout_nanoseconds);
        if (native_preparation.status == R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED &&
            native_preparation.native_error == EBADF) {
            payload.immediate = 1;
            payload.immediate_error =
                fs_error(R_STD_FS_ERROR_CLOSED, (int64_t)native_preparation.native_error);
            payload.immediate_event_sequence = r_runtime_darwin_event_sequence_next();
            return start_task(&payload);
        }
        preparation_status = native_prepare_status(native_preparation);
        if (preparation_status != R_RUNTIME_TASK_START_OK) {
            return task_start_failure(preparation_status);
        }
        payload.prepared = native_preparation.prepared;
    }
    return start_task(&payload);
}
