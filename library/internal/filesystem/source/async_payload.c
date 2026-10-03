#include "r_library_fs_internal.h"

#include "r_library_clock_internal.h"
#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum RLibraryFsPayloadMode {
    R_LIBRARY_FS_PAYLOAD_READ = 0,
    R_LIBRARY_FS_PAYLOAD_WRITE,
    R_LIBRARY_FS_PAYLOAD_WRITE_ALL
} RLibraryFsPayloadMode;

typedef enum RLibraryFsPayloadImmediate {
    R_LIBRARY_FS_PAYLOAD_IMMEDIATE_NONE = 0,
    R_LIBRARY_FS_PAYLOAD_IMMEDIATE_SUCCESS,
    R_LIBRARY_FS_PAYLOAD_IMMEDIATE_ERROR
} RLibraryFsPayloadImmediate;

typedef enum RLibraryFsPayloadDeadlineStatus {
    R_LIBRARY_FS_PAYLOAD_DEADLINE_READY = 0,
    R_LIBRARY_FS_PAYLOAD_DEADLINE_EXPIRED,
    R_LIBRARY_FS_PAYLOAD_DEADLINE_ERROR
} RLibraryFsPayloadDeadlineStatus;

typedef enum RLibraryFsPayloadStage {
    R_LIBRARY_FS_PAYLOAD_STAGE_PREPARED = 0,
    R_LIBRARY_FS_PAYLOAD_STAGE_IO,
    R_LIBRARY_FS_PAYLOAD_STAGE_FINALIZING,
    R_LIBRARY_FS_PAYLOAD_STAGE_FINALIZED
} RLibraryFsPayloadStage;

typedef enum RLibraryFsPayloadTerminal {
    R_LIBRARY_FS_PAYLOAD_TERMINAL_NONE = 0,
    R_LIBRARY_FS_PAYLOAD_TERMINAL_NATIVE,
    R_LIBRARY_FS_PAYLOAD_TERMINAL_CANCELLED,
    R_LIBRARY_FS_PAYLOAD_TERMINAL_DEADLINE,
    R_LIBRARY_FS_PAYLOAD_TERMINAL_TASK_CANCEL
} RLibraryFsPayloadTerminal;

typedef struct RLibraryFsPayload RLibraryFsPayload;

typedef struct RLibraryFsPayloadControl {
    _Atomic size_t references;
    RRuntimeAllocator *allocator;
    pthread_mutex_t mutex;
    RLibraryFsPayloadMode mode;
    RStdFsDeadline absolute_deadline;
    RLibraryFsPayloadStage stage;
    RLibraryFsPayloadTerminal terminal;
    uint64_t terminal_sequence;
    RLibraryFsPositionCancelReason pending_cancel_reason;
    uint64_t pending_cancel_sequence;
    RLibraryFsHandleStorage *handle_storage;
    RLibraryFsPositionNode position_node;
    RLibraryFsOperationRegistration registration;
    RLibraryFsPositionDeadline deadline;
    RRuntimeDarwinIoHandle *io_handle;
    RRuntimeDarwinIoPreparedRequest *io_prepared;
    RRuntimeDarwinIoRequest *io_request;
    RRuntimeDarwinIoResult io_result;
    RStdIoError non_io_error;
    RRuntimeTaskExternalExecution *execution;
    RLibraryFsPayload *payload;
    /* R-SLIB-FS-0015: a positional operation takes its turn in the position order at
       explicit_offset and leaves the shared position unchanged. */
    uint64_t explicit_offset;
    _Bool positional;
    _Bool append;
    _Bool position_active;
    _Bool pending_cancel;
    _Bool task_cancel_reported;
    _Bool native_cleanup_done;
    _Bool io_result_available;
    _Bool non_io_error_available;
    _Bool start_in_progress;
} RLibraryFsPayloadControl;

struct RLibraryFsPayload {
    RLibraryFsPayloadMode mode;
    RLibraryFsPayloadImmediate immediate;
    RStdIoError immediate_error;
    uint64_t immediate_event_sequence;
    RRuntimeArray *staged_buffer;
    RRuntimeArray buffer;
    /*
     * Borrowed mode (read_into/write_from/write_all_from): the caller keeps the view alive until
     * the result is published. It is submitted with a NULL allocator, reclaimed through
     * request_take_buffer before the request is released, and never destroyed or moved.
     * buffer_owned then records whether the payload (rather than the native request) holds it.
     */
    uint8_t *borrowed_data;
    size_t borrowed_length;
    uint64_t offset;
    RLibraryFsPayloadControl *control;
    RRuntimeTaskExternalExecution *execution;
    void *result;
    _Bool buffer_owned;
    _Bool borrowed;
    _Bool positional;
};

typedef struct RLibraryFsPayloadSnapshot {
    RLibraryFsHandleStorage *storage;
    RRuntimeAllocator *allocator;
    RStdFsAccess access;
    int descriptor;
    _Bool append;
    _Bool closed;
} RLibraryFsPayloadSnapshot;

#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
static pthread_mutex_t payload_testing_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t payload_testing_condition = PTHREAD_COND_INITIALIZER;
static _Bool payload_testing_pause_before_cancel_report;
static _Bool payload_testing_cancel_report_reached;
static _Bool payload_testing_pause_before_immediate_select;
static _Bool payload_testing_immediate_select_reached;
static _Bool payload_testing_pause_after_cancel_retain;
static _Bool payload_testing_cancel_retain_reached;
static _Atomic _Bool payload_testing_terminal_after_cancel_observed;
static _Atomic _Bool payload_testing_task_cancel_won;
static _Atomic _Bool payload_testing_deadline_won;
static _Atomic size_t payload_testing_position_cancel_applications;
static _Atomic size_t payload_testing_cancel_reports_finished;

void r_library_internal_fs_payload_testing_pause_before_cancel_report(_Bool paused) {
    if (pthread_mutex_lock(&payload_testing_mutex) != 0) {
        abort();
    }
    payload_testing_pause_before_cancel_report = paused;
    if (paused) {
        payload_testing_cancel_report_reached = 0;
        atomic_store_explicit(
            &payload_testing_terminal_after_cancel_observed, 0, memory_order_relaxed);
        atomic_store_explicit(&payload_testing_task_cancel_won, 0, memory_order_relaxed);
        atomic_store_explicit(&payload_testing_deadline_won, 0, memory_order_relaxed);
    }
    if (pthread_cond_broadcast(&payload_testing_condition) != 0 ||
        pthread_mutex_unlock(&payload_testing_mutex) != 0) {
        abort();
    }
}

_Bool r_library_internal_fs_payload_testing_cancel_report_reached(void) {
    _Bool reached;

    if (pthread_mutex_lock(&payload_testing_mutex) != 0) {
        abort();
    }
    reached = payload_testing_cancel_report_reached;
    if (pthread_mutex_unlock(&payload_testing_mutex) != 0) {
        abort();
    }
    return reached;
}

void r_library_internal_fs_payload_testing_pause_before_immediate_select(_Bool paused) {
    if (pthread_mutex_lock(&payload_testing_mutex) != 0) {
        abort();
    }
    payload_testing_pause_before_immediate_select = paused;
    if (paused) {
        payload_testing_immediate_select_reached = 0;
    }
    if (pthread_cond_broadcast(&payload_testing_condition) != 0 ||
        pthread_mutex_unlock(&payload_testing_mutex) != 0) {
        abort();
    }
}

_Bool r_library_internal_fs_payload_testing_immediate_select_reached(void) {
    _Bool reached;

    if (pthread_mutex_lock(&payload_testing_mutex) != 0) {
        abort();
    }
    reached = payload_testing_immediate_select_reached;
    if (pthread_mutex_unlock(&payload_testing_mutex) != 0) {
        abort();
    }
    return reached;
}

void r_library_internal_fs_payload_testing_pause_after_cancel_retain(_Bool paused) {
    if (pthread_mutex_lock(&payload_testing_mutex) != 0) {
        abort();
    }
    payload_testing_pause_after_cancel_retain = paused;
    if (paused) {
        payload_testing_cancel_retain_reached = 0;
    }
    if (pthread_cond_broadcast(&payload_testing_condition) != 0 ||
        pthread_mutex_unlock(&payload_testing_mutex) != 0) {
        abort();
    }
}

_Bool r_library_internal_fs_payload_testing_cancel_retain_reached(void) {
    _Bool reached;

    if (pthread_mutex_lock(&payload_testing_mutex) != 0) {
        abort();
    }
    reached = payload_testing_cancel_retain_reached;
    if (pthread_mutex_unlock(&payload_testing_mutex) != 0) {
        abort();
    }
    return reached;
}

_Bool r_library_internal_fs_payload_testing_task_cancel_won(void) {
    return atomic_load_explicit(&payload_testing_task_cancel_won, memory_order_acquire);
}

_Bool r_library_internal_fs_payload_testing_terminal_after_cancel_observed(void) {
    return atomic_load_explicit(&payload_testing_terminal_after_cancel_observed,
                                memory_order_acquire);
}

_Bool r_library_internal_fs_payload_testing_deadline_won(void) {
    return atomic_load_explicit(&payload_testing_deadline_won, memory_order_acquire);
}

size_t r_library_internal_fs_payload_testing_position_cancel_applications(void) {
    return atomic_load_explicit(&payload_testing_position_cancel_applications,
                                memory_order_acquire);
}

size_t r_library_internal_fs_payload_testing_cancel_reports_finished(void) {
    return atomic_load_explicit(&payload_testing_cancel_reports_finished, memory_order_acquire);
}

static void payload_testing_wait_before_cancel_report(void) {
    if (pthread_mutex_lock(&payload_testing_mutex) != 0) {
        abort();
    }
    if (payload_testing_pause_before_cancel_report) {
        payload_testing_cancel_report_reached = 1;
        if (pthread_cond_broadcast(&payload_testing_condition) != 0) {
            (void)pthread_mutex_unlock(&payload_testing_mutex);
            abort();
        }
        while (payload_testing_pause_before_cancel_report) {
            if (pthread_cond_wait(&payload_testing_condition, &payload_testing_mutex) != 0) {
                (void)pthread_mutex_unlock(&payload_testing_mutex);
                abort();
            }
        }
    }
    if (pthread_mutex_unlock(&payload_testing_mutex) != 0) {
        abort();
    }
}

static void payload_testing_wait_before_immediate_select(void) {
    if (pthread_mutex_lock(&payload_testing_mutex) != 0) {
        abort();
    }
    if (payload_testing_pause_before_immediate_select) {
        payload_testing_immediate_select_reached = 1;
        if (pthread_cond_broadcast(&payload_testing_condition) != 0) {
            (void)pthread_mutex_unlock(&payload_testing_mutex);
            abort();
        }
        while (payload_testing_pause_before_immediate_select) {
            if (pthread_cond_wait(&payload_testing_condition, &payload_testing_mutex) != 0) {
                (void)pthread_mutex_unlock(&payload_testing_mutex);
                abort();
            }
        }
    }
    if (pthread_mutex_unlock(&payload_testing_mutex) != 0) {
        abort();
    }
}

static void payload_testing_wait_after_cancel_retain(void) {
    if (pthread_mutex_lock(&payload_testing_mutex) != 0) {
        abort();
    }
    if (payload_testing_pause_after_cancel_retain) {
        payload_testing_cancel_retain_reached = 1;
        if (pthread_cond_broadcast(&payload_testing_condition) != 0) {
            (void)pthread_mutex_unlock(&payload_testing_mutex);
            abort();
        }
        while (payload_testing_pause_after_cancel_retain) {
            if (pthread_cond_wait(&payload_testing_condition, &payload_testing_mutex) != 0) {
                (void)pthread_mutex_unlock(&payload_testing_mutex);
                abort();
            }
        }
    }
    if (pthread_mutex_unlock(&payload_testing_mutex) != 0) {
        abort();
    }
}

static void payload_testing_record_task_cancel(_Bool won) {
    atomic_store_explicit(&payload_testing_task_cancel_won, won, memory_order_release);
    atomic_store_explicit(&payload_testing_terminal_after_cancel_observed, 1, memory_order_release);
}

static void payload_testing_record_deadline(void) {
    atomic_store_explicit(&payload_testing_deadline_won, 1, memory_order_release);
}

static void payload_testing_record_position_cancel_applied(void) {
    (void)atomic_fetch_add_explicit(
        &payload_testing_position_cancel_applications, 1U, memory_order_acq_rel);
}

static void payload_testing_record_cancel_report_finished(void) {
    (void)atomic_fetch_add_explicit(
        &payload_testing_cancel_reports_finished, 1U, memory_order_acq_rel);
}
#else
static void payload_testing_wait_before_cancel_report(void) {
}

static void payload_testing_wait_before_immediate_select(void) {
}

static void payload_testing_wait_after_cancel_retain(void) {
}

static void payload_testing_record_task_cancel(_Bool won) {
    (void)won;
}

static void payload_testing_record_deadline(void) {
}

static void payload_testing_record_position_cancel_applied(void) {
}

static void payload_testing_record_cancel_report_finished(void) {
}
#endif

_Noreturn static void payload_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                    (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
}

static RStdIoError io_error(RStdIoErrorCode code, int64_t native_code) {
    return (RStdIoError){code, native_code};
}

static void clear_array(RRuntimeArray *array) {
    (void)memset(array, 0, sizeof(*array));
}

static void move_array(RRuntimeArray *destination, RRuntimeArray *source) {
    *destination = *source;
    clear_array(source);
}

static _Bool mode_can_access(RLibraryFsPayloadMode mode, RStdFsAccess access) {
    if (mode == R_LIBRARY_FS_PAYLOAD_READ) {
        return access == R_STD_FS_ACCESS_READ || access == R_STD_FS_ACCESS_READ_WRITE;
    }
    return access == R_STD_FS_ACCESS_WRITE || access == R_STD_FS_ACCESS_READ_WRITE;
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

static RLibraryFsPayloadDeadlineStatus check_deadline(RStdFsDeadline deadline, RStdIoError *error) {
    RStdTimeInstantResult now;

    if (!deadline.has_value) {
        return R_LIBRARY_FS_PAYLOAD_DEADLINE_READY;
    }
    if (deadline.value.storage_seconds < 0 ||
        deadline.value.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
        *error = io_error(R_STD_IO_ERROR_INVALID_OPERATION, INT64_C(0));
        return R_LIBRARY_FS_PAYLOAD_DEADLINE_ERROR;
    }
    now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
    if (!now.is_ok) {
        *error = io_error(R_STD_IO_ERROR_OTHER, now.error.native_code);
        return R_LIBRARY_FS_PAYLOAD_DEADLINE_ERROR;
    }
    if (compare_instant(deadline.value, now.value) <= 0) {
        *error = io_error(R_STD_IO_ERROR_TIMED_OUT, INT64_C(0));
        return R_LIBRARY_FS_PAYLOAD_DEADLINE_EXPIRED;
    }
    return R_LIBRARY_FS_PAYLOAD_DEADLINE_READY;
}

static RStdIoError classify_native_error(int native_error) {
    RStdIoErrorCode code = R_STD_IO_ERROR_OTHER;

    if (native_error == EBADF) {
        code = R_STD_IO_ERROR_CLOSED;
    } else if (native_error == EPIPE) {
        code = R_STD_IO_ERROR_BROKEN_PIPE;
    } else if (native_error == EACCES || native_error == EPERM || native_error == EROFS) {
        code = R_STD_IO_ERROR_PERMISSION_DENIED;
    } else if (native_error == ENOMEM || native_error == ENOBUFS || native_error == EMFILE ||
               native_error == ENFILE || native_error == ENOSPC || native_error == EDQUOT ||
               native_error == EFBIG || native_error == EOVERFLOW || native_error == EAGAIN) {
        code = R_STD_IO_ERROR_RESOURCE_EXHAUSTED;
    } else if (native_error == EINVAL || native_error == ESPIPE || native_error == ENXIO ||
               native_error == ENODEV
#if defined(ENOTSUP)
               || native_error == ENOTSUP
#endif
    ) {
        code = R_STD_IO_ERROR_INVALID_OPERATION;
    }
    return io_error(code, (int64_t)native_error);
}

static RStdIoError native_result_error(RRuntimeDarwinIoResult result) {
    if (result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED) {
        return io_error(R_STD_IO_ERROR_CANCELLED, (int64_t)result.native_error);
    }
    if (result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_TIMED_OUT) {
        return io_error(R_STD_IO_ERROR_TIMED_OUT, (int64_t)result.native_error);
    }
    if (result.native_error != 0) {
        return classify_native_error(result.native_error);
    }
    return io_error(R_STD_IO_ERROR_OTHER, INT64_C(0));
}

static void control_retain(void *context) {
    RLibraryFsPayloadControl *control = context;
    size_t current = atomic_load_explicit(&control->references, memory_order_relaxed);

    for (;;) {
        if (current == 0U || current == SIZE_MAX) {
            payload_panic();
        }
        if (atomic_compare_exchange_weak_explicit(&control->references,
                                                  &current,
                                                  current + 1U,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
            return;
        }
    }
}

static void control_release(void *context) {
    RLibraryFsPayloadControl *control = context;
    size_t previous = atomic_fetch_sub_explicit(&control->references, 1U, memory_order_acq_rel);

    if (previous == 0U) {
        payload_panic();
    }
    if (previous != 1U) {
        return;
    }
    if (control->registration.registered || control->io_prepared != NULL ||
        control->io_request != NULL || control->io_handle != NULL ||
        control->deadline.token != NULL ||
        (control->execution != NULL && control->stage != R_LIBRARY_FS_PAYLOAD_STAGE_FINALIZED)) {
        payload_panic();
    }
    if (pthread_mutex_destroy(&control->mutex) != 0) {
        payload_panic();
    }
    r_runtime_allocator_deallocate(control, _Alignof(RLibraryFsPayloadControl));
}

static RLibraryFsPayloadControl *control_create(RRuntimeAllocator *allocator,
                                                RLibraryFsPayloadMode mode) {
    RLibraryFsPayloadControl *control = NULL;

    if (allocator == NULL ||
        r_runtime_allocator_allocate(
            allocator, sizeof(*control), _Alignof(RLibraryFsPayloadControl), (void **)&control) !=
            R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    (void)memset(control, 0, sizeof(*control));
    if (pthread_mutex_init(&control->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(control, _Alignof(RLibraryFsPayloadControl));
        return NULL;
    }
    atomic_init(&control->references, 1U);
    control->allocator = allocator;
    control->mode = mode;
    control->stage = R_LIBRARY_FS_PAYLOAD_STAGE_PREPARED;
    control->terminal = R_LIBRARY_FS_PAYLOAD_TERMINAL_NONE;
    return control;
}

static void payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryFsPayload *destination = destination_pointer;
    RLibraryFsPayload *source = source_pointer;

    (void)memset(destination, 0, sizeof(*destination));
    destination->mode = source->mode;
    destination->immediate = source->immediate;
    destination->immediate_error = source->immediate_error;
    destination->immediate_event_sequence = source->immediate_event_sequence;
    destination->control = source->control;
    source->control = NULL;
    destination->borrowed = source->borrowed;
    if (source->borrowed) {
        destination->borrowed_data = source->borrowed_data;
        destination->borrowed_length = source->borrowed_length;
        destination->buffer_owned = 1;
    } else {
        if (source->staged_buffer == NULL) {
            payload_panic();
        }
        move_array(&destination->buffer, source->staged_buffer);
        destination->buffer_owned = 1;
        source->staged_buffer = NULL;
    }
}

static void payload_drop(void *value) {
    RLibraryFsPayload *payload = value;

    if (payload->buffer_owned) {
        if (!payload->borrowed) {
            r_runtime_array_destroy(&payload->buffer);
        }
        payload->buffer_owned = 0;
    }
    if (payload->control != NULL) {
        control_release(payload->control);
        payload->control = NULL;
    }
}

static void read_result_move(void *destination_pointer, void *source_pointer) {
    RStdIoReadResult *destination = destination_pointer;
    RStdIoReadResult *source = source_pointer;

    *destination = *source;
    clear_array(&source->buffer);
}

static void read_result_drop(void *value) {
    RStdIoReadResult *result = value;

    r_runtime_array_destroy(&result->buffer);
}

static void write_result_move(void *destination_pointer, void *source_pointer) {
    RStdIoWriteResult *destination = destination_pointer;
    RStdIoWriteResult *source = source_pointer;

    *destination = *source;
    clear_array(&source->buffer);
}

static void write_result_drop(void *value) {
    RStdIoWriteResult *result = value;

    r_runtime_array_destroy(&result->buffer);
}

static void write_all_result_move(void *destination_pointer, void *source_pointer) {
    RStdIoWriteAllResult *destination = destination_pointer;
    RStdIoWriteAllResult *source = source_pointer;

    *destination = *source;
    clear_array(&source->buffer);
}

static void write_all_result_drop(void *value) {
    RStdIoWriteAllResult *result = value;

    r_runtime_array_destroy(&result->buffer);
}

static RRuntimeTypeInfo result_type(RLibraryFsPayloadMode mode, _Bool borrowed) {
    if (borrowed) {
        if (mode == R_LIBRARY_FS_PAYLOAD_WRITE_ALL) {
            return (RRuntimeTypeInfo){
                sizeof(RStdIoVoidResult), _Alignof(RStdIoVoidResult), NULL, NULL};
        }
        return (RRuntimeTypeInfo){
            sizeof(RStdIoCountResult), _Alignof(RStdIoCountResult), NULL, NULL};
    }
    switch (mode) {
    case R_LIBRARY_FS_PAYLOAD_READ:
        return (RRuntimeTypeInfo){sizeof(RStdIoReadResult),
                                  _Alignof(RStdIoReadResult),
                                  read_result_move,
                                  read_result_drop};
    case R_LIBRARY_FS_PAYLOAD_WRITE:
        return (RRuntimeTypeInfo){sizeof(RStdIoWriteResult),
                                  _Alignof(RStdIoWriteResult),
                                  write_result_move,
                                  write_result_drop};
    case R_LIBRARY_FS_PAYLOAD_WRITE_ALL:
        return (RRuntimeTypeInfo){sizeof(RStdIoWriteAllResult),
                                  _Alignof(RStdIoWriteAllResult),
                                  write_all_result_move,
                                  write_all_result_drop};
    }
    payload_panic();
}

static void move_payload_buffer(RLibraryFsPayload *payload, RRuntimeArray *destination) {
    if (!payload->buffer_owned || payload->borrowed) {
        payload_panic();
    }
    move_array(destination, &payload->buffer);
    payload->buffer_owned = 0;
}

/* Borrowed completion: read_into/write_from carry a count, write_all_from the void carrier. */
static void
fill_borrowed_result(RLibraryFsPayload *payload, _Bool success, size_t count, RStdIoError error) {
    if (!payload->buffer_owned || !payload->borrowed) {
        payload_panic();
    }
    if (payload->mode == R_LIBRARY_FS_PAYLOAD_WRITE_ALL) {
        RStdIoVoidResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        if (!success) {
            result->r_tag = UINT32_C(1);
            result->r_payload.r_err = error;
        }
    } else {
        RStdIoCountResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        if (success) {
            result->r_payload.r_value = count;
        } else {
            result->r_tag = UINT32_C(1);
            result->r_payload.r_error_00000001 = error;
        }
    }
    payload->buffer_owned = 0;
}

static void fill_result(RLibraryFsPayload *payload,
                        RLibraryFsPayloadTerminal terminal,
                        _Bool io_result_available,
                        RRuntimeDarwinIoResult native_result,
                        _Bool non_io_error_available,
                        RStdIoError non_io_error) {
    const size_t progress = io_result_available ? native_result.bytes_transferred : 0U;
    const _Bool native_success =
        terminal == R_LIBRARY_FS_PAYLOAD_TERMINAL_NATIVE && io_result_available &&
        native_result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE &&
        native_result.native_error == 0;
    RStdIoError error = {0};

    if (terminal == R_LIBRARY_FS_PAYLOAD_TERMINAL_CANCELLED) {
        error = io_error(R_STD_IO_ERROR_CANCELLED, INT64_C(0));
    } else if (terminal == R_LIBRARY_FS_PAYLOAD_TERMINAL_DEADLINE) {
        error = io_error(R_STD_IO_ERROR_TIMED_OUT, INT64_C(0));
    } else if (!native_success) {
        error = io_result_available
                    ? native_result_error(native_result)
                    : (non_io_error_available ? non_io_error
                                              : io_error(R_STD_IO_ERROR_OTHER, INT64_C(0)));
    }

    if (payload->borrowed) {
        if (payload->mode == R_LIBRARY_FS_PAYLOAD_READ && native_success && progress == 0U &&
            !native_result.eof) {
            payload_panic();
        }
        fill_borrowed_result(payload, native_success, native_success ? progress : 0U, error);
        return;
    }
    switch (payload->mode) {
    case R_LIBRARY_FS_PAYLOAD_READ: {
        RStdIoReadResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        if (native_success && progress != 0U) {
            result->kind = R_STD_IO_READ_RESULT_READ;
        } else if (native_success && native_result.eof) {
            result->kind = R_STD_IO_READ_RESULT_END;
        } else if (native_success) {
            payload_panic();
        } else {
            result->kind = R_STD_IO_READ_RESULT_FAILED;
            result->error = error;
        }
        result->count = progress;
        move_payload_buffer(payload, &result->buffer);
        return;
    }
    case R_LIBRARY_FS_PAYLOAD_WRITE: {
        RStdIoWriteResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->kind =
            native_success ? R_STD_IO_WRITE_RESULT_WRITTEN : R_STD_IO_WRITE_RESULT_FAILED;
        result->count = progress;
        if (!native_success) {
            result->error = error;
        }
        move_payload_buffer(payload, &result->buffer);
        return;
    }
    case R_LIBRARY_FS_PAYLOAD_WRITE_ALL: {
        RStdIoWriteAllResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->kind =
            native_success ? R_STD_IO_WRITE_ALL_RESULT_WRITTEN : R_STD_IO_WRITE_ALL_RESULT_FAILED;
        result->written = progress;
        if (!native_success) {
            result->error = error;
        }
        move_payload_buffer(payload, &result->buffer);
        return;
    }
    }
    payload_panic();
}

static void fill_immediate_result(RLibraryFsPayload *payload) {
    const _Bool success = payload->immediate == R_LIBRARY_FS_PAYLOAD_IMMEDIATE_SUCCESS;

    if (payload->borrowed) {
        fill_borrowed_result(payload, success, 0U, payload->immediate_error);
        return;
    }
    switch (payload->mode) {
    case R_LIBRARY_FS_PAYLOAD_READ: {
        RStdIoReadResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->kind = success ? R_STD_IO_READ_RESULT_READ : R_STD_IO_READ_RESULT_FAILED;
        result->error = payload->immediate_error;
        move_payload_buffer(payload, &result->buffer);
        return;
    }
    case R_LIBRARY_FS_PAYLOAD_WRITE: {
        RStdIoWriteResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->kind = success ? R_STD_IO_WRITE_RESULT_WRITTEN : R_STD_IO_WRITE_RESULT_FAILED;
        result->error = payload->immediate_error;
        move_payload_buffer(payload, &result->buffer);
        return;
    }
    case R_LIBRARY_FS_PAYLOAD_WRITE_ALL: {
        RStdIoWriteAllResult *result = payload->result;

        (void)memset(result, 0, sizeof(*result));
        result->kind =
            success ? R_STD_IO_WRITE_ALL_RESULT_WRITTEN : R_STD_IO_WRITE_ALL_RESULT_FAILED;
        result->error = payload->immediate_error;
        move_payload_buffer(payload, &result->buffer);
        return;
    }
    }
    payload_panic();
}

static void payload_make_immediate(RLibraryFsPayload *payload,
                                   RLibraryFsPayloadImmediate immediate,
                                   RStdIoError error) {
    if (payload == NULL || immediate == R_LIBRARY_FS_PAYLOAD_IMMEDIATE_NONE ||
        payload->immediate != R_LIBRARY_FS_PAYLOAD_IMMEDIATE_NONE ||
        payload->immediate_event_sequence != UINT64_C(0)) {
        payload_panic();
    }
    payload->immediate = immediate;
    payload->immediate_error = error;
    payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
}

/* Staged storage exactly as submitted natively: the owner's array or the borrowed view. */
static RRuntimeDarwinIoBuffer staged_native_view(RLibraryFsPayloadMode mode,
                                                 const RRuntimeArray *buffer,
                                                 uint8_t *borrowed_data,
                                                 size_t borrowed_length,
                                                 _Bool borrowed) {
    RRuntimeDarwinIoBuffer view = {0};

    if (borrowed) {
        view.data = borrowed_data;
        view.capacity = borrowed_length;
        view.size = mode == R_LIBRARY_FS_PAYLOAD_READ ? 0U : borrowed_length;
        return view;
    }
    view.allocator = buffer->allocator;
    view.data = buffer->data;
    view.capacity = mode == R_LIBRARY_FS_PAYLOAD_READ ? buffer->length : buffer->capacity;
    view.size = mode == R_LIBRARY_FS_PAYLOAD_READ ? 0U : buffer->length;
    return view;
}

static RRuntimeDarwinIoBuffer native_buffer(const RLibraryFsPayload *payload) {
    return staged_native_view(payload->mode,
                              &payload->buffer,
                              payload->borrowed_data,
                              payload->borrowed_length,
                              payload->borrowed);
}

static void restore_native_buffer(RLibraryFsPayloadControl *control,
                                  RRuntimeDarwinIoRequest *request) {
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer expected;
    RLibraryFsPayload *payload = control->payload;

    if (payload == NULL || payload->buffer_owned) {
        payload_panic();
    }
    expected = native_buffer(payload);
    buffer = r_runtime_darwin_io_request_take_buffer(request);
    if (buffer.allocator != expected.allocator || buffer.data != expected.data ||
        buffer.capacity != expected.capacity) {
        payload_panic();
    }
    payload->buffer_owned = 1;
}

static _Bool terminal_can_finalize_locked(const RLibraryFsPayloadControl *control) {
    if (control->start_in_progress || !control->native_cleanup_done ||
        control->stage == R_LIBRARY_FS_PAYLOAD_STAGE_FINALIZING ||
        control->stage == R_LIBRARY_FS_PAYLOAD_STAGE_FINALIZED ||
        control->terminal == R_LIBRARY_FS_PAYLOAD_TERMINAL_NONE) {
        return 0;
    }
    return control->terminal != R_LIBRARY_FS_PAYLOAD_TERMINAL_TASK_CANCEL ||
           control->task_cancel_reported;
}

static void finalize_operation(RLibraryFsPayloadControl *control) {
    RRuntimeDarwinIoPreparedRequest *io_prepared;
    RRuntimeDarwinIoHandle *io_handle;
    RLibraryFsPayload *payload;
    RRuntimeTaskExternalExecution *execution;
    RLibraryFsPayloadTerminal terminal;
    RRuntimeDarwinIoResult io_result;
    RStdIoError non_io_error;
    RLibraryFsPositionUpdate update = R_LIBRARY_FS_POSITION_KEEP;
    uint64_t update_value = UINT64_C(0);
    _Bool io_result_available;
    _Bool non_io_error_available;
    _Bool position_active;

    control_retain(control);
    if (pthread_mutex_lock(&control->mutex) != 0) {
        payload_panic();
    }
    if (!terminal_can_finalize_locked(control)) {
        if (pthread_mutex_unlock(&control->mutex) != 0) {
            payload_panic();
        }
        control_release(control);
        return;
    }
    control->stage = R_LIBRARY_FS_PAYLOAD_STAGE_FINALIZING;
    payload = control->payload;
    execution = control->execution;
    terminal = control->terminal;
    io_result = control->io_result;
    io_result_available = control->io_result_available;
    non_io_error = control->non_io_error;
    non_io_error_available = control->non_io_error_available;
    position_active = control->position_active;
    io_prepared = control->io_prepared;
    control->io_prepared = NULL;
    io_handle = control->io_handle;
    control->io_handle = NULL;
    if (payload == NULL || execution == NULL || control->io_request != NULL) {
        if (pthread_mutex_unlock(&control->mutex) != 0) {
            payload_panic();
        }
        payload_panic();
    }
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        payload_panic();
    }

    r_runtime_darwin_io_prepared_abort(&io_prepared);
    r_library_internal_fs_position_deadline_destroy(&control->deadline);
    if (io_handle != NULL) {
        r_runtime_darwin_io_handle_release(io_handle);
    }
    if (position_active) {
        if (io_result_available && !control->append && !control->positional) {
            update = R_LIBRARY_FS_POSITION_ADVANCE;
            update_value = (uint64_t)io_result.bytes_transferred;
        }
        r_library_internal_fs_position_finish(&control->position_node, update, update_value);
    }
    if (control->registration.registered) {
        r_library_internal_fs_operation_unregister(control->handle_storage, &control->registration);
    }
    if (terminal != R_LIBRARY_FS_PAYLOAD_TERMINAL_TASK_CANCEL) {
        fill_result(payload,
                    terminal,
                    io_result_available,
                    io_result,
                    non_io_error_available,
                    non_io_error);
    }
    if (pthread_mutex_lock(&control->mutex) != 0) {
        payload_panic();
    }
    control->stage = R_LIBRARY_FS_PAYLOAD_STAGE_FINALIZED;
    control->payload = NULL;
    control->execution = NULL;
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        payload_panic();
    }
    r_runtime_task_external_acknowledge(execution);
    control_release(control);
}

static void select_task_cancellation_locked(RLibraryFsPayloadControl *control) {
    uint64_t event_sequence;

    if (control->execution == NULL) {
        payload_panic();
    }
    event_sequence = r_runtime_task_external_cancellation_sequence(control->execution);
    if (event_sequence == UINT64_C(0)) {
        payload_panic();
    }
    if (control->terminal == R_LIBRARY_FS_PAYLOAD_TERMINAL_NONE ||
        event_sequence < control->terminal_sequence) {
        control->terminal = R_LIBRARY_FS_PAYLOAD_TERMINAL_TASK_CANCEL;
        control->terminal_sequence = event_sequence;
    }
}

static void select_completion_terminal_locked(RLibraryFsPayloadControl *control,
                                              RLibraryFsPayloadTerminal terminal,
                                              uint64_t event_sequence) {
    _Bool selected;

    if (event_sequence == UINT64_C(0) || terminal == R_LIBRARY_FS_PAYLOAD_TERMINAL_NONE ||
        terminal == R_LIBRARY_FS_PAYLOAD_TERMINAL_TASK_CANCEL || control->execution == NULL) {
        payload_panic();
    }
    if (control->terminal != R_LIBRARY_FS_PAYLOAD_TERMINAL_NONE &&
        control->terminal_sequence <= event_sequence) {
        return;
    }
    if (control->terminal == R_LIBRARY_FS_PAYLOAD_TERMINAL_NONE ||
        control->terminal == R_LIBRARY_FS_PAYLOAD_TERMINAL_TASK_CANCEL) {
        selected =
            r_runtime_task_external_try_select_completion_at(control->execution, event_sequence);
        if (!selected) {
            if (control->terminal == R_LIBRARY_FS_PAYLOAD_TERMINAL_NONE) {
                select_task_cancellation_locked(control);
            }
            return;
        }
    }
    control->terminal = terminal;
    control->terminal_sequence = event_sequence;
}

static void select_position_cancellation_locked(RLibraryFsPayloadControl *control,
                                                RLibraryFsPositionCancelReason reason,
                                                uint64_t event_sequence) {
    RLibraryFsPayloadTerminal terminal;

    if (reason == R_LIBRARY_FS_POSITION_CANCEL_TASK) {
        select_task_cancellation_locked(control);
        return;
    }
    if (reason == R_LIBRARY_FS_POSITION_CANCEL_CLOSE) {
        terminal = R_LIBRARY_FS_PAYLOAD_TERMINAL_CANCELLED;
    } else if (control->non_io_error_available) {
        terminal = R_LIBRARY_FS_PAYLOAD_TERMINAL_NATIVE;
    } else {
        terminal = R_LIBRARY_FS_PAYLOAD_TERMINAL_DEADLINE;
    }
    select_completion_terminal_locked(control, terminal, event_sequence);
    if (control->terminal == R_LIBRARY_FS_PAYLOAD_TERMINAL_DEADLINE &&
        control->terminal_sequence == event_sequence) {
        payload_testing_record_deadline();
    }
}

static void complete_inactive_cancellation(RLibraryFsPayloadControl *control,
                                           RLibraryFsPositionCancelReason reason,
                                           uint64_t event_sequence) {
    _Bool finalize;

    if (pthread_mutex_lock(&control->mutex) != 0) {
        payload_panic();
    }
    if (control->stage == R_LIBRARY_FS_PAYLOAD_STAGE_FINALIZING ||
        control->stage == R_LIBRARY_FS_PAYLOAD_STAGE_FINALIZED) {
        if (pthread_mutex_unlock(&control->mutex) != 0) {
            payload_panic();
        }
        return;
    }
    control->native_cleanup_done = 1;
    if (control->execution == NULL) {
        control->pending_cancel = 1;
        control->pending_cancel_reason = reason;
        control->pending_cancel_sequence = event_sequence;
        if (pthread_mutex_unlock(&control->mutex) != 0) {
            payload_panic();
        }
        return;
    }
    select_position_cancellation_locked(control, reason, event_sequence);
    finalize = terminal_can_finalize_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        payload_panic();
    }
    if (finalize) {
        finalize_operation(control);
    }
}

static void position_cancel_apply(RLibraryFsPayloadControl *control,
                                  RLibraryFsPositionCancelReason reason,
                                  _Bool active) {
    uint64_t event_sequence;
    _Bool finalize;

    if (reason == R_LIBRARY_FS_POSITION_CANCEL_TASK) {
        RRuntimeTaskExternalExecution *execution;

        if (pthread_mutex_lock(&control->mutex) != 0) {
            payload_panic();
        }
        execution = control->execution;
        if (pthread_mutex_unlock(&control->mutex) != 0 || execution == NULL) {
            payload_panic();
        }
        event_sequence = r_runtime_task_external_cancellation_sequence(execution);
        if (event_sequence == UINT64_C(0)) {
            payload_panic();
        }
    } else {
        event_sequence = r_runtime_darwin_event_sequence_next();
    }

    if (pthread_mutex_lock(&control->mutex) != 0) {
        payload_panic();
    }
    if (control->stage == R_LIBRARY_FS_PAYLOAD_STAGE_FINALIZING ||
        control->stage == R_LIBRARY_FS_PAYLOAD_STAGE_FINALIZED) {
        if (pthread_mutex_unlock(&control->mutex) != 0) {
            payload_panic();
        }
        return;
    }
    if (active) {
        select_position_cancellation_locked(control, reason, event_sequence);
        if (control->native_cleanup_done) {
            finalize = terminal_can_finalize_locked(control);
            if (pthread_mutex_unlock(&control->mutex) != 0) {
                payload_panic();
            }
            if (finalize) {
                finalize_operation(control);
            }
            return;
        }
        if (control->stage == R_LIBRARY_FS_PAYLOAD_STAGE_IO) {
            if (control->io_request == NULL) {
                if (pthread_mutex_unlock(&control->mutex) != 0) {
                    payload_panic();
                }
                payload_panic();
            }
            if (reason == R_LIBRARY_FS_POSITION_CANCEL_DEADLINE) {
                (void)r_runtime_darwin_io_request_deadline_expired(control->io_request);
            } else {
                (void)r_runtime_darwin_io_request_cancel(control->io_request);
            }
        } else {
            if (pthread_mutex_unlock(&control->mutex) != 0) {
                payload_panic();
            }
            payload_panic();
        }
        finalize = terminal_can_finalize_locked(control);
    } else {
        finalize = 0;
    }
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        payload_panic();
    }
    if (!active) {
        complete_inactive_cancellation(control, reason, event_sequence);
        return;
    }
    if (finalize) {
        finalize_operation(control);
    }
}

static void position_cancelled(void *context, RLibraryFsPositionCancelReason reason, _Bool active) {
    position_cancel_apply(context, reason, active);
    payload_testing_record_position_cancel_applied();
}

/* Task cancellation, deadline and close callbacks may run after finalization unregistered the
   operation and dropped the storage reference of its registration, and the file may be gone by
   then. Before finalization the registration still holds the storage, so a reference taken under
   the control mutex keeps it alive through the position cancellation. */
static RLibraryFsHandleStorage *
retain_registered_storage_locked(RLibraryFsPayloadControl *control) {
    if (control->stage == R_LIBRARY_FS_PAYLOAD_STAGE_FINALIZING ||
        control->stage == R_LIBRARY_FS_PAYLOAD_STAGE_FINALIZED ||
        !control->registration.registered) {
        return NULL;
    }
    r_library_internal_fs_handle_retain_registered(control->handle_storage);
    return control->handle_storage;
}

static void cancel_position_retained(RLibraryFsPayloadControl *control,
                                     RLibraryFsHandleStorage *storage,
                                     RLibraryFsPositionCancelReason reason) {
    if (storage == NULL) {
        return;
    }
    r_library_internal_fs_position_cancel_retained(storage, &control->position_node, reason);
    r_library_internal_fs_handle_release(storage);
}

static void cancel_position_late(RLibraryFsPayloadControl *control,
                                 RLibraryFsPositionCancelReason reason) {
    RLibraryFsHandleStorage *storage;

    if (pthread_mutex_lock(&control->mutex) != 0) {
        payload_panic();
    }
    storage = retain_registered_storage_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        payload_panic();
    }
    cancel_position_retained(control, storage, reason);
}

static void close_cancel(void *context) {
    cancel_position_late(context, R_LIBRARY_FS_POSITION_CANCEL_CLOSE);
}

static void deadline_expired(void *context) {
    cancel_position_late(context, R_LIBRARY_FS_POSITION_CANCEL_DEADLINE);
}

static void io_completed(RRuntimeDarwinIoRequest *request, void *context) {
    RLibraryFsPayloadControl *control = context;
    RRuntimeDarwinIoResult result = r_runtime_darwin_io_request_wait(request);
    _Bool finalize;
    _Bool task_cancel_won;

    if (pthread_mutex_lock(&control->mutex) != 0) {
        payload_panic();
    }
    if (control->stage != R_LIBRARY_FS_PAYLOAD_STAGE_IO || control->io_request != request) {
        if (pthread_mutex_unlock(&control->mutex) != 0) {
            payload_panic();
        }
        payload_panic();
    }
    control->io_request = NULL;
    control->io_result = result;
    control->io_result_available = 1;
    restore_native_buffer(control, request);
    control->native_cleanup_done = 1;
    select_completion_terminal_locked(
        control, R_LIBRARY_FS_PAYLOAD_TERMINAL_NATIVE, result.terminal_event_sequence);
    task_cancel_won = control->terminal == R_LIBRARY_FS_PAYLOAD_TERMINAL_TASK_CANCEL;
    finalize = terminal_can_finalize_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        payload_panic();
    }
    r_runtime_darwin_io_request_release(request);
    payload_testing_record_task_cancel(task_cancel_won);
    if (finalize) {
        finalize_operation(control);
    }
    control_release(control);
}

static RRuntimeDarwinIoSubmitResult activate_io_locked(RLibraryFsPayloadControl *control) {
    RRuntimeDarwinIoBuffer buffer = native_buffer(control->payload);
    RRuntimeDarwinIoSubmitResult submission =
        r_runtime_darwin_io_prepared_activate(&control->io_prepared, &buffer);

    if (submission.status != R_RUNTIME_DARWIN_IO_START_OK || submission.request == NULL ||
        control->io_prepared != NULL) {
        payload_panic();
    }
    control->io_request = submission.request;
    control->payload->buffer_owned = 0;
    control->stage = R_LIBRARY_FS_PAYLOAD_STAGE_IO;
    control_retain(control);
    return submission;
}

static void complete_active_error(RLibraryFsPayloadControl *control, RStdIoError error) {
    const uint64_t event_sequence = r_runtime_darwin_event_sequence_next();
    _Bool finalize;

    if (pthread_mutex_lock(&control->mutex) != 0) {
        payload_panic();
    }
    if (control->stage != R_LIBRARY_FS_PAYLOAD_STAGE_PREPARED || control->payload == NULL ||
        control->execution == NULL || control->native_cleanup_done) {
        if (pthread_mutex_unlock(&control->mutex) != 0) {
            payload_panic();
        }
        payload_panic();
    }
    control->position_active = 1;
    control->native_cleanup_done = 1;
    control->non_io_error = error;
    control->non_io_error_available = 1;
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        payload_panic();
    }
    r_library_internal_fs_position_activation_commit(&control->position_node);
    if (pthread_mutex_lock(&control->mutex) != 0) {
        payload_panic();
    }
    select_completion_terminal_locked(
        control, R_LIBRARY_FS_PAYLOAD_TERMINAL_NATIVE, event_sequence);
    finalize = terminal_can_finalize_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        payload_panic();
    }
    if (finalize) {
        finalize_operation(control);
    }
}

static void position_activate(void *context, uint64_t position) {
    RLibraryFsPayloadControl *control = context;
    RRuntimeDarwinIoSubmitResult io_submission = {0};
    RStdIoError deadline_error = {0};
    RLibraryFsPayloadDeadlineStatus deadline_status;

    deadline_status = check_deadline(control->absolute_deadline, &deadline_error);
    if (deadline_status != R_LIBRARY_FS_PAYLOAD_DEADLINE_READY) {
        if (deadline_status == R_LIBRARY_FS_PAYLOAD_DEADLINE_ERROR) {
            if (pthread_mutex_lock(&control->mutex) != 0) {
                payload_panic();
            }
            control->non_io_error = deadline_error;
            control->non_io_error_available = 1;
            if (pthread_mutex_unlock(&control->mutex) != 0) {
                payload_panic();
            }
        }
        r_library_internal_fs_position_cancel(&control->position_node,
                                              R_LIBRARY_FS_POSITION_CANCEL_DEADLINE);
        if (r_library_internal_fs_position_activation_begin(&control->position_node)) {
            payload_panic();
        }
        return;
    }
    if (!r_library_internal_fs_position_activation_begin(&control->position_node)) {
        return;
    }
    if (control->positional) {
        position = control->explicit_offset;
    }
    if (!control->append && position > (uint64_t)INT64_MAX) {
        complete_active_error(control, io_error(R_STD_IO_ERROR_INVALID_OPERATION, INT64_C(0)));
        return;
    }
    if (!control->append &&
        !r_runtime_darwin_io_prepared_set_stream_position(control->io_prepared, (off_t)position)) {
        payload_panic();
    }
    if (pthread_mutex_lock(&control->mutex) != 0) {
        payload_panic();
    }
    if (control->stage != R_LIBRARY_FS_PAYLOAD_STAGE_PREPARED || control->payload == NULL ||
        control->execution == NULL) {
        if (pthread_mutex_unlock(&control->mutex) != 0) {
            payload_panic();
        }
        payload_panic();
    }
    control->position_active = 1;
    io_submission = activate_io_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        payload_panic();
    }
    r_library_internal_fs_position_activation_commit(&control->position_node);
    if (!r_runtime_darwin_io_request_set_completion_inline(
            io_submission.request, io_completed, control)) {
        payload_panic();
    }
}

static _Bool select_pending_cancel(RLibraryFsPayloadControl *control) {
    _Bool pending;
    RLibraryFsPositionCancelReason reason = R_LIBRARY_FS_POSITION_CANCEL_TASK;
    uint64_t event_sequence = UINT64_C(0);

    if (pthread_mutex_lock(&control->mutex) != 0) {
        payload_panic();
    }
    pending = control->pending_cancel;
    if (pending) {
        reason = control->pending_cancel_reason;
        event_sequence = control->pending_cancel_sequence;
        control->pending_cancel = 0;
    }
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        payload_panic();
    }
    if (pending) {
        complete_inactive_cancellation(control, reason, event_sequence);
    }
    return pending;
}

static void external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryFsPayload *payload = payload_pointer;
    RLibraryFsPayloadControl *control = payload->control;
    RLibraryFsHandleStorage *storage;

    if (payload->execution != execution) {
        payload_panic();
    }
    payload_testing_wait_before_cancel_report();
    if (control == NULL) {
        r_runtime_task_external_acknowledge(execution);
        return;
    }
    control_retain(control);
    if (pthread_mutex_lock(&control->mutex) != 0) {
        payload_panic();
    }
    control->task_cancel_reported = 1;
    storage = retain_registered_storage_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        payload_panic();
    }
    payload_testing_wait_after_cancel_retain();
    cancel_position_retained(control, storage, R_LIBRARY_FS_POSITION_CANCEL_TASK);
    finalize_operation(control);
    control_release(control);
    payload_testing_record_cancel_report_finished();
}

static void external_start(RRuntimeTaskExternalExecution *execution,
                           void *payload_pointer,
                           void *result_pointer) {
    RLibraryFsPayload *payload = payload_pointer;
    RLibraryFsPayloadControl *control = payload->control;
    _Bool finalize;

    payload->execution = execution;
    payload->result = result_pointer;
    if (control == NULL) {
        _Bool selected;

        if (payload->immediate == R_LIBRARY_FS_PAYLOAD_IMMEDIATE_NONE ||
            payload->immediate_event_sequence == UINT64_C(0)) {
            payload_panic();
        }
        payload_testing_wait_before_immediate_select();
        selected = r_runtime_task_external_try_select_completion_at(
            execution, payload->immediate_event_sequence);

        r_runtime_task_external_start_ready(execution);
        if (selected) {
            fill_immediate_result(payload);
            r_runtime_task_external_acknowledge(execution);
        }
        return;
    }
    control_retain(control);
    if (pthread_mutex_lock(&control->mutex) != 0) {
        payload_panic();
    }
    control->execution = execution;
    control->payload = payload;
    control->start_in_progress = 1;
    if (control->deadline.token != NULL) {
        r_library_internal_fs_position_deadline_activate(&control->deadline);
    }
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        payload_panic();
    }
    r_runtime_task_external_start_ready(execution);
    (void)select_pending_cancel(control);
    if (r_runtime_task_external_cancel_requested(execution)) {
        r_library_internal_fs_position_cancel(&control->position_node,
                                              R_LIBRARY_FS_POSITION_CANCEL_TASK);
    }
    r_library_internal_fs_position_publish(&control->position_node);
    if (pthread_mutex_lock(&control->mutex) != 0) {
        payload_panic();
    }
    control->start_in_progress = 0;
    finalize = terminal_can_finalize_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        payload_panic();
    }
    if (finalize) {
        finalize_operation(control);
    }
    control_release(control);
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
    payload_panic();
}

static void cleanup_control_before_commit(RLibraryFsPayloadControl *control) {
    RRuntimeDarwinIoPreparedRequest *io_prepared;
    RRuntimeDarwinIoHandle *io_handle;

    if (control == NULL) {
        return;
    }
    if (pthread_mutex_lock(&control->mutex) != 0) {
        payload_panic();
    }
    if (control->stage != R_LIBRARY_FS_PAYLOAD_STAGE_PREPARED || control->execution != NULL ||
        control->io_request != NULL) {
        if (pthread_mutex_unlock(&control->mutex) != 0) {
            payload_panic();
        }
        payload_panic();
    }
    control->stage = R_LIBRARY_FS_PAYLOAD_STAGE_FINALIZED;
    io_prepared = control->io_prepared;
    control->io_prepared = NULL;
    io_handle = control->io_handle;
    control->io_handle = NULL;
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        payload_panic();
    }
    r_runtime_darwin_io_prepared_abort(&io_prepared);
    r_library_internal_fs_position_deadline_destroy(&control->deadline);
    if (io_handle != NULL) {
        r_runtime_darwin_io_handle_release(io_handle);
    }
    if (control->registration.registered) {
        r_library_internal_fs_position_abort(&control->position_node);
        r_library_internal_fs_operation_unregister(control->handle_storage, &control->registration);
    }
    control_release(control);
}

static RStdFsTaskStartResult start_task(RLibraryFsPayload *payload) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryFsPayload),
        _Alignof(RLibraryFsPayload),
        payload_move,
        payload_drop,
    };
    RRuntimeTaskPrepareResult preparation =
        r_runtime_task_external_start_prepare(payload_type,
                                              result_type(payload->mode, payload->borrowed),
                                              external_start,
                                              external_cancel);
    RRuntimeTaskStartResult started;
    RStdFsTaskStartResult result = {0};

    if (preparation.status != R_RUNTIME_TASK_START_OK) {
        cleanup_control_before_commit(payload->control);
        payload->control = NULL;
        return task_start_failure(preparation.status);
    }
    started = r_runtime_task_start_commit(&preparation.transaction, payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        cleanup_control_before_commit(payload->control);
        payload->control = NULL;
        return task_start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

static RLibraryFsPayloadSnapshot file_snapshot(const RStdFsFile *file) {
    RLibraryFsPayloadSnapshot snapshot;

    (void)memset(&snapshot, 0, sizeof(snapshot));
    snapshot.storage = r_library_internal_fs_file_handle_storage(file);
    if (snapshot.storage == NULL || pthread_mutex_lock(&snapshot.storage->mutex) != 0) {
        payload_panic();
    }
    snapshot.allocator = snapshot.storage->allocator;
    snapshot.access = snapshot.storage->access;
    snapshot.descriptor = snapshot.storage->descriptor;
    snapshot.append = snapshot.storage->append;
    snapshot.closed =
        snapshot.storage->terminal || snapshot.storage->close_reserved || snapshot.descriptor < 0;
    if (pthread_mutex_unlock(&snapshot.storage->mutex) != 0) {
        payload_panic();
    }
    return snapshot;
}

static RRuntimeTaskStartStatus io_prepare_status(RRuntimeDarwinIoPrepareResult result) {
    switch (result.status) {
    case R_RUNTIME_DARWIN_IO_START_OK:
        return R_RUNTIME_TASK_START_OK;
    case R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED:
    case R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED:
        return R_RUNTIME_TASK_START_ALLOCATION_FAILED;
    case R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED:
    case R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT:
    case R_RUNTIME_DARWIN_IO_START_ORDERING_BLOCKED:
        payload_panic();
    }
    payload_panic();
}

static RRuntimeTaskStartStatus ensure_io_handle_locked(RLibraryFsPayloadControl *control,
                                                       RLibraryFsHandleStorage *storage,
                                                       int *native_error) {
    RRuntimeDarwinIoHandle *payload_io = NULL;
    RRuntimeDarwinIoStartStatus status;

    status = r_library_internal_fs_payload_io_retain_locked(storage, &payload_io, native_error);
    switch (status) {
    case R_RUNTIME_DARWIN_IO_START_OK:
        if (payload_io == NULL) {
            payload_panic();
        }
        control->io_handle = payload_io;
        return R_RUNTIME_TASK_START_OK;
    case R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED:
    case R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED:
        return R_RUNTIME_TASK_START_ALLOCATION_FAILED;
    case R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED:
        return R_RUNTIME_TASK_START_INVALID;
    case R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT:
    case R_RUNTIME_DARWIN_IO_START_ORDERING_BLOCKED:
        payload_panic();
    }
    payload_panic();
}

static RRuntimeTaskStartStatus prepare_and_reserve_locked(RLibraryFsPayload *payload,
                                                          RLibraryFsPayloadControl *control,
                                                          RLibraryFsHandleStorage *storage,
                                                          RStdIoError *operation_error) {
    RRuntimeDarwinIoPrepareResult io_preparation;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeTaskStartStatus status;
    int reserved_descriptor;
    RStdFsAccess reserved_access;
    _Bool reserved_append;
    int native_error = 0;

    if (storage->terminal || storage->close_reserved || storage->descriptor < 0) {
        *operation_error = io_error(R_STD_IO_ERROR_CLOSED, INT64_C(0));
        return R_RUNTIME_TASK_START_INVALID;
    }
    if (!mode_can_access(payload->mode, storage->access)) {
        *operation_error = io_error(R_STD_IO_ERROR_INVALID_OPERATION, INT64_C(0));
        return R_RUNTIME_TASK_START_INVALID;
    }
    control->append = storage->append && payload->mode != R_LIBRARY_FS_PAYLOAD_READ;
    status = ensure_io_handle_locked(control, storage, &native_error);
    if (status != R_RUNTIME_TASK_START_OK) {
        *operation_error = io_error(R_STD_IO_ERROR_CLOSED, (int64_t)native_error);
        return status;
    }
    buffer = staged_native_view(payload->mode,
                                payload->staged_buffer,
                                payload->borrowed_data,
                                payload->borrowed_length,
                                payload->borrowed);
    if (payload->mode == R_LIBRARY_FS_PAYLOAD_READ) {
        io_preparation = r_runtime_darwin_io_prepare_read_some(
            control->io_handle, (off_t)0, &buffer, UINT64_C(0));
    } else if (payload->mode == R_LIBRARY_FS_PAYLOAD_WRITE) {
        io_preparation = r_runtime_darwin_io_prepare_write_some(
            control->io_handle, (off_t)0, &buffer, UINT64_C(0));
    } else {
        io_preparation =
            r_runtime_darwin_io_prepare_write(control->io_handle, (off_t)0, &buffer, UINT64_C(0));
    }
    status = io_prepare_status(io_preparation);
    if (status != R_RUNTIME_TASK_START_OK) {
        return status;
    }
    control->io_prepared = io_preparation.prepared;
    if (!r_library_internal_fs_position_reserve_locked(storage,
                                                       &control->position_node,
                                                       &control->registration,
                                                       position_activate,
                                                       position_cancelled,
                                                       control,
                                                       close_cancel,
                                                       control_retain,
                                                       control_release,
                                                       &reserved_descriptor,
                                                       &reserved_access,
                                                       &reserved_append)) {
        payload_panic();
    }
    if (reserved_descriptor != storage->descriptor || reserved_access != storage->access ||
        reserved_append != storage->append) {
        payload_panic();
    }
    return R_RUNTIME_TASK_START_OK;
}

/*
 * The staged payload already names either the owner (staged_buffer) or the borrowed view; every
 * failure before commit leaves that storage untouched because buffer_owned is still clear.
 */
static RStdFsTaskStartResult start_payload(RLibraryFsPayloadMode mode,
                                           const RStdFsFile *file,
                                           const RLibraryFsPayload *staged,
                                           RStdFsDeadline deadline) {
    RLibraryFsPayload payload = *staged;
    RLibraryFsPayloadSnapshot snapshot;
    RLibraryFsPayloadControl *control;
    RRuntimeTaskStartStatus status;
    RStdIoError deadline_error = {0};
    RStdIoError operation_error = {0};
    const size_t staged_length =
        payload.borrowed ? payload.borrowed_length : payload.staged_buffer->length;

    payload.mode = mode;
    snapshot = file_snapshot(file);
    if (snapshot.closed) {
        payload_make_immediate(&payload,
                               R_LIBRARY_FS_PAYLOAD_IMMEDIATE_ERROR,
                               io_error(R_STD_IO_ERROR_CLOSED, INT64_C(0)));
        return start_task(&payload);
    }
    if (!mode_can_access(mode, snapshot.access)) {
        payload_make_immediate(&payload,
                               R_LIBRARY_FS_PAYLOAD_IMMEDIATE_ERROR,
                               io_error(R_STD_IO_ERROR_INVALID_OPERATION, INT64_C(0)));
        return start_task(&payload);
    }
    /* An offset beyond i64 cannot be positioned, and an append-mode write selects the end. */
    if (payload.positional &&
        (payload.offset > (uint64_t)INT64_MAX ||
         (mode != R_LIBRARY_FS_PAYLOAD_READ && snapshot.append))) {
        payload_make_immediate(&payload,
                               R_LIBRARY_FS_PAYLOAD_IMMEDIATE_ERROR,
                               io_error(R_STD_IO_ERROR_INVALID_OPERATION, INT64_C(0)));
        return start_task(&payload);
    }
    if (staged_length == 0U) {
        payload_make_immediate(&payload, R_LIBRARY_FS_PAYLOAD_IMMEDIATE_SUCCESS, (RStdIoError){0});
        return start_task(&payload);
    }
    if (check_deadline(deadline, &deadline_error) != R_LIBRARY_FS_PAYLOAD_DEADLINE_READY) {
        payload_make_immediate(&payload, R_LIBRARY_FS_PAYLOAD_IMMEDIATE_ERROR, deadline_error);
        return start_task(&payload);
    }
    control = control_create(snapshot.allocator, mode);
    if (control == NULL) {
        return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    payload.control = control;
    control->handle_storage = snapshot.storage;
    control->absolute_deadline = deadline;
    control->positional = payload.positional;
    control->explicit_offset = payload.offset;
    if (deadline.has_value &&
        !r_library_internal_fs_position_deadline_initialize(&control->deadline,
                                                            control->allocator,
                                                            control->absolute_deadline.value,
                                                            deadline_expired,
                                                            control_retain,
                                                            control_release,
                                                            control)) {
        cleanup_control_before_commit(control);
        payload.control = NULL;
        return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    if (pthread_mutex_lock(&snapshot.storage->mutex) != 0) {
        payload_panic();
    }
    status = prepare_and_reserve_locked(&payload, control, snapshot.storage, &operation_error);
    if (pthread_mutex_unlock(&snapshot.storage->mutex) != 0) {
        payload_panic();
    }
    if (status != R_RUNTIME_TASK_START_OK) {
        cleanup_control_before_commit(control);
        payload.control = NULL;
        if (status != R_RUNTIME_TASK_START_INVALID) {
            return task_start_failure(status);
        }
        payload_make_immediate(&payload, R_LIBRARY_FS_PAYLOAD_IMMEDIATE_ERROR, operation_error);
        return start_task(&payload);
    }
    return start_task(&payload);
}

static RStdFsTaskStartResult start_owned(RLibraryFsPayloadMode mode,
                                         const RStdFsFile *file,
                                         RRuntimeArray *buffer,
                                         RStdFsDeadline deadline) {
    RLibraryFsPayload staged = {0};

    staged.staged_buffer = buffer;
    return start_payload(mode, file, &staged, deadline);
}

static RStdFsTaskStartResult start_owned_at(RLibraryFsPayloadMode mode,
                                            const RStdFsFile *file,
                                            uint64_t offset,
                                            RRuntimeArray *buffer,
                                            RStdFsDeadline deadline) {
    RLibraryFsPayload staged = {0};

    staged.staged_buffer = buffer;
    staged.positional = 1;
    staged.offset = offset;
    return start_payload(mode, file, &staged, deadline);
}

/* The runtime reads borrowed write sources through an immutable Dispatch data view. */
static uint8_t *borrowed_write_data(const uint8_t *data) {
    return (uint8_t *)(uintptr_t)data;
}

static RStdFsTaskStartResult start_borrowed(RLibraryFsPayloadMode mode,
                                            const RStdFsFile *file,
                                            uint8_t *data,
                                            size_t length,
                                            RStdFsDeadline deadline) {
    RLibraryFsPayload staged = {0};

    if (length != 0U && data == NULL) {
        payload_panic();
    }
    staged.borrowed = 1;
    staged.borrowed_data = data;
    staged.borrowed_length = length;
    return start_payload(mode, file, &staged, deadline);
}

static RStdFsTaskStartResult start_borrowed_at(RLibraryFsPayloadMode mode,
                                               const RStdFsFile *file,
                                               uint64_t offset,
                                               uint8_t *data,
                                               size_t length,
                                               RStdFsDeadline deadline) {
    RLibraryFsPayload staged = {0};

    if (length != 0U && data == NULL) {
        payload_panic();
    }
    staged.borrowed = 1;
    staged.borrowed_data = data;
    staged.borrowed_length = length;
    staged.positional = 1;
    staged.offset = offset;
    return start_payload(mode, file, &staged, deadline);
}

RStdFsTaskStartResult
r_library_internal_fs_read(const RStdFsFile *file, RRuntimeArray *buffer, RStdFsDeadline deadline) {
    return start_owned(R_LIBRARY_FS_PAYLOAD_READ, file, buffer, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_write(const RStdFsFile *file,
                                                  RRuntimeArray *buffer,
                                                  RStdFsDeadline deadline) {
    return start_owned(R_LIBRARY_FS_PAYLOAD_WRITE, file, buffer, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_write_all(const RStdFsFile *file,
                                                      RRuntimeArray *buffer,
                                                      RStdFsDeadline deadline) {
    return start_owned(R_LIBRARY_FS_PAYLOAD_WRITE_ALL, file, buffer, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_read_into(const RStdFsFile *file,
                                                      RStdFsMutableBytes target,
                                                      RStdFsDeadline deadline) {
    return start_borrowed(R_LIBRARY_FS_PAYLOAD_READ, file, target.data, target.length, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_write_from(const RStdFsFile *file,
                                                       RStdFsConstBytes source,
                                                       RStdFsDeadline deadline) {
    return start_borrowed(R_LIBRARY_FS_PAYLOAD_WRITE,
                          file,
                          borrowed_write_data(source.data),
                          source.length,
                          deadline);
}

RStdFsTaskStartResult r_library_internal_fs_write_all_from(const RStdFsFile *file,
                                                           RStdFsConstBytes source,
                                                           RStdFsDeadline deadline) {
    return start_borrowed(R_LIBRARY_FS_PAYLOAD_WRITE_ALL,
                          file,
                          borrowed_write_data(source.data),
                          source.length,
                          deadline);
}

RStdFsTaskStartResult r_library_internal_fs_read_at(const RStdFsFile *file,
                                                    uint64_t offset,
                                                    RRuntimeArray *buffer,
                                                    RStdFsDeadline deadline) {
    return start_owned_at(R_LIBRARY_FS_PAYLOAD_READ, file, offset, buffer, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_write_all_at(const RStdFsFile *file,
                                                         uint64_t offset,
                                                         RRuntimeArray *buffer,
                                                         RStdFsDeadline deadline) {
    return start_owned_at(R_LIBRARY_FS_PAYLOAD_WRITE_ALL, file, offset, buffer, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_read_at_into(const RStdFsFile *file,
                                                         uint64_t offset,
                                                         RStdFsMutableBytes target,
                                                         RStdFsDeadline deadline) {
    return start_borrowed_at(
        R_LIBRARY_FS_PAYLOAD_READ, file, offset, target.data, target.length, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_write_all_at_from(const RStdFsFile *file,
                                                              uint64_t offset,
                                                              RStdFsConstBytes source,
                                                              RStdFsDeadline deadline) {
    return start_borrowed_at(R_LIBRARY_FS_PAYLOAD_WRITE_ALL,
                             file,
                             offset,
                             borrowed_write_data(source.data),
                             source.length,
                             deadline);
}
