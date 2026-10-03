#include "r_library_fs_internal.h"

#include "r_library_clock_internal.h"
#include "r_library_duration_internal.h"
#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_darwin_fs_lane.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef enum RLibraryFsOpenMode {
    R_LIBRARY_FS_OPEN_DIRECTORY = 0,
    R_LIBRARY_FS_OPEN_DIRECTORY_BENEATH,
    R_LIBRARY_FS_OPEN_FILE,
    R_LIBRARY_FS_OPEN_FILE_BENEATH
} RLibraryFsOpenMode;

typedef enum RLibraryFsDeadlineStatus {
    R_LIBRARY_FS_DEADLINE_READY = 0,
    R_LIBRARY_FS_DEADLINE_IMMEDIATE,
    R_LIBRARY_FS_DEADLINE_ERROR
} RLibraryFsDeadlineStatus;

enum {
    R_LIBRARY_FS_CANCEL_REPORTED = 1U,
    R_LIBRARY_FS_CALLBACK_RELEASED = 2U,
    R_LIBRARY_FS_COMPLETION_REQUIRED = 4U
};

typedef struct RLibraryFsOpenPayload {
    RLibraryFsOpenMode mode;
    RStdFsOpenFileOptions options;
    RStdFsDeadline deadline;
    RStdFsError immediate_error;
    uint64_t immediate_event_sequence;
    RRuntimeDarwinFsResult native_result;
    RRuntimeDarwinFsPreparedRequest *prepared;
    RRuntimeDarwinFsRequest *request;
    RStdFsDirectoryStorage *directory_storage;
    RStdFsFileStorage *file_storage;
    RLibraryFsHandleStorage *handle_storage;
    RLibraryFsOperationRegistration registration;
    RRuntimeTaskExternalExecution *execution;
    void *result;
    _Atomic unsigned int cancellation_state;
    _Bool immediate;
} RLibraryFsOpenPayload;

_Noreturn static void fs_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                    (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
}

static _Bool mode_is_directory(RLibraryFsOpenMode mode) {
    return mode == R_LIBRARY_FS_OPEN_DIRECTORY || mode == R_LIBRARY_FS_OPEN_DIRECTORY_BENEATH;
}

static _Bool mode_is_beneath(RLibraryFsOpenMode mode) {
    return mode == R_LIBRARY_FS_OPEN_DIRECTORY_BENEATH || mode == R_LIBRARY_FS_OPEN_FILE_BENEATH;
}

static RStdFsError fs_error(RStdFsErrorCode code, int64_t native_code) {
    return (RStdFsError){code, native_code};
}

static void release_reserved_storage(RLibraryFsOpenPayload *payload) {
    if (payload->directory_storage != NULL) {
        r_library_internal_fs_directory_storage_release(payload->directory_storage);
        payload->directory_storage = NULL;
    }
    if (payload->file_storage != NULL) {
        r_library_internal_fs_file_storage_release(payload->file_storage);
        payload->file_storage = NULL;
    }
}

static void payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryFsOpenPayload *destination = destination_pointer;
    RLibraryFsOpenPayload *source = source_pointer;

    (void)memset(destination, 0, sizeof(*destination));
    destination->mode = source->mode;
    destination->options = source->options;
    destination->deadline = source->deadline;
    destination->immediate_error = source->immediate_error;
    destination->immediate_event_sequence = source->immediate_event_sequence;
    destination->prepared = source->prepared;
    destination->directory_storage = source->directory_storage;
    destination->file_storage = source->file_storage;
    destination->handle_storage = source->handle_storage;
    destination->immediate = source->immediate;
    atomic_init(&destination->cancellation_state, 0U);
    source->prepared = NULL;
    source->directory_storage = NULL;
    source->file_storage = NULL;
    source->handle_storage = NULL;
}

static void release_tracking(RLibraryFsOpenPayload *payload) {
    if (payload->registration.registered) {
        r_library_internal_fs_operation_unregister(payload->handle_storage, &payload->registration);
        payload->handle_storage = NULL;
    } else if (payload->handle_storage != NULL) {
        r_library_internal_fs_handle_release(payload->handle_storage);
        payload->handle_storage = NULL;
    }
}

static void payload_drop(void *value) {
    RLibraryFsOpenPayload *payload = value;

    if (payload->prepared != NULL) {
        r_runtime_darwin_fs_prepared_abort(&payload->prepared);
    }
    if (payload->request != NULL) {
        fs_panic();
    }
    release_tracking(payload);
    release_reserved_storage(payload);
}

static void directory_result_move(void *destination_pointer, void *source_pointer) {
    RStdFsDirectoryResult *destination = destination_pointer;
    RStdFsDirectoryResult *source = source_pointer;

    *destination = *source;
    if (source->r_tag == UINT32_C(0)) {
        source->r_payload.r_ok.storage = NULL;
    }
}

static void directory_result_drop(void *value) {
    RStdFsDirectoryResult *result = value;

    if (result->r_tag == UINT32_C(0)) {
        r_library_internal_fs_directory_drop(&result->r_payload.r_ok);
    }
}

static void file_result_move(void *destination_pointer, void *source_pointer) {
    RStdFsFileResult *destination = destination_pointer;
    RStdFsFileResult *source = source_pointer;

    *destination = *source;
    if (source->r_tag == UINT32_C(0)) {
        source->r_payload.r_ok.storage = NULL;
    }
}

static void file_result_drop(void *value) {
    RStdFsFileResult *result = value;

    if (result->r_tag == UINT32_C(0)) {
        r_library_internal_fs_file_drop(&result->r_payload.r_ok);
    }
}

static RRuntimeTypeInfo result_type(RLibraryFsOpenMode mode) {
    if (mode_is_directory(mode)) {
        return (RRuntimeTypeInfo){
            sizeof(RStdFsDirectoryResult),
            _Alignof(RStdFsDirectoryResult),
            directory_result_move,
            directory_result_drop,
        };
    }
    return (RRuntimeTypeInfo){
        sizeof(RStdFsFileResult),
        _Alignof(RStdFsFileResult),
        file_result_move,
        file_result_drop,
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

static RLibraryFsDeadlineStatus
deadline_timeout(RStdFsDeadline deadline, uint64_t *timeout_nanoseconds, RStdFsError *error) {
    const uint64_t nanoseconds_per_second = UINT64_C(1000000000);
    RStdTimeInstantResult now;
    RStdTimeDurationTimeResult remaining;
    uint64_t seconds;

    *timeout_nanoseconds = 0U;
    if (!deadline.has_value) {
        return R_LIBRARY_FS_DEADLINE_READY;
    }
    if (deadline.value.storage_seconds < 0 ||
        deadline.value.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
        *error = fs_error(R_STD_FS_ERROR_INVALID_OPERATION, INT64_C(0));
        return R_LIBRARY_FS_DEADLINE_ERROR;
    }
    now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
    if (!now.is_ok) {
        *error = fs_error(R_STD_FS_ERROR_OTHER, now.error.native_code);
        return R_LIBRARY_FS_DEADLINE_ERROR;
    }
    if (compare_instant(deadline.value, now.value) <= 0) {
        *error = fs_error(R_STD_FS_ERROR_TIMED_OUT, INT64_C(0));
        return R_LIBRARY_FS_DEADLINE_IMMEDIATE;
    }
    remaining = r_library_internal_time_instant_duration(deadline.value, now.value);
    if (!remaining.is_ok || remaining.value.seconds < 0) {
        *error = fs_error(R_STD_FS_ERROR_OTHER, INT64_C(0));
        return R_LIBRARY_FS_DEADLINE_ERROR;
    }
    seconds = (uint64_t)remaining.value.seconds;
    if (seconds > ((uint64_t)INT64_MAX / nanoseconds_per_second)) {
        *error = fs_error(R_STD_FS_ERROR_UNSUPPORTED, INT64_C(0));
        return R_LIBRARY_FS_DEADLINE_ERROR;
    }
    *timeout_nanoseconds = seconds * nanoseconds_per_second;
    if ((uint64_t)remaining.value.nanoseconds > ((uint64_t)INT64_MAX - *timeout_nanoseconds)) {
        *error = fs_error(R_STD_FS_ERROR_UNSUPPORTED, INT64_C(0));
        return R_LIBRARY_FS_DEADLINE_ERROR;
    }
    *timeout_nanoseconds += (uint64_t)remaining.value.nanoseconds;
    return R_LIBRARY_FS_DEADLINE_READY;
}

static RStdFsErrorCode classify_native_error(RLibraryFsOpenPayload *payload, int native_error) {
    if (native_error == EBADF) {
        return R_STD_FS_ERROR_CLOSED;
    }
    if (mode_is_beneath(payload->mode) && (native_error == ELOOP || native_error == EINVAL
#if defined(ENOTCAPABLE)
                                           || native_error == ENOTCAPABLE
#endif
                                           )) {
        return R_STD_FS_ERROR_INVALID_RELATIVE_PATH;
    }
    if (!mode_is_beneath(payload->mode) && native_error == ELOOP &&
        !payload->options.follow_final_symlink) {
        return R_STD_FS_ERROR_INVALID_PATH;
    }
    if (native_error == EINVAL || native_error == ENXIO || native_error == ESPIPE) {
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
    if (native_error == ELOOP) {
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

static RStdFsError native_result_error(RLibraryFsOpenPayload *payload,
                                       RRuntimeDarwinFsResult native_result) {
    if (native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_CANCELLED ||
        native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_STOPPING) {
        return fs_error(R_STD_FS_ERROR_CANCELLED, (int64_t)native_result.native_error);
    }
    if (native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_TIMED_OUT) {
        return fs_error(R_STD_FS_ERROR_TIMED_OUT, (int64_t)native_result.native_error);
    }
    return fs_error(classify_native_error(payload, native_result.native_error),
                    (int64_t)native_result.native_error);
}

static void fill_immediate_result(RLibraryFsOpenPayload *payload) {
    if (mode_is_directory(payload->mode)) {
        RStdFsDirectoryResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->r_tag = UINT32_C(1);
        result->r_payload.r_err = payload->immediate_error;
    } else {
        RStdFsFileResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->r_tag = UINT32_C(1);
        result->r_payload.r_err = payload->immediate_error;
    }
}

static void fill_native_result(RLibraryFsOpenPayload *payload,
                               RRuntimeDarwinFsResult native_result) {
    const _Bool success = native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE &&
                          native_result.native_error == 0;

    if (mode_is_directory(payload->mode)) {
        RStdFsDirectoryResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->r_tag = success ? UINT32_C(0) : UINT32_C(1);
        if (success) {
            int descriptor = r_runtime_darwin_fs_request_take_opened_fd(payload->request);

            if (descriptor < 0 || payload->directory_storage == NULL) {
                fs_panic();
            }
            r_library_internal_fs_directory_publish(payload->directory_storage, descriptor);
            result->r_payload.r_ok.storage = payload->directory_storage;
            payload->directory_storage = NULL;
        } else {
            result->r_payload.r_err = native_result_error(payload, native_result);
        }
    } else {
        RStdFsFileResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->r_tag = success ? UINT32_C(0) : UINT32_C(1);
        if (success) {
            RRuntimeDarwinIoHandleCreateResult io_created;
            RRuntimeDarwinIoHandle *payload_io;
            int descriptor = r_runtime_darwin_fs_request_opened_fd(payload->request);
            int terminal_error = 0;

            if (descriptor < 0 || payload->file_storage == NULL) {
                fs_panic();
            }
            io_created = r_library_internal_fs_payload_handle_create(
                payload->file_storage->handle.allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
            if (io_created.status != R_RUNTIME_DARWIN_IO_START_OK) {
                int64_t materialization_native_code;

                if (io_created.handle != NULL ||
                    (io_created.status != R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED &&
                     io_created.status != R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED)) {
                    fs_panic();
                }
                materialization_native_code =
                    io_created.status == R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED
                        ? INT64_C(0)
                        : (int64_t)io_created.native_error;
                if (!r_runtime_darwin_fs_request_discard_opened_fd(payload->request)) {
                    fs_panic();
                }
                result->r_tag = UINT32_C(1);
                result->r_payload.r_err =
                    fs_error(R_STD_FS_ERROR_RESOURCE_EXHAUSTED, materialization_native_code);
                return;
            }
            payload_io = io_created.handle;
            if (payload_io == NULL) {
                fs_panic();
            }
            if (r_runtime_darwin_io_handle_terminal_close_failure(payload_io, &terminal_error)) {
                r_runtime_darwin_io_handle_release(payload_io);
                if (!r_runtime_darwin_fs_request_discard_opened_fd(payload->request)) {
                    fs_panic();
                }
                result->r_tag = UINT32_C(1);
                result->r_payload.r_err =
                    fs_error(R_STD_FS_ERROR_RESOURCE_EXHAUSTED, terminal_error);
                return;
            }
            descriptor = r_runtime_darwin_fs_request_take_opened_fd(payload->request);
            if (descriptor < 0) {
                r_runtime_darwin_io_handle_release(payload_io);
                fs_panic();
            }
            r_library_internal_fs_file_publish(
                payload->file_storage, descriptor, payload->options, payload_io);
            result->r_payload.r_ok.storage = payload->file_storage;
            payload->file_storage = NULL;
        } else {
            result->r_payload.r_err = native_result_error(payload, native_result);
        }
    }
}

static void release_native_request(RLibraryFsOpenPayload *payload) {
    RRuntimeDarwinFsRequest *request = payload->request;

    if (request == NULL) {
        fs_panic();
    }
    release_tracking(payload);
    payload->request = NULL;
    r_runtime_darwin_fs_request_release(request);
}

static void finalize_cancellation(RLibraryFsOpenPayload *payload) {
    if (payload->request != NULL) {
        release_native_request(payload);
    } else {
        release_tracking(payload);
    }
    r_runtime_task_external_acknowledge(payload->execution);
}

static void finalize_terminal_completion(RLibraryFsOpenPayload *payload) {
    if (!r_runtime_task_external_select_terminal_completion(payload->execution)) {
        fs_panic();
    }
    fill_native_result(payload, payload->native_result);
    release_native_request(payload);
    r_runtime_task_external_acknowledge(payload->execution);
}

static void finalize_after_cancellation(RLibraryFsOpenPayload *payload, unsigned int state) {
    if ((state & R_LIBRARY_FS_COMPLETION_REQUIRED) != 0U) {
        finalize_terminal_completion(payload);
    } else {
        finalize_cancellation(payload);
    }
}

static void native_completed(RRuntimeDarwinFsRequest *request, void *context) {
    RLibraryFsOpenPayload *payload = context;
    RRuntimeDarwinFsResult result;
    unsigned int previous_state;
    unsigned int published_state;

    if (payload->request != request) {
        fs_panic();
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
    published_state = R_LIBRARY_FS_CALLBACK_RELEASED;
    if (result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE && result.native_error == 0 &&
        result.committed) {
        published_state |= R_LIBRARY_FS_COMPLETION_REQUIRED;
    }
    previous_state = atomic_fetch_or_explicit(
        &payload->cancellation_state, published_state, memory_order_acq_rel);
    if ((previous_state & R_LIBRARY_FS_CANCEL_REPORTED) != 0U) {
        finalize_after_cancellation(payload, previous_state | published_state);
    }
}

static void external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryFsOpenPayload *payload = payload_pointer;
    unsigned int previous_state;
    unsigned int published_state;

    if (payload->execution != execution) {
        fs_panic();
    }
    if (payload->request == NULL) {
        finalize_cancellation(payload);
        return;
    }
    (void)r_runtime_darwin_fs_request_cancel(payload->request);
    published_state = R_LIBRARY_FS_CANCEL_REPORTED;
    previous_state = atomic_fetch_or_explicit(
        &payload->cancellation_state, published_state, memory_order_acq_rel);
    if ((previous_state & R_LIBRARY_FS_CALLBACK_RELEASED) != 0U) {
        finalize_after_cancellation(payload, previous_state | published_state);
    }
}

static void complete_immediately(RLibraryFsOpenPayload *payload,
                                 RRuntimeTaskExternalExecution *execution) {
    _Bool selected = 0;

    if (payload->immediate) {
        if (payload->immediate_event_sequence == UINT64_C(0)) {
            fs_panic();
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
    RLibraryFsOpenPayload *payload = payload_pointer;
    RRuntimeDarwinFsSubmitResult submission;
    RLibraryFsDeadlineStatus deadline_status;
    RStdFsError deadline_error;
    uint64_t timeout_nanoseconds;

    payload->execution = execution;
    payload->result = result_pointer;
    if (!payload->immediate && payload->deadline.has_value) {
        deadline_status =
            deadline_timeout(payload->deadline, &timeout_nanoseconds, &deadline_error);
        if (deadline_status != R_LIBRARY_FS_DEADLINE_READY) {
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
        fs_panic();
    }
    payload->request = submission.request;
    if (payload->handle_storage != NULL &&
        !r_library_internal_fs_operation_register(
            payload->handle_storage, &payload->registration, payload->request)) {
        fs_panic();
    }
    r_runtime_task_external_start_ready(execution);
    if (!r_runtime_darwin_fs_request_set_completion(payload->request, native_completed, payload)) {
        fs_panic();
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
    fs_panic();
}

static void release_uncommitted_payload(RLibraryFsOpenPayload *payload) {
    if (payload->prepared != NULL) {
        r_runtime_darwin_fs_prepared_abort(&payload->prepared);
    }
    release_tracking(payload);
    release_reserved_storage(payload);
}

static RStdFsTaskStartResult start_task(RLibraryFsOpenPayload *payload) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryFsOpenPayload),
        _Alignof(RLibraryFsOpenPayload),
        payload_move,
        payload_drop,
    };
    RRuntimeTaskPrepareResult preparation = r_runtime_task_external_start_prepare(
        payload_type, result_type(payload->mode), external_start, external_cancel);
    RRuntimeTaskStartResult started;
    RStdFsTaskStartResult result = {0};

    if (preparation.status != R_RUNTIME_TASK_START_OK) {
        release_uncommitted_payload(payload);
        return task_start_failure(preparation.status);
    }
    started = r_runtime_task_start_commit(&preparation.transaction, payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        release_uncommitted_payload(payload);
        return task_start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

static _Bool options_are_valid(RStdFsOpenFileOptions options) {
    const _Bool write_access =
        options.access == R_STD_FS_ACCESS_WRITE || options.access == R_STD_FS_ACCESS_READ_WRITE;

    return (options.access == R_STD_FS_ACCESS_READ || options.access == R_STD_FS_ACCESS_WRITE ||
            options.access == R_STD_FS_ACCESS_READ_WRITE) &&
           (options.create == R_STD_FS_CREATE_EXISTING ||
            options.create == R_STD_FS_CREATE_OPEN_OR_CREATE ||
            options.create == R_STD_FS_CREATE_NEW) &&
           (!options.truncate || write_access) && (!options.append || write_access);
}

static int file_open_flags(RStdFsOpenFileOptions options) {
    int flags;

    switch (options.access) {
    case R_STD_FS_ACCESS_READ:
        flags = O_RDONLY;
        break;
    case R_STD_FS_ACCESS_WRITE:
        flags = O_WRONLY;
        break;
    case R_STD_FS_ACCESS_READ_WRITE:
        flags = O_RDWR;
        break;
    }
    switch (options.create) {
    case R_STD_FS_CREATE_EXISTING:
        break;
    case R_STD_FS_CREATE_OPEN_OR_CREATE:
        flags |= O_CREAT;
        break;
    case R_STD_FS_CREATE_NEW:
        flags |= O_CREAT | O_EXCL;
        break;
    }
    if (options.truncate) {
        flags |= O_TRUNC;
    }
    if (options.append) {
        flags |= O_APPEND;
    }
    return flags;
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
        fs_panic();
    }
    fs_panic();
}

static RStdFsTaskStartResult start_open(RLibraryFsOpenMode mode,
                                        const RStdFsDirectory *root,
                                        const RStdFsPath *path,
                                        RStdFsOpenFileOptions options,
                                        RStdFsDeadline deadline) {
    const mode_t creation_mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH | S_IWOTH;
    RLibraryFsOpenPayload payload;
    RRuntimeDarwinFsPrepareResult native_preparation;
    RRuntimeTaskStartStatus preparation_status;
    RLibraryFsDeadlineStatus deadline_status;
    RRuntimeAllocator *allocator;
    uint64_t timeout_nanoseconds = 0U;
    int descriptor = -1;
    int flags;

    (void)memset(&payload, 0, sizeof(payload));
    payload.mode = mode;
    payload.options = options;
    payload.deadline = deadline;
    atomic_init(&payload.cancellation_state, 0U);
    if (mode_is_beneath(mode)) {
        payload.handle_storage = r_library_internal_fs_directory_handle_storage(root);
        if (payload.handle_storage == NULL) {
            fs_panic();
        }
        descriptor = r_library_internal_fs_directory_descriptor(root);
        if (descriptor < 0) {
            fs_panic();
        }
        if (r_library_internal_fs_validate_beneath_relative(path) !=
                R_LIBRARY_FS_RELATIVE_PATH_VALID ||
            (!mode_is_directory(mode) && options.follow_final_symlink)) {
            payload.immediate = 1;
            payload.immediate_error = fs_error(R_STD_FS_ERROR_INVALID_RELATIVE_PATH, INT64_C(0));
        }
    } else {
        if (r_library_internal_fs_path_length(path) == 0U ||
            r_library_internal_fs_path_has_reserved_component(path)) {
            payload.immediate = 1;
            payload.immediate_error = fs_error(R_STD_FS_ERROR_INVALID_PATH, INT64_C(0));
        }
    }
    if (!payload.immediate &&
        r_library_internal_fs_path_length(path) >= R_RUNTIME_DARWIN_FS_PATH_SCAN_LIMIT) {
        payload.immediate = 1;
        payload.immediate_error = fs_error(R_STD_FS_ERROR_NAME_TOO_LONG, INT64_C(0));
    }
    if (!mode_is_directory(mode) && !options_are_valid(options) && !payload.immediate) {
        payload.immediate = 1;
        payload.immediate_error = fs_error(R_STD_FS_ERROR_INVALID_OPERATION, INT64_C(0));
    }
    if (!payload.immediate) {
        deadline_status =
            deadline_timeout(deadline, &timeout_nanoseconds, &payload.immediate_error);
        payload.immediate = deadline_status != R_LIBRARY_FS_DEADLINE_READY;
    }
    if (payload.immediate) {
        payload.immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        payload.handle_storage = NULL;
        return start_task(&payload);
    }
    if (payload.handle_storage != NULL &&
        !r_library_internal_fs_handle_retain(payload.handle_storage)) {
        fs_panic();
    }
    allocator = r_library_internal_fs_path_allocator(path);
    if (mode_is_directory(mode)) {
        payload.directory_storage = r_library_internal_fs_directory_reserve(allocator);
        if (payload.directory_storage == NULL) {
            release_tracking(&payload);
            return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
        }
        flags = O_RDONLY | O_DIRECTORY;
        options.follow_final_symlink = !mode_is_beneath(mode);
        payload.options = options;
    } else {
        payload.file_storage = r_library_internal_fs_file_reserve(allocator);
        if (payload.file_storage == NULL) {
            release_tracking(&payload);
            return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
        }
        flags = file_open_flags(options);
    }
    if (mode_is_beneath(mode)) {
        native_preparation = r_runtime_darwin_fs_service_prepare_open_beneath(
            descriptor,
            (const char *)r_library_internal_fs_path_bytes(path),
            flags,
            creation_mode,
            timeout_nanoseconds);
    } else {
        native_preparation = r_runtime_darwin_fs_service_prepare_open(
            (const char *)r_library_internal_fs_path_bytes(path),
            flags,
            creation_mode,
            options.follow_final_symlink,
            timeout_nanoseconds);
    }
    preparation_status = native_prepare_status(native_preparation);
    if (preparation_status != R_RUNTIME_TASK_START_OK) {
        release_tracking(&payload);
        release_reserved_storage(&payload);
        return task_start_failure(preparation_status);
    }
    payload.prepared = native_preparation.prepared;
    return start_task(&payload);
}

RStdFsTaskStartResult r_library_internal_fs_open_directory(const RStdFsPath *path,
                                                           RStdFsDeadline deadline) {
    return start_open(
        R_LIBRARY_FS_OPEN_DIRECTORY, NULL, path, (RStdFsOpenFileOptions){0}, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_open_directory_beneath(const RStdFsDirectory *root,
                                                                   const RStdFsPath *relative,
                                                                   RStdFsDeadline deadline) {
    return start_open(
        R_LIBRARY_FS_OPEN_DIRECTORY_BENEATH, root, relative, (RStdFsOpenFileOptions){0}, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_open_file(const RStdFsPath *path,
                                                      RStdFsOpenFileOptions options,
                                                      RStdFsDeadline deadline) {
    return start_open(R_LIBRARY_FS_OPEN_FILE, NULL, path, options, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_open_file_beneath(const RStdFsDirectory *root,
                                                              const RStdFsPath *relative,
                                                              RStdFsOpenFileOptions options,
                                                              RStdFsDeadline deadline) {
    return start_open(R_LIBRARY_FS_OPEN_FILE_BENEATH, root, relative, options, deadline);
}
