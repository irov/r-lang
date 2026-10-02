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

#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
#include <sched.h>
#endif

#define R_LIBRARY_FS_STAGE_SOURCE_CAPACITY                                                         \
    (R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY + sizeof(R_RUNTIME_DARWIN_FS_STAGING_PAYLOAD_NAME))

typedef enum RLibraryFsAtomicWriteDeadlineStatus {
    R_LIBRARY_FS_ATOMIC_WRITE_DEADLINE_READY = 0,
    R_LIBRARY_FS_ATOMIC_WRITE_DEADLINE_IMMEDIATE,
    R_LIBRARY_FS_ATOMIC_WRITE_DEADLINE_ERROR
} RLibraryFsAtomicWriteDeadlineStatus;

typedef enum RLibraryFsAtomicWritePhase {
    R_LIBRARY_FS_ATOMIC_WRITE_PHASE_PREPARED = 0,
    R_LIBRARY_FS_ATOMIC_WRITE_PHASE_PARENT_OPEN,
    R_LIBRARY_FS_ATOMIC_WRITE_PHASE_STAGE_PREPARING,
    R_LIBRARY_FS_ATOMIC_WRITE_PHASE_STAGE_CREATE,
    R_LIBRARY_FS_ATOMIC_WRITE_PHASE_PIPELINE_PREPARING,
    R_LIBRARY_FS_ATOMIC_WRITE_PHASE_STAGE_CLOSE,
    R_LIBRARY_FS_ATOMIC_WRITE_PHASE_IO_WRITE,
    R_LIBRARY_FS_ATOMIC_WRITE_PHASE_IO_ROOT_CLEANUP,
    R_LIBRARY_FS_ATOMIC_WRITE_PHASE_FILE_SYNC,
    R_LIBRARY_FS_ATOMIC_WRITE_PHASE_PUBLICATION,
    R_LIBRARY_FS_ATOMIC_WRITE_PHASE_STAGE_CLEANUP,
    R_LIBRARY_FS_ATOMIC_WRITE_PHASE_PARENT_SYNC,
    R_LIBRARY_FS_ATOMIC_WRITE_PHASE_PARENT_CLOSE,
    R_LIBRARY_FS_ATOMIC_WRITE_PHASE_FINAL
} RLibraryFsAtomicWritePhase;

typedef enum RLibraryFsAtomicWriteOutcome {
    R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_NONE = 0,
    R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_FAILED,
    R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_CANCELLED,
    R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_COMMITTED
} RLibraryFsAtomicWriteOutcome;

typedef struct RLibraryFsAtomicWriteControl RLibraryFsAtomicWriteControl;

typedef struct RLibraryFsAtomicWritePayload {
    RLibraryFsAtomicWriteControl *control;
    RRuntimeArray *staged_data;
    RRuntimeArray data;
    _Bool data_owned;
} RLibraryFsAtomicWritePayload;

struct RLibraryFsAtomicWriteControl {
    _Atomic size_t references;
    RRuntimeAllocator *allocator;
    pthread_mutex_t mutex;
    RLibraryFsPositionDeadline deadline_timer;
    RLibraryFsHandleStorage *root_storage;
    RLibraryFsOperationRegistration root_registration;
    RRuntimeTaskExternalExecution *execution;
    RLibraryFsAtomicWritePayload *payload;
    RStdFsWriteFileResult *result;
    RRuntimeDarwinFsPreparedRequest *parent_open_prepared;
    RRuntimeDarwinFsPreparedRequest *parent_close_prepared;
    RRuntimeDarwinFsPreparedRequest *stage_close_prepared;
    RRuntimeDarwinFsPreparedRequest *stage_prepared;
    RRuntimeDarwinFsPreparedRequest *stage_cleanup_prepared;
    RRuntimeDarwinFsPreparedRequest *file_sync_prepared;
    RRuntimeDarwinFsPreparedRequest *rename_prepared;
    RRuntimeDarwinFsPreparedRequest *parent_sync_prepared;
    RRuntimeDarwinFsRequest *fs_request;
    RRuntimeDarwinIoPreparedRequest *io_prepared;
    RRuntimeDarwinIoRequest *io_request;
    RStdFsDeadline deadline;
    RStdFsError error;
    char *destination;
    char *durability_parent;
    char staging_name[R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY];
    char staging_source[R_LIBRARY_FS_STAGE_SOURCE_CAPACITY];
    int parent_descriptor;
    int stage_descriptor;
    RLibraryFsAtomicWritePhase phase;
    RLibraryFsAtomicWriteOutcome outcome;
    uint64_t outcome_sequence;
    _Bool beneath;
    _Bool immediate;
    _Bool start_ready;
    _Bool deadline_initialized;
    _Bool stage_owned;
    _Bool root_tracking_releasing;
    _Bool io_root_cleanup_done;
    _Bool task_cancel_seen;
    _Bool cancel_reported;
    _Bool wait_cancel_report;
    _Bool selection_started;
    _Bool selection_finished;
    _Bool completion_selected;
    _Bool acknowledged;
};

static void begin_parent_close(RLibraryFsAtomicWriteControl *control);
static void begin_stage_cleanup(RLibraryFsAtomicWriteControl *control);
static void begin_stage_close(RLibraryFsAtomicWriteControl *control);
static void begin_file_sync(RLibraryFsAtomicWriteControl *control);
static void begin_io_write(RLibraryFsAtomicWriteControl *control);
static void deadline_expired(void *context);
static void finish_operation(RLibraryFsAtomicWriteControl *control);
static void result_move(void *destination, void *source);
static void result_drop(void *value);

#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
static _Atomic _Bool atomic_write_testing_pause_before_cancel_report;
static _Atomic _Bool atomic_write_testing_cancel_report_reached;
static _Atomic _Bool atomic_write_testing_pause_before_publication_activation;
static _Atomic _Bool atomic_write_testing_publication_activation_reached;
static _Atomic _Bool atomic_write_testing_publication_activation_observed;
static _Atomic(RLibraryFsAtomicWriteControl *) atomic_write_testing_publication_control;
static _Atomic _Bool atomic_write_testing_pause_before_publication_commit;
static _Atomic _Bool atomic_write_testing_publication_commit_reached;
static _Atomic _Bool atomic_write_testing_pause_after_failure_select;
static _Atomic _Bool atomic_write_testing_failure_select_reached;
static _Atomic uint64_t atomic_write_testing_failure_selection_count;
static _Atomic uint64_t atomic_write_testing_final_phase_count;
static _Atomic uint64_t atomic_write_testing_acknowledgement_count;

static void atomic_write_testing_wait(_Atomic _Bool *pause, _Atomic _Bool *reached) {
    atomic_store_explicit(reached, 1, memory_order_release);
    while (atomic_load_explicit(pause, memory_order_acquire)) {
        (void)sched_yield();
    }
    atomic_store_explicit(reached, 0, memory_order_release);
}

void r_library_internal_fs_atomic_write_testing_reset(void) {
    atomic_store_explicit(
        &atomic_write_testing_pause_before_cancel_report, 0, memory_order_relaxed);
    atomic_store_explicit(&atomic_write_testing_cancel_report_reached, 0, memory_order_relaxed);
    atomic_store_explicit(
        &atomic_write_testing_pause_before_publication_activation, 0, memory_order_relaxed);
    atomic_store_explicit(
        &atomic_write_testing_publication_activation_reached, 0, memory_order_relaxed);
    atomic_store_explicit(
        &atomic_write_testing_publication_activation_observed, 0, memory_order_relaxed);
    atomic_store_explicit(&atomic_write_testing_publication_control, NULL, memory_order_relaxed);
    atomic_store_explicit(
        &atomic_write_testing_pause_before_publication_commit, 0, memory_order_relaxed);
    atomic_store_explicit(
        &atomic_write_testing_publication_commit_reached, 0, memory_order_relaxed);
    atomic_store_explicit(
        &atomic_write_testing_pause_after_failure_select, 0, memory_order_relaxed);
    atomic_store_explicit(&atomic_write_testing_failure_select_reached, 0, memory_order_relaxed);
    atomic_store_explicit(
        &atomic_write_testing_failure_selection_count, UINT64_C(0), memory_order_relaxed);
    atomic_store_explicit(
        &atomic_write_testing_final_phase_count, UINT64_C(0), memory_order_relaxed);
    atomic_store_explicit(
        &atomic_write_testing_acknowledgement_count, UINT64_C(0), memory_order_relaxed);
}

void r_library_internal_fs_atomic_write_testing_pause_before_cancel_report(_Bool paused) {
    atomic_store_explicit(
        &atomic_write_testing_pause_before_cancel_report, paused, memory_order_release);
}

_Bool r_library_internal_fs_atomic_write_testing_cancel_report_reached(void) {
    return atomic_load_explicit(&atomic_write_testing_cancel_report_reached, memory_order_acquire);
}

void r_library_internal_fs_atomic_write_testing_pause_before_publication_activation(_Bool paused) {
    atomic_store_explicit(
        &atomic_write_testing_pause_before_publication_activation, paused, memory_order_release);
}

_Bool r_library_internal_fs_atomic_write_testing_publication_activation_reached(void) {
    return atomic_load_explicit(&atomic_write_testing_publication_activation_reached,
                                memory_order_acquire);
}

_Bool r_library_internal_fs_atomic_write_testing_publication_activation_observed(void) {
    return atomic_load_explicit(&atomic_write_testing_publication_activation_observed,
                                memory_order_acquire);
}

_Bool r_library_internal_fs_atomic_write_testing_trigger_publication_deadline(void) {
    RLibraryFsAtomicWriteControl *control = atomic_exchange_explicit(
        &atomic_write_testing_publication_control, NULL, memory_order_acq_rel);

    if (control == NULL) {
        return 0;
    }
    deadline_expired(control);
    return 1;
}

void r_library_internal_fs_atomic_write_testing_pause_before_publication_commit(_Bool paused) {
    atomic_store_explicit(
        &atomic_write_testing_pause_before_publication_commit, paused, memory_order_release);
}

_Bool r_library_internal_fs_atomic_write_testing_publication_commit_reached(void) {
    return atomic_load_explicit(&atomic_write_testing_publication_commit_reached,
                                memory_order_acquire);
}

void r_library_internal_fs_atomic_write_testing_pause_after_failure_select(_Bool paused) {
    atomic_store_explicit(
        &atomic_write_testing_pause_after_failure_select, paused, memory_order_release);
}

_Bool r_library_internal_fs_atomic_write_testing_failure_select_reached(void) {
    return atomic_load_explicit(&atomic_write_testing_failure_select_reached, memory_order_acquire);
}

uint64_t r_library_internal_fs_atomic_write_testing_failure_selection_count(void) {
    return atomic_load_explicit(&atomic_write_testing_failure_selection_count,
                                memory_order_acquire);
}

uint64_t r_library_internal_fs_atomic_write_testing_final_phase_count(void) {
    return atomic_load_explicit(&atomic_write_testing_final_phase_count, memory_order_acquire);
}

uint64_t r_library_internal_fs_atomic_write_testing_acknowledgement_count(void) {
    return atomic_load_explicit(&atomic_write_testing_acknowledgement_count, memory_order_acquire);
}
#endif

_Noreturn static void atomic_write_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                    (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
}

static void control_lock(RLibraryFsAtomicWriteControl *control) {
    if (pthread_mutex_lock(&control->mutex) != 0) {
        atomic_write_panic();
    }
}

static void control_unlock(RLibraryFsAtomicWriteControl *control) {
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        atomic_write_panic();
    }
}

static RStdFsError fs_error(RStdFsErrorCode code, int64_t native_code) {
    return (RStdFsError){code, native_code};
}

static void clear_array(RRuntimeArray *array) {
    (void)memset(array, 0, sizeof(*array));
}

static void move_array(RRuntimeArray *destination, RRuntimeArray *source) {
    *destination = *source;
    clear_array(source);
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

static RLibraryFsAtomicWriteDeadlineStatus classify_deadline(RStdFsDeadline deadline,
                                                             RStdFsError *error) {
    RStdTimeInstantResult now;

    if (!deadline.has_value) {
        return R_LIBRARY_FS_ATOMIC_WRITE_DEADLINE_READY;
    }
    if (deadline.value.storage_seconds < 0 ||
        deadline.value.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
        *error = fs_error(R_STD_FS_ERROR_INVALID_OPERATION, INT64_C(0));
        return R_LIBRARY_FS_ATOMIC_WRITE_DEADLINE_ERROR;
    }
    now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
    if (!now.is_ok) {
        *error = fs_error(R_STD_FS_ERROR_OTHER, now.error.native_code);
        return R_LIBRARY_FS_ATOMIC_WRITE_DEADLINE_ERROR;
    }
    if (compare_instant(deadline.value, now.value) <= 0) {
        *error = fs_error(R_STD_FS_ERROR_TIMED_OUT, INT64_C(0));
        return R_LIBRARY_FS_ATOMIC_WRITE_DEADLINE_IMMEDIATE;
    }
    return R_LIBRARY_FS_ATOMIC_WRITE_DEADLINE_READY;
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

static RStdFsError fs_result_error(_Bool beneath, RRuntimeDarwinFsResult result) {
    if (result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_TIMED_OUT) {
        return fs_error(R_STD_FS_ERROR_TIMED_OUT, (int64_t)result.native_error);
    }
    if (result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_CANCELLED ||
        result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_STOPPING) {
        return fs_error(R_STD_FS_ERROR_CANCELLED, (int64_t)result.native_error);
    }
    return native_error(beneath, result.native_error);
}

static RStdFsError io_result_error(RRuntimeDarwinIoResult result) {
    if (result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_TIMED_OUT) {
        return fs_error(R_STD_FS_ERROR_TIMED_OUT, (int64_t)result.native_error);
    }
    if (result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED) {
        return fs_error(R_STD_FS_ERROR_CANCELLED, (int64_t)result.native_error);
    }
    return native_error(0, result.native_error);
}

static void control_retain(void *context) {
    RLibraryFsAtomicWriteControl *control = context;
    size_t references = atomic_load_explicit(&control->references, memory_order_relaxed);

    for (;;) {
        if (references == 0U || references == SIZE_MAX) {
            atomic_write_panic();
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
    RLibraryFsAtomicWriteControl *control = context;
    size_t previous;

    if (control == NULL) {
        return;
    }
    previous = atomic_fetch_sub_explicit(&control->references, 1U, memory_order_acq_rel);
    if (previous == 0U) {
        atomic_write_panic();
    }
    if (previous != 1U) {
        return;
    }
    if (control->root_storage != NULL || control->root_registration.registered ||
        control->parent_open_prepared != NULL || control->parent_close_prepared != NULL ||
        control->stage_close_prepared != NULL || control->stage_prepared != NULL ||
        control->stage_cleanup_prepared != NULL || control->file_sync_prepared != NULL ||
        control->rename_prepared != NULL || control->parent_sync_prepared != NULL ||
        control->fs_request != NULL || control->io_prepared != NULL ||
        control->io_request != NULL || control->deadline_timer.token != NULL ||
        control->parent_descriptor >= 0 || control->stage_descriptor >= 0 || control->stage_owned ||
        control->phase != R_LIBRARY_FS_ATOMIC_WRITE_PHASE_FINAL) {
        atomic_write_panic();
    }
    r_runtime_allocator_deallocate(control->destination, _Alignof(char));
    r_runtime_allocator_deallocate(control->durability_parent, _Alignof(char));
    if (pthread_mutex_destroy(&control->mutex) != 0) {
        atomic_write_panic();
    }
    r_runtime_allocator_deallocate(control, _Alignof(RLibraryFsAtomicWriteControl));
}

static RLibraryFsAtomicWriteControl *
control_create(RRuntimeAllocator *allocator, _Bool beneath, RStdFsDeadline deadline) {
    RLibraryFsAtomicWriteControl *control = NULL;

    if (allocator == NULL ||
        r_runtime_allocator_allocate(allocator,
                                     sizeof(*control),
                                     _Alignof(RLibraryFsAtomicWriteControl),
                                     (void **)&control) != R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    (void)memset(control, 0, sizeof(*control));
    if (pthread_mutex_init(&control->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(control, _Alignof(RLibraryFsAtomicWriteControl));
        return NULL;
    }
    atomic_init(&control->references, 1U);
    control->allocator = allocator;
    control->beneath = beneath;
    control->deadline = deadline;
    control->parent_descriptor = -1;
    control->stage_descriptor = -1;
    control->phase = R_LIBRARY_FS_ATOMIC_WRITE_PHASE_PREPARED;
    control->io_root_cleanup_done = 1;
    return control;
}

static void payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryFsAtomicWritePayload *destination = destination_pointer;
    RLibraryFsAtomicWritePayload *source = source_pointer;

    (void)memset(destination, 0, sizeof(*destination));
    destination->control = source->control;
    source->control = NULL;
    if (source->staged_data == NULL) {
        atomic_write_panic();
    }
    move_array(&destination->data, source->staged_data);
    destination->data_owned = 1;
    source->staged_data = NULL;
}

static void payload_drop(void *value) {
    RLibraryFsAtomicWritePayload *payload = value;

    if (payload->data_owned) {
        r_runtime_array_destroy(&payload->data);
        payload->data_owned = 0;
    }
    if (payload->control != NULL) {
        control_release(payload->control);
        payload->control = NULL;
    }
}

void r_library_internal_fs_write_file_result_move(RStdFsWriteFileResult *destination,
                                                  RStdFsWriteFileResult *source) {
    *destination = *source;
    clear_array(&source->data);
}

void r_library_internal_fs_write_file_result_drop(RStdFsWriteFileResult *result) {
    r_runtime_array_destroy(&result->data);
}

static RRuntimeTypeInfo result_type(void) {
    return (RRuntimeTypeInfo){
        sizeof(RStdFsWriteFileResult), _Alignof(RStdFsWriteFileResult), result_move, result_drop};
}

static _Bool begin_failure_locked(RLibraryFsAtomicWriteControl *control,
                                  RStdFsError error,
                                  uint64_t event_sequence) {
    if (event_sequence == UINT64_C(0)) {
        atomic_write_panic();
    }
    if (control->outcome == R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_COMMITTED ||
        (control->outcome != R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_NONE &&
         control->outcome_sequence <= event_sequence)) {
        return 0;
    }
    control->outcome = R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_FAILED;
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

static void finish_failure_selection(RLibraryFsAtomicWriteControl *control) {
    uint64_t event_sequence;
    _Bool selected;
    _Bool force_commit;

    for (;;) {
        force_commit = 0;
        control_lock(control);
        if (!control->selection_started || control->selection_finished ||
            control->completion_selected) {
            control_unlock(control);
            atomic_write_panic();
        }
        if (control->outcome == R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_COMMITTED) {
            force_commit = 1;
            event_sequence = UINT64_C(0);
        } else if (control->outcome == R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_FAILED) {
            event_sequence = control->outcome_sequence;
        } else {
            control->selection_finished = 1;
            control_unlock(control);
            finish_operation(control);
            return;
        }
        control_unlock(control);
        selected = force_commit
                       ? r_runtime_task_external_select_terminal_completion(control->execution)
                       : r_runtime_task_external_try_select_completion_at(control->execution,
                                                                          event_sequence);
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
        atomic_write_testing_wait(&atomic_write_testing_pause_after_failure_select,
                                  &atomic_write_testing_failure_select_reached);
#endif
        control_lock(control);
        if (!control->selection_started || control->selection_finished ||
            control->completion_selected) {
            control_unlock(control);
            atomic_write_panic();
        }
        if (control->outcome == R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_COMMITTED && !selected) {
            force_commit = 1;
        } else if (selected) {
            control->completion_selected = 1;
            control->selection_finished = 1;
        } else if (control->outcome == R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_FAILED &&
                   control->outcome_sequence < event_sequence) {
            control_unlock(control);
            continue;
        } else if (control->outcome == R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_FAILED) {
            control->outcome = R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_CANCELLED;
            control->outcome_sequence =
                r_runtime_task_external_cancellation_sequence(control->execution);
            if (control->outcome_sequence == UINT64_C(0)) {
                control_unlock(control);
                atomic_write_panic();
            }
            control->wait_cancel_report = 1;
            control->selection_finished = 1;
        } else if (control->outcome == R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_CANCELLED) {
            control->selection_finished = 1;
        } else {
            control_unlock(control);
            atomic_write_panic();
        }
        control_unlock(control);
        if (force_commit && !selected) {
            selected = r_runtime_task_external_select_terminal_completion(control->execution);
            if (!selected) {
                atomic_write_panic();
            }
            control_lock(control);
            if (control->outcome != R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_COMMITTED ||
                control->completion_selected) {
                control_unlock(control);
                atomic_write_panic();
            }
            control->completion_selected = 1;
            control->selection_finished = 1;
            if (!control->cancel_reported) {
                control->wait_cancel_report = 1;
            }
            control_unlock(control);
        }
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
        (void)atomic_fetch_add_explicit(
            &atomic_write_testing_failure_selection_count, UINT64_C(1), memory_order_release);
#endif
        finish_operation(control);
        return;
    }
}

static void establish_failure_at(RLibraryFsAtomicWriteControl *control,
                                 RStdFsError error,
                                 uint64_t event_sequence) {
    _Bool select;

    control_lock(control);
    select = begin_failure_locked(control, error, event_sequence);
    control_unlock(control);
    if (select) {
        finish_failure_selection(control);
    }
}

static void establish_failure(RLibraryFsAtomicWriteControl *control, RStdFsError error) {
    establish_failure_at(control, error, r_runtime_darwin_event_sequence_next());
}

static void establish_fs_result_failure(RLibraryFsAtomicWriteControl *control,
                                        _Bool beneath,
                                        RRuntimeDarwinFsResult result) {
    RStdFsError error;
    uint64_t event_sequence;

    if (result.operation_native_error != 0 && result.native_event_sequence != UINT64_C(0)) {
        error = native_error(beneath, result.operation_native_error);
        event_sequence = result.native_event_sequence;
    } else {
        error = fs_result_error(beneath, result);
        event_sequence = result.terminal_event_sequence;
    }
    if (event_sequence == UINT64_C(0)) {
        event_sequence = r_runtime_darwin_event_sequence_next();
    }
    establish_failure_at(control, error, event_sequence);
}

static void establish_io_result_failure(RLibraryFsAtomicWriteControl *control,
                                        RRuntimeDarwinIoResult result) {
    RStdFsError error;
    uint64_t event_sequence;

    if (result.operation_native_error != 0 && result.native_event_sequence != UINT64_C(0)) {
        error = native_error(0, result.operation_native_error);
        event_sequence = result.native_event_sequence;
    } else {
        error = io_result_error(result);
        event_sequence = result.terminal_event_sequence;
    }
    if (event_sequence == UINT64_C(0)) {
        event_sequence = r_runtime_darwin_event_sequence_next();
    }
    establish_failure_at(control, error, event_sequence);
}

static void establish_commit(RLibraryFsAtomicWriteControl *control, uint64_t event_sequence) {
    uint64_t completion_sequence;
    _Bool needs_selection;
    _Bool selected_initially = 1;
    _Bool selected = 1;

    control_lock(control);
    if (control->outcome == R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_COMMITTED) {
        control_unlock(control);
        return;
    }
    control->outcome = R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_COMMITTED;
    control->outcome_sequence =
        event_sequence == UINT64_C(0) ? r_runtime_darwin_event_sequence_next() : event_sequence;
    completion_sequence = control->outcome_sequence;
    needs_selection = !control->completion_selected &&
                      (!control->selection_started || control->selection_finished);
    if (needs_selection) {
        control->selection_started = 1;
        control->selection_finished = 0;
    }
    control_unlock(control);

    if (needs_selection) {
        selected = r_runtime_task_external_try_select_completion_at(control->execution,
                                                                    completion_sequence);
        selected_initially = selected;
        if (!selected) {
            selected = r_runtime_task_external_select_terminal_completion(control->execution);
        }
        if (!selected) {
            atomic_write_panic();
        }
    } else {
        return;
    }
    control_lock(control);
    control->completion_selected = 1;
    control->selection_finished = 1;
    if (needs_selection && !selected_initially && !control->cancel_reported) {
        control->wait_cancel_report = 1;
    }
    control_unlock(control);
}

static void establish_cancellation_locked(RLibraryFsAtomicWriteControl *control,
                                          uint64_t event_sequence) {
    if (event_sequence == UINT64_C(0)) {
        atomic_write_panic();
    }
    if (control->outcome != R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_COMMITTED &&
        (control->outcome == R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_NONE ||
         event_sequence < control->outcome_sequence)) {
        control->outcome = R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_CANCELLED;
        control->outcome_sequence = event_sequence;
        control->wait_cancel_report = 1;
    }
}

static void establish_observed_cancellation_locked(RLibraryFsAtomicWriteControl *control) {
    uint64_t event_sequence;

    if (control->execution == NULL ||
        !r_runtime_task_external_cancel_requested(control->execution)) {
        return;
    }
    event_sequence = r_runtime_task_external_cancellation_sequence(control->execution);
    if (event_sequence == UINT64_C(0)) {
        atomic_write_panic();
    }
    establish_cancellation_locked(control, event_sequence);
}

static void result_move(void *destination, void *source) {
    r_library_internal_fs_write_file_result_move(destination, source);
}

static void result_drop(void *value) {
    r_library_internal_fs_write_file_result_drop(value);
}

static void release_root_tracking(RLibraryFsAtomicWriteControl *control) {
    RLibraryFsHandleStorage *storage;
    _Bool registered;

    control_lock(control);
    if (control->root_storage == NULL || control->root_tracking_releasing) {
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
        atomic_write_panic();
    }
    control->root_storage = NULL;
    control->root_tracking_releasing = 0;
    control_unlock(control);
}

static void abort_fs_prepared(RRuntimeDarwinFsPreparedRequest **prepared) {
    if (*prepared != NULL) {
        r_runtime_darwin_fs_prepared_abort(prepared);
    }
}

static void abort_unused_preparations(RLibraryFsAtomicWriteControl *control) {
    RRuntimeDarwinFsPreparedRequest *parent_open;
    RRuntimeDarwinFsPreparedRequest *stage;
    RRuntimeDarwinFsPreparedRequest *file_sync;
    RRuntimeDarwinFsPreparedRequest *rename;
    RRuntimeDarwinFsPreparedRequest *parent_sync;
    RRuntimeDarwinIoPreparedRequest *io;

    control_lock(control);
    parent_open = control->parent_open_prepared;
    control->parent_open_prepared = NULL;
    stage = control->stage_prepared;
    control->stage_prepared = NULL;
    file_sync = control->file_sync_prepared;
    control->file_sync_prepared = NULL;
    rename = control->rename_prepared;
    control->rename_prepared = NULL;
    parent_sync = control->parent_sync_prepared;
    control->parent_sync_prepared = NULL;
    io = control->io_prepared;
    control->io_prepared = NULL;
    control_unlock(control);

    abort_fs_prepared(&parent_open);
    abort_fs_prepared(&stage);
    abort_fs_prepared(&file_sync);
    abort_fs_prepared(&rename);
    abort_fs_prepared(&parent_sync);
    r_runtime_darwin_io_prepared_abort(&io);
}

static void abort_prepublication_preparations(RLibraryFsAtomicWriteControl *control) {
    RRuntimeDarwinFsPreparedRequest *parent_open;
    RRuntimeDarwinFsPreparedRequest *stage;
    RRuntimeDarwinFsPreparedRequest *file_sync;
    RRuntimeDarwinFsPreparedRequest *rename;
    RRuntimeDarwinIoPreparedRequest *io;

    control_lock(control);
    parent_open = control->parent_open_prepared;
    control->parent_open_prepared = NULL;
    stage = control->stage_prepared;
    control->stage_prepared = NULL;
    file_sync = control->file_sync_prepared;
    control->file_sync_prepared = NULL;
    rename = control->rename_prepared;
    control->rename_prepared = NULL;
    io = control->io_prepared;
    control->io_prepared = NULL;
    control_unlock(control);

    abort_fs_prepared(&parent_open);
    abort_fs_prepared(&stage);
    abort_fs_prepared(&file_sync);
    abort_fs_prepared(&rename);
    r_runtime_darwin_io_prepared_abort(&io);
}

static void destroy_deadline(RLibraryFsAtomicWriteControl *control) {
    _Bool initialized;

    control_lock(control);
    initialized = control->deadline_initialized;
    control->deadline_initialized = 0;
    control_unlock(control);
    if (initialized) {
        r_library_internal_fs_position_deadline_destroy(&control->deadline_timer);
    }
}

static void fill_result(RLibraryFsAtomicWriteControl *control) {
    RLibraryFsAtomicWritePayload *payload = control->payload;

    if (payload == NULL || !payload->data_owned || control->result == NULL) {
        atomic_write_panic();
    }
    (void)memset(control->result, 0, sizeof(*control->result));
    if (control->outcome == R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_COMMITTED) {
        control->result->kind = R_STD_FS_WRITE_FILE_COMMITTED;
    } else if (control->outcome == R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_FAILED) {
        control->result->kind = R_STD_FS_WRITE_FILE_FAILED;
        control->result->error = control->error;
    } else {
        atomic_write_panic();
    }
    move_array(&control->result->data, &payload->data);
    payload->data_owned = 0;
}

static void finish_operation(RLibraryFsAtomicWriteControl *control) {
    RRuntimeTaskExternalExecution *execution = NULL;
    _Bool fill = 0;

    control_retain(control);
    control_lock(control);
    if (!control->acknowledged && control->start_ready &&
        control->phase == R_LIBRARY_FS_ATOMIC_WRITE_PHASE_FINAL && control->fs_request == NULL &&
        control->io_request == NULL && control->parent_descriptor < 0 &&
        control->stage_descriptor < 0 && !control->stage_owned && control->root_storage == NULL &&
        !control->root_tracking_releasing &&
        (control->outcome == R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_CANCELLED ||
         control->completion_selected) &&
        (!control->wait_cancel_report || control->cancel_reported)) {
        control->acknowledged = 1;
        execution = control->execution;
        fill = control->outcome == R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_COMMITTED ||
               control->outcome == R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_FAILED;
        if (fill) {
            fill_result(control);
        }
        control->execution = NULL;
        control->payload = NULL;
        control->result = NULL;
    }
    control_unlock(control);
    if (execution != NULL) {
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
        (void)atomic_fetch_add_explicit(
            &atomic_write_testing_acknowledgement_count, UINT64_C(1), memory_order_release);
#endif
        r_runtime_task_external_acknowledge(execution);
    }
    control_release(control);
}

static void enter_final_phase(RLibraryFsAtomicWriteControl *control) {
    RRuntimeDarwinFsPreparedRequest *parent_close;
    RRuntimeDarwinFsPreparedRequest *stage_close;
    RRuntimeDarwinFsPreparedRequest *stage_cleanup;

    abort_unused_preparations(control);
    release_root_tracking(control);
    control_lock(control);
    parent_close = control->parent_close_prepared;
    control->parent_close_prepared = NULL;
    stage_close = control->stage_close_prepared;
    control->stage_close_prepared = NULL;
    stage_cleanup = control->stage_cleanup_prepared;
    control->stage_cleanup_prepared = NULL;
    if (control->parent_descriptor >= 0 || control->stage_descriptor >= 0 || control->stage_owned ||
        control->fs_request != NULL || control->io_request != NULL) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->phase = R_LIBRARY_FS_ATOMIC_WRITE_PHASE_FINAL;
    control_unlock(control);
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    (void)atomic_fetch_add_explicit(
        &atomic_write_testing_final_phase_count, UINT64_C(1), memory_order_release);
#endif
    abort_fs_prepared(&parent_close);
    abort_fs_prepared(&stage_close);
    abort_fs_prepared(&stage_cleanup);
    destroy_deadline(control);
    finish_operation(control);
}

static void parent_close_completed(RRuntimeDarwinFsRequest *request, void *context) {
    RLibraryFsAtomicWriteControl *control = context;
    RRuntimeDarwinFsResult result = r_runtime_darwin_fs_request_wait(request);
    int unclosed = r_runtime_darwin_fs_request_take_unclosed_fd(request);

    if (unclosed >= 0) {
        atomic_write_panic();
    }
    control_lock(control);
    if (control->fs_request != request ||
        control->phase != R_LIBRARY_FS_ATOMIC_WRITE_PHASE_PARENT_CLOSE) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->fs_request = NULL;
    control_unlock(control);
    r_runtime_darwin_fs_request_release(request);
    if (result.terminal_event != R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE || result.native_error != 0) {
        establish_fs_result_failure(control, 0, result);
    }
    enter_final_phase(control);
    control_release(control);
}

static void begin_parent_close(RLibraryFsAtomicWriteControl *control) {
    RRuntimeDarwinFsPreparedRequest *prepared;
    RRuntimeDarwinFsSubmitResult submission;
    int descriptor;

    control_lock(control);
    if (control->fs_request != NULL || control->io_request != NULL || control->stage_owned ||
        control->stage_descriptor >= 0) {
        control_unlock(control);
        atomic_write_panic();
    }
    prepared = control->parent_close_prepared;
    control->parent_close_prepared = NULL;
    descriptor = control->parent_descriptor;
    if (descriptor < 0) {
        control_unlock(control);
        abort_fs_prepared(&prepared);
        enter_final_phase(control);
        return;
    }
    submission = r_runtime_darwin_fs_prepared_activate_close(&prepared, &descriptor);
    if (submission.status != R_RUNTIME_DARWIN_FS_START_OK || submission.request == NULL ||
        prepared != NULL || descriptor >= 0) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->parent_descriptor = -1;
    control->fs_request = submission.request;
    control->phase = R_LIBRARY_FS_ATOMIC_WRITE_PHASE_PARENT_CLOSE;
    control_retain(control);
    control_unlock(control);
    if (!r_runtime_darwin_fs_request_set_completion(
            submission.request, parent_close_completed, control)) {
        atomic_write_panic();
    }
}

static void parent_sync_completed(RRuntimeDarwinFsRequest *request, void *context) {
    RLibraryFsAtomicWriteControl *control = context;
    RRuntimeDarwinFsResult result = r_runtime_darwin_fs_request_wait(request);

    control_lock(control);
    if (control->fs_request != request ||
        control->phase != R_LIBRARY_FS_ATOMIC_WRITE_PHASE_PARENT_SYNC) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->fs_request = NULL;
    control_unlock(control);
    r_runtime_darwin_fs_request_release(request);
    if (result.terminal_event != R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE || result.native_error != 0) {
        establish_fs_result_failure(control, control->beneath, result);
    }
    begin_parent_close(control);
    control_release(control);
}

static void begin_parent_sync(RLibraryFsAtomicWriteControl *control) {
    RRuntimeDarwinFsPreparedRequest *prepared;
    RRuntimeDarwinFsSubmitResult submission;

    control_lock(control);
    prepared = control->parent_sync_prepared;
    control->parent_sync_prepared = NULL;
    if (prepared == NULL) {
        control_unlock(control);
        begin_parent_close(control);
        return;
    }
    submission = r_runtime_darwin_fs_prepared_activate(&prepared);
    if (submission.status != R_RUNTIME_DARWIN_FS_START_OK || submission.request == NULL ||
        prepared != NULL) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->fs_request = submission.request;
    control->phase = R_LIBRARY_FS_ATOMIC_WRITE_PHASE_PARENT_SYNC;
    control_retain(control);
    control_unlock(control);
    if (!r_runtime_darwin_fs_request_set_completion(
            submission.request, parent_sync_completed, control)) {
        atomic_write_panic();
    }
}

static void stage_cleanup_completed(RRuntimeDarwinFsRequest *request, void *context) {
    RLibraryFsAtomicWriteControl *control = context;
    RRuntimeDarwinFsResult result = r_runtime_darwin_fs_request_wait(request);

    if (result.terminal_event != R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE || result.native_error != 0 ||
        !result.committed) {
        atomic_write_panic();
    }
    control_lock(control);
    if (control->fs_request != request ||
        control->phase != R_LIBRARY_FS_ATOMIC_WRITE_PHASE_STAGE_CLEANUP) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->fs_request = NULL;
    control_unlock(control);
    r_runtime_darwin_fs_request_release(request);
    begin_parent_sync(control);
    control_release(control);
}

static void begin_stage_cleanup(RLibraryFsAtomicWriteControl *control) {
    RRuntimeDarwinFsPreparedRequest *prepared;
    RRuntimeDarwinFsSubmitResult submission;

    abort_prepublication_preparations(control);
    control_lock(control);
    prepared = control->stage_cleanup_prepared;
    control->stage_cleanup_prepared = NULL;
    if (!control->stage_owned) {
        control_unlock(control);
        abort_fs_prepared(&prepared);
        begin_parent_sync(control);
        return;
    }
    if (prepared == NULL || control->fs_request != NULL || control->io_request != NULL ||
        control->stage_descriptor >= 0) {
        control_unlock(control);
        atomic_write_panic();
    }
    submission = r_runtime_darwin_fs_prepared_activate(&prepared);
    if (submission.status != R_RUNTIME_DARWIN_FS_START_OK || submission.request == NULL ||
        prepared != NULL) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->stage_owned = 0;
    control->fs_request = submission.request;
    control->phase = R_LIBRARY_FS_ATOMIC_WRITE_PHASE_STAGE_CLEANUP;
    control_retain(control);
    control_unlock(control);
    if (!r_runtime_darwin_fs_request_set_completion(
            submission.request, stage_cleanup_completed, control)) {
        atomic_write_panic();
    }
}

static void publication_completed(RRuntimeDarwinFsRequest *request, void *context) {
    RLibraryFsAtomicWriteControl *control = context;
    RRuntimeDarwinFsResult result = r_runtime_darwin_fs_request_wait(request);
    _Bool success = result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE &&
                    result.native_error == 0 && result.committed;

    control_lock(control);
    if (control->fs_request != request ||
        control->phase != R_LIBRARY_FS_ATOMIC_WRITE_PHASE_PUBLICATION) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->fs_request = NULL;
    control_unlock(control);
    r_runtime_darwin_fs_request_release(request);
    if (success) {
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
        atomic_write_testing_wait(&atomic_write_testing_pause_before_publication_commit,
                                  &atomic_write_testing_publication_commit_reached);
#endif
        establish_commit(control, result.native_event_sequence);
    } else {
        establish_fs_result_failure(control, control->beneath, result);
    }
    begin_stage_cleanup(control);
    control_release(control);
}

static void begin_publication(RLibraryFsAtomicWriteControl *control) {
    RRuntimeDarwinFsPreparedRequest *prepared;
    RRuntimeDarwinFsSubmitResult submission;
    _Bool cancelled;

#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    atomic_write_testing_wait(&atomic_write_testing_pause_before_publication_activation,
                              &atomic_write_testing_publication_activation_reached);
#endif
    control_lock(control);
    cancelled = control->outcome != R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_NONE ||
                (control->execution != NULL &&
                 r_runtime_task_external_cancel_requested(control->execution));
    if (cancelled) {
        establish_observed_cancellation_locked(control);
        control_unlock(control);
        begin_stage_cleanup(control);
        return;
    }
    prepared = control->rename_prepared;
    control->rename_prepared = NULL;
    if (prepared == NULL || control->fs_request != NULL) {
        control_unlock(control);
        atomic_write_panic();
    }
    submission = r_runtime_darwin_fs_prepared_activate(&prepared);
    if (submission.status != R_RUNTIME_DARWIN_FS_START_OK || submission.request == NULL ||
        prepared != NULL) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->fs_request = submission.request;
    control->phase = R_LIBRARY_FS_ATOMIC_WRITE_PHASE_PUBLICATION;
    control_retain(control);
    control_unlock(control);
    if (!r_runtime_darwin_fs_request_set_completion(
            submission.request, publication_completed, control)) {
        atomic_write_panic();
    }
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    atomic_store_explicit(&atomic_write_testing_publication_control, control, memory_order_release);
    atomic_store_explicit(
        &atomic_write_testing_publication_activation_observed, 1, memory_order_release);
#endif
}

static void file_sync_completed(RRuntimeDarwinFsRequest *request, void *context) {
    RLibraryFsAtomicWriteControl *control = context;
    RRuntimeDarwinFsResult result = r_runtime_darwin_fs_request_wait(request);
    _Bool success = result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE &&
                    result.native_error == 0 && result.committed;

    control_lock(control);
    if (control->fs_request != request ||
        control->phase != R_LIBRARY_FS_ATOMIC_WRITE_PHASE_FILE_SYNC) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->fs_request = NULL;
    control_unlock(control);
    r_runtime_darwin_fs_request_release(request);
    if (!success) {
        establish_fs_result_failure(control, 0, result);
    }
    begin_stage_close(control);
    control_release(control);
}

static void begin_file_sync(RLibraryFsAtomicWriteControl *control) {
    RRuntimeDarwinFsPreparedRequest *prepared;
    RRuntimeDarwinFsSubmitResult submission;
    _Bool cancelled;

    control_lock(control);
    cancelled = control->outcome != R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_NONE ||
                (control->execution != NULL &&
                 r_runtime_task_external_cancel_requested(control->execution));
    if (cancelled) {
        establish_observed_cancellation_locked(control);
        control_unlock(control);
        begin_stage_close(control);
        return;
    }
    prepared = control->file_sync_prepared;
    control->file_sync_prepared = NULL;
    if (prepared == NULL || control->fs_request != NULL) {
        control_unlock(control);
        atomic_write_panic();
    }
    submission = r_runtime_darwin_fs_prepared_activate(&prepared);
    if (submission.status != R_RUNTIME_DARWIN_FS_START_OK || submission.request == NULL ||
        prepared != NULL) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->fs_request = submission.request;
    control->phase = R_LIBRARY_FS_ATOMIC_WRITE_PHASE_FILE_SYNC;
    control_retain(control);
    control_unlock(control);
    if (!r_runtime_darwin_fs_request_set_completion(
            submission.request, file_sync_completed, control)) {
        atomic_write_panic();
    }
}

static void io_root_cleanup_completed(void *context, int native_error_code) {
    RLibraryFsAtomicWriteControl *control = context;
    _Bool outcome_none;

    control_lock(control);
    if (control->phase != R_LIBRARY_FS_ATOMIC_WRITE_PHASE_IO_ROOT_CLEANUP ||
        control->io_request != NULL || control->io_root_cleanup_done) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->io_root_cleanup_done = 1;
    control_unlock(control);
    if (native_error_code != 0) {
        establish_failure(control, native_error(0, native_error_code));
    }
    control_lock(control);
    outcome_none = control->outcome == R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_NONE;
    control_unlock(control);
    if (outcome_none) {
        begin_file_sync(control);
    } else {
        begin_stage_close(control);
    }
    control_release(control);
}

static void io_write_completed(RRuntimeDarwinIoRequest *request, void *context) {
    RLibraryFsAtomicWriteControl *control = context;
    RRuntimeDarwinIoResult result = r_runtime_darwin_io_request_wait(request);
    size_t expected;
    _Bool success;

    control_lock(control);
    if (control->io_request != request ||
        control->phase != R_LIBRARY_FS_ATOMIC_WRITE_PHASE_IO_WRITE || control->payload == NULL) {
        control_unlock(control);
        atomic_write_panic();
    }
    expected = control->payload->data.length;
    control->io_request = NULL;
    control->phase = R_LIBRARY_FS_ATOMIC_WRITE_PHASE_IO_ROOT_CLEANUP;
    control_unlock(control);
    success = result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE &&
              result.native_error == 0 && result.bytes_transferred == expected &&
              result.bytes_remaining == 0U;
    r_runtime_darwin_io_request_release(request);
    if (!success) {
        establish_io_result_failure(control, result);
    }
    control_release(control);
}

static void begin_io_write(RLibraryFsAtomicWriteControl *control) {
    RRuntimeDarwinIoPreparedRequest *prepared;
    RRuntimeDarwinIoSubmitResult submission;
    _Bool cancelled;

    control_lock(control);
    cancelled = control->outcome != R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_NONE ||
                (control->execution != NULL &&
                 r_runtime_task_external_cancel_requested(control->execution));
    if (cancelled) {
        establish_observed_cancellation_locked(control);
        prepared = control->io_prepared;
        control->io_prepared = NULL;
        control_unlock(control);
        r_runtime_darwin_io_prepared_abort(&prepared);
        begin_stage_close(control);
        return;
    }
    if (control->io_prepared == NULL) {
        control_unlock(control);
        begin_file_sync(control);
        return;
    }
    control->io_root_cleanup_done = 0;
    control_retain(control);
    submission = r_runtime_darwin_io_prepared_bind_activate_borrowed_random_shared_write(
        &control->io_prepared, control->stage_descriptor, io_root_cleanup_completed, control);
    if (submission.status != R_RUNTIME_DARWIN_IO_START_OK || submission.request == NULL ||
        control->io_prepared != NULL) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->io_request = submission.request;
    control->phase = R_LIBRARY_FS_ATOMIC_WRITE_PHASE_IO_WRITE;
    control_retain(control);
    control_unlock(control);
    if (!r_runtime_darwin_io_request_set_completion(
            submission.request, io_write_completed, control)) {
        atomic_write_panic();
    }
}

static void stage_close_completed(RRuntimeDarwinFsRequest *request, void *context) {
    RLibraryFsAtomicWriteControl *control = context;
    RRuntimeDarwinFsResult result = r_runtime_darwin_fs_request_wait(request);
    int unclosed = r_runtime_darwin_fs_request_take_unclosed_fd(request);
    _Bool outcome_none;

    if (unclosed >= 0) {
        atomic_write_panic();
    }
    control_lock(control);
    if (control->fs_request != request ||
        control->phase != R_LIBRARY_FS_ATOMIC_WRITE_PHASE_STAGE_CLOSE) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->fs_request = NULL;
    control_unlock(control);
    r_runtime_darwin_fs_request_release(request);
    if (result.terminal_event != R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE || result.native_error != 0) {
        establish_fs_result_failure(control, 0, result);
    }
    control_lock(control);
    outcome_none = control->outcome == R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_NONE;
    control_unlock(control);
    if (outcome_none) {
        begin_publication(control);
    } else {
        begin_stage_cleanup(control);
    }
    control_release(control);
}

static void begin_stage_close(RLibraryFsAtomicWriteControl *control) {
    RRuntimeDarwinFsPreparedRequest *prepared;
    RRuntimeDarwinFsSubmitResult submission;
    int descriptor;

    control_lock(control);
    prepared = control->stage_close_prepared;
    control->stage_close_prepared = NULL;
    descriptor = control->stage_descriptor;
    if (prepared == NULL || descriptor < 0 || control->fs_request != NULL) {
        control_unlock(control);
        atomic_write_panic();
    }
    submission = r_runtime_darwin_fs_prepared_activate_close(&prepared, &descriptor);
    if (submission.status != R_RUNTIME_DARWIN_FS_START_OK || submission.request == NULL ||
        prepared != NULL || descriptor >= 0) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->stage_descriptor = -1;
    control->fs_request = submission.request;
    control->phase = R_LIBRARY_FS_ATOMIC_WRITE_PHASE_STAGE_CLOSE;
    control_retain(control);
    control_unlock(control);
    if (!r_runtime_darwin_fs_request_set_completion(
            submission.request, stage_close_completed, control)) {
        atomic_write_panic();
    }
}

static void build_staging_source(RLibraryFsAtomicWriteControl *control) {
    const size_t name_length = R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY - 1U;
    const size_t payload_size = sizeof(R_RUNTIME_DARWIN_FS_STAGING_PAYLOAD_NAME);

    memcpy(control->staging_source, control->staging_name, name_length);
    control->staging_source[name_length] = '/';
    memcpy(control->staging_source + name_length + 1U,
           R_RUNTIME_DARWIN_FS_STAGING_PAYLOAD_NAME,
           payload_size);
}

static void stage_completed(RRuntimeDarwinFsRequest *request, void *context) {
    RLibraryFsAtomicWriteControl *control = context;
    RRuntimeDarwinFsResult result = r_runtime_darwin_fs_request_wait(request);
    RRuntimeDarwinFsPreparedRequest *cleanup = NULL;
    char staging_name[R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY];
    int stage_descriptor = -1;
    _Bool stage_taken = 0;

    if (result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE && result.native_error == 0) {
        stage_taken = r_runtime_darwin_fs_request_take_file_stage(
            request, &stage_descriptor, staging_name, &cleanup);
        if (!stage_taken) {
            atomic_write_panic();
        }
    }
    control_lock(control);
    if (control->fs_request != request ||
        control->phase != R_LIBRARY_FS_ATOMIC_WRITE_PHASE_STAGE_CREATE) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->fs_request = NULL;
    if (stage_taken) {
        control->stage_descriptor = stage_descriptor;
        control->stage_cleanup_prepared = cleanup;
        control->stage_owned = 1;
        memcpy(control->staging_name, staging_name, sizeof(control->staging_name));
        build_staging_source(control);
        if (!r_runtime_darwin_fs_prepared_bind_fsync(control->file_sync_prepared,
                                                     stage_descriptor) ||
            !r_runtime_darwin_fs_prepared_bind_staging_rename(
                control->rename_prepared, control->parent_descriptor, control->staging_source)) {
            control_unlock(control);
            atomic_write_panic();
        }
    }
    control_unlock(control);
    r_runtime_darwin_fs_request_release(request);

    if (!stage_taken) {
        establish_fs_result_failure(control, control->beneath, result);
        begin_parent_close(control);
        control_release(control);
        return;
    }

    begin_io_write(control);
    control_release(control);
}

static void prepare_and_begin_stage(RLibraryFsAtomicWriteControl *control) {
    RRuntimeDarwinFsSubmitResult submission;
    _Bool cancelled;

    control_lock(control);
    if (control->phase != R_LIBRARY_FS_ATOMIC_WRITE_PHASE_STAGE_PREPARING ||
        control->fs_request != NULL) {
        control_unlock(control);
        atomic_write_panic();
    }
    cancelled = control->outcome != R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_NONE ||
                (control->execution != NULL &&
                 r_runtime_task_external_cancel_requested(control->execution));
    if (cancelled) {
        establish_observed_cancellation_locked(control);
        control_unlock(control);
        begin_parent_close(control);
        return;
    }
    if (!r_runtime_darwin_fs_prepared_bind_file_stage(control->stage_prepared,
                                                      control->parent_descriptor) ||
        ((control->beneath && control->durability_parent != NULL)
             ? !r_runtime_darwin_fs_prepared_bind_full_fsync_directory_at(
                   control->parent_sync_prepared, control->parent_descriptor)
             : !r_runtime_darwin_fs_prepared_bind_fsync(control->parent_sync_prepared,
                                                        control->parent_descriptor))) {
        control_unlock(control);
        atomic_write_panic();
    }
    submission = r_runtime_darwin_fs_prepared_activate(&control->stage_prepared);
    if (submission.status != R_RUNTIME_DARWIN_FS_START_OK || submission.request == NULL ||
        control->stage_prepared != NULL) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->fs_request = submission.request;
    control->phase = R_LIBRARY_FS_ATOMIC_WRITE_PHASE_STAGE_CREATE;
    control_retain(control);
    control_unlock(control);
    if (!r_runtime_darwin_fs_request_set_completion(submission.request, stage_completed, control)) {
        atomic_write_panic();
    }
}

static void parent_open_completed(RRuntimeDarwinFsRequest *request, void *context) {
    RLibraryFsAtomicWriteControl *control = context;
    RRuntimeDarwinFsResult result = r_runtime_darwin_fs_request_wait(request);
    int descriptor = -1;
    _Bool outcome_none;

    if (result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE && result.native_error == 0) {
        descriptor = r_runtime_darwin_fs_request_take_opened_fd(request);
        if (descriptor < 0) {
            atomic_write_panic();
        }
    }
    control_lock(control);
    if (control->fs_request != request ||
        control->phase != R_LIBRARY_FS_ATOMIC_WRITE_PHASE_PARENT_OPEN) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->fs_request = NULL;
    control->parent_descriptor = descriptor;
    if (descriptor >= 0) {
        control->phase = R_LIBRARY_FS_ATOMIC_WRITE_PHASE_STAGE_PREPARING;
    }
    control_unlock(control);
    release_root_tracking(control);
    r_runtime_darwin_fs_request_release(request);

    if (descriptor < 0) {
        establish_fs_result_failure(control, control->beneath, result);
        begin_parent_close(control);
    } else {
        control_lock(control);
        outcome_none = control->outcome == R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_NONE;
        control_unlock(control);
        if (outcome_none) {
            prepare_and_begin_stage(control);
        } else {
            begin_parent_close(control);
        }
    }
    control_release(control);
}

static void signal_active_prepublication_locked(RLibraryFsAtomicWriteControl *control,
                                                _Bool deadline) {
    if (control->fs_request != NULL &&
        (control->phase == R_LIBRARY_FS_ATOMIC_WRITE_PHASE_PARENT_OPEN ||
         control->phase == R_LIBRARY_FS_ATOMIC_WRITE_PHASE_STAGE_CREATE ||
         control->phase == R_LIBRARY_FS_ATOMIC_WRITE_PHASE_FILE_SYNC ||
         control->phase == R_LIBRARY_FS_ATOMIC_WRITE_PHASE_PUBLICATION)) {
        if (deadline) {
            (void)r_runtime_darwin_fs_request_deadline_expired(control->fs_request);
        } else {
            (void)r_runtime_darwin_fs_request_cancel(control->fs_request);
        }
    } else if (control->io_request != NULL &&
               control->phase == R_LIBRARY_FS_ATOMIC_WRITE_PHASE_IO_WRITE) {
        if (deadline) {
            (void)r_runtime_darwin_io_request_deadline_expired(control->io_request);
        } else {
            (void)r_runtime_darwin_io_request_cancel(control->io_request);
        }
    }
}

static void deadline_expired(void *context) {
    RLibraryFsAtomicWriteControl *control = context;
    const uint64_t event_sequence = r_runtime_darwin_event_sequence_next();

    establish_failure_at(control, fs_error(R_STD_FS_ERROR_TIMED_OUT, INT64_C(0)), event_sequence);
    control_lock(control);
    if (control->outcome != R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_COMMITTED) {
        signal_active_prepublication_locked(control, 1);
    }
    control_unlock(control);
}

static void external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryFsAtomicWritePayload *payload = payload_pointer;
    RLibraryFsAtomicWriteControl *control = payload->control;
    uint64_t event_sequence;

    if (control == NULL) {
        atomic_write_panic();
    }
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    atomic_write_testing_wait(&atomic_write_testing_pause_before_cancel_report,
                              &atomic_write_testing_cancel_report_reached);
#endif
    event_sequence = r_runtime_task_external_cancellation_sequence(execution);
    if (event_sequence == UINT64_C(0)) {
        atomic_write_panic();
    }
    control_retain(control);
    control_lock(control);
    if (control->execution != execution || control->cancel_reported) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->task_cancel_seen = 1;
    control->wait_cancel_report = 1;
    establish_cancellation_locked(control, event_sequence);
    if (control->outcome != R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_COMMITTED) {
        signal_active_prepublication_locked(control, 0);
    }
    control->cancel_reported = 1;
    control_unlock(control);
    finish_operation(control);
    control_release(control);
}

static void mark_start_ready(RLibraryFsAtomicWriteControl *control) {
    r_runtime_task_external_start_ready(control->execution);
    control_lock(control);
    if (control->start_ready) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->start_ready = 1;
    if (control->deadline_initialized &&
        control->outcome == R_LIBRARY_FS_ATOMIC_WRITE_OUTCOME_NONE) {
        r_library_internal_fs_position_deadline_activate(&control->deadline_timer);
    }
    control_unlock(control);
    finish_operation(control);
}

static void complete_pre_start_failure(RLibraryFsAtomicWriteControl *control, RStdFsError error) {
    establish_failure(control, error);
    enter_final_phase(control);
    mark_start_ready(control);
}

static void complete_pre_start_cancellation(RLibraryFsAtomicWriteControl *control) {
    control_lock(control);
    establish_observed_cancellation_locked(control);
    control->wait_cancel_report = 1;
    control_unlock(control);
    enter_final_phase(control);
    mark_start_ready(control);
}

static void external_start(RRuntimeTaskExternalExecution *execution,
                           void *payload_pointer,
                           void *result_pointer) {
    RLibraryFsAtomicWritePayload *payload = payload_pointer;
    RLibraryFsAtomicWriteControl *control = payload->control;
    RRuntimeDarwinFsSubmitResult submission;
    RLibraryFsAtomicWriteDeadlineStatus deadline_status;
    RStdFsError deadline_error = {0};
    _Bool registration_failed = 0;
    _Bool select = 0;

    if (control == NULL || result_pointer == NULL || !payload->data_owned) {
        atomic_write_panic();
    }
    control_lock(control);
    control->execution = execution;
    control->payload = payload;
    control->result = result_pointer;
    control_unlock(control);

    if (!control->immediate && control->deadline.has_value) {
        deadline_status = classify_deadline(control->deadline, &deadline_error);
        if (deadline_status != R_LIBRARY_FS_ATOMIC_WRITE_DEADLINE_READY) {
            control->immediate = 1;
            control->error = deadline_error;
        }
    }
    if (control->immediate) {
        complete_pre_start_failure(control, control->error);
        return;
    }
    if (r_runtime_task_external_cancel_requested(execution)) {
        complete_pre_start_cancellation(control);
        return;
    }

    submission = r_runtime_darwin_fs_prepared_activate(&control->parent_open_prepared);
    if (submission.status != R_RUNTIME_DARWIN_FS_START_OK || submission.request == NULL ||
        control->parent_open_prepared != NULL) {
        complete_pre_start_failure(
            control, fs_error(R_STD_FS_ERROR_CANCELLED, (int64_t)submission.native_error));
        return;
    }
    control_lock(control);
    control->fs_request = submission.request;
    control->phase = R_LIBRARY_FS_ATOMIC_WRITE_PHASE_PARENT_OPEN;
    if (control->root_storage != NULL &&
        !r_library_internal_fs_operation_register(
            control->root_storage, &control->root_registration, submission.request)) {
        registration_failed = 1;
        select = begin_failure_locked(control,
                                      fs_error(R_STD_FS_ERROR_CLOSED, INT64_C(0)),
                                      r_runtime_darwin_event_sequence_next());
    }
    control_retain(control);
    control_unlock(control);
    if (!r_runtime_darwin_fs_request_set_completion(
            submission.request, parent_open_completed, control)) {
        atomic_write_panic();
    }
    mark_start_ready(control);
    if (registration_failed) {
        if (!select) {
            atomic_write_panic();
        }
        finish_failure_selection(control);
        (void)r_runtime_darwin_fs_request_cancel(submission.request);
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
    atomic_write_panic();
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
        atomic_write_panic();
    }
    atomic_write_panic();
}

static RRuntimeTaskStartStatus native_io_prepare_status(RRuntimeDarwinIoPrepareResult prepared) {
    switch (prepared.status) {
    case R_RUNTIME_DARWIN_IO_START_OK:
        return R_RUNTIME_TASK_START_OK;
    case R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED:
    case R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED:
        return R_RUNTIME_TASK_START_ALLOCATION_FAILED;
    case R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT:
    case R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED:
    case R_RUNTIME_DARWIN_IO_START_ORDERING_BLOCKED:
        atomic_write_panic();
    }
    atomic_write_panic();
}

static void discard_uncommitted(RLibraryFsAtomicWriteControl *control) {
    abort_unused_preparations(control);
    abort_fs_prepared(&control->parent_close_prepared);
    abort_fs_prepared(&control->stage_close_prepared);
    abort_fs_prepared(&control->stage_cleanup_prepared);
    release_root_tracking(control);
    destroy_deadline(control);
    control_lock(control);
    if (control->parent_descriptor >= 0 || control->stage_descriptor >= 0 || control->stage_owned ||
        control->fs_request != NULL || control->io_request != NULL) {
        control_unlock(control);
        atomic_write_panic();
    }
    control->phase = R_LIBRARY_FS_ATOMIC_WRITE_PHASE_FINAL;
    control_unlock(control);
    control_release(control);
}

static char *copy_path_part(RRuntimeAllocator *allocator, const uint8_t *bytes, size_t length) {
    char *copy = NULL;

    if (length == SIZE_MAX ||
        r_runtime_allocator_allocate(allocator, length + 1U, _Alignof(char), (void **)&copy) !=
            R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    if (length != 0U) {
        memcpy(copy, bytes, length);
    }
    copy[length] = '\0';
    return copy;
}

static size_t last_separator(const uint8_t *bytes, size_t length) {
    size_t index;

    for (index = length; index != 0U; --index) {
        if (bytes[index - 1U] == UINT8_C('/')) {
            return index - 1U;
        }
    }
    return SIZE_MAX;
}

static _Bool prepare_ordinary_paths(RLibraryFsAtomicWriteControl *control,
                                    const RStdFsPath *path,
                                    char **parent_path) {
    static const uint8_t current_directory[] = ".";
    const uint8_t *bytes = r_library_internal_fs_path_bytes(path);
    const size_t length = r_library_internal_fs_path_length(path);
    const size_t separator = last_separator(bytes, length);
    const uint8_t *parent_bytes;
    size_t destination_start;
    size_t parent_length;

    if (separator == SIZE_MAX) {
        parent_bytes = current_directory;
        parent_length = sizeof(current_directory) - 1U;
        destination_start = 0U;
    } else if (separator == 0U) {
        parent_bytes = bytes;
        parent_length = 1U;
        destination_start = 1U;
    } else {
        parent_bytes = bytes;
        parent_length = separator;
        destination_start = separator + 1U;
    }
    if (destination_start >= length) {
        return 0;
    }
    control->destination =
        copy_path_part(control->allocator, bytes + destination_start, length - destination_start);
    if (control->destination == NULL) {
        return 0;
    }
    *parent_path = copy_path_part(control->allocator, parent_bytes, parent_length);
    return *parent_path != NULL;
}

static _Bool prepare_beneath_paths(RLibraryFsAtomicWriteControl *control,
                                   const RStdFsPath *relative) {
    const uint8_t *bytes = r_library_internal_fs_path_bytes(relative);
    const size_t length = r_library_internal_fs_path_length(relative);
    const size_t separator = last_separator(bytes, length);

    control->destination = copy_path_part(control->allocator, bytes, length);
    if (control->destination == NULL) {
        return 0;
    }
    if (separator != SIZE_MAX) {
        control->durability_parent = copy_path_part(control->allocator, bytes, separator);
        if (control->durability_parent == NULL) {
            return 0;
        }
    }
    return 1;
}

static _Bool beneath_root_closed(const RStdFsDirectory *root) {
    RLibraryFsHandleStorage *storage = r_library_internal_fs_directory_handle_storage(root);
    _Bool closed;

    if (storage == NULL || pthread_mutex_lock(&storage->mutex) != 0) {
        atomic_write_panic();
    }
    closed = storage->terminal || storage->close_reserved || storage->descriptor < 0;
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        atomic_write_panic();
    }
    return closed;
}

static RRuntimeDarwinFsPrepareResult prepare_beneath_parent_open(
    RLibraryFsAtomicWriteControl *control, const RStdFsDirectory *root, _Bool *closed) {
    RRuntimeDarwinFsPrepareResult prepared = {0};
    RLibraryFsHandleStorage *storage = r_library_internal_fs_directory_handle_storage(root);
    size_t references;

    *closed = 0;
    if (storage == NULL || pthread_mutex_lock(&storage->mutex) != 0) {
        atomic_write_panic();
    }
    references = atomic_load_explicit(&storage->references, memory_order_relaxed);
    if (storage->terminal || storage->close_reserved || storage->descriptor < 0) {
        *closed = 1;
    } else if (references == SIZE_MAX) {
        (void)pthread_mutex_unlock(&storage->mutex);
        atomic_write_panic();
    } else {
        (void)atomic_fetch_add_explicit(&storage->references, 1U, memory_order_relaxed);
        control->root_storage = storage;
        prepared = r_runtime_darwin_fs_service_prepare_open_beneath(
            storage->descriptor, ".", O_RDONLY | O_DIRECTORY, (mode_t)0, UINT64_C(0));
    }
    if (pthread_mutex_unlock(&storage->mutex) != 0) {
        atomic_write_panic();
    }
    return prepared;
}

static RStdFsTaskStartResult atomic_write_start(const RStdFsDirectory *root,
                                                const RStdFsPath *path,
                                                RRuntimeArray *data,
                                                RStdFsDeadline deadline) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryFsAtomicWritePayload),
        _Alignof(RLibraryFsAtomicWritePayload),
        payload_move,
        payload_drop,
    };
    RLibraryFsAtomicWriteControl *control;
    RLibraryFsAtomicWritePayload payload = {0};
    RRuntimeDarwinFsPrepareResult parent_open = {0};
    RRuntimeDarwinFsPrepareResult parent_close = {0};
    RRuntimeDarwinFsPrepareResult stage_close = {0};
    RRuntimeDarwinFsPrepareResult stage = {0};
    RRuntimeDarwinFsPrepareResult file_sync = {0};
    RRuntimeDarwinFsPrepareResult rename = {0};
    RRuntimeDarwinFsPrepareResult parent_sync = {0};
    RRuntimeDarwinIoPrepareResult io = {0};
    RRuntimeTaskPrepareResult task_prepared;
    RRuntimeTaskStartResult task_started;
    RRuntimeTaskStartStatus native_status;
    RStdFsTaskStartResult result = {0};
    RStdFsError immediate_error = {0};
    RRuntimeAllocator *allocator;
    char *parent_path = NULL;
    size_t path_length;
    _Bool beneath = root != NULL;
    _Bool closed = 0;

    allocator = r_library_internal_fs_path_allocator(path);
    path_length = r_library_internal_fs_path_length(path);
    control = control_create(allocator, beneath, deadline);
    if (control == NULL) {
        return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    payload.control = control;
    payload.staged_data = data;

    if (beneath &&
        r_library_internal_fs_validate_beneath_relative(path) != R_LIBRARY_FS_RELATIVE_PATH_VALID) {
        control->immediate = 1;
        control->error = fs_error(R_STD_FS_ERROR_INVALID_RELATIVE_PATH, INT64_C(0));
    } else if (!beneath &&
               (path_length == 0U ||
                r_library_internal_fs_path_bytes(path)[path_length - 1U] == UINT8_C('/') ||
                r_library_internal_fs_path_has_reserved_component(path))) {
        control->immediate = 1;
        control->error = fs_error(R_STD_FS_ERROR_INVALID_PATH, INT64_C(0));
    } else if (path_length >= R_RUNTIME_DARWIN_FS_PATH_SCAN_LIMIT) {
        control->immediate = 1;
        control->error = fs_error(R_STD_FS_ERROR_NAME_TOO_LONG, INT64_C(0));
    } else if (beneath && beneath_root_closed(root)) {
        control->immediate = 1;
        control->error = fs_error(R_STD_FS_ERROR_CLOSED, INT64_C(0));
    } else if (classify_deadline(deadline, &immediate_error) !=
               R_LIBRARY_FS_ATOMIC_WRITE_DEADLINE_READY) {
        control->immediate = 1;
        control->error = immediate_error;
    }

    if (!control->immediate) {
        if ((beneath && !prepare_beneath_paths(control, path)) ||
            (!beneath && !prepare_ordinary_paths(control, path, &parent_path))) {
            r_runtime_allocator_deallocate(parent_path, _Alignof(char));
            discard_uncommitted(control);
            return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
        }
    }
    if (!control->immediate && deadline.has_value) {
        if (!r_library_internal_fs_position_deadline_initialize(&control->deadline_timer,
                                                                allocator,
                                                                deadline.value,
                                                                deadline_expired,
                                                                control_retain,
                                                                control_release,
                                                                control)) {
            r_runtime_allocator_deallocate(parent_path, _Alignof(char));
            discard_uncommitted(control);
            return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
        }
        control->deadline_initialized = 1;
    }
    if (!control->immediate) {
        if (beneath) {
            parent_open = prepare_beneath_parent_open(control, root, &closed);
            if (closed) {
                control->immediate = 1;
                control->error = fs_error(R_STD_FS_ERROR_CLOSED, INT64_C(0));
            }
        } else {
            parent_open = r_runtime_darwin_fs_service_prepare_open(
                parent_path, O_RDONLY | O_DIRECTORY, (mode_t)0, 1, UINT64_C(0));
        }
    }
    r_runtime_allocator_deallocate(parent_path, _Alignof(char));
    if (!control->immediate) {
        native_status = native_prepare_status(parent_open);
        if (native_status != R_RUNTIME_TASK_START_OK) {
            discard_uncommitted(control);
            return task_start_failure(native_status);
        }
        control->parent_open_prepared = parent_open.prepared;

        parent_close = r_runtime_darwin_fs_service_prepare_close(UINT64_C(0));
        native_status = native_prepare_status(parent_close);
        if (native_status != R_RUNTIME_TASK_START_OK) {
            discard_uncommitted(control);
            return task_start_failure(native_status);
        }
        control->parent_close_prepared = parent_close.prepared;

        stage_close = r_runtime_darwin_fs_service_prepare_close(UINT64_C(0));
        native_status = native_prepare_status(stage_close);
        if (native_status != R_RUNTIME_TASK_START_OK) {
            discard_uncommitted(control);
            return task_start_failure(native_status);
        }
        control->stage_close_prepared = stage_close.prepared;

        stage = r_runtime_darwin_fs_service_prepare_file_stage_late_bound();
        native_status = native_prepare_status(stage);
        if (native_status != R_RUNTIME_TASK_START_OK) {
            discard_uncommitted(control);
            return task_start_failure(native_status);
        }
        control->stage_prepared = stage.prepared;

        file_sync = r_runtime_darwin_fs_service_prepare_fsync_late_bound(1);
        native_status = native_prepare_status(file_sync);
        if (native_status != R_RUNTIME_TASK_START_OK) {
            discard_uncommitted(control);
            return task_start_failure(native_status);
        }
        control->file_sync_prepared = file_sync.prepared;

        rename =
            r_runtime_darwin_fs_service_prepare_staging_rename_late_bound(control->destination, 1);
        native_status = native_prepare_status(rename);
        if (native_status != R_RUNTIME_TASK_START_OK) {
            discard_uncommitted(control);
            return task_start_failure(native_status);
        }
        control->rename_prepared = rename.prepared;

        if (beneath && control->durability_parent != NULL) {
            parent_sync = r_runtime_darwin_fs_service_prepare_full_fsync_directory_at_late_bound(
                control->durability_parent, 1);
        } else {
            parent_sync = r_runtime_darwin_fs_service_prepare_fsync_late_bound(1);
        }
        native_status = native_prepare_status(parent_sync);
        if (native_status != R_RUNTIME_TASK_START_OK) {
            discard_uncommitted(control);
            return task_start_failure(native_status);
        }
        control->parent_sync_prepared = parent_sync.prepared;

        if (data->length != 0U) {
            io = r_runtime_darwin_io_prepare_borrowed_random_shared_write(
                allocator, (off_t)0, data->data, data->length, UINT64_C(0));
            native_status = native_io_prepare_status(io);
            if (native_status != R_RUNTIME_TASK_START_OK) {
                discard_uncommitted(control);
                return task_start_failure(native_status);
            }
            control->io_prepared = io.prepared;
        }
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

RStdFsTaskStartResult r_library_internal_fs_write_file_atomic_no_replace(const RStdFsPath *path,
                                                                         RRuntimeArray *data,
                                                                         RStdFsDeadline deadline) {
    return atomic_write_start(NULL, path, data, deadline);
}

RStdFsTaskStartResult
r_library_internal_fs_write_file_atomic_no_replace_beneath(const RStdFsDirectory *root,
                                                           const RStdFsPath *relative,
                                                           RRuntimeArray *data,
                                                           RStdFsDeadline deadline) {
    if (root == NULL) {
        atomic_write_panic();
    }
    return atomic_write_start(root, relative, data, deadline);
}
