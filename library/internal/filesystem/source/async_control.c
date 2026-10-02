#include "r_library_fs_internal.h"

#include "r_library_clock_internal.h"
#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_darwin_fs_lane.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef enum RLibraryFsControlMode {
    R_LIBRARY_FS_CONTROL_SEEK = 0,
    R_LIBRARY_FS_CONTROL_FLUSH,
    R_LIBRARY_FS_CONTROL_SYNC,
    R_LIBRARY_FS_CONTROL_TRY_LOCK,
    R_LIBRARY_FS_CONTROL_LOCK,
    R_LIBRARY_FS_CONTROL_UNLOCK
} RLibraryFsControlMode;

/* The kind and range of a lock operation (R-SLIB-FS-0016); type is F_RDLCK, F_WRLCK or F_UNLCK. */
typedef struct RLibraryFsControlLock {
    short type;
    uint64_t start;
    uint64_t length;
} RLibraryFsControlLock;

/* A lock that another open file holds is attempted again after 1 ms, doubling to 64 ms. */
#define R_LIBRARY_FS_LOCK_RETRY_FIRST_NANOSECONDS UINT64_C(1000000)
#define R_LIBRARY_FS_LOCK_RETRY_DOUBLINGS 6U

typedef enum RLibraryFsControlDeadlineStatus {
    R_LIBRARY_FS_CONTROL_DEADLINE_READY = 0,
    R_LIBRARY_FS_CONTROL_DEADLINE_IMMEDIATE,
    R_LIBRARY_FS_CONTROL_DEADLINE_ERROR
} RLibraryFsControlDeadlineStatus;

typedef struct RLibraryFsControl {
    _Atomic size_t references;
    RRuntimeAllocator *allocator;
    pthread_mutex_t mutex;
    RLibraryFsPositionNode position;
    RLibraryFsOperationRegistration registration;
    RLibraryFsPositionDeadline deadline_timer;
    RLibraryFsControlMode mode;
    RStdFsSeekOrigin origin;
    int64_t offset;
    RStdFsSyncLevel sync_level;
    RLibraryFsControlLock lock;
    RLibraryFsPositionDeadline retry_timer;
    uint32_t retry_attempts;
    int descriptor;
    RStdFsDeadline deadline;
    RStdFsError immediate_error;
    RStdFsError terminal_error;
    RRuntimeDarwinFsResult terminal_native_result;
    uint64_t terminal_position;
    uint64_t terminal_event_sequence;
    RRuntimeDarwinFsPreparedRequest *prepared;
    RRuntimeDarwinFsRequest *request;
    RLibraryFsHandleStorage *storage;
    RRuntimeTaskExternalExecution *execution;
    void *result;
    RLibraryFsPositionCancelReason cancel_reason;
    _Bool immediate;
    _Bool cancel_pending;
    _Bool terminal;
    _Bool terminal_is_native;
    _Bool terminal_is_position;
    _Bool backend_done;
    _Bool completion_selected;
    _Bool completion_required;
    _Bool task_cancel_reported;
    _Bool finalization_claimed;
    _Bool deadline_initialized;
    _Bool start_published;
    _Bool retry_waiting;
    _Bool retry_resubmitting;
    _Bool terminal_is_bool;
    _Bool terminal_bool;
} RLibraryFsControl;

typedef struct RLibraryFsControlPayload {
    RLibraryFsControl *control;
} RLibraryFsControlPayload;

typedef struct RLibraryFsControlFinalization {
    RRuntimeTaskExternalExecution *execution;
    void *result;
    RLibraryFsControlMode mode;
    RStdFsError error;
    RRuntimeDarwinFsResult native_result;
    uint64_t position;
    _Bool is_native;
    _Bool is_position;
    _Bool is_bool;
    _Bool bool_value;
    _Bool completion;
    _Bool override_cancellation;
} RLibraryFsControlFinalization;

static void native_completed(RRuntimeDarwinFsRequest *request, void *context);

#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
static pthread_mutex_t control_testing_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t control_testing_condition = PTHREAD_COND_INITIALIZER;
static _Bool control_testing_pause_before_publish;
static _Bool control_testing_publish_reached;
static _Bool control_testing_pause_after_logical_commit;
static _Bool control_testing_logical_commit_reached;
static _Atomic _Bool control_testing_cancel_reached;

void r_library_internal_fs_control_testing_pause_before_publish(_Bool paused) {
    if (pthread_mutex_lock(&control_testing_mutex) != 0) {
        abort();
    }
    control_testing_pause_before_publish = paused;
    if (paused) {
        control_testing_publish_reached = 0;
        atomic_store_explicit(&control_testing_cancel_reached, 0, memory_order_relaxed);
    }
    if (pthread_cond_broadcast(&control_testing_condition) != 0 ||
        pthread_mutex_unlock(&control_testing_mutex) != 0) {
        abort();
    }
}

_Bool r_library_internal_fs_control_testing_publish_reached(void) {
    _Bool reached;

    if (pthread_mutex_lock(&control_testing_mutex) != 0) {
        abort();
    }
    reached = control_testing_publish_reached;
    if (pthread_mutex_unlock(&control_testing_mutex) != 0) {
        abort();
    }
    return reached;
}

_Bool r_library_internal_fs_control_testing_cancel_reached(void) {
    return atomic_load_explicit(&control_testing_cancel_reached, memory_order_acquire);
}

void r_library_internal_fs_control_testing_pause_after_logical_commit(_Bool paused) {
    if (pthread_mutex_lock(&control_testing_mutex) != 0) {
        abort();
    }
    control_testing_pause_after_logical_commit = paused;
    if (paused) {
        control_testing_logical_commit_reached = 0;
    }
    if (pthread_cond_broadcast(&control_testing_condition) != 0 ||
        pthread_mutex_unlock(&control_testing_mutex) != 0) {
        abort();
    }
}

_Bool r_library_internal_fs_control_testing_logical_commit_reached(void) {
    _Bool reached;

    if (pthread_mutex_lock(&control_testing_mutex) != 0) {
        abort();
    }
    reached = control_testing_logical_commit_reached;
    if (pthread_mutex_unlock(&control_testing_mutex) != 0) {
        abort();
    }
    return reached;
}

static void control_testing_wait_before_publish(void) {
    if (pthread_mutex_lock(&control_testing_mutex) != 0) {
        abort();
    }
    if (control_testing_pause_before_publish) {
        control_testing_publish_reached = 1;
        if (pthread_cond_broadcast(&control_testing_condition) != 0) {
            abort();
        }
        while (control_testing_pause_before_publish) {
            if (pthread_cond_wait(&control_testing_condition, &control_testing_mutex) != 0) {
                abort();
            }
        }
    }
    if (pthread_mutex_unlock(&control_testing_mutex) != 0) {
        abort();
    }
}

static void control_testing_wait_after_logical_commit(void) {
    if (pthread_mutex_lock(&control_testing_mutex) != 0) {
        abort();
    }
    if (control_testing_pause_after_logical_commit) {
        control_testing_logical_commit_reached = 1;
        if (pthread_cond_broadcast(&control_testing_condition) != 0) {
            abort();
        }
        while (control_testing_pause_after_logical_commit) {
            if (pthread_cond_wait(&control_testing_condition, &control_testing_mutex) != 0) {
                abort();
            }
        }
    }
    if (pthread_mutex_unlock(&control_testing_mutex) != 0) {
        abort();
    }
}
#endif

_Noreturn static void control_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                    (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
}

static void control_lock(RLibraryFsControl *control) {
    if (pthread_mutex_lock(&control->mutex) != 0) {
        control_panic();
    }
}

static void control_unlock(RLibraryFsControl *control) {
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        control_panic();
    }
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

static RLibraryFsControlDeadlineStatus classify_deadline(RStdFsDeadline deadline,
                                                         RStdFsError *error) {
    RStdTimeInstantResult now;

    if (!deadline.has_value) {
        return R_LIBRARY_FS_CONTROL_DEADLINE_READY;
    }
    if (deadline.value.storage_seconds < 0 ||
        deadline.value.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
        *error = fs_error(R_STD_FS_ERROR_INVALID_OPERATION, INT64_C(0));
        return R_LIBRARY_FS_CONTROL_DEADLINE_ERROR;
    }
    now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
    if (!now.is_ok) {
        *error = fs_error(R_STD_FS_ERROR_OTHER, now.error.native_code);
        return R_LIBRARY_FS_CONTROL_DEADLINE_ERROR;
    }
    if (compare_instant(deadline.value, now.value) <= 0) {
        *error = fs_error(R_STD_FS_ERROR_TIMED_OUT, INT64_C(0));
        return R_LIBRARY_FS_CONTROL_DEADLINE_IMMEDIATE;
    }
    return R_LIBRARY_FS_CONTROL_DEADLINE_READY;
}

static RStdFsErrorCode classify_native_error(int native_error) {
    if (native_error == EBADF) {
        return R_STD_FS_ERROR_CLOSED;
    }
    if (native_error == EINVAL || native_error == ESPIPE || native_error == ENXIO) {
        return R_STD_FS_ERROR_INVALID_OPERATION;
    }
    if (native_error == EOVERFLOW || native_error == EFBIG) {
        return R_STD_FS_ERROR_FILE_TOO_LARGE;
    }
    if (native_error == EROFS) {
        return R_STD_FS_ERROR_READ_ONLY;
    }
    if (native_error == ENOSPC || native_error == EDQUOT) {
        return R_STD_FS_ERROR_NO_SPACE;
    }
    if (native_error == EACCES || native_error == EPERM) {
        return R_STD_FS_ERROR_PERMISSION_DENIED;
    }
    if (native_error == ENOMEM || native_error == EMFILE || native_error == ENFILE ||
        native_error == ENOBUFS || native_error == EAGAIN || native_error == ENOLCK) {
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

static void control_retain(void *context) {
    RLibraryFsControl *control = context;
    size_t references = atomic_load_explicit(&control->references, memory_order_relaxed);

    for (;;) {
        if (references == 0U || references == SIZE_MAX) {
            control_panic();
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
    RLibraryFsControl *control = context;
    size_t previous;

    if (control == NULL) {
        return;
    }
    previous = atomic_fetch_sub_explicit(&control->references, 1U, memory_order_acq_rel);
    if (previous == 0U) {
        control_panic();
    }
    if (previous != 1U) {
        return;
    }
    if (control->prepared != NULL || control->request != NULL || control->storage != NULL ||
        control->registration.registered || control->deadline_initialized ||
        control->deadline_timer.token != NULL || control->retry_timer.token != NULL) {
        control_panic();
    }
    if (pthread_mutex_destroy(&control->mutex) != 0) {
        control_panic();
    }
    r_runtime_allocator_deallocate(control, _Alignof(RLibraryFsControl));
}

static RLibraryFsControl *control_create(RLibraryFsHandleStorage *storage,
                                         RLibraryFsControlMode mode,
                                         RStdFsSeekOrigin origin,
                                         int64_t offset,
                                         RStdFsDeadline deadline) {
    RLibraryFsControl *control = NULL;
    RRuntimeAllocator *allocator;

    if (storage == NULL || storage->allocator == NULL) {
        control_panic();
    }
    allocator = storage->allocator;
    if (r_runtime_allocator_allocate(
            allocator, sizeof(*control), _Alignof(RLibraryFsControl), (void **)&control) !=
        R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    (void)memset(control, 0, sizeof(*control));
    if (pthread_mutex_init(&control->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(control, _Alignof(RLibraryFsControl));
        return NULL;
    }
    atomic_init(&control->references, 1U);
    control->allocator = allocator;
    control->mode = mode;
    control->origin = origin;
    control->offset = offset;
    control->deadline = deadline;
    control->cancel_reason = R_LIBRARY_FS_POSITION_CANCEL_TASK;
    return control;
}

static void payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryFsControlPayload *destination = destination_pointer;
    RLibraryFsControlPayload *source = source_pointer;

    destination->control = source->control;
    source->control = NULL;
}

static void payload_drop(void *value) {
    RLibraryFsControlPayload *payload = value;
    RLibraryFsControl *control = payload->control;

    payload->control = NULL;
    control_release(control);
}

static RRuntimeTypeInfo result_type(RLibraryFsControlMode mode) {
    if (mode == R_LIBRARY_FS_CONTROL_SEEK) {
        return (RRuntimeTypeInfo){sizeof(RStdFsU64Result), _Alignof(RStdFsU64Result), NULL, NULL};
    }
    if (mode == R_LIBRARY_FS_CONTROL_TRY_LOCK) {
        return (RRuntimeTypeInfo){
            sizeof(RStdFsBoolResult), _Alignof(RStdFsBoolResult), NULL, NULL};
    }
    return (RRuntimeTypeInfo){sizeof(RStdFsVoidResult), _Alignof(RStdFsVoidResult), NULL, NULL};
}

static void fill_error_result(RLibraryFsControlMode mode, void *result_pointer, RStdFsError error) {
    if (mode == R_LIBRARY_FS_CONTROL_SEEK) {
        RStdFsU64Result *result = result_pointer;

        (void)memset(result, 0, sizeof(*result));
        result->r_tag = UINT32_C(1);
        result->r_payload.r_err = error;
    } else if (mode == R_LIBRARY_FS_CONTROL_TRY_LOCK) {
        RStdFsBoolResult *result = result_pointer;

        (void)memset(result, 0, sizeof(*result));
        result->r_tag = UINT32_C(1);
        result->r_payload.r_err = error;
    } else {
        RStdFsVoidResult *result = result_pointer;

        (void)memset(result, 0, sizeof(*result));
        result->r_tag = UINT32_C(1);
        result->r_payload.r_err = error;
    }
}

static void fill_native_result(RLibraryFsControlMode mode,
                               void *result_pointer,
                               RRuntimeDarwinFsResult native_result) {
    const _Bool success = native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE &&
                          native_result.native_error == 0;

    if (mode == R_LIBRARY_FS_CONTROL_SEEK) {
        RStdFsU64Result *result = result_pointer;

        if (success) {
            control_panic();
        }
        (void)memset(result, 0, sizeof(*result));
        result->r_tag = UINT32_C(1);
        result->r_payload.r_err = native_result_error(native_result);
    } else if (mode == R_LIBRARY_FS_CONTROL_TRY_LOCK) {
        RStdFsBoolResult *result = result_pointer;

        if (success) {
            control_panic();
        }
        (void)memset(result, 0, sizeof(*result));
        result->r_tag = UINT32_C(1);
        result->r_payload.r_err = native_result_error(native_result);
    } else {
        RStdFsVoidResult *result = result_pointer;

        (void)memset(result, 0, sizeof(*result));
        result->r_tag = success ? UINT32_C(0) : UINT32_C(1);
        if (!success) {
            result->r_payload.r_err = native_result_error(native_result);
        }
    }
}

static void fill_bool_result(void *result_pointer, _Bool value) {
    RStdFsBoolResult *result = result_pointer;

    (void)memset(result, 0, sizeof(*result));
    result->r_tag = UINT32_C(0);
    result->r_payload.r_ok = value;
}

static void fill_position_result(void *result_pointer, uint64_t position) {
    RStdFsU64Result *result = result_pointer;

    (void)memset(result, 0, sizeof(*result));
    result->r_tag = UINT32_C(0);
    result->r_payload.r_ok = position;
}

static void control_unregister(RLibraryFsControl *control) {
    RLibraryFsHandleStorage *storage;

    control_lock(control);
    storage = control->storage;
    control->storage = NULL;
    control_unlock(control);
    if (storage != NULL) {
        r_library_internal_fs_operation_unregister(storage, &control->registration);
    }
}

static void control_destroy_deadline(RLibraryFsControl *control) {
    _Bool initialized;

    control_lock(control);
    initialized = control->deadline_initialized;
    control->deadline_initialized = 0;
    control_unlock(control);
    if (initialized) {
        r_library_internal_fs_position_deadline_destroy(&control->deadline_timer);
    }
}

static RLibraryFsControlFinalization control_claim_finalization(RLibraryFsControl *control) {
    RLibraryFsControlFinalization finalization;

    (void)memset(&finalization, 0, sizeof(finalization));
    control_lock(control);
    if (!control->backend_done || control->finalization_claimed ||
        (!control->completion_selected && !control->task_cancel_reported)) {
        control_unlock(control);
        return finalization;
    }
    control->finalization_claimed = 1;
    finalization.execution = control->execution;
    finalization.result = control->result;
    finalization.mode = control->mode;
    finalization.error = control->terminal_error;
    finalization.native_result = control->terminal_native_result;
    finalization.position = control->terminal_position;
    finalization.is_native = control->terminal_is_native;
    finalization.is_position = control->terminal_is_position;
    finalization.is_bool = control->terminal_is_bool;
    finalization.bool_value = control->terminal_bool;
    finalization.completion = control->completion_selected || control->completion_required;
    finalization.override_cancellation =
        !control->completion_selected && control->completion_required;
    control_unlock(control);
    return finalization;
}

static void control_try_finalize(RLibraryFsControl *control) {
    RLibraryFsControlFinalization finalization = control_claim_finalization(control);

    if (finalization.execution == NULL) {
        return;
    }
    if (finalization.override_cancellation &&
        !r_runtime_task_external_select_terminal_completion(finalization.execution)) {
        control_panic();
    }
    if (finalization.completion) {
        if (finalization.is_bool) {
            fill_bool_result(finalization.result, finalization.bool_value);
        } else if (finalization.is_position) {
            fill_position_result(finalization.result, finalization.position);
        } else if (finalization.is_native) {
            fill_native_result(finalization.mode, finalization.result, finalization.native_result);
        } else {
            fill_error_result(finalization.mode, finalization.result, finalization.error);
        }
    }
    r_runtime_task_external_acknowledge(finalization.execution);
}

static void control_publish_backend_done(RLibraryFsControl *control) {
    RRuntimeTaskExternalExecution *execution;
    uint64_t event_sequence;
    _Bool selected;

    control_lock(control);
    if (control->backend_done || control->execution == NULL || !control->terminal) {
        control_unlock(control);
        control_panic();
    }
    execution = control->execution;
    event_sequence = control->terminal_event_sequence;
    if (event_sequence == UINT64_C(0)) {
        control_unlock(control);
        control_panic();
    }
    control_unlock(control);
    selected = r_runtime_task_external_try_select_completion_at(execution, event_sequence);
    control_lock(control);
    if (control->backend_done) {
        control_unlock(control);
        control_panic();
    }
    control->completion_selected = selected;
    control->backend_done = 1;
    control_unlock(control);
    control_try_finalize(control);
}

static RRuntimeDarwinFsPreparedRequest *control_take_prepared(RLibraryFsControl *control) {
    RRuntimeDarwinFsPreparedRequest *prepared;

    control_lock(control);
    prepared = control->prepared;
    control->prepared = NULL;
    control_unlock(control);
    return prepared;
}

static void control_abort_prepared(RLibraryFsControl *control) {
    RRuntimeDarwinFsPreparedRequest *prepared = control_take_prepared(control);

    if (prepared != NULL) {
        r_runtime_darwin_fs_prepared_abort(&prepared);
    }
}

static void control_complete_error(RLibraryFsControl *control, RStdFsError error) {
    RRuntimeDarwinFsPreparedRequest *prepared;

    control_lock(control);
    if (control->terminal) {
        control_unlock(control);
        return;
    }
    if (control->execution == NULL) {
        control_unlock(control);
        control_panic();
    }
    control->terminal = 1;
    control->terminal_is_native = 0;
    control->terminal_error = error;
    control->terminal_event_sequence = r_runtime_darwin_event_sequence_next();
    control->completion_required = 0;
    prepared = control->prepared;
    control->prepared = NULL;
    control_unlock(control);
    if (prepared != NULL) {
        r_runtime_darwin_fs_prepared_abort(&prepared);
    }
    control_destroy_deadline(control);
    control_unregister(control);
    control_publish_backend_done(control);
}

static RStdFsError cancellation_error(RLibraryFsPositionCancelReason reason) {
    if (reason == R_LIBRARY_FS_POSITION_CANCEL_DEADLINE) {
        return fs_error(R_STD_FS_ERROR_TIMED_OUT, INT64_C(0));
    }
    return fs_error(R_STD_FS_ERROR_CANCELLED, INT64_C(0));
}

static void control_complete_pending_cancellation(RLibraryFsControl *control) {
    RLibraryFsPositionCancelReason reason;
    _Bool pending;

    control_lock(control);
    pending = control->cancel_pending && control->execution != NULL && control->start_published &&
              !control->terminal;
    reason = control->cancel_reason;
    control_unlock(control);
    if (pending) {
        control_complete_error(control, cancellation_error(reason));
    }
}

static void control_finish_lock_wait(RLibraryFsControl *control, RStdFsError error);

static void
control_position_cancel(void *context, RLibraryFsPositionCancelReason reason, _Bool active) {
    RLibraryFsControl *control = context;
    RRuntimeDarwinFsPreparedRequest *prepared = NULL;
    RRuntimeDarwinFsRequest *request = NULL;
    RLibraryFsPositionCancelReason selected_reason;
    _Bool finish = 0;
    _Bool retry_cancelled = 0;

    control_lock(control);
    if (control->terminal || (control->immediate && control->execution == NULL)) {
        control_unlock(control);
        return;
    }
    if (!control->cancel_pending) {
        control->cancel_pending = 1;
        control->cancel_reason = reason;
    }
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    if (!control->start_published) {
        atomic_store_explicit(&control_testing_cancel_reached, 1, memory_order_release);
    }
#endif
    selected_reason = control->cancel_reason;
    if (active && control->request == NULL && control->retry_waiting) {
        /* A lock waiting before its next attempt completes with the cancellation now. */
        control->retry_waiting = 0;
        retry_cancelled = 1;
    } else if (active && control->request == NULL && control->retry_resubmitting) {
        /* The resubmission observes cancel_pending and completes the lock itself. */
        control_unlock(control);
        return;
    } else if (active) {
        request = control->request;
        if (request == NULL) {
            control_unlock(control);
            control_panic();
        }
        r_runtime_darwin_fs_request_retain(request);
    } else {
        prepared = control->prepared;
        control->prepared = NULL;
        finish = control->execution != NULL && control->start_published;
    }
    control_unlock(control);
    if (retry_cancelled) {
        r_library_internal_fs_position_deadline_destroy(&control->retry_timer);
        control_finish_lock_wait(control, cancellation_error(selected_reason));
        return;
    }
    if (prepared != NULL) {
        r_runtime_darwin_fs_prepared_abort(&prepared);
    }
    if (request != NULL) {
        if (reason == R_LIBRARY_FS_POSITION_CANCEL_DEADLINE) {
            (void)r_runtime_darwin_fs_request_deadline_expired(request);
        } else {
            (void)r_runtime_darwin_fs_request_cancel(request);
        }
        r_runtime_darwin_fs_request_release(request);
    } else if (finish) {
        control_complete_pending_cancellation(control);
    }
}

/* Task cancellation, deadline and close callbacks may run after control_unregister dropped the
   registration's storage reference, and the file may be gone by then. control->storage stays
   set until that unregistration, so a reference taken under the control lock keeps the storage
   alive through the position cancellation. */
static RLibraryFsHandleStorage *
control_retain_registered_storage_locked(RLibraryFsControl *control) {
    RLibraryFsHandleStorage *storage = control->storage;

    if (storage != NULL) {
        r_library_internal_fs_handle_retain_registered(storage);
    }
    return storage;
}

static void control_cancel_position_retained(RLibraryFsControl *control,
                                             RLibraryFsHandleStorage *storage,
                                             RLibraryFsPositionCancelReason reason) {
    if (storage == NULL) {
        return;
    }
    r_library_internal_fs_position_cancel_retained(storage, &control->position, reason);
    r_library_internal_fs_handle_release(storage);
}

static void control_cancel_position_late(RLibraryFsControl *control,
                                         RLibraryFsPositionCancelReason reason) {
    RLibraryFsHandleStorage *storage;

    control_lock(control);
    storage = control_retain_registered_storage_locked(control);
    control_unlock(control);
    control_cancel_position_retained(control, storage, reason);
}

static void control_close_cancel(void *context) {
    control_cancel_position_late(context, R_LIBRARY_FS_POSITION_CANCEL_CLOSE);
}

static void control_deadline_expired(void *context) {
    control_cancel_position_late(context, R_LIBRARY_FS_POSITION_CANCEL_DEADLINE);
}

static _Bool add_signed_position(uint64_t position, int64_t offset, uint64_t *result) {
    uint64_t magnitude;

    if (offset >= 0) {
        if ((uint64_t)offset > UINT64_MAX - position) {
            return 0;
        }
        *result = position + (uint64_t)offset;
        return 1;
    }
    magnitude = (uint64_t)(-(offset + 1)) + 1U;
    if (magnitude > position) {
        return 0;
    }
    *result = position - magnitude;
    return 1;
}

static _Bool control_logical_seek_position(RLibraryFsControl *control,
                                           uint64_t current_position,
                                           uint64_t *position) {
    uint64_t base;

    if (control->origin == R_STD_FS_SEEK_ORIGIN_START) {
        base = UINT64_C(0);
    } else if (control->origin == R_STD_FS_SEEK_ORIGIN_CURRENT) {
        base = current_position;
    } else {
        control_panic();
    }
    return add_signed_position(base, control->offset, position);
}

static void control_complete_logical_seek(RLibraryFsControl *control, uint64_t current_position) {
    uint64_t position = 0U;
    _Bool success = control_logical_seek_position(control, current_position, &position);

    control_lock(control);
    if (control->terminal || control->prepared != NULL || control->request != NULL ||
        control->execution == NULL || control->origin == R_STD_FS_SEEK_ORIGIN_END) {
        control_unlock(control);
        control_panic();
    }
    if (!r_library_internal_fs_position_activation_begin(&control->position)) {
        control_unlock(control);
        return;
    }
    control->terminal = 1;
    control->terminal_is_position = success;
    control->terminal_error = fs_error(R_STD_FS_ERROR_INVALID_OPERATION, INT64_C(0));
    control->terminal_position = position;
    control->terminal_event_sequence = r_runtime_darwin_event_sequence_next();
    control->completion_required = success;
    control_unlock(control);

    r_library_internal_fs_position_activation_commit(&control->position);
    control_destroy_deadline(control);
    r_library_internal_fs_position_finish(&control->position,
                                          success ? R_LIBRARY_FS_POSITION_SET
                                                  : R_LIBRARY_FS_POSITION_KEEP,
                                          position);
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    if (success) {
        control_testing_wait_after_logical_commit();
    }
#endif
    control_unregister(control);
    control_publish_backend_done(control);
}

static void control_position_activate(void *context, uint64_t position) {
    RLibraryFsControl *control = context;
    RRuntimeDarwinFsSubmitResult submission;
    RRuntimeDarwinFsRequest *request;
    RLibraryFsControlDeadlineStatus deadline_status;
    RStdFsError deadline_error;

    if (control->deadline.has_value) {
        deadline_status = classify_deadline(control->deadline, &deadline_error);
        if (deadline_status != R_LIBRARY_FS_CONTROL_DEADLINE_READY) {
            r_library_internal_fs_position_cancel(&control->position,
                                                  R_LIBRARY_FS_POSITION_CANCEL_DEADLINE);
            if (r_library_internal_fs_position_activation_begin(&control->position)) {
                control_panic();
            }
            return;
        }
    }
    if (control->mode == R_LIBRARY_FS_CONTROL_SEEK && control->origin != R_STD_FS_SEEK_ORIGIN_END) {
        control_complete_logical_seek(control, position);
        return;
    }
    control_lock(control);
    if (control->terminal || control->prepared == NULL || control->request != NULL) {
        control_unlock(control);
        control_panic();
    }
    if (!r_library_internal_fs_position_activation_begin(&control->position)) {
        control_unlock(control);
        return;
    }
    submission = r_runtime_darwin_fs_prepared_activate(&control->prepared);
    if (submission.status != R_RUNTIME_DARWIN_FS_START_OK || submission.request == NULL ||
        control->prepared != NULL) {
        control_unlock(control);
        control_panic();
    }
    control->request = submission.request;
    request = submission.request;
    control_unlock(control);
    r_library_internal_fs_position_activation_commit(&control->position);
    if (!r_runtime_darwin_fs_request_set_completion(request, native_completed, control)) {
        control_panic();
    }
}

static RStdTimeInstant instant_after(RStdTimeInstant base, uint64_t nanoseconds) {
    const uint64_t total = (uint64_t)base.storage_nanoseconds + nanoseconds;
    RStdTimeInstant result;

    result.storage_seconds =
        base.storage_seconds + (int64_t)(total / (uint64_t)R_STD_TIME_NANOSECONDS_PER_SECOND);
    result.storage_nanoseconds = (uint32_t)(total % (uint64_t)R_STD_TIME_NANOSECONDS_PER_SECOND);
    return result;
}

/* Completes a lock whose position turn is active while no lane request is: it waited for, or
   was about to resubmit, its next attempt. */
static void control_finish_lock_wait(RLibraryFsControl *control, RStdFsError error) {
    control_lock(control);
    if (control->terminal) {
        control_unlock(control);
        return;
    }
    control->terminal = 1;
    control->terminal_is_native = 0;
    control->terminal_error = error;
    control->terminal_event_sequence = r_runtime_darwin_event_sequence_next();
    control->completion_required = 0;
    control_unlock(control);
    control_destroy_deadline(control);
    r_library_internal_fs_position_finish(
        &control->position, R_LIBRARY_FS_POSITION_KEEP, UINT64_C(0));
    control_unregister(control);
    control_publish_backend_done(control);
}

/* The retry timer of a waiting lock: one more non-blocking attempt on the lane. */
static void control_retry_fired(void *context) {
    RLibraryFsControl *control = context;
    RRuntimeDarwinFsPrepareResult preparation;
    RRuntimeDarwinFsSubmitResult submission;
    RRuntimeDarwinFsRequest *request;
    RLibraryFsControlLock lock;
    RStdFsError error;
    int descriptor;

    control_lock(control);
    if (!control->retry_waiting || control->terminal) {
        control_unlock(control);
        return;
    }
    control->retry_waiting = 0;
    control->retry_resubmitting = 1;
    descriptor = control->descriptor;
    lock = control->lock;
    control_unlock(control);
    r_library_internal_fs_position_deadline_destroy(&control->retry_timer);
    preparation = r_runtime_darwin_fs_service_prepare_ofd_lock(
        descriptor, lock.type, (off_t)lock.start, (off_t)lock.length, UINT64_C(0));
    control_lock(control);
    control->retry_resubmitting = 0;
    if (control->cancel_pending || preparation.prepared == NULL) {
        if (control->cancel_pending) {
            error = cancellation_error(control->cancel_reason);
        } else if (preparation.status == R_RUNTIME_DARWIN_FS_START_RUNTIME_STOPPING) {
            error = fs_error(R_STD_FS_ERROR_CANCELLED, INT64_C(0));
        } else {
            error = fs_error(R_STD_FS_ERROR_RESOURCE_EXHAUSTED, (int64_t)preparation.native_error);
        }
        control_unlock(control);
        if (preparation.prepared != NULL) {
            r_runtime_darwin_fs_prepared_abort(&preparation.prepared);
        }
        control_finish_lock_wait(control, error);
        return;
    }
    submission = r_runtime_darwin_fs_prepared_activate(&preparation.prepared);
    if (submission.status != R_RUNTIME_DARWIN_FS_START_OK || submission.request == NULL) {
        control_unlock(control);
        if (preparation.prepared != NULL) {
            r_runtime_darwin_fs_prepared_abort(&preparation.prepared);
        }
        control_finish_lock_wait(control,
                                 fs_error(R_STD_FS_ERROR_RESOURCE_EXHAUSTED, INT64_C(0)));
        return;
    }
    control->request = submission.request;
    request = submission.request;
    control_unlock(control);
    if (!r_runtime_darwin_fs_request_set_completion(request, native_completed, control)) {
        control_panic();
    }
}

/* Waits before the next attempt of a lock that another open file holds. False when the lock
   completes instead: a cancellation or deadline is pending, or no timer could be made. */
static _Bool control_schedule_retry(RLibraryFsControl *control, RRuntimeDarwinFsRequest *request) {
    const uint32_t doublings = control->retry_attempts < R_LIBRARY_FS_LOCK_RETRY_DOUBLINGS
                                   ? control->retry_attempts
                                   : R_LIBRARY_FS_LOCK_RETRY_DOUBLINGS;
    RStdTimeInstantResult now =
        r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());

    if (!now.is_ok) {
        return 0;
    }
    control_lock(control);
    if (control->terminal || control->request != request || control->cancel_pending ||
        control->execution == NULL) {
        control_unlock(control);
        return 0;
    }
    control_unlock(control);
    if (!r_library_internal_fs_position_deadline_initialize(
            &control->retry_timer,
            control->allocator,
            instant_after(now.value, R_LIBRARY_FS_LOCK_RETRY_FIRST_NANOSECONDS << doublings),
            control_retry_fired,
            control_retain,
            control_release,
            control)) {
        return 0;
    }
    control_lock(control);
    if (control->cancel_pending || control->terminal) {
        control_unlock(control);
        r_library_internal_fs_position_deadline_destroy(&control->retry_timer);
        return 0;
    }
    control->retry_attempts += 1U;
    control->retry_waiting = 1;
    control->request = NULL;
    r_library_internal_fs_position_deadline_activate(&control->retry_timer);
    control_unlock(control);
    r_runtime_darwin_fs_request_release(request);
    return 1;
}

static void native_completed(RRuntimeDarwinFsRequest *request, void *context) {
    RLibraryFsControl *control = context;
    RRuntimeDarwinFsResult native_result;
    RLibraryFsPositionUpdate update = R_LIBRARY_FS_POSITION_KEEP;
    uint64_t position = 0U;
    _Bool native_success;
    _Bool contention;
    _Bool position_success = 0;
    _Bool position_invalid = 0;

    control_retain(control);
    native_result = r_runtime_darwin_fs_request_wait(request);
    native_success = native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE &&
                     native_result.native_error == 0;
    /* Another open file holds a conflicting lock. */
    contention = (control->mode == R_LIBRARY_FS_CONTROL_TRY_LOCK ||
                  control->mode == R_LIBRARY_FS_CONTROL_LOCK) &&
                 native_result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE &&
                 (native_result.native_error == EAGAIN || native_result.native_error == EACCES);
    if (control->mode == R_LIBRARY_FS_CONTROL_LOCK && contention &&
        control_schedule_retry(control, request)) {
        control_release(control);
        return;
    }
    if (control->mode == R_LIBRARY_FS_CONTROL_SEEK && native_success) {
        if (native_result.metadata.st_size < 0 ||
            !add_signed_position(
                (uint64_t)native_result.metadata.st_size, control->offset, &position)) {
            position_invalid = 1;
        } else {
            position_success = 1;
        }
    }
    control_lock(control);
    if (control->terminal || control->request != request || control->prepared != NULL ||
        control->execution == NULL) {
        control_unlock(control);
        control_panic();
    }
    control->terminal = 1;
    control->terminal_native_result = native_result;
    control->terminal_is_native = !position_success && !position_invalid;
    control->terminal_is_position = position_success;
    control->terminal_position = position;
    control->terminal_event_sequence = native_result.terminal_event_sequence;
    if (position_invalid) {
        control->terminal_error = fs_error(R_STD_FS_ERROR_INVALID_OPERATION, INT64_C(0));
    }
    if (control->mode == R_LIBRARY_FS_CONTROL_TRY_LOCK && (native_success || contention)) {
        control->terminal_is_native = 0;
        control->terminal_is_bool = 1;
        control->terminal_bool = native_success;
    } else if (control->mode == R_LIBRARY_FS_CONTROL_LOCK && contention) {
        control->terminal_is_native = 0;
        control->terminal_error =
            control->cancel_pending
                ? cancellation_error(control->cancel_reason)
                : fs_error(R_STD_FS_ERROR_RESOURCE_EXHAUSTED, (int64_t)native_result.native_error);
    }
    control->completion_required =
        position_success ||
        ((control->mode == R_LIBRARY_FS_CONTROL_FLUSH || control->mode == R_LIBRARY_FS_CONTROL_SYNC ||
          control->mode == R_LIBRARY_FS_CONTROL_TRY_LOCK ||
          control->mode == R_LIBRARY_FS_CONTROL_LOCK ||
          control->mode == R_LIBRARY_FS_CONTROL_UNLOCK) &&
         native_success && native_result.committed);
    control->request = NULL;
    control_unlock(control);
    if (position_success) {
        update = R_LIBRARY_FS_POSITION_SET;
    }
    control_destroy_deadline(control);
    r_library_internal_fs_position_finish(&control->position, update, position);
    control_unregister(control);
    r_runtime_darwin_fs_request_release(request);
    control_publish_backend_done(control);
    control_release(control);
}

static void external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryFsControlPayload *payload = payload_pointer;
    RLibraryFsControl *control = payload->control;
    RLibraryFsHandleStorage *storage;

    if (control == NULL) {
        return;
    }
    control_retain(control);
    control_lock(control);
    if (control->execution != execution) {
        control_unlock(control);
        control_panic();
    }
    control->task_cancel_reported = 1;
    storage = control_retain_registered_storage_locked(control);
    control_unlock(control);
    if (control->immediate) {
        if (storage != NULL) {
            r_library_internal_fs_handle_release(storage);
        }
        control_try_finalize(control);
    } else {
        control_cancel_position_retained(control, storage, R_LIBRARY_FS_POSITION_CANCEL_TASK);
        control_complete_pending_cancellation(control);
        control_try_finalize(control);
    }
    control_release(control);
}

static void external_start(RRuntimeTaskExternalExecution *execution,
                           void *payload_pointer,
                           void *result_pointer) {
    RLibraryFsControlPayload *payload = payload_pointer;
    RLibraryFsControl *control = payload->control;
    uint64_t immediate_event_sequence;
    _Bool immediate_selected;
    _Bool cancel_pending;

    if (control == NULL) {
        control_panic();
    }
    control_retain(control);
    control_lock(control);
    if (control->execution != NULL || control->result != NULL) {
        control_unlock(control);
        control_panic();
    }
    control->execution = execution;
    control->result = result_pointer;
    control_unlock(control);
    if (control->immediate) {
        control_lock(control);
        immediate_event_sequence = control->terminal_event_sequence;
        if (immediate_event_sequence == UINT64_C(0)) {
            control_unlock(control);
            control_panic();
        }
        control_unlock(control);
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
        control_testing_wait_before_publish();
#endif
        immediate_selected =
            r_runtime_task_external_try_select_completion_at(execution, immediate_event_sequence);
        control_lock(control);
        control->terminal = 1;
        control->terminal_is_native = 0;
        control->terminal_error = control->immediate_error;
        control->completion_selected = immediate_selected;
        control->backend_done = 1;
        control_unlock(control);
        r_runtime_task_external_start_ready(execution);
        control_try_finalize(control);
        control_release(control);
        return;
    }
    r_runtime_task_external_start_ready(execution);
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    control_testing_wait_before_publish();
#endif
    control_lock(control);
    cancel_pending = control->cancel_pending;
    if (!cancel_pending && control->deadline_initialized) {
        r_library_internal_fs_position_deadline_activate(&control->deadline_timer);
    }
    control_unlock(control);
    r_library_internal_fs_position_publish(&control->position);
    control_lock(control);
    control->start_published = 1;
    cancel_pending = control->cancel_pending;
    control_unlock(control);
    if (cancel_pending) {
        control_complete_pending_cancellation(control);
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
    control_panic();
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
        control_panic();
    }
    control_panic();
}

static void control_start_failed(RLibraryFsControl *control) {
    control_lock(control);
    control->terminal = 1;
    control_unlock(control);
    control_abort_prepared(control);
    control_destroy_deadline(control);
    r_library_internal_fs_position_abort(&control->position);
    control_unregister(control);
    control_release(control);
}

static RStdFsTaskStartResult start_task(RLibraryFsControl *control) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryFsControlPayload),
        _Alignof(RLibraryFsControlPayload),
        payload_move,
        payload_drop,
    };
    RLibraryFsControlPayload payload = {control};
    RRuntimeTaskPrepareResult preparation = r_runtime_task_external_start_prepare(
        payload_type, result_type(control->mode), external_start, external_cancel);
    RRuntimeTaskStartResult started;
    RStdFsTaskStartResult result = {0};

    if (preparation.status != R_RUNTIME_TASK_START_OK) {
        control_start_failed(control);
        return task_start_failure(preparation.status);
    }
    started = r_runtime_task_start_commit(&preparation.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        control_start_failed(control);
        return task_start_failure(started.status);
    }
    if (payload.control != NULL) {
        control_panic();
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

static void control_make_immediate(RLibraryFsControl *control, RStdFsError error) {
    control_lock(control);
    if (control->immediate || control->terminal_event_sequence != UINT64_C(0)) {
        control_unlock(control);
        control_panic();
    }
    control->immediate = 1;
    control->immediate_error = error;
    control->terminal_event_sequence = r_runtime_darwin_event_sequence_next();
    control_unlock(control);
    control_abort_prepared(control);
    control_destroy_deadline(control);
    r_library_internal_fs_position_abort(&control->position);
    control_unregister(control);
}

static _Bool is_lock_mode(RLibraryFsControlMode mode) {
    return mode == R_LIBRARY_FS_CONTROL_TRY_LOCK || mode == R_LIBRARY_FS_CONTROL_LOCK ||
           mode == R_LIBRARY_FS_CONTROL_UNLOCK;
}

/* A lock range reaches at most the largest off_t; zero length reaches past every end. */
static _Bool valid_lock(const RLibraryFsControlLock *lock) {
    if (lock->type != F_RDLCK && lock->type != F_WRLCK && lock->type != F_UNLCK) {
        return 0;
    }
    if (lock->start > (uint64_t)INT64_MAX || lock->length > (uint64_t)INT64_MAX) {
        return 0;
    }
    return lock->length == UINT64_C(0) || lock->start <= (uint64_t)INT64_MAX - lock->length;
}

/* A shared lock needs read access and an exclusive lock write access; unlock needs neither. */
static _Bool lock_access_allowed(short type, RStdFsAccess access) {
    if (type == F_RDLCK) {
        return access != R_STD_FS_ACCESS_WRITE;
    }
    if (type == F_WRLCK) {
        return access != R_STD_FS_ACCESS_READ;
    }
    return 1;
}

static _Bool valid_seek_origin(RStdFsSeekOrigin origin, int64_t offset) {
    if (origin == R_STD_FS_SEEK_ORIGIN_START) {
        return offset >= 0;
    }
    return origin == R_STD_FS_SEEK_ORIGIN_CURRENT || origin == R_STD_FS_SEEK_ORIGIN_END;
}

static RStdFsTaskStartResult start_control(RLibraryFsControlMode mode,
                                           const RStdFsFile *file,
                                           RStdFsSeekOrigin origin,
                                           int64_t offset,
                                           RStdFsSyncLevel sync_level,
                                           const RLibraryFsControlLock *lock,
                                           RStdFsDeadline deadline) {
    RLibraryFsHandleStorage *storage = r_library_internal_fs_file_handle_storage(file);
    RLibraryFsControl *control;
    RRuntimeDarwinFsPrepareResult native_preparation;
    RRuntimeTaskStartStatus preparation_status;
    RLibraryFsControlDeadlineStatus deadline_status;
    RStdFsError deadline_error;
    RStdFsAccess access = R_STD_FS_ACCESS_READ;
    int descriptor = -1;
    _Bool append = 0;

    if (storage == NULL) {
        control_panic();
    }
    control = control_create(storage, mode, origin, offset, deadline);
    if (control == NULL) {
        return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    control->sync_level = sync_level;
    if (lock != NULL) {
        control->lock = *lock;
    }
    if (is_lock_mode(mode) && (lock == NULL || !valid_lock(lock))) {
        control_make_immediate(control, fs_error(R_STD_FS_ERROR_INVALID_OPERATION, INT64_C(0)));
        return start_task(control);
    }
    if (mode == R_LIBRARY_FS_CONTROL_SEEK && !valid_seek_origin(origin, offset)) {
        control_make_immediate(control, fs_error(R_STD_FS_ERROR_INVALID_OPERATION, INT64_C(0)));
        return start_task(control);
    }
    if (mode == R_LIBRARY_FS_CONTROL_SYNC && sync_level != R_STD_FS_SYNC_LEVEL_BARRIER &&
        sync_level != R_STD_FS_SYNC_LEVEL_DEVICE && sync_level != R_STD_FS_SYNC_LEVEL_MEDIA) {
        control_make_immediate(control, fs_error(R_STD_FS_ERROR_INVALID_OPERATION, INT64_C(0)));
        return start_task(control);
    }
    if (!r_library_internal_fs_position_reserve(storage,
                                                &control->position,
                                                &control->registration,
                                                control_position_activate,
                                                control_position_cancel,
                                                control,
                                                control_close_cancel,
                                                control_retain,
                                                control_release,
                                                &descriptor,
                                                &access,
                                                &append)) {
        control_make_immediate(control, fs_error(R_STD_FS_ERROR_CLOSED, INT64_C(0)));
        return start_task(control);
    }
    control_lock(control);
    control->storage = storage;
    control_unlock(control);
    (void)append;
    control_lock(control);
    control->descriptor = descriptor;
    control_unlock(control);
    if (is_lock_mode(mode) && !lock_access_allowed(control->lock.type, access)) {
        control_make_immediate(control, fs_error(R_STD_FS_ERROR_INVALID_OPERATION, INT64_C(0)));
        return start_task(control);
    }
    if ((mode == R_LIBRARY_FS_CONTROL_FLUSH || mode == R_LIBRARY_FS_CONTROL_SYNC) &&
        access == R_STD_FS_ACCESS_READ) {
        control_make_immediate(control, fs_error(R_STD_FS_ERROR_INVALID_OPERATION, INT64_C(0)));
        return start_task(control);
    }
    deadline_status = classify_deadline(deadline, &deadline_error);
    if (deadline_status != R_LIBRARY_FS_CONTROL_DEADLINE_READY) {
        control_make_immediate(control, deadline_error);
        return start_task(control);
    }
    if (deadline.has_value) {
        if (!r_library_internal_fs_position_deadline_initialize(&control->deadline_timer,
                                                                control->allocator,
                                                                control->deadline.value,
                                                                control_deadline_expired,
                                                                control_retain,
                                                                control_release,
                                                                control)) {
            control_start_failed(control);
            return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
        }
        control_lock(control);
        control->deadline_initialized = 1;
        control_unlock(control);
    }
    if (mode == R_LIBRARY_FS_CONTROL_SEEK && origin != R_STD_FS_SEEK_ORIGIN_END) {
        return start_task(control);
    }
    if (mode == R_LIBRARY_FS_CONTROL_SEEK) {
        native_preparation =
            r_runtime_darwin_fs_service_prepare_file_metadata(descriptor, UINT64_C(0));
    } else if (is_lock_mode(mode)) {
        native_preparation = r_runtime_darwin_fs_service_prepare_ofd_lock(descriptor,
                                                                          control->lock.type,
                                                                          (off_t)control->lock.start,
                                                                          (off_t)control->lock.length,
                                                                          UINT64_C(0));
    } else if (mode == R_LIBRARY_FS_CONTROL_SYNC && sync_level == R_STD_FS_SYNC_LEVEL_BARRIER) {
        native_preparation =
            r_runtime_darwin_fs_service_prepare_barrier_fsync(descriptor, UINT64_C(0));
    } else if (mode == R_LIBRARY_FS_CONTROL_SYNC && sync_level == R_STD_FS_SYNC_LEVEL_DEVICE) {
        native_preparation = r_runtime_darwin_fs_service_prepare_fsync(descriptor, 0, UINT64_C(0));
    } else {
        native_preparation = r_runtime_darwin_fs_service_prepare_fsync(descriptor, 1, UINT64_C(0));
    }
    if (native_preparation.status == R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED &&
        native_preparation.native_error == EBADF) {
        control_make_immediate(
            control, fs_error(R_STD_FS_ERROR_CLOSED, (int64_t)native_preparation.native_error));
        return start_task(control);
    }
    preparation_status = native_prepare_status(native_preparation);
    if (preparation_status != R_RUNTIME_TASK_START_OK) {
        control_start_failed(control);
        return task_start_failure(preparation_status);
    }
    control_lock(control);
    if (control->cancel_pending) {
        control_unlock(control);
        r_runtime_darwin_fs_prepared_abort(&native_preparation.prepared);
    } else {
        control->prepared = native_preparation.prepared;
        native_preparation.prepared = NULL;
        control_unlock(control);
    }
    return start_task(control);
}

RStdFsTaskStartResult r_library_internal_fs_seek(const RStdFsFile *file,
                                                 RStdFsSeekOrigin origin,
                                                 int64_t offset,
                                                 RStdFsDeadline deadline) {
    return start_control(
        R_LIBRARY_FS_CONTROL_SEEK, file, origin, offset, R_STD_FS_SYNC_LEVEL_MEDIA, NULL, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_flush(const RStdFsFile *file, RStdFsDeadline deadline) {
    return start_control(R_LIBRARY_FS_CONTROL_FLUSH,
                         file,
                         R_STD_FS_SEEK_ORIGIN_START,
                         INT64_C(0),
                         R_STD_FS_SYNC_LEVEL_MEDIA,
                         NULL,
                         deadline);
}

RStdFsTaskStartResult
r_library_internal_fs_sync(const RStdFsFile *file, RStdFsSyncLevel level, RStdFsDeadline deadline) {
    return start_control(R_LIBRARY_FS_CONTROL_SYNC,
                         file,
                         R_STD_FS_SEEK_ORIGIN_START,
                         INT64_C(0),
                         level,
                         NULL,
                         deadline);
}

static RStdFsTaskStartResult start_lock(RLibraryFsControlMode mode,
                                        const RStdFsFile *file,
                                        short type,
                                        uint64_t start,
                                        uint64_t length,
                                        RStdFsDeadline deadline) {
    const RLibraryFsControlLock lock = {type, start, length};

    return start_control(mode,
                         file,
                         R_STD_FS_SEEK_ORIGIN_START,
                         INT64_C(0),
                         R_STD_FS_SYNC_LEVEL_MEDIA,
                         &lock,
                         deadline);
}

static short lock_type_of(RStdFsLockKind kind) {
    if (kind == R_STD_FS_LOCK_KIND_SHARED) {
        return F_RDLCK;
    }
    if (kind == R_STD_FS_LOCK_KIND_EXCLUSIVE) {
        return F_WRLCK;
    }
    return (short)-1;
}

RStdFsTaskStartResult r_library_internal_fs_try_lock(const RStdFsFile *file,
                                                     RStdFsLockKind kind,
                                                     uint64_t start,
                                                     uint64_t length,
                                                     RStdFsDeadline deadline) {
    return start_lock(
        R_LIBRARY_FS_CONTROL_TRY_LOCK, file, lock_type_of(kind), start, length, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_lock(const RStdFsFile *file,
                                                 RStdFsLockKind kind,
                                                 uint64_t start,
                                                 uint64_t length,
                                                 RStdFsDeadline deadline) {
    return start_lock(R_LIBRARY_FS_CONTROL_LOCK, file, lock_type_of(kind), start, length, deadline);
}

RStdFsTaskStartResult r_library_internal_fs_unlock(const RStdFsFile *file,
                                                   uint64_t start,
                                                   uint64_t length,
                                                   RStdFsDeadline deadline) {
    return start_lock(R_LIBRARY_FS_CONTROL_UNLOCK, file, F_UNLCK, start, length, deadline);
}
