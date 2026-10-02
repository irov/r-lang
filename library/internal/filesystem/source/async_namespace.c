#include "r_library_fs_internal.h"

#include "r_library_clock_internal.h"
#include "r_library_duration_internal.h"
#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_darwin_fs_lane.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <errno.h>
#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

typedef enum RLibraryFsNamespaceMode {
    R_LIBRARY_FS_CREATE_DIRECTORY = 0,
    R_LIBRARY_FS_CREATE_DIRECTORY_BENEATH,
    R_LIBRARY_FS_METADATA,
    R_LIBRARY_FS_FILE_METADATA,
    R_LIBRARY_FS_METADATA_BENEATH,
    R_LIBRARY_FS_REMOVE_FILE_BENEATH,
    R_LIBRARY_FS_REMOVE_DIRECTORY_BENEATH,
    R_LIBRARY_FS_RENAME_BENEATH
} RLibraryFsNamespaceMode;

typedef enum RLibraryFsNamespaceDeadlineStatus {
    R_LIBRARY_FS_NAMESPACE_DEADLINE_READY = 0,
    R_LIBRARY_FS_NAMESPACE_DEADLINE_IMMEDIATE,
    R_LIBRARY_FS_NAMESPACE_DEADLINE_ERROR
} RLibraryFsNamespaceDeadlineStatus;

enum {
    R_LIBRARY_FS_NAMESPACE_CANCEL_REPORTED = 1U,
    R_LIBRARY_FS_NAMESPACE_CALLBACK_RELEASED = 2U,
    R_LIBRARY_FS_NAMESPACE_COMPLETION_REQUIRED = 4U
};

typedef struct RLibraryFsNamespacePayload {
    RLibraryFsNamespaceMode mode;
    RStdFsDeadline deadline;
    RStdFsError immediate_error;
    uint64_t immediate_event_sequence;
    RRuntimeDarwinFsResult native_result;
    RRuntimeDarwinFsPreparedRequest *prepared;
    RRuntimeDarwinFsRequest *request;
    RRuntimeTaskExternalExecution *execution;
    RLibraryFsHandleStorage *handle_storage[2];
    RLibraryFsOperationRegistration registration[2];
    size_t handle_count;
    void *result;
    _Atomic unsigned int cancellation_state;
    _Bool immediate;
} RLibraryFsNamespacePayload;

_Static_assert(sizeof(time_t) <= sizeof(int64_t), "Darwin time_t must fit R system_time");
_Static_assert(sizeof(off_t) <= sizeof(uint64_t), "Darwin off_t must fit R metadata size");

_Noreturn static void fs_namespace_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                    (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
}

static RStdFsError fs_error(RStdFsErrorCode code, int64_t native_code) {
    return (RStdFsError){code, native_code};
}

static _Bool mode_is_metadata(RLibraryFsNamespaceMode mode) {
    return mode == R_LIBRARY_FS_METADATA || mode == R_LIBRARY_FS_FILE_METADATA ||
           mode == R_LIBRARY_FS_METADATA_BENEATH;
}

static _Bool mode_is_beneath(RLibraryFsNamespaceMode mode) {
    return mode == R_LIBRARY_FS_CREATE_DIRECTORY_BENEATH || mode == R_LIBRARY_FS_METADATA_BENEATH ||
           mode == R_LIBRARY_FS_REMOVE_FILE_BENEATH ||
           mode == R_LIBRARY_FS_REMOVE_DIRECTORY_BENEATH || mode == R_LIBRARY_FS_RENAME_BENEATH;
}

static void payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryFsNamespacePayload *destination = destination_pointer;
    RLibraryFsNamespacePayload *source = source_pointer;

    (void)memset(destination, 0, sizeof(*destination));
    destination->mode = source->mode;
    destination->deadline = source->deadline;
    destination->immediate_error = source->immediate_error;
    destination->immediate_event_sequence = source->immediate_event_sequence;
    destination->prepared = source->prepared;
    destination->handle_count = source->handle_count;
    destination->handle_storage[0] = source->handle_storage[0];
    destination->handle_storage[1] = source->handle_storage[1];
    destination->immediate = source->immediate;
    atomic_init(&destination->cancellation_state, 0U);
    source->prepared = NULL;
    source->handle_count = 0U;
    source->handle_storage[0] = NULL;
    source->handle_storage[1] = NULL;
}

static void release_tracking(RLibraryFsNamespacePayload *payload) {
    size_t index;

    for (index = payload->handle_count; index > 0U; --index) {
        size_t slot = index - 1U;

        if (payload->registration[slot].registered) {
            r_library_internal_fs_operation_unregister(payload->handle_storage[slot],
                                                       &payload->registration[slot]);
        } else if (payload->handle_storage[slot] != NULL) {
            r_library_internal_fs_handle_release(payload->handle_storage[slot]);
        }
        payload->handle_storage[slot] = NULL;
    }
    payload->handle_count = 0U;
}

static void payload_drop(void *value) {
    RLibraryFsNamespacePayload *payload = value;

    if (payload->prepared != NULL) {
        r_runtime_darwin_fs_prepared_abort(&payload->prepared);
    }
    if (payload->request != NULL) {
        fs_namespace_panic();
    }
    release_tracking(payload);
}

static RRuntimeTypeInfo result_type(RLibraryFsNamespaceMode mode) {
    if (mode_is_metadata(mode)) {
        return (RRuntimeTypeInfo){
            sizeof(RStdFsMetadataResult),
            _Alignof(RStdFsMetadataResult),
            NULL,
            NULL,
        };
    }
    return (RRuntimeTypeInfo){
        sizeof(RStdFsVoidResult),
        _Alignof(RStdFsVoidResult),
        NULL,
        NULL,
    };
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

static RLibraryFsNamespaceDeadlineStatus
deadline_timeout(RStdFsDeadline deadline, uint64_t *timeout_nanoseconds, RStdFsError *error) {
    const uint64_t nanoseconds_per_second = UINT64_C(1000000000);
    RStdTimeInstantResult now;
    RStdTimeDurationTimeResult remaining;
    uint64_t seconds;

    *timeout_nanoseconds = 0U;
    if (!deadline.has_value) {
        return R_LIBRARY_FS_NAMESPACE_DEADLINE_READY;
    }
    if (deadline.value.storage_seconds < 0 ||
        deadline.value.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
        *error = fs_error(R_STD_FS_ERROR_INVALID_OPERATION, INT64_C(0));
        return R_LIBRARY_FS_NAMESPACE_DEADLINE_ERROR;
    }
    now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
    if (!now.is_ok) {
        *error = fs_error(R_STD_FS_ERROR_OTHER, now.error.native_code);
        return R_LIBRARY_FS_NAMESPACE_DEADLINE_ERROR;
    }
    if (compare_instant(deadline.value, now.value) <= 0) {
        *error = fs_error(R_STD_FS_ERROR_TIMED_OUT, INT64_C(0));
        return R_LIBRARY_FS_NAMESPACE_DEADLINE_IMMEDIATE;
    }
    remaining = r_library_internal_time_instant_duration(deadline.value, now.value);
    if (!remaining.is_ok || remaining.value.seconds < 0) {
        *error = fs_error(R_STD_FS_ERROR_OTHER, INT64_C(0));
        return R_LIBRARY_FS_NAMESPACE_DEADLINE_ERROR;
    }
    seconds = (uint64_t)remaining.value.seconds;
    if (seconds > ((uint64_t)INT64_MAX / nanoseconds_per_second)) {
        *error = fs_error(R_STD_FS_ERROR_UNSUPPORTED, INT64_C(0));
        return R_LIBRARY_FS_NAMESPACE_DEADLINE_ERROR;
    }
    *timeout_nanoseconds = seconds * nanoseconds_per_second;
    if ((uint64_t)remaining.value.nanoseconds > ((uint64_t)INT64_MAX - *timeout_nanoseconds)) {
        *error = fs_error(R_STD_FS_ERROR_UNSUPPORTED, INT64_C(0));
        return R_LIBRARY_FS_NAMESPACE_DEADLINE_ERROR;
    }
    *timeout_nanoseconds += (uint64_t)remaining.value.nanoseconds;
    return R_LIBRARY_FS_NAMESPACE_DEADLINE_READY;
}

static RStdFsErrorCode classify_native_error(RLibraryFsNamespaceMode mode, int native_error) {
    if (native_error == EBADF) {
        return R_STD_FS_ERROR_CLOSED;
    }
    if (mode_is_beneath(mode) && (native_error == ELOOP
#if defined(ENOTCAPABLE)
                                  || native_error == ENOTCAPABLE
#endif
                                  )) {
        return R_STD_FS_ERROR_INVALID_RELATIVE_PATH;
    }
    if (mode == R_LIBRARY_FS_REMOVE_FILE_BENEATH && native_error == EPERM) {
        return R_STD_FS_ERROR_IS_DIRECTORY;
    }
    if (native_error == EINVAL || native_error == EBUSY || native_error == ENXIO) {
        return R_STD_FS_ERROR_INVALID_OPERATION;
    }
    if (native_error == ENOENT) {
        return R_STD_FS_ERROR_NOT_FOUND;
    }
    if (native_error == EEXIST) {
        return R_STD_FS_ERROR_ALREADY_EXISTS;
    }
    if (native_error == ENOTDIR) {
        return R_STD_FS_ERROR_NOT_DIRECTORY;
    }
    if (native_error == EISDIR) {
        return R_STD_FS_ERROR_IS_DIRECTORY;
    }
    if (native_error == ENOTEMPTY) {
        return R_STD_FS_ERROR_DIRECTORY_NOT_EMPTY;
    }
    if (native_error == EROFS) {
        return R_STD_FS_ERROR_READ_ONLY;
    }
    if (native_error == ENAMETOOLONG) {
        return R_STD_FS_ERROR_NAME_TOO_LONG;
    }
    if (native_error == ELOOP || native_error == EMLINK) {
        return R_STD_FS_ERROR_TOO_MANY_LINKS;
    }
    if (native_error == ENOSPC || native_error == EDQUOT) {
        return R_STD_FS_ERROR_NO_SPACE;
    }
    if (native_error == EFBIG || native_error == EOVERFLOW) {
        return R_STD_FS_ERROR_FILE_TOO_LARGE;
    }
    if (native_error == EACCES || native_error == EPERM) {
        return R_STD_FS_ERROR_PERMISSION_DENIED;
    }
    if (native_error == ENOMEM || native_error == EMFILE || native_error == ENFILE ||
        native_error == ENOBUFS || native_error == EAGAIN) {
        return R_STD_FS_ERROR_RESOURCE_EXHAUSTED;
    }
    if (native_error == EXDEV || native_error == ENOSYS) {
        return R_STD_FS_ERROR_UNSUPPORTED;
    }
#if defined(ENOTSUP)
    if (native_error == ENOTSUP) {
        return R_STD_FS_ERROR_UNSUPPORTED;
    }
#endif
#if defined(EOPNOTSUPP) && (!defined(ENOTSUP) || EOPNOTSUPP != ENOTSUP)
    if (native_error == EOPNOTSUPP) {
        return R_STD_FS_ERROR_UNSUPPORTED;
    }
#endif
    return R_STD_FS_ERROR_OTHER;
}

static RStdFsError native_result_error(RLibraryFsNamespacePayload *payload,
                                       RRuntimeDarwinFsResult native_result) {
    if (native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_CANCELLED ||
        native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_STOPPING) {
        return fs_error(R_STD_FS_ERROR_CANCELLED, (int64_t)native_result.native_error);
    }
    if (native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_TIMED_OUT) {
        return fs_error(R_STD_FS_ERROR_TIMED_OUT, (int64_t)native_result.native_error);
    }
    return fs_error(classify_native_error(payload->mode, native_result.native_error),
                    (int64_t)native_result.native_error);
}

static RStdFsFileKind file_kind(mode_t mode) {
    if (S_ISREG(mode)) {
        return R_STD_FS_FILE_KIND_REGULAR;
    }
    if (S_ISDIR(mode)) {
        return R_STD_FS_FILE_KIND_DIRECTORY;
    }
    if (S_ISLNK(mode)) {
        return R_STD_FS_FILE_KIND_SYMLINK;
    }
    if (S_ISCHR(mode)) {
        return R_STD_FS_FILE_KIND_CHARACTER_DEVICE;
    }
    if (S_ISBLK(mode)) {
        return R_STD_FS_FILE_KIND_BLOCK_DEVICE;
    }
    if (S_ISFIFO(mode)) {
        return R_STD_FS_FILE_KIND_FIFO;
    }
    if (S_ISSOCK(mode)) {
        return R_STD_FS_FILE_KIND_SOCKET;
    }
    return R_STD_FS_FILE_KIND_OTHER;
}

static RStdFsSystemTimeOption system_time_option(struct timespec value) {
    RStdFsSystemTimeOption result = {0};

    if (value.tv_nsec < 0 || value.tv_nsec >= (long)R_STD_TIME_NANOSECONDS_PER_SECOND) {
        return result;
    }
    result.r_tag = UINT32_C(1);
    result.r_payload.r_some.unix_seconds = (int64_t)value.tv_sec;
    result.r_payload.r_some.nanoseconds = (uint32_t)value.tv_nsec;
    return result;
}

static RStdFsMetadata convert_metadata(struct stat value) {
    RStdFsMetadata result = {0};

    result.kind = file_kind(value.st_mode);
    if ((result.kind == R_STD_FS_FILE_KIND_REGULAR || result.kind == R_STD_FS_FILE_KIND_SYMLINK) &&
        value.st_size >= 0) {
        result.size = (uint64_t)value.st_size;
    }
    result.created = system_time_option(value.st_birthtimespec);
    result.modified = system_time_option(value.st_mtimespec);
    result.accessed = system_time_option(value.st_atimespec);
    return result;
}

static void fill_immediate_result(RLibraryFsNamespacePayload *payload) {
    if (mode_is_metadata(payload->mode)) {
        RStdFsMetadataResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->r_tag = UINT32_C(1);
        result->r_payload.r_err = payload->immediate_error;
    } else {
        RStdFsVoidResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->r_tag = UINT32_C(1);
        result->r_payload.r_err = payload->immediate_error;
    }
}

static void fill_native_result(RLibraryFsNamespacePayload *payload,
                               RRuntimeDarwinFsResult native_result) {
    const _Bool success = native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE &&
                          native_result.native_error == 0;

    if (mode_is_metadata(payload->mode)) {
        RStdFsMetadataResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->r_tag = success ? UINT32_C(0) : UINT32_C(1);
        if (success) {
            result->r_payload.r_ok = convert_metadata(native_result.metadata);
        } else {
            result->r_payload.r_err = native_result_error(payload, native_result);
        }
    } else {
        RStdFsVoidResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->r_tag = success ? UINT32_C(0) : UINT32_C(1);
        if (!success) {
            result->r_payload.r_err = native_result_error(payload, native_result);
        }
    }
}

static void release_native_request(RLibraryFsNamespacePayload *payload) {
    RRuntimeDarwinFsRequest *request = payload->request;

    if (request == NULL) {
        fs_namespace_panic();
    }
    release_tracking(payload);
    payload->request = NULL;
    r_runtime_darwin_fs_request_release(request);
}

static void finalize_cancellation(RLibraryFsNamespacePayload *payload) {
    if (payload->request != NULL) {
        release_native_request(payload);
    } else {
        release_tracking(payload);
    }
    r_runtime_task_external_acknowledge(payload->execution);
}

static void finalize_terminal_completion(RLibraryFsNamespacePayload *payload) {
    if (!r_runtime_task_external_select_terminal_completion(payload->execution)) {
        fs_namespace_panic();
    }
    fill_native_result(payload, payload->native_result);
    release_native_request(payload);
    r_runtime_task_external_acknowledge(payload->execution);
}

static void finalize_after_cancellation(RLibraryFsNamespacePayload *payload, unsigned int state) {
    if ((state & R_LIBRARY_FS_NAMESPACE_COMPLETION_REQUIRED) != 0U) {
        finalize_terminal_completion(payload);
    } else {
        finalize_cancellation(payload);
    }
}

static void native_completed(RRuntimeDarwinFsRequest *request, void *context) {
    RLibraryFsNamespacePayload *payload = context;
    RRuntimeDarwinFsResult result;
    unsigned int previous_state;
    unsigned int published_state;

    if (payload->request != request) {
        fs_namespace_panic();
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
    published_state = R_LIBRARY_FS_NAMESPACE_CALLBACK_RELEASED;
    if (result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE && result.native_error == 0 &&
        result.committed) {
        published_state |= R_LIBRARY_FS_NAMESPACE_COMPLETION_REQUIRED;
    }
    previous_state = atomic_fetch_or_explicit(
        &payload->cancellation_state, published_state, memory_order_acq_rel);
    if ((previous_state & R_LIBRARY_FS_NAMESPACE_CANCEL_REPORTED) != 0U) {
        finalize_after_cancellation(payload, previous_state | published_state);
    }
}

static void external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryFsNamespacePayload *payload = payload_pointer;
    unsigned int previous_state;
    unsigned int published_state;

    if (payload->execution != execution) {
        fs_namespace_panic();
    }
    if (payload->request == NULL) {
        finalize_cancellation(payload);
        return;
    }
    (void)r_runtime_darwin_fs_request_cancel(payload->request);
    published_state = R_LIBRARY_FS_NAMESPACE_CANCEL_REPORTED;
    previous_state = atomic_fetch_or_explicit(
        &payload->cancellation_state, published_state, memory_order_acq_rel);
    if ((previous_state & R_LIBRARY_FS_NAMESPACE_CALLBACK_RELEASED) != 0U) {
        finalize_after_cancellation(payload, previous_state | published_state);
    }
}

static void complete_immediately(RLibraryFsNamespacePayload *payload,
                                 RRuntimeTaskExternalExecution *execution) {
    _Bool selected = 0;

    if (payload->immediate) {
        if (payload->immediate_event_sequence == UINT64_C(0)) {
            fs_namespace_panic();
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

static void external_start(RRuntimeTaskExternalExecution *execution,
                           void *payload_pointer,
                           void *result_pointer) {
    RLibraryFsNamespacePayload *payload = payload_pointer;
    RRuntimeDarwinFsSubmitResult submission;
    RLibraryFsNamespaceDeadlineStatus deadline_status;
    RStdFsError deadline_error;
    uint64_t timeout_nanoseconds;

    payload->execution = execution;
    payload->result = result_pointer;
    if (!payload->immediate && payload->deadline.has_value) {
        deadline_status =
            deadline_timeout(payload->deadline, &timeout_nanoseconds, &deadline_error);
        if (deadline_status != R_LIBRARY_FS_NAMESPACE_DEADLINE_READY) {
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
    submission = r_runtime_darwin_fs_prepared_activate(&payload->prepared);
    if (submission.status != R_RUNTIME_DARWIN_FS_START_OK || submission.request == NULL ||
        payload->prepared != NULL) {
        fs_namespace_panic();
    }
    payload->request = submission.request;
    for (size_t index = 0U; index < payload->handle_count; ++index) {
        if (!r_library_internal_fs_operation_register(
                payload->handle_storage[index], &payload->registration[index], payload->request)) {
            fs_namespace_panic();
        }
    }
    r_runtime_task_external_start_ready(execution);
    if (!r_runtime_darwin_fs_request_set_completion(payload->request, native_completed, payload)) {
        fs_namespace_panic();
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
    fs_namespace_panic();
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
        fs_namespace_panic();
    }
    fs_namespace_panic();
}

static RStdFsTaskStartResult start_task(RLibraryFsNamespacePayload *payload) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryFsNamespacePayload),
        _Alignof(RLibraryFsNamespacePayload),
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
        return task_start_failure(preparation.status);
    }
    started = r_runtime_task_start_commit(&preparation.transaction, payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        if (payload->prepared != NULL) {
            r_runtime_darwin_fs_prepared_abort(&payload->prepared);
        }
        release_tracking(payload);
        return task_start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

static void
set_path_error(RLibraryFsNamespacePayload *payload, const RStdFsPath *path, _Bool beneath) {
    if (beneath &&
        r_library_internal_fs_validate_beneath_relative(path) != R_LIBRARY_FS_RELATIVE_PATH_VALID) {
        payload->immediate = 1;
        payload->immediate_error = fs_error(R_STD_FS_ERROR_INVALID_RELATIVE_PATH, INT64_C(0));
    } else if (!beneath && (r_library_internal_fs_path_length(path) == 0U ||
                            r_library_internal_fs_path_has_reserved_component(path))) {
        payload->immediate = 1;
        payload->immediate_error = fs_error(R_STD_FS_ERROR_INVALID_PATH, INT64_C(0));
    } else if (r_library_internal_fs_path_length(path) >= R_RUNTIME_DARWIN_FS_PATH_SCAN_LIMIT) {
        payload->immediate = 1;
        payload->immediate_error = fs_error(R_STD_FS_ERROR_NAME_TOO_LONG, INT64_C(0));
    }
}

static void retain_handle(RLibraryFsNamespacePayload *payload, RLibraryFsHandleStorage *storage) {
    if (payload->handle_count >= 2U || storage == NULL ||
        !r_library_internal_fs_handle_retain(storage)) {
        fs_namespace_panic();
    }
    payload->handle_storage[payload->handle_count] = storage;
    payload->handle_count += 1U;
}

static RStdFsTaskStartResult start_namespace(RLibraryFsNamespaceMode mode,
                                             const RStdFsDirectory *first_root,
                                             const RStdFsPath *first_path,
                                             const RStdFsDirectory *second_root,
                                             const RStdFsPath *second_path,
                                             const RStdFsFile *file,
                                             _Bool recursive,
                                             RStdFsDeadline deadline) {
    const mode_t directory_mode = S_IRWXU;
    RLibraryFsNamespacePayload payload;
    RRuntimeDarwinFsPrepareResult native_preparation;
    RRuntimeTaskStartStatus preparation_status;
    RLibraryFsNamespaceDeadlineStatus deadline_status;
    uint64_t timeout_nanoseconds = 0U;
    int first_descriptor = -1;
    int second_descriptor = -1;

    (void)memset(&payload, 0, sizeof(payload));
    payload.mode = mode;
    payload.deadline = deadline;
    atomic_init(&payload.cancellation_state, 0U);
    if (mode == R_LIBRARY_FS_FILE_METADATA) {
        first_descriptor = r_library_internal_fs_file_descriptor(file);
        if (first_descriptor < 0) {
            fs_namespace_panic();
        }
    } else {
        set_path_error(&payload, first_path, mode_is_beneath(mode));
        if (mode_is_beneath(mode)) {
            first_descriptor = r_library_internal_fs_directory_descriptor(first_root);
            if (first_descriptor < 0) {
                fs_namespace_panic();
            }
        }
        if (mode == R_LIBRARY_FS_RENAME_BENEATH) {
            if (!payload.immediate) {
                set_path_error(&payload, second_path, 1);
            }
            second_descriptor = r_library_internal_fs_directory_descriptor(second_root);
            if (second_descriptor < 0) {
                fs_namespace_panic();
            }
        }
    }
    if (!payload.immediate) {
        deadline_status =
            deadline_timeout(deadline, &timeout_nanoseconds, &payload.immediate_error);
        payload.immediate = deadline_status != R_LIBRARY_FS_NAMESPACE_DEADLINE_READY;
    }
    if (payload.immediate) {
        payload.immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return start_task(&payload);
    }
    if (mode == R_LIBRARY_FS_FILE_METADATA) {
        retain_handle(&payload, r_library_internal_fs_file_handle_storage(file));
    } else if (mode_is_beneath(mode)) {
        retain_handle(&payload, r_library_internal_fs_directory_handle_storage(first_root));
        if (mode == R_LIBRARY_FS_RENAME_BENEATH) {
            retain_handle(&payload, r_library_internal_fs_directory_handle_storage(second_root));
        }
    }
    switch (mode) {
    case R_LIBRARY_FS_CREATE_DIRECTORY:
        native_preparation = r_runtime_darwin_fs_service_prepare_create_directory(
            (const char *)r_library_internal_fs_path_bytes(first_path),
            directory_mode,
            recursive,
            timeout_nanoseconds);
        break;
    case R_LIBRARY_FS_CREATE_DIRECTORY_BENEATH:
        native_preparation = r_runtime_darwin_fs_service_prepare_create_directory_beneath(
            first_descriptor,
            (const char *)r_library_internal_fs_path_bytes(first_path),
            directory_mode,
            recursive,
            timeout_nanoseconds);
        break;
    case R_LIBRARY_FS_METADATA:
        native_preparation = r_runtime_darwin_fs_service_prepare_metadata(
            (const char *)r_library_internal_fs_path_bytes(first_path), timeout_nanoseconds);
        break;
    case R_LIBRARY_FS_FILE_METADATA:
        native_preparation = r_runtime_darwin_fs_service_prepare_file_metadata(first_descriptor,
                                                                               timeout_nanoseconds);
        break;
    case R_LIBRARY_FS_METADATA_BENEATH:
        native_preparation = r_runtime_darwin_fs_service_prepare_metadata_beneath(
            first_descriptor,
            (const char *)r_library_internal_fs_path_bytes(first_path),
            timeout_nanoseconds);
        break;
    case R_LIBRARY_FS_REMOVE_FILE_BENEATH:
    case R_LIBRARY_FS_REMOVE_DIRECTORY_BENEATH:
        native_preparation = r_runtime_darwin_fs_service_prepare_remove_beneath(
            first_descriptor,
            (const char *)r_library_internal_fs_path_bytes(first_path),
            mode == R_LIBRARY_FS_REMOVE_DIRECTORY_BENEATH,
            timeout_nanoseconds);
        break;
    case R_LIBRARY_FS_RENAME_BENEATH:
        native_preparation = r_runtime_darwin_fs_service_prepare_rename_beneath(
            first_descriptor,
            (const char *)r_library_internal_fs_path_bytes(first_path),
            second_descriptor,
            (const char *)r_library_internal_fs_path_bytes(second_path),
            timeout_nanoseconds);
        break;
    }
    preparation_status = native_prepare_status(native_preparation);
    if (preparation_status != R_RUNTIME_TASK_START_OK) {
        release_tracking(&payload);
        return task_start_failure(preparation_status);
    }
    payload.prepared = native_preparation.prepared;
    return start_task(&payload);
}

RStdFsTaskStartResult r_library_internal_fs_create_directory(const RStdFsPath *path,
                                                             _Bool recursive,
                                                             RStdFsDeadline deadline) {
    return start_namespace(
        R_LIBRARY_FS_CREATE_DIRECTORY, NULL, path, NULL, NULL, NULL, recursive, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_create_directory_beneath(const RStdFsDirectory *root,
                                                                     const RStdFsPath *relative,
                                                                     _Bool recursive,
                                                                     RStdFsDeadline deadline) {
    return start_namespace(R_LIBRARY_FS_CREATE_DIRECTORY_BENEATH,
                           root,
                           relative,
                           NULL,
                           NULL,
                           NULL,
                           recursive,
                           deadline);
}

RStdFsTaskStartResult r_library_internal_fs_file_metadata(const RStdFsFile *file,
                                                          RStdFsDeadline deadline) {
    return start_namespace(R_LIBRARY_FS_FILE_METADATA, NULL, NULL, NULL, NULL, file, 0, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_metadata(const RStdFsPath *path,
                                                     RStdFsDeadline deadline) {
    return start_namespace(R_LIBRARY_FS_METADATA, NULL, path, NULL, NULL, NULL, 0, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_metadata_beneath(const RStdFsDirectory *root,
                                                             const RStdFsPath *relative,
                                                             RStdFsDeadline deadline) {
    return start_namespace(
        R_LIBRARY_FS_METADATA_BENEATH, root, relative, NULL, NULL, NULL, 0, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_remove_file_beneath(const RStdFsDirectory *root,
                                                                const RStdFsPath *relative,
                                                                RStdFsDeadline deadline) {
    return start_namespace(
        R_LIBRARY_FS_REMOVE_FILE_BENEATH, root, relative, NULL, NULL, NULL, 0, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_remove_directory_beneath(const RStdFsDirectory *root,
                                                                     const RStdFsPath *relative,
                                                                     RStdFsDeadline deadline) {
    return start_namespace(
        R_LIBRARY_FS_REMOVE_DIRECTORY_BENEATH, root, relative, NULL, NULL, NULL, 0, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_rename_beneath(const RStdFsDirectory *source_root,
                                                           const RStdFsPath *source,
                                                           const RStdFsDirectory *destination_root,
                                                           const RStdFsPath *destination,
                                                           RStdFsDeadline deadline) {
    return start_namespace(R_LIBRARY_FS_RENAME_BENEATH,
                           source_root,
                           source,
                           destination_root,
                           destination,
                           NULL,
                           0,
                           deadline);
}
