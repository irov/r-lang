#include "r_library_fs_internal.h"

#include "r_library_clock_internal.h"
#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_darwin_fs_lane.h"
#include "r_runtime_darwin_io.h"
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
#include <sys/types.h>

enum {
    R_LIBRARY_FS_READ_FILE_CHUNK_SIZE = 65536U
};

typedef enum RLibraryFsReadFileDeadlineStatus {
    R_LIBRARY_FS_READ_FILE_DEADLINE_READY = 0,
    R_LIBRARY_FS_READ_FILE_DEADLINE_IMMEDIATE,
    R_LIBRARY_FS_READ_FILE_DEADLINE_ERROR
} RLibraryFsReadFileDeadlineStatus;

typedef enum RLibraryFsReadFileOutcome {
    R_LIBRARY_FS_READ_FILE_OUTCOME_NONE = 0,
    R_LIBRARY_FS_READ_FILE_OUTCOME_SUCCESS,
    R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR,
    R_LIBRARY_FS_READ_FILE_OUTCOME_CANCELLED
} RLibraryFsReadFileOutcome;

typedef struct RLibraryFsReadFileControl {
    _Atomic size_t references;
    RRuntimeAllocator *allocator;
    pthread_mutex_t mutex;
    RLibraryFsPositionDeadline deadline_timer;
    RLibraryFsHandleStorage *root_storage;
    RLibraryFsOperationRegistration root_registration;
    RRuntimeDarwinFsPreparedRequest *open_prepared;
    RRuntimeDarwinFsPreparedRequest *close_prepared;
    RRuntimeDarwinFsRequest *open_request;
    RRuntimeDarwinFsRequest *close_request;
    RRuntimeDarwinIoHandle *io_handle;
    RRuntimeDarwinIoRequest *io_request;
    RRuntimeTaskExternalExecution *execution;
    RStdFsArrayResult *result;
    RRuntimeArray output;
    RStdFsDeadline deadline;
    RStdFsError error;
    size_t limit;
    RLibraryFsReadFileOutcome outcome;
    uint64_t outcome_sequence;
    uint64_t immediate_sequence;
    _Bool immediate;
    _Bool start_ready;
    _Bool output_owned;
    _Bool materializing;
    _Bool preparing_read;
    _Bool root_tracking_releasing;
    _Bool root_release_started;
    _Bool root_cleanup_done;
    _Bool deadline_initialized;
    _Bool deadline_cleanup_started;
    _Bool deadline_cleanup_done;
    _Bool open_cancel_sent;
    _Bool open_task_cancel_accepted;
    _Bool io_cancel_sent;
    _Bool io_task_cancel_accepted;
    _Bool task_cancel_seen;
    _Bool cancel_reported;
    _Bool wait_cancel_report;
    _Bool selection_started;
    _Bool selection_finished;
    _Bool completion_selected;
    _Bool cleanup_driving;
    _Bool acknowledged;
} RLibraryFsReadFileControl;

typedef struct RLibraryFsReadFilePayload {
    RLibraryFsReadFileControl *control;
} RLibraryFsReadFilePayload;

#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
static pthread_mutex_t read_file_testing_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t read_file_testing_condition = PTHREAD_COND_INITIALIZER;
static _Bool read_file_testing_pause_before_start_deadline_check;
static _Bool read_file_testing_start_deadline_check_reached;
static _Bool read_file_testing_pause_before_cancel_report;
static _Bool read_file_testing_cancel_report_reached;
static _Bool read_file_testing_pause_before_close_completion;
static _Bool read_file_testing_close_completion_reached;
static _Bool read_file_testing_pause_before_read_activation;
static _Bool read_file_testing_read_activation_reached;
static _Bool read_file_testing_pause_after_read_activation;
static _Bool read_file_testing_after_read_activation_reached;
static _Bool read_file_testing_pause_before_read_completion;
static _Bool read_file_testing_read_completion_reached;
static _Atomic _Bool read_file_testing_limit_after_cancel_observed;
static _Atomic _Bool read_file_testing_limit_forced_completion;
static _Atomic _Bool read_file_testing_read_activation_cancel_observed;
static _Atomic _Bool read_file_testing_cancel_reported_observed;
static _Atomic _Bool read_file_testing_deadline_expired_observed;
static _Atomic uint64_t read_file_testing_read_submission_count;
static _Atomic uint64_t read_file_testing_acknowledgement_count;

static void read_file_testing_set_pause(_Bool *pause, _Bool *reached, _Bool paused) {
    if (pthread_mutex_lock(&read_file_testing_mutex) != 0) {
        abort();
    }
    *pause = paused;
    if (paused) {
        *reached = 0;
    }
    if (pthread_cond_broadcast(&read_file_testing_condition) != 0 ||
        pthread_mutex_unlock(&read_file_testing_mutex) != 0) {
        abort();
    }
}

static _Bool read_file_testing_reached(const _Bool *reached) {
    _Bool value;

    if (pthread_mutex_lock(&read_file_testing_mutex) != 0) {
        abort();
    }
    value = *reached;
    if (pthread_mutex_unlock(&read_file_testing_mutex) != 0) {
        abort();
    }
    return value;
}

static void read_file_testing_wait(_Bool *pause, _Bool *reached) {
    if (pthread_mutex_lock(&read_file_testing_mutex) != 0) {
        abort();
    }
    if (*pause) {
        *reached = 1;
        if (pthread_cond_broadcast(&read_file_testing_condition) != 0) {
            (void)pthread_mutex_unlock(&read_file_testing_mutex);
            abort();
        }
        while (*pause) {
            if (pthread_cond_wait(&read_file_testing_condition, &read_file_testing_mutex) != 0) {
                (void)pthread_mutex_unlock(&read_file_testing_mutex);
                abort();
            }
        }
    }
    if (pthread_mutex_unlock(&read_file_testing_mutex) != 0) {
        abort();
    }
}

void r_library_internal_fs_read_file_testing_pause_before_start_deadline_check(_Bool paused) {
    read_file_testing_set_pause(&read_file_testing_pause_before_start_deadline_check,
                                &read_file_testing_start_deadline_check_reached,
                                paused);
}

_Bool r_library_internal_fs_read_file_testing_start_deadline_check_reached(void) {
    return read_file_testing_reached(&read_file_testing_start_deadline_check_reached);
}

void r_library_internal_fs_read_file_testing_pause_before_cancel_report(_Bool paused) {
    read_file_testing_set_pause(&read_file_testing_pause_before_cancel_report,
                                &read_file_testing_cancel_report_reached,
                                paused);
    if (paused) {
        atomic_store_explicit(
            &read_file_testing_limit_after_cancel_observed, 0, memory_order_relaxed);
        atomic_store_explicit(&read_file_testing_limit_forced_completion, 0, memory_order_relaxed);
    }
}

_Bool r_library_internal_fs_read_file_testing_cancel_report_reached(void) {
    return read_file_testing_reached(&read_file_testing_cancel_report_reached);
}

void r_library_internal_fs_read_file_testing_pause_before_close_completion(_Bool paused) {
    read_file_testing_set_pause(&read_file_testing_pause_before_close_completion,
                                &read_file_testing_close_completion_reached,
                                paused);
}

_Bool r_library_internal_fs_read_file_testing_close_completion_reached(void) {
    return read_file_testing_reached(&read_file_testing_close_completion_reached);
}

void r_library_internal_fs_read_file_testing_pause_before_read_activation(_Bool paused) {
    read_file_testing_set_pause(&read_file_testing_pause_before_read_activation,
                                &read_file_testing_read_activation_reached,
                                paused);
}

_Bool r_library_internal_fs_read_file_testing_read_activation_reached(void) {
    return read_file_testing_reached(&read_file_testing_read_activation_reached);
}

void r_library_internal_fs_read_file_testing_pause_after_read_activation(_Bool paused) {
    read_file_testing_set_pause(&read_file_testing_pause_after_read_activation,
                                &read_file_testing_after_read_activation_reached,
                                paused);
}

_Bool r_library_internal_fs_read_file_testing_after_read_activation_reached(void) {
    return read_file_testing_reached(&read_file_testing_after_read_activation_reached);
}

void r_library_internal_fs_read_file_testing_pause_before_read_completion(_Bool paused) {
    read_file_testing_set_pause(&read_file_testing_pause_before_read_completion,
                                &read_file_testing_read_completion_reached,
                                paused);
}

_Bool r_library_internal_fs_read_file_testing_read_completion_reached(void) {
    return read_file_testing_reached(&read_file_testing_read_completion_reached);
}

void r_library_internal_fs_read_file_testing_reset_observations(void) {
    atomic_store_explicit(&read_file_testing_limit_after_cancel_observed, 0, memory_order_relaxed);
    atomic_store_explicit(&read_file_testing_limit_forced_completion, 0, memory_order_relaxed);
    atomic_store_explicit(
        &read_file_testing_read_activation_cancel_observed, 0, memory_order_relaxed);
    atomic_store_explicit(&read_file_testing_cancel_reported_observed, 0, memory_order_relaxed);
    atomic_store_explicit(&read_file_testing_deadline_expired_observed, 0, memory_order_relaxed);
    atomic_store_explicit(&read_file_testing_read_submission_count, 0U, memory_order_relaxed);
    atomic_store_explicit(&read_file_testing_acknowledgement_count, 0U, memory_order_relaxed);
}

uint64_t r_library_internal_fs_read_file_testing_read_submission_count(void) {
    return atomic_load_explicit(&read_file_testing_read_submission_count, memory_order_acquire);
}

_Bool r_library_internal_fs_read_file_testing_read_activation_cancel_observed(void) {
    return atomic_load_explicit(&read_file_testing_read_activation_cancel_observed,
                                memory_order_acquire);
}

_Bool r_library_internal_fs_read_file_testing_cancel_reported_observed(void) {
    return atomic_load_explicit(&read_file_testing_cancel_reported_observed, memory_order_acquire);
}

_Bool r_library_internal_fs_read_file_testing_deadline_expired_observed(void) {
    return atomic_load_explicit(&read_file_testing_deadline_expired_observed, memory_order_acquire);
}

uint64_t r_library_internal_fs_read_file_testing_acknowledgement_count(void) {
    return atomic_load_explicit(&read_file_testing_acknowledgement_count, memory_order_acquire);
}

_Bool r_library_internal_fs_read_file_testing_limit_after_cancel_observed(void) {
    return atomic_load_explicit(&read_file_testing_limit_after_cancel_observed,
                                memory_order_acquire);
}

_Bool r_library_internal_fs_read_file_testing_limit_forced_completion(void) {
    return atomic_load_explicit(&read_file_testing_limit_forced_completion, memory_order_acquire);
}
#endif

_Static_assert(sizeof(off_t) <= sizeof(int64_t), "Darwin off_t must fit signed 64 bits");

static void drive_cleanup(RLibraryFsReadFileControl *control);
static void submit_next_read(RLibraryFsReadFileControl *control);

_Noreturn static void read_file_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                    (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
}

static void control_lock(RLibraryFsReadFileControl *control) {
    if (pthread_mutex_lock(&control->mutex) != 0) {
        read_file_panic();
    }
}

static void control_unlock(RLibraryFsReadFileControl *control) {
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        read_file_panic();
    }
}

static RStdFsError fs_error(RStdFsErrorCode code, int64_t native_code) {
    return (RStdFsError){code, native_code};
}

static void clear_array(RRuntimeArray *array) {
    (void)memset(array, 0, sizeof(*array));
}

static RRuntimeTypeInfo byte_type(void) {
    return (RRuntimeTypeInfo){sizeof(uint8_t), _Alignof(uint8_t), NULL, NULL};
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

static RLibraryFsReadFileDeadlineStatus classify_deadline(RStdFsDeadline deadline,
                                                          RStdFsError *error) {
    RStdTimeInstantResult now;

    if (!deadline.has_value) {
        return R_LIBRARY_FS_READ_FILE_DEADLINE_READY;
    }
    if (deadline.value.storage_seconds < 0 ||
        deadline.value.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
        *error = fs_error(R_STD_FS_ERROR_INVALID_OPERATION, INT64_C(0));
        return R_LIBRARY_FS_READ_FILE_DEADLINE_ERROR;
    }
    now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
    if (!now.is_ok) {
        *error = fs_error(R_STD_FS_ERROR_OTHER, now.error.native_code);
        return R_LIBRARY_FS_READ_FILE_DEADLINE_ERROR;
    }
    if (compare_instant(deadline.value, now.value) <= 0) {
        *error = fs_error(R_STD_FS_ERROR_TIMED_OUT, INT64_C(0));
        return R_LIBRARY_FS_READ_FILE_DEADLINE_IMMEDIATE;
    }
    return R_LIBRARY_FS_READ_FILE_DEADLINE_READY;
}

static RStdFsErrorCode classify_native_error(_Bool beneath, int native_error) {
    if (native_error == EBADF) {
        return R_STD_FS_ERROR_CLOSED;
    }
    if (beneath && (native_error == ELOOP || native_error == EINVAL
#if defined(ENOTCAPABLE)
                    || native_error == ENOTCAPABLE
#endif
                    )) {
        return R_STD_FS_ERROR_INVALID_RELATIVE_PATH;
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

static RStdFsError native_error(_Bool beneath, int error) {
    return fs_error(classify_native_error(beneath, error), (int64_t)error);
}

static void control_retain(void *context) {
    RLibraryFsReadFileControl *control = context;
    size_t references = atomic_load_explicit(&control->references, memory_order_relaxed);

    for (;;) {
        if (references == 0U || references == SIZE_MAX) {
            read_file_panic();
        }
        if (atomic_compare_exchange_weak_explicit(&control->references,
                                                  &references,
                                                  references + 1U,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
            return;
        }
    }
}

static void control_release(void *context) {
    RLibraryFsReadFileControl *control = context;
    size_t previous;

    if (control == NULL) {
        return;
    }
    previous = atomic_fetch_sub_explicit(&control->references, 1U, memory_order_acq_rel);
    if (previous == 0U) {
        read_file_panic();
    }
    if (previous != 1U) {
        return;
    }
    if (control->root_storage != NULL || control->root_registration.registered ||
        control->open_prepared != NULL || control->close_prepared != NULL ||
        control->open_request != NULL || control->close_request != NULL ||
        control->io_handle != NULL || control->io_request != NULL ||
        control->deadline_timer.token != NULL || control->output_owned || control->materializing ||
        control->preparing_read || control->cleanup_driving) {
        read_file_panic();
    }
    if (pthread_mutex_destroy(&control->mutex) != 0) {
        read_file_panic();
    }
    r_runtime_allocator_deallocate(control, _Alignof(RLibraryFsReadFileControl));
}

static RLibraryFsReadFileControl *
control_create(RRuntimeAllocator *allocator, size_t limit, RStdFsDeadline deadline) {
    RLibraryFsReadFileControl *control = NULL;

    if (allocator == NULL ||
        r_runtime_allocator_allocate(
            allocator, sizeof(*control), _Alignof(RLibraryFsReadFileControl), (void **)&control) !=
            R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    (void)memset(control, 0, sizeof(*control));
    if (pthread_mutex_init(&control->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(control, _Alignof(RLibraryFsReadFileControl));
        return NULL;
    }
    atomic_init(&control->references, 1U);
    control->allocator = allocator;
    control->limit = limit;
    control->deadline = deadline;
    control->output_owned = 1;
    control->root_cleanup_done = 1;
    control->deadline_cleanup_done = 1;
    r_runtime_array_initialize(&control->output, allocator, byte_type());
    return control;
}

static void set_immediate_error(RLibraryFsReadFileControl *control, RStdFsError error) {
    if (control->immediate) {
        read_file_panic();
    }
    control->immediate = 1;
    control->error = error;
    control->immediate_sequence = r_runtime_darwin_event_sequence_next();
}

static void payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryFsReadFilePayload *destination = destination_pointer;
    RLibraryFsReadFilePayload *source = source_pointer;

    destination->control = source->control;
    source->control = NULL;
}

static void payload_drop(void *value) {
    RLibraryFsReadFilePayload *payload = value;
    RLibraryFsReadFileControl *control = payload->control;

    payload->control = NULL;
    control_release(control);
}

static void result_move(void *destination_pointer, void *source_pointer) {
    RStdFsArrayResult *destination = destination_pointer;
    RStdFsArrayResult *source = source_pointer;

    *destination = *source;
    if (source->r_tag == UINT32_C(0)) {
        clear_array(&source->r_payload.r_ok);
    }
}

static void result_drop(void *value) {
    RStdFsArrayResult *result = value;

    if (result->r_tag == UINT32_C(0)) {
        r_runtime_array_destroy(&result->r_payload.r_ok);
    }
}

static RRuntimeTypeInfo result_type(void) {
    return (RRuntimeTypeInfo){
        sizeof(RStdFsArrayResult),
        _Alignof(RStdFsArrayResult),
        result_move,
        result_drop,
    };
}

static _Bool begin_completion_locked(RLibraryFsReadFileControl *control,
                                     RLibraryFsReadFileOutcome outcome,
                                     RStdFsError error,
                                     uint64_t event_sequence) {
    if (outcome != R_LIBRARY_FS_READ_FILE_OUTCOME_SUCCESS &&
        outcome != R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR) {
        read_file_panic();
    }
    if (event_sequence == UINT64_C(0)) {
        read_file_panic();
    }
    if (control->outcome != R_LIBRARY_FS_READ_FILE_OUTCOME_NONE &&
        control->outcome_sequence <= event_sequence) {
        return 0;
    }
    control->outcome = outcome;
    control->error = error;
    control->outcome_sequence = event_sequence;
    if (control->completion_selected ||
        (control->selection_started && !control->selection_finished)) {
        return 0;
    }
    control->selection_started = 1;
    control->selection_finished = 0;
    return 1;
}

static void finish_completion_selection(RLibraryFsReadFileControl *control) {
    uint64_t cancellation_sequence;
    uint64_t event_sequence;
    _Bool selected;

    for (;;) {
        control_lock(control);
        if (control->acknowledged || !control->selection_started || control->selection_finished ||
            control->completion_selected) {
            control_unlock(control);
            read_file_panic();
        }
        if (control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_SUCCESS ||
            control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR) {
            event_sequence = control->outcome_sequence;
        } else if (control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_CANCELLED) {
            control->selection_finished = 1;
            control_unlock(control);
            drive_cleanup(control);
            return;
        } else {
            control_unlock(control);
            read_file_panic();
        }
        control_unlock(control);

        selected =
            r_runtime_task_external_try_select_completion_at(control->execution, event_sequence);
        control_lock(control);
        if (control->acknowledged || !control->selection_started || control->selection_finished ||
            control->completion_selected) {
            control_unlock(control);
            read_file_panic();
        }
        if (selected) {
            control->completion_selected = 1;
            control->selection_finished = 1;
            cancellation_sequence =
                r_runtime_task_external_cancellation_sequence(control->execution);
            if (cancellation_sequence != UINT64_C(0) && !control->cancel_reported) {
                control->wait_cancel_report = 1;
            }
        } else if ((control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_SUCCESS ||
                    control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR) &&
                   control->outcome_sequence < event_sequence) {
            control_unlock(control);
            continue;
        } else if (control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_SUCCESS ||
                   control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR) {
            cancellation_sequence =
                r_runtime_task_external_cancellation_sequence(control->execution);
            if (cancellation_sequence == UINT64_C(0) || cancellation_sequence > event_sequence) {
                control_unlock(control);
                read_file_panic();
            }
            control->outcome = R_LIBRARY_FS_READ_FILE_OUTCOME_CANCELLED;
            control->outcome_sequence = cancellation_sequence;
            control->wait_cancel_report = 1;
            control->selection_finished = 1;
        } else if (control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_CANCELLED) {
            control->selection_finished = 1;
        } else {
            control_unlock(control);
            read_file_panic();
        }
        control_unlock(control);
        drive_cleanup(control);
        return;
    }
}

static void establish_completion_at(RLibraryFsReadFileControl *control,
                                    RLibraryFsReadFileOutcome outcome,
                                    RStdFsError error,
                                    uint64_t event_sequence) {
    _Bool select;

    control_lock(control);
    select = begin_completion_locked(control, outcome, error, event_sequence);
    control_unlock(control);
    if (select) {
        finish_completion_selection(control);
    } else {
        drive_cleanup(control);
    }
}

static void establish_cancellation_locked(RLibraryFsReadFileControl *control,
                                          uint64_t event_sequence) {
    if (event_sequence == UINT64_C(0)) {
        read_file_panic();
    }
    if (control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_NONE ||
        event_sequence < control->outcome_sequence) {
        control->outcome = R_LIBRARY_FS_READ_FILE_OUTCOME_CANCELLED;
        control->outcome_sequence = event_sequence;
        control->wait_cancel_report = 1;
    }
}

static _Bool observe_task_cancellation_locked(RLibraryFsReadFileControl *control) {
    uint64_t cancellation_sequence;

    if (!control->task_cancel_seen && control->execution != NULL &&
        r_runtime_task_external_cancel_requested(control->execution)) {
        control->task_cancel_seen = 1;
    }
    if (control->task_cancel_seen) {
        cancellation_sequence = r_runtime_task_external_cancellation_sequence(control->execution);
        if (cancellation_sequence == UINT64_C(0)) {
            read_file_panic();
        }
        establish_cancellation_locked(control, cancellation_sequence);
        return 1;
    }
    return 0;
}

static void replace_success_with_error_locked(RLibraryFsReadFileControl *control,
                                              RStdFsError error) {
    if (control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_SUCCESS) {
        control->outcome = R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR;
        control->error = error;
    }
}

static void maybe_finish(RLibraryFsReadFileControl *control) {
    RRuntimeTaskExternalExecution *execution = NULL;
    RRuntimeArray discarded;
    _Bool discard_output = 0;

    clear_array(&discarded);
    control_retain(control);
    control_lock(control);
    if (!control->acknowledged && control->start_ready &&
        control->outcome != R_LIBRARY_FS_READ_FILE_OUTCOME_NONE &&
        (control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_CANCELLED ||
         control->completion_selected) &&
        (!control->selection_started || control->selection_finished) &&
        (!control->wait_cancel_report || control->cancel_reported) &&
        control->open_prepared == NULL && control->close_prepared == NULL &&
        control->open_request == NULL && control->close_request == NULL &&
        control->io_handle == NULL && control->io_request == NULL &&
        control->root_storage == NULL && !control->root_tracking_releasing &&
        !control->materializing && !control->preparing_read && control->root_cleanup_done &&
        control->deadline_cleanup_done) {
        control->acknowledged = 1;
        execution = control->execution;
        if (control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_SUCCESS) {
            (void)memset(control->result, 0, sizeof(*control->result));
            control->result->r_payload.r_ok = control->output;
            clear_array(&control->output);
            control->output_owned = 0;
        } else {
            if (control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR) {
                (void)memset(control->result, 0, sizeof(*control->result));
                control->result->r_tag = UINT32_C(1);
                control->result->r_payload.r_err = control->error;
            }
            if (control->output_owned) {
                discarded = control->output;
                clear_array(&control->output);
                control->output_owned = 0;
                discard_output = 1;
            }
        }
    }
    control_unlock(control);
    if (discard_output) {
        r_runtime_array_destroy(&discarded);
    }
    if (execution != NULL) {
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
        (void)atomic_fetch_add_explicit(
            &read_file_testing_acknowledgement_count, UINT64_C(1), memory_order_release);
#endif
        r_runtime_task_external_acknowledge(execution);
    }
    control_release(control);
}

static void root_cleanup_completed(void *context, int native_error_value) {
    RLibraryFsReadFileControl *control = context;

    control_lock(control);
    if (!control->root_release_started || control->root_cleanup_done) {
        control_unlock(control);
        read_file_panic();
    }
    control->root_cleanup_done = 1;
    if (native_error_value != 0) {
        replace_success_with_error_locked(control, native_error(0, native_error_value));
    }
    control_unlock(control);
    drive_cleanup(control);
    control_release(control);
}

static void release_root_tracking(RLibraryFsReadFileControl *control) {
    RLibraryFsHandleStorage *storage;
    _Bool registered;

    control_lock(control);
    if (control->root_storage == NULL || control->root_tracking_releasing ||
        control->open_request != NULL) {
        control_unlock(control);
        return;
    }
    storage = control->root_storage;
    registered = control->root_registration.registered;
    control->root_tracking_releasing = 1;
    control_unlock(control);

    if (registered) {
        r_library_internal_fs_operation_unregister(storage, &control->root_registration);
    } else {
        r_library_internal_fs_handle_release(storage);
    }

    control_lock(control);
    if (control->root_storage != storage || control->root_registration.registered) {
        control_unlock(control);
        read_file_panic();
    }
    control->root_storage = NULL;
    control->root_tracking_releasing = 0;
    control_unlock(control);
}

static void drive_cleanup(RLibraryFsReadFileControl *control) {
    control_retain(control);
    control_lock(control);
    if (control->cleanup_driving) {
        control_unlock(control);
        control_release(control);
        return;
    }
    control->cleanup_driving = 1;
    control_unlock(control);

    for (;;) {
        RRuntimeDarwinFsPreparedRequest *fs_prepared = NULL;
        RRuntimeDarwinIoHandle *io_handle = NULL;
        _Bool destroy_deadline = 0;
        _Bool release_tracking = 0;

        control_lock(control);
        if (control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_NONE) {
            control->cleanup_driving = 0;
            control_unlock(control);
            break;
        }
        if (!control->deadline_cleanup_started) {
            control->deadline_cleanup_started = 1;
            if (control->deadline_initialized) {
                destroy_deadline = 1;
            } else {
                control->deadline_cleanup_done = 1;
            }
        } else if (control->open_request != NULL && !control->open_cancel_sent) {
            control->open_cancel_sent = 1;
            (void)r_runtime_darwin_fs_request_cancel(control->open_request);
        } else if (control->io_request != NULL && !control->io_cancel_sent) {
            control->io_cancel_sent = 1;
            (void)r_runtime_darwin_io_request_cancel(control->io_request);
        } else if (control->open_request == NULL && !control->materializing &&
                   !control->preparing_read && control->open_prepared != NULL) {
            fs_prepared = control->open_prepared;
            control->open_prepared = NULL;
        } else if (control->open_request == NULL && !control->materializing &&
                   !control->preparing_read && control->close_request == NULL &&
                   control->close_prepared != NULL) {
            fs_prepared = control->close_prepared;
            control->close_prepared = NULL;
        } else if (control->open_request == NULL && control->root_storage != NULL &&
                   !control->root_tracking_releasing) {
            release_tracking = 1;
        } else if (control->open_request == NULL && control->io_request == NULL &&
                   !control->materializing && !control->preparing_read &&
                   control->io_handle != NULL && !control->root_release_started) {
            io_handle = control->io_handle;
            control->io_handle = NULL;
            control->root_release_started = 1;
            control->root_cleanup_done = 0;
            control_retain(control);
        } else {
            control->cleanup_driving = 0;
            control_unlock(control);
            break;
        }
        control_unlock(control);

        if (destroy_deadline) {
            r_library_internal_fs_position_deadline_destroy(&control->deadline_timer);
            control_lock(control);
            control->deadline_initialized = 0;
            control->deadline_cleanup_done = 1;
            control_unlock(control);
        } else if (fs_prepared != NULL) {
            r_runtime_darwin_fs_prepared_abort(&fs_prepared);
        } else if (release_tracking) {
            release_root_tracking(control);
        } else if (io_handle != NULL && !r_runtime_darwin_io_handle_release_with_cleanup(
                                            io_handle, root_cleanup_completed, control)) {
            control_release(control);
            read_file_panic();
        }
    }
    maybe_finish(control);
    control_release(control);
}

static void deadline_expired(void *context) {
    RLibraryFsReadFileControl *control = context;
    uint64_t event_sequence = r_runtime_darwin_event_sequence_next();

    establish_completion_at(control,
                            R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR,
                            fs_error(R_STD_FS_ERROR_TIMED_OUT, INT64_C(0)),
                            event_sequence);
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    atomic_store_explicit(&read_file_testing_deadline_expired_observed, 1, memory_order_release);
#endif
}

static RStdFsError open_result_error(_Bool beneath, RRuntimeDarwinFsResult result) {
    int native_code =
        result.operation_native_error != 0 ? result.operation_native_error : result.native_error;

    if (result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_TIMED_OUT) {
        return fs_error(R_STD_FS_ERROR_TIMED_OUT, (int64_t)native_code);
    }
    if (result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_CANCELLED ||
        result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_STOPPING) {
        return fs_error(R_STD_FS_ERROR_CANCELLED, (int64_t)native_code);
    }
    return native_error(beneath, native_code);
}

static RStdFsError io_result_error(RRuntimeDarwinIoResult result) {
    int native_code =
        result.operation_native_error != 0 ? result.operation_native_error : result.native_error;

    if (result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_TIMED_OUT) {
        return fs_error(R_STD_FS_ERROR_TIMED_OUT, (int64_t)native_code);
    }
    if (result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED) {
        return fs_error(R_STD_FS_ERROR_CANCELLED, (int64_t)native_code);
    }
    return native_error(0, native_code);
}

static RStdFsError io_create_error(RRuntimeDarwinIoHandleCreateResult created) {
    if (created.status == R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED) {
        return fs_error(R_STD_FS_ERROR_RESOURCE_EXHAUSTED, INT64_C(0));
    }
    if (created.status == R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED) {
        return native_error(0, created.native_error);
    }
    if (created.status == R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED) {
        return fs_error(R_STD_FS_ERROR_CLOSED, (int64_t)created.native_error);
    }
    if (created.status == R_RUNTIME_DARWIN_IO_START_ORDERING_BLOCKED) {
        return fs_error(R_STD_FS_ERROR_INVALID_OPERATION, (int64_t)created.native_error);
    }
    read_file_panic();
}

static void close_completed(RRuntimeDarwinFsRequest *request, void *context) {
    RLibraryFsReadFileControl *control = context;
    RRuntimeDarwinFsResult result = r_runtime_darwin_fs_request_wait(request);
    RStdFsError error = {0};
    _Bool select = 0;
    _Bool should_read = 0;
    int unclosed;

    unclosed = r_runtime_darwin_fs_request_take_unclosed_fd(request);
    if (unclosed >= 0 || result.terminal_event_sequence == UINT64_C(0)) {
        read_file_panic();
    }
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    read_file_testing_wait(&read_file_testing_pause_before_close_completion,
                           &read_file_testing_close_completion_reached);
#endif
    control_lock(control);
    if (control->close_request != request) {
        control_unlock(control);
        read_file_panic();
    }
    control->close_request = NULL;
    if (result.terminal_event != R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE || result.native_error != 0) {
        error = open_result_error(0, result);
        select = begin_completion_locked(
            control, R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR, error, result.terminal_event_sequence);
    } else if (!observe_task_cancellation_locked(control)) {
        should_read =
            control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_NONE && control->io_handle != NULL;
    }
    control_unlock(control);
    r_runtime_darwin_fs_request_release(request);
    if (select) {
        finish_completion_selection(control);
    } else if (should_read) {
        submit_next_read(control);
    } else {
        drive_cleanup(control);
    }
    control_release(control);
}

static RStdFsError array_status_error(RRuntimeArrayStatus status) {
    if (status == R_RUNTIME_ARRAY_ALLOCATION_FAILED) {
        return fs_error(R_STD_FS_ERROR_RESOURCE_EXHAUSTED, INT64_C(0));
    }
    if (status == R_RUNTIME_ARRAY_SIZE_OVERFLOW) {
        return fs_error(R_STD_FS_ERROR_FILE_TOO_LARGE, INT64_C(0));
    }
    read_file_panic();
}

static void read_completed(RRuntimeDarwinIoRequest *request, void *context) {
    RLibraryFsReadFileControl *control = context;
    RRuntimeDarwinIoResult native_result = r_runtime_darwin_io_request_wait(request);
    RRuntimeDarwinIoBuffer buffer = r_runtime_darwin_io_request_take_buffer(request);
    RStdFsError error = {0};
    uint64_t error_sequence;
    _Bool continue_reading = 0;
    _Bool process_native_data;
    _Bool select = 0;

#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    read_file_testing_wait(&read_file_testing_pause_before_read_completion,
                           &read_file_testing_read_completion_reached);
#endif
    control_lock(control);
    if (control->io_request != request || buffer.size != native_result.bytes_transferred ||
        buffer.size > buffer.capacity || control->output.length > control->limit ||
        native_result.terminal_event_sequence == UINT64_C(0)) {
        control_unlock(control);
        read_file_panic();
    }
    control->io_request = NULL;
    control->io_cancel_sent = 0;
    (void)observe_task_cancellation_locked(control);
    if (native_result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE &&
        native_result.native_error == 0) {
        if (buffer.size > control->limit - control->output.length) {
            error = fs_error(R_STD_FS_ERROR_FILE_TOO_LARGE, INT64_C(0));
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
            if (r_runtime_task_external_cancel_requested(control->execution)) {
                atomic_store_explicit(
                    &read_file_testing_limit_after_cancel_observed, 1, memory_order_release);
            }
#endif
            select = begin_completion_locked(control,
                                             R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR,
                                             error,
                                             native_result.terminal_event_sequence);
        } else {
            process_native_data = control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_NONE ||
                                  (native_result.eof && native_result.terminal_event_sequence <
                                                            control->outcome_sequence);
            if (process_native_data && buffer.size != 0U) {
                RRuntimeArrayStatus status = r_runtime_array_reserve(&control->output, buffer.size);

                if (status != R_RUNTIME_ARRAY_OK) {
                    error = array_status_error(status);
                    error_sequence = r_runtime_darwin_event_sequence_next();
                    select = begin_completion_locked(
                        control, R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR, error, error_sequence);
                } else {
                    (void)memcpy((uint8_t *)control->output.data + control->output.length,
                                 buffer.data,
                                 buffer.size);
                    control->output.length += buffer.size;
                    if (native_result.eof) {
                        select = begin_completion_locked(control,
                                                         R_LIBRARY_FS_READ_FILE_OUTCOME_SUCCESS,
                                                         (RStdFsError){0},
                                                         native_result.terminal_event_sequence);
                    } else {
                        continue_reading = 1;
                    }
                }
            } else if (native_result.eof && process_native_data) {
                select = begin_completion_locked(control,
                                                 R_LIBRARY_FS_READ_FILE_OUTCOME_SUCCESS,
                                                 (RStdFsError){0},
                                                 native_result.terminal_event_sequence);
            } else if (!native_result.eof && buffer.size == 0U &&
                       control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_NONE) {
                error = fs_error(R_STD_FS_ERROR_OTHER, INT64_C(0));
                error_sequence = r_runtime_darwin_event_sequence_next();
                select = begin_completion_locked(
                    control, R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR, error, error_sequence);
            }
        }
    } else {
        error = io_result_error(native_result);
        select = begin_completion_locked(control,
                                         R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR,
                                         error,
                                         native_result.terminal_event_sequence);
    }
    control->io_task_cancel_accepted = 0;
    control_unlock(control);

    r_runtime_darwin_io_request_release(request);
    r_runtime_darwin_io_buffer_release(&buffer);
    if (select) {
        finish_completion_selection(control);
    } else if (continue_reading) {
        submit_next_read(control);
    } else {
        drive_cleanup(control);
    }
    control_release(control);
}

static RStdFsError io_prepare_error(RRuntimeDarwinIoPrepareResult prepared) {
    if (prepared.status == R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED) {
        return fs_error(R_STD_FS_ERROR_RESOURCE_EXHAUSTED, INT64_C(0));
    }
    if (prepared.status == R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED) {
        return native_error(0, prepared.native_error);
    }
    if (prepared.status == R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED) {
        return fs_error(R_STD_FS_ERROR_CLOSED, (int64_t)prepared.native_error);
    }
    if (prepared.status == R_RUNTIME_DARWIN_IO_START_ORDERING_BLOCKED) {
        return fs_error(R_STD_FS_ERROR_INVALID_OPERATION, (int64_t)prepared.native_error);
    }
    read_file_panic();
}

static size_t next_read_capacity(const RLibraryFsReadFileControl *control) {
    size_t remaining;

    if (control->output.length > control->limit) {
        read_file_panic();
    }
    if (control->limit == SIZE_MAX) {
        if (control->output.length == SIZE_MAX) {
            return 0U;
        }
        remaining = SIZE_MAX - control->output.length;
        return remaining < R_LIBRARY_FS_READ_FILE_CHUNK_SIZE ? remaining
                                                             : R_LIBRARY_FS_READ_FILE_CHUNK_SIZE;
    }
    remaining = control->limit - control->output.length;
    if (remaining >= R_LIBRARY_FS_READ_FILE_CHUNK_SIZE - 1U) {
        return R_LIBRARY_FS_READ_FILE_CHUNK_SIZE;
    }
    return remaining + 1U;
}

static void submit_next_read(RLibraryFsReadFileControl *control) {
    RRuntimeDarwinIoBufferResult allocated;
    RRuntimeDarwinIoPrepareResult prepared;
    RRuntimeDarwinIoSubmitResult submitted;
    RRuntimeDarwinIoHandle *handle;
    RStdFsError error = {0};
    size_t capacity;
    size_t offset;
    uint64_t prepare_error_sequence = UINT64_C(0);
    uint64_t submit_error_sequence = UINT64_C(0);
    _Bool cancellation_observed;
    _Bool select = 0;

    control_retain(control);
    control_lock(control);
    if (control->acknowledged || control->outcome != R_LIBRARY_FS_READ_FILE_OUTCOME_NONE ||
        control->io_handle == NULL || control->io_request != NULL || control->preparing_read ||
        control->materializing) {
        control_unlock(control);
        drive_cleanup(control);
        control_release(control);
        return;
    }
    if (observe_task_cancellation_locked(control)) {
        control_unlock(control);
        drive_cleanup(control);
        control_release(control);
        return;
    }
    capacity = next_read_capacity(control);
    offset = control->output.length;
    if (capacity == 0U || offset > (size_t)INT64_MAX) {
        select = begin_completion_locked(control,
                                         R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR,
                                         fs_error(R_STD_FS_ERROR_FILE_TOO_LARGE, INT64_C(0)),
                                         r_runtime_darwin_event_sequence_next());
        control_unlock(control);
        if (select) {
            finish_completion_selection(control);
        }
        control_release(control);
        return;
    }
    handle = control->io_handle;
    control->preparing_read = 1;
    control_unlock(control);

    allocated = r_runtime_darwin_io_buffer_allocate(control->allocator, capacity);
    if (allocated.status == R_RUNTIME_DARWIN_IO_START_OK) {
        prepared = r_runtime_darwin_io_prepare_read_some(
            handle, (off_t)offset, &allocated.buffer, UINT64_C(0));
    } else {
        (void)memset(&prepared, 0, sizeof(prepared));
        prepared.status = allocated.status;
    }
    if (prepared.status != R_RUNTIME_DARWIN_IO_START_OK || prepared.prepared == NULL) {
        prepare_error_sequence = r_runtime_darwin_event_sequence_next();
    }

#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    read_file_testing_wait(&read_file_testing_pause_before_read_activation,
                           &read_file_testing_read_activation_reached);
#endif

    control_lock(control);
    cancellation_observed = control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_NONE &&
                            observe_task_cancellation_locked(control);
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    if (cancellation_observed) {
        atomic_store_explicit(
            &read_file_testing_read_activation_cancel_observed, 1, memory_order_release);
    }
#endif
    if (control->outcome != R_LIBRARY_FS_READ_FILE_OUTCOME_NONE || cancellation_observed) {
        control_unlock(control);
        if (prepared.prepared != NULL) {
            r_runtime_darwin_io_prepared_abort(&prepared.prepared);
        }
        r_runtime_darwin_io_buffer_release(&allocated.buffer);
        control_lock(control);
        if (!control->preparing_read) {
            control_unlock(control);
            read_file_panic();
        }
        control->preparing_read = 0;
        control_unlock(control);
        drive_cleanup(control);
        control_release(control);
        return;
    }
    if (prepared.status != R_RUNTIME_DARWIN_IO_START_OK || prepared.prepared == NULL) {
        error = io_prepare_error(prepared);
        select = begin_completion_locked(
            control, R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR, error, prepare_error_sequence);
        control_unlock(control);
        r_runtime_darwin_io_buffer_release(&allocated.buffer);
        control_lock(control);
        if (!control->preparing_read) {
            control_unlock(control);
            read_file_panic();
        }
        control->preparing_read = 0;
        control_unlock(control);
        if (select) {
            finish_completion_selection(control);
        } else {
            drive_cleanup(control);
        }
        control_release(control);
        return;
    }
    submitted = r_runtime_darwin_io_prepared_activate(&prepared.prepared, &allocated.buffer);
    if (submitted.status != R_RUNTIME_DARWIN_IO_START_OK || submitted.request == NULL ||
        prepared.prepared != NULL) {
        submit_error_sequence = r_runtime_darwin_event_sequence_next();
        control_unlock(control);
        if (prepared.prepared != NULL) {
            r_runtime_darwin_io_prepared_abort(&prepared.prepared);
        }
        r_runtime_darwin_io_buffer_release(&allocated.buffer);
        control_lock(control);
        if (!control->preparing_read) {
            control_unlock(control);
            read_file_panic();
        }
        control->preparing_read = 0;
        control_unlock(control);
        establish_completion_at(control,
                                R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR,
                                io_prepare_error((RRuntimeDarwinIoPrepareResult){
                                    NULL, submitted.status, submitted.native_error}),
                                submit_error_sequence);
        control_release(control);
        return;
    }
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    (void)atomic_fetch_add_explicit(
        &read_file_testing_read_submission_count, UINT64_C(1), memory_order_release);
#endif
    control->io_request = submitted.request;
    control->io_cancel_sent = 0;
    control->io_task_cancel_accepted = 0;
    control->preparing_read = 0;
    control_retain(control);
    control_unlock(control);
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    read_file_testing_wait(&read_file_testing_pause_after_read_activation,
                           &read_file_testing_after_read_activation_reached);
#endif
    if (!r_runtime_darwin_io_request_set_completion(submitted.request, read_completed, control)) {
        read_file_panic();
    }
    control_release(control);
}

static void open_completed(RRuntimeDarwinFsRequest *request, void *context) {
    RLibraryFsReadFileControl *control = context;
    RRuntimeDarwinFsResult native_result = r_runtime_darwin_fs_request_wait(request);
    RRuntimeDarwinIoHandleCreateResult created;
    RRuntimeDarwinFsPreparedRequest *close_prepared;
    RRuntimeDarwinFsSubmitResult close_submitted;
    RStdFsError error = {0};
    uint64_t created_error_sequence = UINT64_C(0);
    _Bool beneath;
    _Bool select = 0;
    int descriptor = -1;

    if (native_result.terminal_event_sequence == UINT64_C(0)) {
        read_file_panic();
    }
    if (native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE &&
        native_result.native_error == 0) {
        descriptor = r_runtime_darwin_fs_request_take_opened_fd(request);
        if (descriptor < 0) {
            read_file_panic();
        }
    }
    control_lock(control);
    if (control->open_request != request) {
        control_unlock(control);
        read_file_panic();
    }
    beneath = control->root_storage != NULL;
    control->open_request = NULL;
    control->open_cancel_sent = 0;
    control->materializing = descriptor >= 0;
    control_unlock(control);

    release_root_tracking(control);
    r_runtime_darwin_fs_request_release(request);

    if (descriptor < 0) {
        control_lock(control);
        (void)observe_task_cancellation_locked(control);
        error = open_result_error(beneath, native_result);
        select = begin_completion_locked(control,
                                         R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR,
                                         error,
                                         native_result.terminal_event_sequence);
        control->open_task_cancel_accepted = 0;
        control_unlock(control);
        if (select) {
            finish_completion_selection(control);
        } else {
            drive_cleanup(control);
        }
        control_release(control);
        return;
    }

    created = r_runtime_darwin_io_handle_create(
        control->allocator, descriptor, R_RUNTIME_DARWIN_IO_RANDOM);
    if (created.status != R_RUNTIME_DARWIN_IO_START_OK || created.handle == NULL) {
        created_error_sequence = r_runtime_darwin_event_sequence_next();
    }
    control_lock(control);
    close_prepared = control->close_prepared;
    control->close_prepared = NULL;
    control_unlock(control);
    close_submitted = r_runtime_darwin_fs_prepared_activate_close(&close_prepared, &descriptor);
    if (close_submitted.status != R_RUNTIME_DARWIN_FS_START_OK || close_submitted.request == NULL ||
        close_prepared != NULL || descriptor >= 0) {
        read_file_panic();
    }

    control_lock(control);
    control->close_request = close_submitted.request;
    if (created.status == R_RUNTIME_DARWIN_IO_START_OK && created.handle != NULL) {
        control->io_handle = created.handle;
        control->root_cleanup_done = 0;
    }
    control->materializing = 0;
    control_retain(control);
    (void)observe_task_cancellation_locked(control);
    if (created.status != R_RUNTIME_DARWIN_IO_START_OK || created.handle == NULL) {
        error = io_create_error(created);
        select = begin_completion_locked(
            control, R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR, error, created_error_sequence);
    }
    control_unlock(control);
    if (!r_runtime_darwin_fs_request_set_completion(
            close_submitted.request, close_completed, control)) {
        read_file_panic();
    }
    if (select) {
        finish_completion_selection(control);
    } else {
        drive_cleanup(control);
    }
    control_release(control);
}

static void external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryFsReadFilePayload *payload = payload_pointer;
    RLibraryFsReadFileControl *control = payload->control;
    uint64_t cancellation_sequence;
    _Bool accepted;

    if (control == NULL) {
        read_file_panic();
    }
    cancellation_sequence = r_runtime_task_external_cancellation_sequence(execution);
    if (cancellation_sequence == UINT64_C(0)) {
        read_file_panic();
    }
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    read_file_testing_wait(&read_file_testing_pause_before_cancel_report,
                           &read_file_testing_cancel_report_reached);
#endif
    control_retain(control);
    control_lock(control);
    if (control->execution != execution || control->cancel_reported) {
        control_unlock(control);
        read_file_panic();
    }
    control->task_cancel_seen = 1;
    control->wait_cancel_report = 1;
    establish_cancellation_locked(control, cancellation_sequence);
    if (control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_CANCELLED) {
        if (control->open_request != NULL) {
            control->open_cancel_sent = 1;
            accepted = r_runtime_darwin_fs_request_cancel(control->open_request);
            control->open_task_cancel_accepted = accepted;
        } else if (control->io_request != NULL) {
            control->io_cancel_sent = 1;
            accepted = r_runtime_darwin_io_request_cancel(control->io_request);
            control->io_task_cancel_accepted = accepted;
        }
    }
    control->cancel_reported = 1;
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    atomic_store_explicit(&read_file_testing_cancel_reported_observed, 1, memory_order_release);
#endif
    control_unlock(control);
    drive_cleanup(control);
    control_release(control);
}

static void mark_start_ready(RLibraryFsReadFileControl *control) {
    r_runtime_task_external_start_ready(control->execution);
    control_lock(control);
    if (control->start_ready) {
        control_unlock(control);
        read_file_panic();
    }
    control->start_ready = 1;
    if (control->deadline_initialized && control->outcome == R_LIBRARY_FS_READ_FILE_OUTCOME_NONE) {
        r_library_internal_fs_position_deadline_activate(&control->deadline_timer);
    }
    control_unlock(control);
    drive_cleanup(control);
}

static void complete_pre_start_error(RLibraryFsReadFileControl *control, RStdFsError error) {
    control_lock(control);
    if (!begin_completion_locked(
            control, R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR, error, control->immediate_sequence)) {
        control_unlock(control);
        read_file_panic();
    }
    control_unlock(control);
    finish_completion_selection(control);
    mark_start_ready(control);
}

static void external_start(RRuntimeTaskExternalExecution *execution,
                           void *payload_pointer,
                           void *result_pointer) {
    RLibraryFsReadFilePayload *payload = payload_pointer;
    RLibraryFsReadFileControl *control = payload->control;
    RRuntimeDarwinFsSubmitResult submitted;
    RLibraryFsReadFileDeadlineStatus deadline_status;
    RStdFsError deadline_error = {0};
    uint64_t submit_error_sequence;
    _Bool registration_failed = 0;
    _Bool select = 0;

    if (control == NULL || result_pointer == NULL) {
        read_file_panic();
    }
    control->execution = execution;
    control->result = result_pointer;
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    read_file_testing_wait(&read_file_testing_pause_before_start_deadline_check,
                           &read_file_testing_start_deadline_check_reached);
#endif
    if (!control->immediate && control->deadline.has_value) {
        deadline_status = classify_deadline(control->deadline, &deadline_error);
        if (deadline_status != R_LIBRARY_FS_READ_FILE_DEADLINE_READY) {
            set_immediate_error(control, deadline_error);
        }
    }
    if (control->immediate) {
        complete_pre_start_error(control, control->error);
        return;
    }
    if (r_runtime_task_external_cancel_requested(execution)) {
        uint64_t cancellation_sequence = r_runtime_task_external_cancellation_sequence(execution);

        control_lock(control);
        establish_cancellation_locked(control, cancellation_sequence);
        control->wait_cancel_report = 1;
        control_unlock(control);
        mark_start_ready(control);
        return;
    }
    submitted = r_runtime_darwin_fs_prepared_activate(&control->open_prepared);
    if (submitted.status != R_RUNTIME_DARWIN_FS_START_OK || submitted.request == NULL ||
        control->open_prepared != NULL) {
        submit_error_sequence = r_runtime_darwin_event_sequence_next();
        establish_completion_at(control,
                                R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR,
                                fs_error(R_STD_FS_ERROR_CANCELLED, (int64_t)submitted.native_error),
                                submit_error_sequence);
        mark_start_ready(control);
        return;
    }
    control_lock(control);
    control->open_request = submitted.request;
    if (control->root_storage != NULL &&
        !r_library_internal_fs_operation_register(
            control->root_storage, &control->root_registration, submitted.request)) {
        registration_failed = 1;
        select = begin_completion_locked(control,
                                         R_LIBRARY_FS_READ_FILE_OUTCOME_ERROR,
                                         fs_error(R_STD_FS_ERROR_CLOSED, INT64_C(0)),
                                         r_runtime_darwin_event_sequence_next());
    }
    control_retain(control);
    control_unlock(control);
    mark_start_ready(control);
    if (!r_runtime_darwin_fs_request_set_completion(submitted.request, open_completed, control)) {
        read_file_panic();
    }
    if (registration_failed) {
        if (!select) {
            read_file_panic();
        }
        finish_completion_selection(control);
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
    read_file_panic();
}

static RRuntimeTaskStartStatus native_prepare_status(RRuntimeDarwinFsPrepareResult prepared) {
    switch (prepared.status) {
    case R_RUNTIME_DARWIN_FS_START_OK:
        return R_RUNTIME_TASK_START_OK;
    case R_RUNTIME_DARWIN_FS_START_RUNTIME_STOPPING:
        return R_RUNTIME_TASK_START_RUNTIME_STOPPING;
    case R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED:
    case R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED:
    case R_RUNTIME_DARWIN_FS_START_QUEUE_FULL:
        return R_RUNTIME_TASK_START_ALLOCATION_FAILED;
    case R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT:
        read_file_panic();
    }
    read_file_panic();
}

static void discard_uncommitted(RLibraryFsReadFileControl *control) {
    if (control->open_prepared != NULL) {
        r_runtime_darwin_fs_prepared_abort(&control->open_prepared);
    }
    if (control->close_prepared != NULL) {
        r_runtime_darwin_fs_prepared_abort(&control->close_prepared);
    }
    if (control->root_storage != NULL) {
        r_library_internal_fs_handle_release(control->root_storage);
        control->root_storage = NULL;
    }
    if (control->deadline_initialized) {
        r_library_internal_fs_position_deadline_destroy(&control->deadline_timer);
        control->deadline_initialized = 0;
    }
    if (control->output_owned) {
        r_runtime_array_destroy(&control->output);
        control->output_owned = 0;
    }
    control_release(control);
}

static _Bool beneath_root_closed(const RStdFsDirectory *root) {
    RLibraryFsHandleStorage *storage = r_library_internal_fs_directory_handle_storage(root);
    _Bool closed;

    if (storage == NULL || pthread_mutex_lock(&storage->mutex) != 0) {
        read_file_panic();
    }
    closed = storage->terminal || storage->close_reserved || storage->descriptor < 0;
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        read_file_panic();
    }
    return closed;
}

static RRuntimeDarwinFsPrepareResult prepare_beneath_open(RLibraryFsReadFileControl *control,
                                                          const RStdFsDirectory *root,
                                                          const RStdFsPath *relative,
                                                          _Bool *closed) {
    RRuntimeDarwinFsPrepareResult prepared = {0};
    RLibraryFsHandleStorage *storage = r_library_internal_fs_directory_handle_storage(root);
    size_t references;

    *closed = 0;
    if (storage == NULL || pthread_mutex_lock(&storage->mutex) != 0) {
        read_file_panic();
    }
    references = atomic_load_explicit(&storage->references, memory_order_relaxed);
    if (storage->terminal || storage->close_reserved || storage->descriptor < 0) {
        *closed = 1;
    } else if (references == SIZE_MAX) {
        (void)pthread_mutex_unlock(&storage->mutex);
        read_file_panic();
    } else {
        (void)atomic_fetch_add_explicit(&storage->references, 1U, memory_order_relaxed);
        control->root_storage = storage;
        prepared = r_runtime_darwin_fs_service_prepare_open_beneath(
            storage->descriptor,
            (const char *)r_library_internal_fs_path_bytes(relative),
            O_RDONLY,
            (mode_t)0,
            UINT64_C(0));
    }
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        read_file_panic();
    }
    return prepared;
}

static RStdFsTaskStartResult r_library_internal_fs_read_file_start(const RStdFsDirectory *root,
                                                                   const RStdFsPath *path,
                                                                   size_t limit,
                                                                   RStdFsDeadline deadline) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryFsReadFilePayload),
        _Alignof(RLibraryFsReadFilePayload),
        payload_move,
        payload_drop,
    };
    RLibraryFsReadFileControl *control;
    RLibraryFsReadFilePayload payload;
    RRuntimeDarwinFsPrepareResult open_prepared = {0};
    RRuntimeDarwinFsPrepareResult close_prepared;
    RRuntimeTaskPrepareResult task_prepared;
    RRuntimeTaskStartResult task_started;
    RRuntimeTaskStartStatus native_status;
    RStdFsError immediate_error = {0};
    RStdFsTaskStartResult result = {0};
    RRuntimeAllocator *allocator;
    _Bool beneath = root != NULL;
    _Bool closed = 0;

    allocator = r_library_internal_fs_path_allocator(path);
    control = control_create(allocator, limit, deadline);
    if (control == NULL) {
        return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    payload.control = control;
    if (beneath &&
        r_library_internal_fs_validate_beneath_relative(path) != R_LIBRARY_FS_RELATIVE_PATH_VALID) {
        set_immediate_error(control, fs_error(R_STD_FS_ERROR_INVALID_RELATIVE_PATH, INT64_C(0)));
    } else if (!beneath && (r_library_internal_fs_path_length(path) == 0U ||
                            r_library_internal_fs_path_has_reserved_component(path))) {
        set_immediate_error(control, fs_error(R_STD_FS_ERROR_INVALID_PATH, INT64_C(0)));
    } else if (r_library_internal_fs_path_length(path) >= R_RUNTIME_DARWIN_FS_PATH_SCAN_LIMIT) {
        set_immediate_error(control, fs_error(R_STD_FS_ERROR_NAME_TOO_LONG, INT64_C(0)));
    } else if (beneath && beneath_root_closed(root)) {
        set_immediate_error(control, fs_error(R_STD_FS_ERROR_CLOSED, INT64_C(0)));
    } else if (classify_deadline(deadline, &immediate_error) !=
               R_LIBRARY_FS_READ_FILE_DEADLINE_READY) {
        set_immediate_error(control, immediate_error);
    }
    if (!control->immediate && deadline.has_value) {
        if (!r_library_internal_fs_position_deadline_initialize(&control->deadline_timer,
                                                                allocator,
                                                                deadline.value,
                                                                deadline_expired,
                                                                control_retain,
                                                                control_release,
                                                                control)) {
            discard_uncommitted(control);
            return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
        }
        control->deadline_initialized = 1;
        control->deadline_cleanup_done = 0;
    }
    if (!control->immediate) {
        if (beneath) {
            open_prepared = prepare_beneath_open(control, root, path, &closed);
            if (closed) {
                set_immediate_error(control, fs_error(R_STD_FS_ERROR_CLOSED, INT64_C(0)));
            }
        } else {
            open_prepared = r_runtime_darwin_fs_service_prepare_open(
                (const char *)r_library_internal_fs_path_bytes(path),
                O_RDONLY,
                (mode_t)0,
                1,
                UINT64_C(0));
        }
    }
    if (!control->immediate) {
        native_status = native_prepare_status(open_prepared);
        if (native_status != R_RUNTIME_TASK_START_OK) {
            discard_uncommitted(control);
            return task_start_failure(native_status);
        }
        control->open_prepared = open_prepared.prepared;
        close_prepared = r_runtime_darwin_fs_service_prepare_close(UINT64_C(0));
        native_status = native_prepare_status(close_prepared);
        if (native_status != R_RUNTIME_TASK_START_OK) {
            discard_uncommitted(control);
            return task_start_failure(native_status);
        }
        control->close_prepared = close_prepared.prepared;
    }
    task_prepared = r_runtime_task_external_start_prepare(
        payload_type, result_type(), external_start, external_cancel);
    if (task_prepared.status != R_RUNTIME_TASK_START_OK) {
        discard_uncommitted(control);
        return task_start_failure(task_prepared.status);
    }
    task_started = r_runtime_task_start_commit(&task_prepared.transaction, &payload);
    if (task_started.status != R_RUNTIME_TASK_START_OK) {
        discard_uncommitted(control);
        return task_start_failure(task_started.status);
    }
    result.is_ok = 1;
    result.task = task_started.task;
    return result;
}

RStdFsTaskStartResult
r_library_internal_fs_read_file(const RStdFsPath *path, size_t limit, RStdFsDeadline deadline) {
    return r_library_internal_fs_read_file_start(NULL, path, limit, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_read_file_beneath(const RStdFsDirectory *root,
                                                              const RStdFsPath *relative,
                                                              size_t limit,
                                                              RStdFsDeadline deadline) {
    if (root == NULL) {
        read_file_panic();
    }
    return r_library_internal_fs_read_file_start(root, relative, limit, deadline);
}
