#include "r_library_fs_internal.h"

#include "r_library_clock_internal.h"
#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_darwin_fs_lane.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef enum RLibraryFsCloseMode {
    R_LIBRARY_FS_CLOSE_FILE = 0,
    R_LIBRARY_FS_CLOSE_DIRECTORY
} RLibraryFsCloseMode;

typedef enum RLibraryFsCloseDeadlineStatus {
    R_LIBRARY_FS_CLOSE_DEADLINE_READY = 0,
    R_LIBRARY_FS_CLOSE_DEADLINE_EXPIRED,
    R_LIBRARY_FS_CLOSE_DEADLINE_ERROR
} RLibraryFsCloseDeadlineStatus;

typedef enum RLibraryFsCloseComponent {
    R_LIBRARY_FS_CLOSE_COMPONENT_NONE = 0U,
    R_LIBRARY_FS_CLOSE_COMPONENT_FS = 1U,
    R_LIBRARY_FS_CLOSE_COMPONENT_IO = 2U
} RLibraryFsCloseComponent;

typedef enum RLibraryFsCloseOutcome {
    R_LIBRARY_FS_CLOSE_OUTCOME_NONE = 0,
    R_LIBRARY_FS_CLOSE_OUTCOME_SUCCESS,
    R_LIBRARY_FS_CLOSE_OUTCOME_ERROR,
    R_LIBRARY_FS_CLOSE_OUTCOME_DEADLINE,
    R_LIBRARY_FS_CLOSE_OUTCOME_TASK_CANCEL
} RLibraryFsCloseOutcome;

enum {
    R_LIBRARY_FS_CLOSE_FS_DONE = 1U,
    R_LIBRARY_FS_CLOSE_OPERATIONS_DRAINED = 2U,
    R_LIBRARY_FS_CLOSE_IO_DONE = 4U,
    R_LIBRARY_FS_CLOSE_TASK_CANCEL_REPORTED = 8U,
    R_LIBRARY_FS_CLOSE_FINALIZED = 16U,
    R_LIBRARY_FS_CLOSE_ACKNOWLEDGE_READY = 32U
};

typedef struct RLibraryFsClosePayload RLibraryFsClosePayload;

typedef struct RLibraryFsCloseDeadlineControl {
    _Atomic size_t references;
    RRuntimeAllocator *allocator;
    pthread_mutex_t mutex;
    RLibraryFsPositionDeadline deadline;
    RRuntimeDarwinFsRequest *fs_request;
    RRuntimeDarwinIoRequest *io_request;
    RRuntimeTaskExternalExecution *execution;
    RLibraryFsClosePayload *payload;
    _Bool deadline_selected;
    _Bool terminal;
} RLibraryFsCloseDeadlineControl;

struct RLibraryFsClosePayload {
    RLibraryFsCloseMode mode;
    void *staged_owner;
    RLibraryFsHandleStorage *storage;
    RRuntimeDarwinFsPreparedRequest *prepared;
    RRuntimeDarwinFsRequest *request;
    RRuntimeDarwinIoHandle *payload_io;
    RRuntimeDarwinIoPreparedRequest *io_prepared;
    RRuntimeDarwinIoRequest *io_request;
    RLibraryFsCloseDeadlineControl *deadline_control;
    RRuntimeTaskExternalExecution *execution;
    RStdFsVoidResult *result;
    RStdFsDeadline deadline;
    RStdFsError forced_error;
    RStdFsError selected_error;
    RRuntimeDarwinFsResult native_result;
    RRuntimeDarwinIoResult io_result;
    pthread_mutex_t outcome_mutex;
    uint64_t forced_event_sequence;
    uint64_t outcome_sequence;
    _Atomic unsigned int state;
    _Atomic unsigned int callback_count;
    RLibraryFsCloseOutcome outcome;
    _Bool payload_io_owned;
    _Bool outcome_mutex_initialized;
    _Bool deadline_expired;
    _Bool has_forced_error;
    _Bool preexisting_failure;
};

#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
static pthread_mutex_t close_testing_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t close_testing_condition = PTHREAD_COND_INITIALIZER;
static _Bool close_testing_pause_before_deadline_check;
static _Bool close_testing_deadline_check_reached;
static _Bool close_testing_pause_before_finalize_selection;
static _Bool close_testing_finalize_selection_reached;
static _Bool close_testing_finalize_selection_completed;
static _Bool close_testing_pause_before_cancel_report;
static _Bool close_testing_cancel_report_reached;
static _Bool close_testing_pause_after_duty_publish;
static unsigned int close_testing_duty_publish_waiter_count;
static _Atomic _Bool close_testing_deadline_selected;
static _Atomic _Bool close_testing_deadline_reported;
static _Bool close_testing_pause_after_component_error_selection;
static unsigned int close_testing_selected_error_component;

static void close_testing_set_pause(_Bool *pause, _Bool *reached, _Bool paused) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    *pause = paused;
    if (paused) {
        *reached = 0;
        if (pause == &close_testing_pause_before_finalize_selection) {
            close_testing_finalize_selection_completed = 0;
        }
    }
    if (pthread_cond_broadcast(&close_testing_condition) != 0 ||
        pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}

static _Bool close_testing_reached(const _Bool *reached) {
    _Bool value;

    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    value = *reached;
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
    return value;
}

static void close_testing_wait(_Bool *pause, _Bool *reached) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    if (*pause) {
        *reached = 1;
        if (pthread_cond_broadcast(&close_testing_condition) != 0) {
            abort();
        }
        while (*pause) {
            if (pthread_cond_wait(&close_testing_condition, &close_testing_mutex) != 0) {
                abort();
            }
        }
    }
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}

void r_library_internal_fs_close_testing_pause_before_deadline_check(_Bool paused) {
    close_testing_set_pause(
        &close_testing_pause_before_deadline_check, &close_testing_deadline_check_reached, paused);
}

_Bool r_library_internal_fs_close_testing_deadline_check_reached(void) {
    return close_testing_reached(&close_testing_deadline_check_reached);
}

void r_library_internal_fs_close_testing_pause_before_finalize_selection(_Bool paused) {
    close_testing_set_pause(&close_testing_pause_before_finalize_selection,
                            &close_testing_finalize_selection_reached,
                            paused);
}

_Bool r_library_internal_fs_close_testing_finalize_selection_reached(void) {
    return close_testing_reached(&close_testing_finalize_selection_reached);
}

_Bool r_library_internal_fs_close_testing_finalize_selection_completed(void) {
    return close_testing_reached(&close_testing_finalize_selection_completed);
}

void r_library_internal_fs_close_testing_pause_before_cancel_report(_Bool paused) {
    close_testing_set_pause(
        &close_testing_pause_before_cancel_report, &close_testing_cancel_report_reached, paused);
}

_Bool r_library_internal_fs_close_testing_cancel_report_reached(void) {
    return close_testing_reached(&close_testing_cancel_report_reached);
}

void r_library_internal_fs_close_testing_pause_after_duty_publish(_Bool paused) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    close_testing_pause_after_duty_publish = paused;
    if (paused) {
        close_testing_duty_publish_waiter_count = 0U;
    }
    if (pthread_cond_broadcast(&close_testing_condition) != 0 ||
        pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}

unsigned int r_library_internal_fs_close_testing_duty_publish_waiter_count(void) {
    unsigned int count;

    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    count = close_testing_duty_publish_waiter_count;
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
    return count;
}

_Bool r_library_internal_fs_close_testing_deadline_selected(void) {
    return atomic_load_explicit(&close_testing_deadline_selected, memory_order_acquire);
}

_Bool r_library_internal_fs_close_testing_deadline_reported(void) {
    return atomic_load_explicit(&close_testing_deadline_reported, memory_order_acquire);
}

void r_library_internal_fs_close_testing_pause_after_component_error_selection(_Bool paused) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    close_testing_pause_after_component_error_selection = paused;
    if (paused) {
        close_testing_selected_error_component = R_LIBRARY_FS_CLOSE_COMPONENT_NONE;
    }
    if (pthread_cond_broadcast(&close_testing_condition) != 0 ||
        pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}

unsigned int r_library_internal_fs_close_testing_selected_error_component(void) {
    unsigned int component;

    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    component = close_testing_selected_error_component;
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
    return component;
}

static void close_testing_wait_after_duty_publish(void) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    if (close_testing_pause_after_duty_publish) {
        if (close_testing_duty_publish_waiter_count == UINT_MAX) {
            (void)pthread_mutex_unlock(&close_testing_mutex);
            abort();
        }
        close_testing_duty_publish_waiter_count += 1U;
        if (pthread_cond_broadcast(&close_testing_condition) != 0) {
            abort();
        }
        while (close_testing_pause_after_duty_publish) {
            if (pthread_cond_wait(&close_testing_condition, &close_testing_mutex) != 0) {
                abort();
            }
        }
    }
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}

static void close_testing_wait_after_component_error_selection(unsigned int component) {
    if (pthread_mutex_lock(&close_testing_mutex) != 0) {
        abort();
    }
    if (!close_testing_pause_after_component_error_selection) {
        if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
            abort();
        }
        return;
    }
    if (close_testing_selected_error_component != R_LIBRARY_FS_CLOSE_COMPONENT_NONE) {
        (void)pthread_mutex_unlock(&close_testing_mutex);
        abort();
    }
    close_testing_selected_error_component = component;
    if (pthread_cond_broadcast(&close_testing_condition) != 0) {
        (void)pthread_mutex_unlock(&close_testing_mutex);
        abort();
    }
    while (close_testing_pause_after_component_error_selection) {
        if (pthread_cond_wait(&close_testing_condition, &close_testing_mutex) != 0) {
            (void)pthread_mutex_unlock(&close_testing_mutex);
            abort();
        }
    }
    if (pthread_mutex_unlock(&close_testing_mutex) != 0) {
        abort();
    }
}
#endif

_Noreturn static void close_panic(void) {
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

static RLibraryFsCloseDeadlineStatus check_deadline(RStdFsDeadline deadline, RStdFsError *error) {
    RStdTimeInstantResult now;

    if (!deadline.has_value) {
        return R_LIBRARY_FS_CLOSE_DEADLINE_READY;
    }
    if (deadline.value.storage_seconds < 0 ||
        deadline.value.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
        *error = fs_error(R_STD_FS_ERROR_INVALID_OPERATION, INT64_C(0));
        return R_LIBRARY_FS_CLOSE_DEADLINE_ERROR;
    }
    now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
    if (!now.is_ok) {
        *error = fs_error(R_STD_FS_ERROR_OTHER, now.error.native_code);
        return R_LIBRARY_FS_CLOSE_DEADLINE_ERROR;
    }
    if (compare_instant(deadline.value, now.value) <= 0) {
        *error = fs_error(R_STD_FS_ERROR_TIMED_OUT, INT64_C(0));
        return R_LIBRARY_FS_CLOSE_DEADLINE_EXPIRED;
    }
    return R_LIBRARY_FS_CLOSE_DEADLINE_READY;
}

static void record_testing_outcome(RLibraryFsCloseOutcome outcome) {
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    atomic_store_explicit(&close_testing_deadline_selected,
                          outcome == R_LIBRARY_FS_CLOSE_OUTCOME_DEADLINE,
                          memory_order_release);
#else
    (void)outcome;
#endif
}

static _Bool select_completion_outcome(RLibraryFsClosePayload *payload,
                                       RLibraryFsCloseOutcome outcome,
                                       RStdFsError error,
                                       uint64_t event_sequence) {
    uint64_t cancellation_sequence;
    _Bool selected = 0;

    if (payload == NULL || payload->execution == NULL || event_sequence == UINT64_C(0) ||
        outcome == R_LIBRARY_FS_CLOSE_OUTCOME_NONE ||
        outcome == R_LIBRARY_FS_CLOSE_OUTCOME_TASK_CANCEL ||
        pthread_mutex_lock(&payload->outcome_mutex) != 0) {
        close_panic();
    }
    if (payload->outcome_sequence != UINT64_C(0) && payload->outcome_sequence <= event_sequence) {
        if (pthread_mutex_unlock(&payload->outcome_mutex) != 0) {
            close_panic();
        }
        return 0;
    }
    if (payload->outcome == R_LIBRARY_FS_CLOSE_OUTCOME_NONE ||
        payload->outcome == R_LIBRARY_FS_CLOSE_OUTCOME_TASK_CANCEL) {
        selected =
            r_runtime_task_external_try_select_completion_at(payload->execution, event_sequence);
        if (!selected) {
            cancellation_sequence =
                r_runtime_task_external_cancellation_sequence(payload->execution);
            if (cancellation_sequence == UINT64_C(0)) {
                (void)pthread_mutex_unlock(&payload->outcome_mutex);
                close_panic();
            }
            if (payload->outcome_sequence == UINT64_C(0) ||
                cancellation_sequence < payload->outcome_sequence) {
                payload->outcome = R_LIBRARY_FS_CLOSE_OUTCOME_TASK_CANCEL;
                payload->outcome_sequence = cancellation_sequence;
                record_testing_outcome(payload->outcome);
            }
            if (pthread_mutex_unlock(&payload->outcome_mutex) != 0) {
                close_panic();
            }
            return 0;
        }
    }
    payload->outcome = outcome;
    payload->outcome_sequence = event_sequence;
    payload->selected_error = error;
    record_testing_outcome(outcome);
    if (pthread_mutex_unlock(&payload->outcome_mutex) != 0) {
        close_panic();
    }
    return 1;
}

static void select_task_cancel_outcome(RLibraryFsClosePayload *payload) {
    uint64_t event_sequence;

    if (payload == NULL || payload->execution == NULL) {
        close_panic();
    }
    event_sequence = r_runtime_task_external_cancellation_sequence(payload->execution);
    if (event_sequence == UINT64_C(0) || pthread_mutex_lock(&payload->outcome_mutex) != 0) {
        close_panic();
    }
    if (payload->outcome_sequence == UINT64_C(0) || event_sequence < payload->outcome_sequence) {
        payload->outcome = R_LIBRARY_FS_CLOSE_OUTCOME_TASK_CANCEL;
        payload->outcome_sequence = event_sequence;
        record_testing_outcome(payload->outcome);
    }
    if (pthread_mutex_unlock(&payload->outcome_mutex) != 0) {
        close_panic();
    }
}

static void deadline_control_retain(void *context) {
    RLibraryFsCloseDeadlineControl *control = context;
    size_t references = atomic_load_explicit(&control->references, memory_order_relaxed);

    for (;;) {
        if (references == 0U || references == SIZE_MAX) {
            close_panic();
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

static void deadline_control_release(void *context) {
    RLibraryFsCloseDeadlineControl *control = context;
    size_t previous;

    if (control == NULL) {
        return;
    }
    previous = atomic_fetch_sub_explicit(&control->references, 1U, memory_order_acq_rel);
    if (previous == 0U) {
        close_panic();
    }
    if (previous != 1U) {
        return;
    }
    if (control->deadline.token != NULL || pthread_mutex_destroy(&control->mutex) != 0) {
        close_panic();
    }
    r_runtime_allocator_deallocate(control, _Alignof(RLibraryFsCloseDeadlineControl));
}

static void deadline_control_expired(void *context) {
    RLibraryFsCloseDeadlineControl *control = context;
    const RStdFsError error = fs_error(R_STD_FS_ERROR_TIMED_OUT, INT64_C(0));
    uint64_t event_sequence;
    _Bool selected;

    event_sequence = r_runtime_darwin_event_sequence_next();
    if (pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    if (!control->terminal) {
        if (control->fs_request == NULL || control->execution == NULL || control->payload == NULL ||
            control->deadline_selected) {
            (void)pthread_mutex_unlock(&control->mutex);
            close_panic();
        }
        selected = select_completion_outcome(
            control->payload, R_LIBRARY_FS_CLOSE_OUTCOME_DEADLINE, error, event_sequence);
        control->deadline_selected = selected;
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
        atomic_store_explicit(&close_testing_deadline_reported, 1, memory_order_release);
#endif
        (void)r_runtime_darwin_fs_request_deadline_expired(control->fs_request);
        if (control->io_request != NULL) {
            (void)r_runtime_darwin_io_request_deadline_expired(control->io_request);
        }
    }
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
}

static RLibraryFsCloseDeadlineControl *deadline_control_create(RRuntimeAllocator *allocator,
                                                               RStdTimeInstant deadline) {
    RLibraryFsCloseDeadlineControl *control = NULL;

    if (allocator == NULL ||
        r_runtime_allocator_allocate(allocator,
                                     sizeof(*control),
                                     _Alignof(RLibraryFsCloseDeadlineControl),
                                     (void **)&control) != R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    (void)memset(control, 0, sizeof(*control));
    if (pthread_mutex_init(&control->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(control, _Alignof(RLibraryFsCloseDeadlineControl));
        return NULL;
    }
    atomic_init(&control->references, 1U);
    control->allocator = allocator;
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    atomic_store_explicit(&close_testing_deadline_selected, 0, memory_order_relaxed);
    atomic_store_explicit(&close_testing_deadline_reported, 0, memory_order_relaxed);
#endif
    if (!r_library_internal_fs_position_deadline_initialize(&control->deadline,
                                                            allocator,
                                                            deadline,
                                                            deadline_control_expired,
                                                            deadline_control_retain,
                                                            deadline_control_release,
                                                            control)) {
        deadline_control_release(control);
        return NULL;
    }
    return control;
}

static void deadline_control_bind(RLibraryFsCloseDeadlineControl *control,
                                  RRuntimeDarwinFsRequest *fs_request,
                                  RRuntimeDarwinIoRequest *io_request,
                                  RRuntimeTaskExternalExecution *execution,
                                  RLibraryFsClosePayload *payload) {
    if (control == NULL || fs_request == NULL || execution == NULL || payload == NULL ||
        pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    if (control->terminal || control->fs_request != NULL || control->io_request != NULL ||
        control->execution != NULL || control->payload != NULL) {
        (void)pthread_mutex_unlock(&control->mutex);
        close_panic();
    }
    control->fs_request = fs_request;
    control->io_request = io_request;
    control->execution = execution;
    control->payload = payload;
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
}

static void deadline_control_stop(RLibraryFsCloseDeadlineControl *control) {
    RLibraryFsPositionDeadline deadline = {0};

    if (control == NULL) {
        return;
    }
    if (pthread_mutex_lock(&control->mutex) != 0) {
        close_panic();
    }
    if (!control->terminal) {
        control->terminal = 1;
        control->fs_request = NULL;
        control->io_request = NULL;
        control->execution = NULL;
        control->payload = NULL;
        deadline = control->deadline;
        control->deadline.token = NULL;
    }
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        close_panic();
    }
    if (deadline.token != NULL) {
        r_library_internal_fs_position_deadline_destroy(&deadline);
    }
}

static RStdFsError classify_native_error(int native_error) {
    if (native_error == EBADF) {
        return fs_error(R_STD_FS_ERROR_CLOSED, (int64_t)native_error);
    }
    if (native_error == EINVAL || native_error == ENXIO) {
        return fs_error(R_STD_FS_ERROR_INVALID_OPERATION, (int64_t)native_error);
    }
    if (native_error == EACCES || native_error == EPERM) {
        return fs_error(R_STD_FS_ERROR_PERMISSION_DENIED, (int64_t)native_error);
    }
    if (native_error == ENOMEM || native_error == EMFILE || native_error == ENFILE ||
        native_error == ENOBUFS || native_error == EAGAIN) {
        return fs_error(R_STD_FS_ERROR_RESOURCE_EXHAUSTED, (int64_t)native_error);
    }
    return fs_error(R_STD_FS_ERROR_OTHER, (int64_t)native_error);
}

static RStdFsError classify_native_result(RRuntimeDarwinFsResult native_result) {
    if (native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_CANCELLED ||
        native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_STOPPING) {
        return fs_error(R_STD_FS_ERROR_CANCELLED, (int64_t)native_result.native_error);
    }
    if (native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_TIMED_OUT) {
        return fs_error(R_STD_FS_ERROR_TIMED_OUT, (int64_t)native_result.native_error);
    }
    return classify_native_error(native_result.native_error);
}

static RStdFsError classify_io_result(RRuntimeDarwinIoResult native_result) {
    if (native_result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED) {
        return fs_error(R_STD_FS_ERROR_CANCELLED, (int64_t)native_result.native_error);
    }
    if (native_result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_TIMED_OUT) {
        return fs_error(R_STD_FS_ERROR_TIMED_OUT, (int64_t)native_result.native_error);
    }
    return classify_native_error(native_result.native_error);
}

static void payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryFsClosePayload *destination = destination_pointer;
    RLibraryFsClosePayload *source = source_pointer;

    (void)memset(destination, 0, sizeof(*destination));
    destination->mode = source->mode;
    destination->deadline = source->deadline;
    destination->forced_error = source->forced_error;
    destination->forced_event_sequence = source->forced_event_sequence;
    destination->deadline_expired = source->deadline_expired;
    destination->has_forced_error = source->has_forced_error;
    destination->preexisting_failure = source->preexisting_failure;
    destination->prepared = source->prepared;
    destination->payload_io = source->payload_io;
    destination->io_prepared = source->io_prepared;
    destination->deadline_control = source->deadline_control;
    if (pthread_mutex_init(&destination->outcome_mutex, NULL) != 0) {
        close_panic();
    }
    destination->outcome_mutex_initialized = 1;
    atomic_init(&destination->state, 0U);
    atomic_init(&destination->callback_count, 0U);
    source->prepared = NULL;
    source->payload_io = NULL;
    source->io_prepared = NULL;
    source->deadline_control = NULL;
    if (source->mode == R_LIBRARY_FS_CLOSE_FILE) {
        RStdFsFile *file = source->staged_owner;

        if (file == NULL || file->storage == NULL) {
            close_panic();
        }
        destination->storage = &file->storage->handle;
        file->storage = NULL;
    } else {
        RStdFsDirectory *directory = source->staged_owner;

        if (directory == NULL || directory->storage == NULL) {
            close_panic();
        }
        destination->storage = &directory->storage->handle;
        directory->storage = NULL;
    }
}

static void payload_drop(void *value) {
    RLibraryFsClosePayload *payload = value;

    if (atomic_load_explicit(&payload->callback_count, memory_order_acquire) != 0U) {
        close_panic();
    }
    if (payload->prepared != NULL) {
        r_runtime_darwin_fs_prepared_abort(&payload->prepared);
    }
    if (payload->io_prepared != NULL) {
        r_runtime_darwin_io_prepared_abort(&payload->io_prepared);
    }
    if (payload->request != NULL || payload->io_request != NULL) {
        close_panic();
    }
    if (payload->deadline_control != NULL) {
        deadline_control_stop(payload->deadline_control);
        deadline_control_release(payload->deadline_control);
        payload->deadline_control = NULL;
    }
    if (payload->payload_io_owned) {
        r_runtime_darwin_io_handle_release(payload->payload_io);
        payload->payload_io = NULL;
        payload->payload_io_owned = 0;
    }
    if (payload->storage != NULL) {
        r_library_internal_fs_handle_release(payload->storage);
        payload->storage = NULL;
    }
    if (payload->outcome_mutex_initialized) {
        if (pthread_mutex_destroy(&payload->outcome_mutex) != 0) {
            close_panic();
        }
        payload->outcome_mutex_initialized = 0;
    }
}

static void fill_result(RLibraryFsClosePayload *payload, RLibraryFsCloseOutcome outcome) {
    (void)memset(payload->result, 0, sizeof(*payload->result));
    if (outcome == R_LIBRARY_FS_CLOSE_OUTCOME_SUCCESS) {
        payload->result->r_tag = UINT32_C(0);
    } else if (outcome == R_LIBRARY_FS_CLOSE_OUTCOME_DEADLINE) {
        payload->result->r_tag = UINT32_C(1);
        payload->result->r_payload.r_err = fs_error(R_STD_FS_ERROR_TIMED_OUT, INT64_C(0));
    } else if (outcome == R_LIBRARY_FS_CLOSE_OUTCOME_ERROR) {
        payload->result->r_tag = UINT32_C(1);
        payload->result->r_payload.r_err = payload->selected_error;
    } else {
        close_panic();
    }
}

static void callback_enter(RLibraryFsClosePayload *payload) {
    unsigned int count = atomic_load_explicit(&payload->callback_count, memory_order_relaxed);

    for (;;) {
        if (count == UINT_MAX) {
            close_panic();
        }
        if (atomic_compare_exchange_weak_explicit(&payload->callback_count,
                                                  &count,
                                                  count + 1U,
                                                  memory_order_acquire,
                                                  memory_order_relaxed)) {
            return;
        }
    }
}

static void callback_leave(RLibraryFsClosePayload *payload) {
    RRuntimeTaskExternalExecution *execution;
    unsigned int previous =
        atomic_fetch_sub_explicit(&payload->callback_count, 1U, memory_order_acq_rel);

    if (previous == 0U) {
        close_panic();
    }
    if (previous != 1U || (atomic_load_explicit(&payload->state, memory_order_acquire) &
                           R_LIBRARY_FS_CLOSE_ACKNOWLEDGE_READY) == 0U) {
        return;
    }
    execution = payload->execution;
    r_runtime_task_external_acknowledge(execution);
}

static void try_finalize(RLibraryFsClosePayload *payload) {
    unsigned int state = atomic_load_explicit(&payload->state, memory_order_acquire);
    RLibraryFsCloseOutcome outcome;
    uint64_t cancellation_sequence;

    if ((state & (R_LIBRARY_FS_CLOSE_FS_DONE | R_LIBRARY_FS_CLOSE_OPERATIONS_DRAINED |
                  R_LIBRARY_FS_CLOSE_IO_DONE)) !=
        (R_LIBRARY_FS_CLOSE_FS_DONE | R_LIBRARY_FS_CLOSE_OPERATIONS_DRAINED |
         R_LIBRARY_FS_CLOSE_IO_DONE)) {
        return;
    }
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    close_testing_wait(&close_testing_pause_before_finalize_selection,
                       &close_testing_finalize_selection_reached);
#endif
    if (payload->deadline_control != NULL) {
        deadline_control_stop(payload->deadline_control);
    }
    (void)select_completion_outcome(payload,
                                    R_LIBRARY_FS_CLOSE_OUTCOME_SUCCESS,
                                    (RStdFsError){0},
                                    r_runtime_darwin_event_sequence_next());
    if (pthread_mutex_lock(&payload->outcome_mutex) != 0) {
        close_panic();
    }
    outcome = payload->outcome;
    if (pthread_mutex_unlock(&payload->outcome_mutex) != 0 ||
        outcome == R_LIBRARY_FS_CLOSE_OUTCOME_NONE) {
        close_panic();
    }
    cancellation_sequence = r_runtime_task_external_cancellation_sequence(payload->execution);
    state = atomic_load_explicit(&payload->state, memory_order_acquire);
    if (cancellation_sequence != UINT64_C(0) &&
        (state & R_LIBRARY_FS_CLOSE_TASK_CANCEL_REPORTED) == 0U) {
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
        if (pthread_mutex_lock(&close_testing_mutex) != 0) {
            close_panic();
        }
        close_testing_finalize_selection_completed = 1;
        if (pthread_cond_broadcast(&close_testing_condition) != 0 ||
            pthread_mutex_unlock(&close_testing_mutex) != 0) {
            close_panic();
        }
#endif
        return;
    }
    for (;;) {
        unsigned int expected;

        if ((state & R_LIBRARY_FS_CLOSE_FINALIZED) != 0U) {
            return;
        }
        expected = state;
        if (atomic_compare_exchange_weak_explicit(&payload->state,
                                                  &expected,
                                                  state | R_LIBRARY_FS_CLOSE_FINALIZED,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
            break;
        }
        state = expected;
    }
    if (outcome != R_LIBRARY_FS_CLOSE_OUTCOME_TASK_CANCEL) {
        fill_result(payload, outcome);
    }
    r_runtime_darwin_fs_request_release(payload->request);
    payload->request = NULL;
    if (payload->io_request != NULL) {
        r_runtime_darwin_io_request_release(payload->io_request);
        payload->io_request = NULL;
    }
    if (payload->payload_io_owned) {
        r_runtime_darwin_io_handle_release(payload->payload_io);
        payload->payload_io = NULL;
        payload->payload_io_owned = 0;
    }
    r_library_internal_fs_handle_release(payload->storage);
    payload->storage = NULL;
    (void)atomic_fetch_or_explicit(
        &payload->state, R_LIBRARY_FS_CLOSE_ACKNOWLEDGE_READY, memory_order_release);
}

static void operations_drained(void *context) {
    RLibraryFsClosePayload *payload = context;

    callback_enter(payload);
    (void)atomic_fetch_or_explicit(
        &payload->state, R_LIBRARY_FS_CLOSE_OPERATIONS_DRAINED, memory_order_release);
    try_finalize(payload);
    callback_leave(payload);
}

static _Bool select_component_error(RLibraryFsClosePayload *payload,
                                    RStdFsError error,
                                    uint64_t event_sequence,
                                    unsigned int component) {
    const _Bool selected =
        select_completion_outcome(payload, R_LIBRARY_FS_CLOSE_OUTCOME_ERROR, error, event_sequence);

#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    if (selected) {
        close_testing_wait_after_component_error_selection(component);
    }
#else
    (void)component;
#endif
    return selected;
}

static void native_completed(RRuntimeDarwinFsRequest *request, void *context) {
    RLibraryFsClosePayload *payload = context;
    unsigned int state = R_LIBRARY_FS_CLOSE_FS_DONE;

    callback_enter(payload);
    if (payload->request != request) {
        close_panic();
    }
    payload->native_result = r_runtime_darwin_fs_request_wait(request);
    if (payload->native_result.terminal_event_sequence == UINT64_C(0)) {
        close_panic();
    }
    if (payload->native_result.terminal_event != R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE ||
        payload->native_result.native_error != 0) {
        (void)select_component_error(payload,
                                     classify_native_result(payload->native_result),
                                     payload->native_result.terminal_event_sequence,
                                     R_LIBRARY_FS_CLOSE_COMPONENT_FS);
    }
    (void)atomic_fetch_or_explicit(&payload->state, state, memory_order_release);
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    close_testing_wait_after_duty_publish();
#endif
    try_finalize(payload);
    callback_leave(payload);
}

static void io_completed(RRuntimeDarwinIoRequest *request, void *context) {
    RLibraryFsClosePayload *payload = context;
    unsigned int state = R_LIBRARY_FS_CLOSE_IO_DONE;

    callback_enter(payload);
    if (payload->io_request != request) {
        close_panic();
    }
    payload->io_result = r_runtime_darwin_io_request_wait(request);
    if (payload->io_result.terminal_event_sequence == UINT64_C(0)) {
        close_panic();
    }
    if (payload->io_result.terminal_event != R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE ||
        payload->io_result.native_error != 0 || !payload->io_result.cleanup_acknowledged) {
        (void)select_component_error(payload,
                                     classify_io_result(payload->io_result),
                                     payload->io_result.terminal_event_sequence,
                                     R_LIBRARY_FS_CLOSE_COMPONENT_IO);
    }
    (void)atomic_fetch_or_explicit(&payload->state, state, memory_order_release);
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    close_testing_wait_after_duty_publish();
#endif
    try_finalize(payload);
    callback_leave(payload);
}

static void external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryFsClosePayload *payload = payload_pointer;

    callback_enter(payload);
    if (payload->execution != execution || payload->request == NULL) {
        close_panic();
    }
    select_task_cancel_outcome(payload);
    (void)r_runtime_darwin_fs_request_cancel(payload->request);
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    close_testing_wait(&close_testing_pause_before_cancel_report,
                       &close_testing_cancel_report_reached);
#endif
    (void)atomic_fetch_or_explicit(
        &payload->state, R_LIBRARY_FS_CLOSE_TASK_CANCEL_REPORTED, memory_order_release);
    try_finalize(payload);
    callback_leave(payload);
}

static void external_start(RRuntimeTaskExternalExecution *execution,
                           void *payload_pointer,
                           void *result_pointer) {
    RLibraryFsClosePayload *payload = payload_pointer;
    RRuntimeDarwinFsSubmitResult submission;
    RRuntimeDarwinIoSubmitResult io_submission;
    RRuntimeDarwinIoHandle *payload_io = NULL;
    RLibraryFsCloseDeadlineStatus deadline_status;
    RStdFsError deadline_error = {0};
    int descriptor = -1;
    _Bool already_drained;
    _Bool deadline_expired = payload->deadline_expired;

    payload->execution = execution;
    payload->result = result_pointer;
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    close_testing_wait(&close_testing_pause_before_deadline_check,
                       &close_testing_deadline_check_reached);
#endif
    if (!payload->preexisting_failure && !payload->has_forced_error &&
        payload->deadline.has_value) {
        deadline_status = check_deadline(payload->deadline, &deadline_error);
        if (deadline_status != R_LIBRARY_FS_CLOSE_DEADLINE_READY) {
            payload->has_forced_error = 1;
            payload->forced_error = deadline_error;
            payload->forced_event_sequence = r_runtime_darwin_event_sequence_next();
            deadline_expired = deadline_status == R_LIBRARY_FS_CLOSE_DEADLINE_EXPIRED;
        }
    }
    if (payload->has_forced_error) {
        if (payload->forced_event_sequence == UINT64_C(0)) {
            close_panic();
        }
        (void)select_completion_outcome(payload,
                                        deadline_expired ? R_LIBRARY_FS_CLOSE_OUTCOME_DEADLINE
                                                         : R_LIBRARY_FS_CLOSE_OUTCOME_ERROR,
                                        payload->forced_error,
                                        payload->forced_event_sequence);
    }
    if (!r_library_internal_fs_handle_begin_close(payload->storage,
                                                  operations_drained,
                                                  payload,
                                                  &descriptor,
                                                  &payload_io,
                                                  &already_drained)) {
        close_panic();
    }
    if (payload_io != payload->payload_io ||
        ((payload_io == NULL) != (payload->io_prepared == NULL))) {
        close_panic();
    }
    payload->payload_io_owned = payload_io != NULL;
    if (already_drained) {
        (void)atomic_fetch_or_explicit(
            &payload->state, R_LIBRARY_FS_CLOSE_OPERATIONS_DRAINED, memory_order_relaxed);
    }
    if (payload->payload_io == NULL) {
        (void)atomic_fetch_or_explicit(
            &payload->state, R_LIBRARY_FS_CLOSE_IO_DONE, memory_order_relaxed);
    } else {
        io_submission = r_runtime_darwin_io_prepared_activate_close(&payload->io_prepared, 0);
        if (io_submission.status != R_RUNTIME_DARWIN_IO_START_OK || io_submission.request == NULL ||
            payload->io_prepared != NULL) {
            close_panic();
        }
        payload->io_request = io_submission.request;
    }
    submission = r_runtime_darwin_fs_prepared_activate_close(&payload->prepared, &descriptor);
    if (submission.status != R_RUNTIME_DARWIN_FS_START_OK || submission.request == NULL ||
        payload->prepared != NULL || descriptor >= 0) {
        close_panic();
    }
    payload->request = submission.request;
    if (payload->deadline_control != NULL) {
        deadline_control_bind(
            payload->deadline_control, payload->request, payload->io_request, execution, payload);
    }
    r_runtime_task_external_start_ready(execution);
    if (payload->deadline_control != NULL && !payload->has_forced_error) {
        r_library_internal_fs_position_deadline_activate(&payload->deadline_control->deadline);
    }
    if (deadline_expired) {
        (void)r_runtime_darwin_fs_request_deadline_expired(payload->request);
        if (payload->io_request != NULL) {
            (void)r_runtime_darwin_io_request_deadline_expired(payload->io_request);
        }
    }
    if (payload->io_request != NULL &&
        !r_runtime_darwin_io_request_set_completion(payload->io_request, io_completed, payload)) {
        close_panic();
    }
    if (!r_runtime_darwin_fs_request_set_completion(payload->request, native_completed, payload)) {
        close_panic();
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
    close_panic();
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
        close_panic();
    }
    close_panic();
}

static RRuntimeTaskStartStatus io_prepare_status(RRuntimeDarwinIoPrepareResult preparation) {
    switch (preparation.status) {
    case R_RUNTIME_DARWIN_IO_START_OK:
        return R_RUNTIME_TASK_START_OK;
    case R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED:
    case R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED:
        return R_RUNTIME_TASK_START_ALLOCATION_FAILED;
    case R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED:
    case R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT:
    case R_RUNTIME_DARWIN_IO_START_ORDERING_BLOCKED:
        close_panic();
    }
    close_panic();
}

static void abort_staged_close(RLibraryFsClosePayload *payload, RLibraryFsHandleStorage *storage) {
    r_runtime_darwin_io_prepared_abort(&payload->io_prepared);
    r_runtime_darwin_fs_prepared_abort(&payload->prepared);
    if (payload->deadline_control != NULL) {
        deadline_control_stop(payload->deadline_control);
        deadline_control_release(payload->deadline_control);
        payload->deadline_control = NULL;
    }
    payload->payload_io = NULL;
    r_library_internal_fs_handle_abort_close(storage);
}

static RStdFsTaskStartResult
close_start(RLibraryFsCloseMode mode, void *staged_owner, RStdFsDeadline deadline) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryFsClosePayload),
        _Alignof(RLibraryFsClosePayload),
        payload_move,
        payload_drop,
    };
    const RRuntimeTypeInfo result_type = {
        sizeof(RStdFsVoidResult),
        _Alignof(RStdFsVoidResult),
        NULL,
        NULL,
    };
    RLibraryFsClosePayload payload;
    RRuntimeDarwinFsPrepareResult native_preparation;
    RRuntimeDarwinIoPrepareResult io_preparation;
    RLibraryFsHandleStorage *storage;
    RRuntimeTaskPrepareResult task_preparation;
    RRuntimeTaskStartResult started;
    RRuntimeTaskStartStatus preparation_status;
    RLibraryFsCloseDeadlineStatus deadline_status;
    RStdFsTaskStartResult result = {0};
    int descriptor = -1;
    int descriptor_status;
    int payload_io_error = 0;

    if (staged_owner == NULL) {
        close_panic();
    }
    (void)memset(&payload, 0, sizeof(payload));
    payload.mode = mode;
    payload.staged_owner = staged_owner;
    payload.deadline = deadline;
    atomic_init(&payload.state, 0U);
    atomic_init(&payload.callback_count, 0U);
    if (mode == R_LIBRARY_FS_CLOSE_FILE) {
        RStdFsFile *file = staged_owner;

        if (file->storage == NULL) {
            close_panic();
        }
        storage = &file->storage->handle;
    } else {
        RStdFsDirectory *directory = staged_owner;

        if (directory->storage == NULL) {
            close_panic();
        }
        storage = &directory->storage->handle;
    }
    if (!r_library_internal_fs_handle_reserve_close(storage, &descriptor, &payload.payload_io)) {
        close_panic();
    }
    do {
        descriptor_status = fcntl(descriptor, F_GETFD);
    } while (descriptor_status < 0 && errno == EINTR);
    if (descriptor_status < 0) {
        const int native_error = errno;

        payload.preexisting_failure = 1;
        payload.has_forced_error = 1;
        payload.forced_error =
            fs_error(native_error == EBADF ? R_STD_FS_ERROR_CLOSED : R_STD_FS_ERROR_OTHER,
                     (int64_t)native_error);
        payload.forced_event_sequence = r_runtime_darwin_event_sequence_next();
    } else if (payload.payload_io != NULL && r_runtime_darwin_io_handle_terminal_close_failure(
                                                 payload.payload_io, &payload_io_error)) {
        payload.preexisting_failure = 1;
        payload.has_forced_error = 1;
        payload.forced_error = classify_native_error(payload_io_error);
        payload.forced_event_sequence = r_runtime_darwin_event_sequence_next();
    } else {
        deadline_status = check_deadline(deadline, &payload.forced_error);
        if (deadline_status != R_LIBRARY_FS_CLOSE_DEADLINE_READY) {
            payload.has_forced_error = 1;
            payload.deadline_expired = deadline_status == R_LIBRARY_FS_CLOSE_DEADLINE_EXPIRED;
            payload.forced_event_sequence = r_runtime_darwin_event_sequence_next();
        } else if (deadline.has_value) {
            payload.deadline_control = deadline_control_create(storage->allocator, deadline.value);
            if (payload.deadline_control == NULL) {
                r_library_internal_fs_handle_abort_close(storage);
                return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
            }
        }
    }
    native_preparation = r_runtime_darwin_fs_service_prepare_close(UINT64_C(0));
    preparation_status = native_prepare_status(native_preparation);
    if (preparation_status != R_RUNTIME_TASK_START_OK) {
        abort_staged_close(&payload, storage);
        return task_start_failure(preparation_status);
    }
    payload.prepared = native_preparation.prepared;
    if (payload.payload_io != NULL) {
        io_preparation = r_runtime_darwin_io_prepare_close(payload.payload_io, UINT64_C(0));
        preparation_status = io_prepare_status(io_preparation);
        if (preparation_status != R_RUNTIME_TASK_START_OK) {
            abort_staged_close(&payload, storage);
            return task_start_failure(preparation_status);
        }
        payload.io_prepared = io_preparation.prepared;
    }
    task_preparation = r_runtime_task_external_start_prepare(
        payload_type, result_type, external_start, external_cancel);
    if (task_preparation.status != R_RUNTIME_TASK_START_OK) {
        abort_staged_close(&payload, storage);
        return task_start_failure(task_preparation.status);
    }
    started = r_runtime_task_start_commit(&task_preparation.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        abort_staged_close(&payload, storage);
        return task_start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

RStdFsTaskStartResult r_library_internal_fs_close_file(RStdFsFile *file, RStdFsDeadline deadline) {
    if (file == NULL || file->storage == NULL) {
        close_panic();
    }
    return close_start(R_LIBRARY_FS_CLOSE_FILE, file, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_close_directory(RStdFsDirectory *directory,
                                                            RStdFsDeadline deadline) {
    if (directory == NULL || directory->storage == NULL) {
        close_panic();
    }
    return close_start(R_LIBRARY_FS_CLOSE_DIRECTORY, directory, deadline);
}
