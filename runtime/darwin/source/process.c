#include "r_runtime_darwin_process.h"

#include "r_runtime_darwin_event.h"
#include "r_runtime_darwin_signal.h"

#include <dispatch/dispatch.h>
#include <errno.h>
#include <fcntl.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct RRuntimeDarwinProcessService {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    RRuntimeAllocator *allocator;
    dispatch_queue_t submission_queue;
    RRuntimeDarwinProcessChild *children;
    size_t preparation_count;
    size_t active_spawn_count;
    size_t active_wait_count;
    size_t source_count;
    size_t pipe_cleanup_count;
    uint64_t reap_count;
    _Bool running;
    _Bool stopping;
} RRuntimeDarwinProcessService;

struct RRuntimeDarwinProcessChild {
    RRuntimeAllocator *allocator;
    RRuntimeDarwinProcessChild *next;
    RRuntimeDarwinIoHandle *pipes[3];
    dispatch_source_t source;
    RRuntimeDarwinProcessSpawn *rollback_request;
    RRuntimeDarwinProcessWait *wait_request;
    size_t references;
    pid_t process_id;
    int wait_status;
    uint64_t reap_sequence;
    uint64_t reap_continuous_nanoseconds;
    _Bool listed;
    _Bool reaped;
    _Bool view_alive;
};

struct RRuntimeDarwinProcessWait {
    RRuntimeAllocator *allocator;
    RRuntimeDarwinProcessChild *child;
    dispatch_source_t deadline_source;
    RRuntimeDarwinProcessWaitCompletionFn completion;
    void *completion_context;
    RRuntimeDarwinProcessWaitResult result;
    size_t references;
    uint64_t continuous_deadline_nanoseconds;
    _Bool deadline_activated;
    _Bool deadline_cancel_pending;
    _Bool bound;
    _Bool activated;
    _Bool terminal_selected;
    _Bool completion_delivered;
};

struct RRuntimeDarwinProcessSpawn {
    RRuntimeAllocator *allocator;
    pthread_mutex_t mutex;
    RRuntimeDarwinProcessSpawnOptions options;
    posix_spawn_file_actions_t file_actions;
    posix_spawnattr_t attributes;
    RRuntimeDarwinProcessChild *child;
    dispatch_source_t deadline_source;
    RRuntimeDarwinProcessCommitFn commit;
    RRuntimeDarwinProcessCompletionFn completion;
    void *callback_context;
    RRuntimeDarwinProcessSpawnResult result;
    uint64_t continuous_deadline_nanoseconds;
    int child_descriptors[3];
    int current_directory;
    _Bool file_actions_initialized;
    _Bool attributes_initialized;
    _Bool deadline_activated;
    _Bool deadline_cancel_pending;
    _Bool rollback_source_pending;
    _Bool activated;
    _Bool submission_done;
    _Bool terminal_selected;
    _Bool creation_commit_arbitrated;
    _Bool completion_delivered;
    _Bool child_taken;
};

static RRuntimeDarwinProcessService process_service = {
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    NULL,
    NULL,
    NULL,
    0U,
    0U,
    0U,
    0U,
    0U,
    UINT64_C(0),
    0,
    0,
};

static void spawn_try_deliver(RRuntimeDarwinProcessSpawn *request);
static void wait_try_deliver(RRuntimeDarwinProcessWait *request);

#if defined(R_RUNTIME_DARWIN_PROCESS_TESTING)
static _Atomic int process_testing_failure_stage;
static _Atomic int process_testing_failure_error;
static pthread_mutex_t process_testing_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t process_testing_condition = PTHREAD_COND_INITIALIZER;
static _Bool process_testing_pause_armed;
static _Bool process_testing_pause_reached;
static _Bool process_testing_pause_released;
static uint64_t process_testing_deadline_count;

void r_runtime_darwin_process_testing_fail_next(RRuntimeDarwinProcessFailureStage stage,
                                                int native_error) {
    atomic_store_explicit(&process_testing_failure_error, native_error, memory_order_relaxed);
    atomic_store_explicit(&process_testing_failure_stage, (int)stage, memory_order_release);
}

static _Bool process_testing_take_failure(RRuntimeDarwinProcessFailureStage stage,
                                          int *native_error) {
    int expected = (int)stage;

    if (!atomic_compare_exchange_strong_explicit(&process_testing_failure_stage,
                                                 &expected,
                                                 (int)R_RUNTIME_DARWIN_PROCESS_FAIL_NONE,
                                                 memory_order_acq_rel,
                                                 memory_order_acquire)) {
        return 0;
    }
    *native_error =
        atomic_exchange_explicit(&process_testing_failure_error, ENOMEM, memory_order_relaxed);
    return 1;
}

void r_runtime_darwin_process_testing_pause_next_before_commit(void) {
    if (pthread_mutex_lock(&process_testing_mutex) != 0) {
        abort();
    }
    if (process_testing_pause_armed || process_testing_pause_reached) {
        (void)pthread_mutex_unlock(&process_testing_mutex);
        abort();
    }
    process_testing_pause_armed = 1;
    process_testing_pause_released = 0;
    if (pthread_mutex_unlock(&process_testing_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_process_testing_wait_before_commit(void) {
    if (pthread_mutex_lock(&process_testing_mutex) != 0) {
        abort();
    }
    while (!process_testing_pause_reached) {
        if (pthread_cond_wait(&process_testing_condition, &process_testing_mutex) != 0) {
            abort();
        }
    }
    if (pthread_mutex_unlock(&process_testing_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_process_testing_release_before_commit(void) {
    if (pthread_mutex_lock(&process_testing_mutex) != 0) {
        abort();
    }
    if (!process_testing_pause_reached) {
        (void)pthread_mutex_unlock(&process_testing_mutex);
        abort();
    }
    process_testing_pause_released = 1;
    if (pthread_cond_broadcast(&process_testing_condition) != 0) {
        (void)pthread_mutex_unlock(&process_testing_mutex);
        abort();
    }
    if (pthread_mutex_unlock(&process_testing_mutex) != 0) {
        abort();
    }
}

static void process_testing_pause(void) {
    if (pthread_mutex_lock(&process_testing_mutex) != 0) {
        abort();
    }
    if (process_testing_pause_armed) {
        process_testing_pause_armed = 0;
        process_testing_pause_reached = 1;
        if (pthread_cond_broadcast(&process_testing_condition) != 0) {
            (void)pthread_mutex_unlock(&process_testing_mutex);
            abort();
        }
        while (!process_testing_pause_released) {
            if (pthread_cond_wait(&process_testing_condition, &process_testing_mutex) != 0) {
                abort();
            }
        }
        process_testing_pause_reached = 0;
        process_testing_pause_released = 0;
    }
    if (pthread_mutex_unlock(&process_testing_mutex) != 0) {
        abort();
    }
}

static void process_testing_record_deadline(void) {
    if (pthread_mutex_lock(&process_testing_mutex) != 0) {
        abort();
    }
    process_testing_deadline_count += UINT64_C(1);
    if (pthread_cond_broadcast(&process_testing_condition) != 0) {
        (void)pthread_mutex_unlock(&process_testing_mutex);
        abort();
    }
    if (pthread_mutex_unlock(&process_testing_mutex) != 0) {
        abort();
    }
}

uint64_t r_runtime_darwin_process_testing_deadline_count(void) {
    uint64_t count;

    if (pthread_mutex_lock(&process_testing_mutex) != 0) {
        abort();
    }
    count = process_testing_deadline_count;
    if (pthread_mutex_unlock(&process_testing_mutex) != 0) {
        abort();
    }
    return count;
}

void r_runtime_darwin_process_testing_wait_deadline_after(uint64_t previous_count) {
    if (pthread_mutex_lock(&process_testing_mutex) != 0) {
        abort();
    }
    while (process_testing_deadline_count <= previous_count) {
        if (pthread_cond_wait(&process_testing_condition, &process_testing_mutex) != 0) {
            abort();
        }
    }
    if (pthread_mutex_unlock(&process_testing_mutex) != 0) {
        abort();
    }
}
#else
static _Bool process_testing_take_failure(int stage, int *native_error) {
    (void)stage;
    (void)native_error;
    return 0;
}

static void process_testing_pause(void) {
}

static void process_testing_record_deadline(void) {
}
#endif

static RRuntimeDarwinProcessStartResult
process_start_result(RRuntimeDarwinProcessStartStatus status, int native_error) {
    return (RRuntimeDarwinProcessStartResult){status, native_error};
}

static RRuntimeDarwinProcessPrepareResult
process_prepare_result(RRuntimeDarwinProcessSpawn *request,
                       RRuntimeDarwinProcessStartStatus status,
                       int native_error) {
    return (RRuntimeDarwinProcessPrepareResult){request, status, native_error};
}

static uint64_t continuous_nanoseconds(void) {
    mach_timebase_info_data_t timebase = {0U, 0U};
    const uint64_t ticks = mach_continuous_time();
    uint64_t quotient;
    uint64_t remainder;

    if (mach_timebase_info(&timebase) != KERN_SUCCESS || timebase.numer == 0U ||
        timebase.denom == 0U) {
        abort();
    }
    quotient = ticks / (uint64_t)timebase.denom;
    remainder = ticks % (uint64_t)timebase.denom;
    if (quotient > UINT64_MAX / (uint64_t)timebase.numer) {
        return UINT64_MAX;
    }
    quotient *= (uint64_t)timebase.numer;
    remainder = (remainder * (uint64_t)timebase.numer) / (uint64_t)timebase.denom;
    return quotient > UINT64_MAX - remainder ? UINT64_MAX : quotient + remainder;
}

static uint64_t deadline_from_timeout(uint64_t timeout_nanoseconds) {
    const uint64_t now = continuous_nanoseconds();

    return timeout_nanoseconds > UINT64_MAX - now ? UINT64_MAX : now + timeout_nanoseconds;
}

static dispatch_source_t wait_select_locked(RRuntimeDarwinProcessWait *request,
                                            RRuntimeDarwinProcessTerminalEvent event,
                                            uint64_t event_sequence,
                                            int native_error,
                                            int wait_status,
                                            _Bool reaped) {
    dispatch_source_t deadline_source = NULL;

    if (request == NULL || event_sequence == UINT64_C(0) || request->completion_delivered) {
        abort();
    }
    if (request->terminal_selected && request->result.terminal_event_sequence <= event_sequence) {
        return NULL;
    }
    request->result = (RRuntimeDarwinProcessWaitResult){
        event,
        native_error,
        wait_status,
        event_sequence,
        reaped,
    };
    request->terminal_selected = 1;
    if (request->child != NULL && request->child->wait_request == request) {
        request->child->wait_request = NULL;
    }
    if (request->deadline_source != NULL && request->deadline_activated &&
        !request->deadline_cancel_pending) {
        request->deadline_cancel_pending = 1;
        deadline_source = request->deadline_source;
    }
    return deadline_source;
}

static void wait_try_deliver(RRuntimeDarwinProcessWait *request) {
    RRuntimeDarwinProcessWaitCompletionFn completion = NULL;
    void *context = NULL;

    if (request == NULL || pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (request->activated && request->terminal_selected && !request->deadline_cancel_pending &&
        request->deadline_source == NULL && !request->completion_delivered) {
        if (!request->bound || request->completion == NULL) {
            (void)pthread_mutex_unlock(&process_service.mutex);
            abort();
        }
        request->completion_delivered = 1;
        completion = request->completion;
        context = request->completion_context;
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    if (completion != NULL) {
        completion(request, context);
    }
}

static void wait_deadline_cancelled(void *context) {
    RRuntimeDarwinProcessWait *request = context;
    dispatch_source_t source;

    if (request == NULL || pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    source = request->deadline_source;
    if (source == NULL || !request->deadline_cancel_pending) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    request->deadline_source = NULL;
    request->deadline_cancel_pending = 0;
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    dispatch_release(source);
    wait_try_deliver(request);
}

static void wait_deadline_fired(void *context) {
    RRuntimeDarwinProcessWait *request = context;
    dispatch_source_t source;
    const uint64_t event_sequence = r_runtime_darwin_event_sequence_next();

    if (request == NULL || pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    source = wait_select_locked(
        request, R_RUNTIME_DARWIN_PROCESS_TERMINAL_TIMED_OUT, event_sequence, 0, 0, 0);
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    if (source != NULL) {
        dispatch_source_cancel(source);
    }
    wait_try_deliver(request);
}

static void wait_dispose_unactivated_deadline(RRuntimeDarwinProcessWait *request) {
    dispatch_source_t source;

    source = request->deadline_source;
    request->deadline_source = NULL;
    if (source == NULL) {
        return;
    }
    dispatch_source_set_event_handler_f(source, NULL);
    dispatch_source_set_cancel_handler_f(source, NULL);
    dispatch_set_context(source, NULL);
    dispatch_source_cancel(source);
    dispatch_activate(source);
    dispatch_release(source);
}

static void wait_deallocate(RRuntimeDarwinProcessWait *request) {
    if (request == NULL || request->references != 0U || request->child != NULL ||
        request->deadline_source != NULL || request->deadline_cancel_pending) {
        abort();
    }
    r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinProcessWait));
}

static void wait_retain_locked(RRuntimeDarwinProcessWait *request) {
    if (request->references == 0U || request->references == SIZE_MAX) {
        abort();
    }
    request->references += 1U;
}

static _Bool wait_release_reference_locked(RRuntimeDarwinProcessWait *request) {
    if (request->references == 0U) {
        abort();
    }
    request->references -= 1U;
    return request->references == 0U;
}

static void wait_release_reference(RRuntimeDarwinProcessWait *request) {
    _Bool destroy;

    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    destroy = wait_release_reference_locked(request);
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    if (destroy) {
        wait_deallocate(request);
    }
}

static void close_descriptor(int *descriptor) {
    if (*descriptor >= 0) {
        (void)close(*descriptor);
        *descriptor = -1;
    }
}

static void child_deallocate(RRuntimeDarwinProcessChild *child) {
    if (child->source != NULL || child->listed || child->references != 0U ||
        child->wait_request != NULL || child->pipes[0] != NULL || child->pipes[1] != NULL ||
        child->pipes[2] != NULL) {
        abort();
    }
    r_runtime_allocator_deallocate(child, _Alignof(RRuntimeDarwinProcessChild));
}

static _Bool child_release_reference_locked(RRuntimeDarwinProcessChild *child) {
    if (child->references == 0U) {
        abort();
    }
    child->references -= 1U;
    return child->references == 0U;
}

static void child_release_reference(RRuntimeDarwinProcessChild *child) {
    _Bool destroy;

    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    destroy = child_release_reference_locked(child);
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    if (destroy) {
        child_deallocate(child);
    }
}

static void child_detach_pipes_locked(RRuntimeDarwinProcessChild *child,
                                      RRuntimeDarwinIoHandle **pipes) {
    size_t index;

    for (index = 0U; index < 3U; ++index) {
        pipes[index] = child->pipes[index];
        child->pipes[index] = NULL;
    }
}

static void service_pipe_cleanup(void *context, int native_error);

static void service_release_pipe(RRuntimeDarwinIoHandle *pipe) {
    if (pipe == NULL || pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (!process_service.running || process_service.pipe_cleanup_count == SIZE_MAX) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    process_service.pipe_cleanup_count += 1U;
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    if (!r_runtime_darwin_io_handle_release_with_cleanup(
            pipe, service_pipe_cleanup, &process_service)) {
        abort();
    }
}

static void child_release_pipes(RRuntimeDarwinIoHandle **pipes) {
    size_t index;

    for (index = 0U; index < 3U; ++index) {
        if (pipes[index] != NULL) {
            service_release_pipe(pipes[index]);
        }
    }
}

static void service_pipe_cleanup(void *context, int native_error) {
    RRuntimeDarwinProcessService *service = context;

    (void)native_error;
    if (service != &process_service || pthread_mutex_lock(&service->mutex) != 0) {
        abort();
    }
    if (service->pipe_cleanup_count == 0U) {
        (void)pthread_mutex_unlock(&service->mutex);
        abort();
    }
    service->pipe_cleanup_count -= 1U;
    if (pthread_cond_broadcast(&service->condition) != 0) {
        (void)pthread_mutex_unlock(&service->mutex);
        abort();
    }
    if (pthread_mutex_unlock(&service->mutex) != 0) {
        abort();
    }
}

static _Bool child_unlink_locked(RRuntimeDarwinProcessChild *child) {
    RRuntimeDarwinProcessChild **slot = &process_service.children;

    if (!child->listed) {
        return 0;
    }
    while (*slot != NULL && *slot != child) {
        slot = &(*slot)->next;
    }
    if (*slot != child) {
        abort();
    }
    *slot = child->next;
    child->next = NULL;
    child->listed = 0;
    return child_release_reference_locked(child);
}

static void child_process_cancelled(void *context) {
    RRuntimeDarwinProcessChild *child = context;
    RRuntimeDarwinProcessSpawn *rollback_request;
    dispatch_source_t source;
    _Bool destroy = 0;

    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    source = child->source;
    if (source == NULL || process_service.source_count == 0U) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    child->source = NULL;
    process_service.source_count -= 1U;
    rollback_request = child->rollback_request;
    child->rollback_request = NULL;
    if (child->reaped && !child->view_alive) {
        destroy = child_unlink_locked(child);
    }
    if (child_release_reference_locked(child)) {
        if (destroy) {
            (void)pthread_mutex_unlock(&process_service.mutex);
            abort();
        }
        destroy = 1;
    }
    if (pthread_cond_broadcast(&process_service.condition) != 0) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    dispatch_release(source);
    if (rollback_request != NULL) {
        if (pthread_mutex_lock(&rollback_request->mutex) != 0) {
            abort();
        }
        if (!rollback_request->rollback_source_pending) {
            (void)pthread_mutex_unlock(&rollback_request->mutex);
            abort();
        }
        rollback_request->rollback_source_pending = 0;
        if (pthread_mutex_unlock(&rollback_request->mutex) != 0) {
            abort();
        }
        spawn_try_deliver(rollback_request);
    }
    if (destroy) {
        child_deallocate(child);
    }
}

static void child_process_exited(void *context) {
    RRuntimeDarwinProcessChild *child = context;
    RRuntimeDarwinProcessWait *wait_request = NULL;
    dispatch_source_t wait_deadline_source = NULL;
    pid_t waited;
    int wait_status = 0;
    uint64_t event_sequence;
    uint64_t event_nanoseconds;
    _Bool wait_active = 0;

    /* The kernel posts the exit event before the child becomes a zombie, and the event fires
       once: a nonblocking wait in that window would lose the reap. The child has exited, so the
       blocking wait ends when its teardown does. */
    do {
        waited = waitpid(child->process_id, &wait_status, 0);
    } while (waited < (pid_t)0 && errno == EINTR);
    if (waited != child->process_id) {
        return;
    }
    event_sequence = r_runtime_darwin_event_sequence_next();
    event_nanoseconds = continuous_nanoseconds();
    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (child->reaped || child->source == NULL) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    child->reaped = 1;
    child->wait_status = wait_status;
    child->reap_sequence = event_sequence;
    child->reap_continuous_nanoseconds = event_nanoseconds;
    process_service.reap_count += UINT64_C(1);
    wait_request = child->wait_request;
    if (wait_request != NULL && wait_request->activated) {
        wait_retain_locked(wait_request);
        wait_active = 1;
        wait_deadline_source = wait_select_locked(wait_request,
                                                  R_RUNTIME_DARWIN_PROCESS_TERMINAL_NATIVE,
                                                  event_sequence,
                                                  0,
                                                  wait_status,
                                                  1);
    }
    dispatch_source_cancel(child->source);
    if (pthread_cond_broadcast(&process_service.condition) != 0) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    if (wait_deadline_source != NULL) {
        dispatch_source_cancel(wait_deadline_source);
    }
    if (wait_active) {
        wait_try_deliver(wait_request);
        wait_release_reference(wait_request);
    }
}

static void child_register_source(RRuntimeDarwinProcessChild *child, dispatch_source_t source) {
    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (!process_service.running || child->listed || child->source != NULL ||
        child->process_id <= (pid_t)0 || child->references != 1U) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    child->source = source;
    child->references += 2U;
    child->listed = 1;
    child->next = process_service.children;
    process_service.children = child;
    process_service.source_count += 1U;
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    dispatch_set_context(source, child);
    dispatch_source_set_event_handler_f(source, child_process_exited);
    dispatch_source_set_cancel_handler_f(source, child_process_cancelled);
    dispatch_activate(source);
}

static void child_mark_view_alive(RRuntimeDarwinProcessChild *child) {
    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (!child->listed || child->view_alive || child->reaped) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    child->view_alive = 1;
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
}

static void child_mark_rollback(RRuntimeDarwinProcessChild *child,
                                RRuntimeDarwinProcessSpawn *request,
                                _Bool source_registered) {
    pid_t waited;
    int wait_status = 0;
    _Bool destroy = 0;

    if (kill(child->process_id, SIGKILL) != 0 && errno != ESRCH) {
        abort();
    }
    do {
        waited = waitpid(child->process_id, &wait_status, 0);
    } while (waited < (pid_t)0 && errno == EINTR);
    if (waited != child->process_id && !(waited < (pid_t)0 && errno == ECHILD)) {
        abort();
    }
    if (source_registered) {
        if (pthread_mutex_lock(&request->mutex) != 0) {
            abort();
        }
        if (request->rollback_source_pending) {
            (void)pthread_mutex_unlock(&request->mutex);
            abort();
        }
        request->rollback_source_pending = 1;
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
    }
    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (child->reaped || child->view_alive) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    child->reaped = 1;
    child->wait_status = wait_status;
    process_service.reap_count += UINT64_C(1);
    if (source_registered) {
        if (child->source == NULL || !child->listed) {
            (void)pthread_mutex_unlock(&process_service.mutex);
            abort();
        }
        child->rollback_request = request;
        dispatch_source_cancel(child->source);
    } else {
        destroy = child_unlink_locked(child);
    }
    if (pthread_cond_broadcast(&process_service.condition) != 0) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    if (destroy) {
        child_deallocate(child);
    }
}

static void spawn_close_child_descriptors(RRuntimeDarwinProcessSpawn *request) {
    size_t index;

    for (index = 0U; index < 3U; ++index) {
        close_descriptor(&request->child_descriptors[index]);
    }
}

static void spawn_try_deliver(RRuntimeDarwinProcessSpawn *request) {
    RRuntimeDarwinProcessCompletionFn completion = NULL;
    void *context = NULL;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->terminal_selected && request->submission_done &&
        !request->deadline_cancel_pending && !request->rollback_source_pending &&
        !request->completion_delivered) {
        if (request->completion == NULL) {
            (void)pthread_mutex_unlock(&request->mutex);
            abort();
        }
        request->completion_delivered = 1;
        completion = request->completion;
        context = request->callback_context;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (completion != NULL) {
        completion(request, context);
        if (pthread_mutex_lock(&process_service.mutex) != 0) {
            abort();
        }
        if (process_service.active_spawn_count == 0U) {
            (void)pthread_mutex_unlock(&process_service.mutex);
            abort();
        }
        process_service.active_spawn_count -= 1U;
        if (pthread_cond_broadcast(&process_service.condition) != 0) {
            (void)pthread_mutex_unlock(&process_service.mutex);
            abort();
        }
        if (pthread_mutex_unlock(&process_service.mutex) != 0) {
            abort();
        }
    }
}

static void spawn_cancel_deadline_locked(RRuntimeDarwinProcessSpawn *request) {
    if (request->deadline_source != NULL && !request->deadline_cancel_pending) {
        if (!request->deadline_activated) {
            request->deadline_activated = 1;
            dispatch_activate(request->deadline_source);
        }
        request->deadline_cancel_pending = 1;
        dispatch_source_cancel(request->deadline_source);
    }
}

static _Bool spawn_select_locked(RRuntimeDarwinProcessSpawn *request,
                                 RRuntimeDarwinProcessTerminalEvent event,
                                 uint64_t sequence,
                                 int native_error,
                                 _Bool spawned,
                                 _Bool commit_selected) {
    if (sequence == UINT64_C(0) || request->completion_delivered ||
        request->creation_commit_arbitrated) {
        return 0;
    }
    if (request->terminal_selected && sequence >= request->result.terminal_event_sequence) {
        return 0;
    }
    request->terminal_selected = 1;
    request->result = (RRuntimeDarwinProcessSpawnResult){
        event,
        native_error,
        sequence,
        spawned,
        commit_selected,
    };
    spawn_cancel_deadline_locked(request);
    return 1;
}

static void spawn_deadline_cancelled(void *context) {
    RRuntimeDarwinProcessSpawn *request = context;
    dispatch_source_t source;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    source = request->deadline_source;
    if (source == NULL || !request->deadline_cancel_pending) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    request->deadline_source = NULL;
    request->deadline_cancel_pending = 0;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    dispatch_release(source);
    spawn_try_deliver(request);
}

static void spawn_deadline_fired(void *context) {
    RRuntimeDarwinProcessSpawn *request = context;
    const uint64_t sequence = r_runtime_darwin_event_sequence_next();
    _Bool selected;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    selected = spawn_select_locked(
        request, R_RUNTIME_DARWIN_PROCESS_TERMINAL_TIMED_OUT, sequence, 0, 0, 0);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (selected) {
        process_testing_record_deadline();
    }
    spawn_try_deliver(request);
}

static void spawn_mark_submission_done(RRuntimeDarwinProcessSpawn *request) {
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->submission_done) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    request->submission_done = 1;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    spawn_try_deliver(request);
}

static void spawn_select_native_failure(RRuntimeDarwinProcessSpawn *request,
                                        uint64_t sequence,
                                        int native_error,
                                        _Bool commit_selected) {
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (commit_selected) {
        if (!request->creation_commit_arbitrated || request->terminal_selected) {
            (void)pthread_mutex_unlock(&request->mutex);
            abort();
        }
        request->terminal_selected = 1;
        request->result = (RRuntimeDarwinProcessSpawnResult){
            R_RUNTIME_DARWIN_PROCESS_TERMINAL_NATIVE,
            native_error,
            sequence,
            0,
            1,
        };
        spawn_cancel_deadline_locked(request);
    } else {
        (void)spawn_select_locked(
            request, R_RUNTIME_DARWIN_PROCESS_TERMINAL_NATIVE, sequence, native_error, 0, 0);
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
}

static void spawn_select_success(RRuntimeDarwinProcessSpawn *request, uint64_t sequence) {
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (!request->creation_commit_arbitrated || request->terminal_selected) {
        (void)pthread_mutex_unlock(&request->mutex);
        abort();
    }
    request->terminal_selected = 1;
    request->result = (RRuntimeDarwinProcessSpawnResult){
        R_RUNTIME_DARWIN_PROCESS_TERMINAL_NATIVE,
        0,
        sequence,
        1,
        1,
    };
    spawn_cancel_deadline_locked(request);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
}

static _Bool spawn_terminal_selected(RRuntimeDarwinProcessSpawn *request) {
    _Bool selected;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    selected = request->terminal_selected;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return selected;
}

static _Bool spawn_arbitrate_commit(RRuntimeDarwinProcessSpawn *request,
                                    uint64_t *commit_sequence) {
    uint64_t earlier_cancellation_sequence = UINT64_C(0);
    _Bool permitted;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->terminal_selected || request->creation_commit_arbitrated ||
        request->commit == NULL) {
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        return 0;
    }
    if (request->continuous_deadline_nanoseconds != UINT64_C(0) &&
        continuous_nanoseconds() >= request->continuous_deadline_nanoseconds) {
        const uint64_t deadline_sequence = r_runtime_darwin_event_sequence_next();

        (void)spawn_select_locked(
            request, R_RUNTIME_DARWIN_PROCESS_TERMINAL_TIMED_OUT, deadline_sequence, 0, 0, 0);
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        return 0;
    }
    *commit_sequence = r_runtime_darwin_event_sequence_next();
    permitted = request->commit(
        request, request->callback_context, *commit_sequence, &earlier_cancellation_sequence);
    if (permitted) {
        request->creation_commit_arbitrated = 1;
    } else {
        if (earlier_cancellation_sequence == UINT64_C(0) ||
            earlier_cancellation_sequence >= *commit_sequence) {
            (void)pthread_mutex_unlock(&request->mutex);
            abort();
        }
        (void)spawn_select_locked(request,
                                  R_RUNTIME_DARWIN_PROCESS_TERMINAL_CANCELLED,
                                  earlier_cancellation_sequence,
                                  0,
                                  0,
                                  0);
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return permitted;
}

static void spawn_execute(void *context) {
    RRuntimeDarwinProcessSpawn *request = context;
    dispatch_source_t process_source = NULL;
    uint64_t native_sequence;
    uint64_t commit_sequence = UINT64_C(0);
    pid_t process_id = (pid_t)0;
    int native_error = 0;
    int forced_error = 0;
    _Bool source_registered = 0;
    _Bool commit_selected = 0;

    if (spawn_terminal_selected(request)) {
        spawn_close_child_descriptors(request);
        spawn_mark_submission_done(request);
        return;
    }
    native_error = posix_spawn(&process_id,
                               request->options.executable,
                               &request->file_actions,
                               &request->attributes,
                               request->options.arguments,
                               request->options.environment);
    native_sequence = r_runtime_darwin_event_sequence_next();
    spawn_close_child_descriptors(request);
    if (native_error != 0) {
        spawn_select_native_failure(request, native_sequence, native_error, 0);
        spawn_mark_submission_done(request);
        return;
    }
    request->child->process_id = process_id;
    if (!process_testing_take_failure(R_RUNTIME_DARWIN_PROCESS_FAIL_PROCESS_SOURCE,
                                      &forced_error)) {
        process_source = dispatch_source_create(DISPATCH_SOURCE_TYPE_PROC,
                                                (uintptr_t)process_id,
                                                DISPATCH_PROC_EXIT,
                                                process_service.submission_queue);
    }
    if (process_source == NULL) {
        if (forced_error == 0) {
            forced_error = ENOMEM;
        }
        child_mark_rollback(request->child, request, 0);
        spawn_select_native_failure(
            request, r_runtime_darwin_event_sequence_next(), forced_error, 0);
        spawn_mark_submission_done(request);
        return;
    }
    child_register_source(request->child, process_source);
    source_registered = 1;
    process_testing_pause();
    if (!spawn_terminal_selected(request)) {
        commit_selected = spawn_arbitrate_commit(request, &commit_sequence);
    }
    if (!commit_selected) {
        child_mark_rollback(request->child, request, source_registered);
        spawn_mark_submission_done(request);
        return;
    }
    if (process_testing_take_failure(R_RUNTIME_DARWIN_PROCESS_FAIL_CONTINUE, &forced_error)) {
        native_error = forced_error == 0 ? EIO : forced_error;
    } else if (kill(process_id, SIGCONT) != 0) {
        native_error = errno;
    }
    if (native_error != 0) {
        child_mark_rollback(request->child, request, source_registered);
        spawn_select_native_failure(request, commit_sequence, native_error, 1);
    } else {
        child_mark_view_alive(request->child);
        spawn_select_success(request, commit_sequence);
    }
    spawn_mark_submission_done(request);
}

static _Bool stdio_mode_valid(RRuntimeDarwinProcessStdioMode mode) {
    return mode == R_RUNTIME_DARWIN_PROCESS_STDIO_INHERIT ||
           mode == R_RUNTIME_DARWIN_PROCESS_STDIO_NULL_DEVICE ||
           mode == R_RUNTIME_DARWIN_PROCESS_STDIO_PIPED;
}

static int descriptor_above_standard(int descriptor) {
    int replacement;

    if (descriptor > STDERR_FILENO) {
        return descriptor;
    }
    replacement = fcntl(descriptor, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
    (void)close(descriptor);
    return replacement;
}

static _Bool set_descriptor_flag(int descriptor, int command, int flag, int *native_error) {
    const int current = fcntl(descriptor, command);

    if (current < 0 ||
        fcntl(descriptor, command == F_GETFD ? F_SETFD : F_SETFL, current | flag) != 0) {
        *native_error = errno;
        return 0;
    }
    return 1;
}

static RRuntimeDarwinProcessStartStatus spawn_prepare_pipe(RRuntimeDarwinProcessSpawn *request,
                                                           size_t index,
                                                           int target_descriptor,
                                                           _Bool parent_writes,
                                                           int *native_error) {
    RRuntimeDarwinIoHandleCreateResult io_result;
    int descriptors[2] = {-1, -1};
    int parent_descriptor;
    int child_descriptor;
    int action_status;
    int forced_error = 0;

    if (process_testing_take_failure(R_RUNTIME_DARWIN_PROCESS_FAIL_PIPE, &forced_error) ||
        pipe(descriptors) != 0) {
        *native_error = forced_error == 0 ? errno : forced_error;
        return R_RUNTIME_DARWIN_PROCESS_START_ALLOCATION_FAILED;
    }
    descriptors[0] = descriptor_above_standard(descriptors[0]);
    descriptors[1] = descriptor_above_standard(descriptors[1]);
    if (descriptors[0] < 0 || descriptors[1] < 0) {
        *native_error = errno;
        close_descriptor(&descriptors[0]);
        close_descriptor(&descriptors[1]);
        return R_RUNTIME_DARWIN_PROCESS_START_ALLOCATION_FAILED;
    }
    parent_descriptor = parent_writes ? descriptors[1] : descriptors[0];
    child_descriptor = parent_writes ? descriptors[0] : descriptors[1];
    if (!set_descriptor_flag(parent_descriptor, F_GETFD, FD_CLOEXEC, native_error) ||
        !set_descriptor_flag(parent_descriptor, F_GETFL, O_NONBLOCK, native_error) ||
        !set_descriptor_flag(child_descriptor, F_GETFD, FD_CLOEXEC, native_error)) {
        close_descriptor(&descriptors[0]);
        close_descriptor(&descriptors[1]);
        return R_RUNTIME_DARWIN_PROCESS_START_ALLOCATION_FAILED;
    }
    if (process_testing_take_failure(R_RUNTIME_DARWIN_PROCESS_FAIL_PIPE_IO, &forced_error)) {
        io_result = (RRuntimeDarwinIoHandleCreateResult){
            NULL,
            R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED,
            forced_error == 0 ? ENOMEM : forced_error,
        };
    } else {
        io_result = r_runtime_darwin_io_handle_create(
            request->allocator, parent_descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    }
    if (io_result.status != R_RUNTIME_DARWIN_IO_START_OK || io_result.handle == NULL) {
        *native_error = io_result.native_error == 0 ? ENOMEM : io_result.native_error;
        close_descriptor(&descriptors[0]);
        close_descriptor(&descriptors[1]);
        return R_RUNTIME_DARWIN_PROCESS_START_ALLOCATION_FAILED;
    }
    request->child->pipes[index] = io_result.handle;
    close_descriptor(&parent_descriptor);
    if (parent_writes) {
        descriptors[1] = -1;
    } else {
        descriptors[0] = -1;
    }
    request->child_descriptors[index] = child_descriptor;
    if (parent_writes) {
        descriptors[0] = -1;
    } else {
        descriptors[1] = -1;
    }
    action_status = posix_spawn_file_actions_adddup2(
        &request->file_actions, request->child_descriptors[index], target_descriptor);
    if (action_status == 0) {
        action_status = posix_spawn_file_actions_addclose(&request->file_actions,
                                                          request->child_descriptors[index]);
    }
    if (action_status != 0) {
        *native_error = action_status;
        return R_RUNTIME_DARWIN_PROCESS_START_ALLOCATION_FAILED;
    }
    return R_RUNTIME_DARWIN_PROCESS_START_OK;
}

static RRuntimeDarwinProcessStartStatus spawn_prepare_stdio(RRuntimeDarwinProcessSpawn *request,
                                                            size_t index,
                                                            int target_descriptor,
                                                            RRuntimeDarwinProcessStdioMode mode,
                                                            int *native_error) {
    int status;

    if (mode == R_RUNTIME_DARWIN_PROCESS_STDIO_INHERIT) {
        status = posix_spawn_file_actions_addinherit_np(&request->file_actions, target_descriptor);
    } else if (mode == R_RUNTIME_DARWIN_PROCESS_STDIO_NULL_DEVICE) {
        status = posix_spawn_file_actions_addopen(&request->file_actions,
                                                  target_descriptor,
                                                  "/dev/null",
                                                  target_descriptor == STDIN_FILENO ? O_RDONLY
                                                                                    : O_WRONLY,
                                                  (mode_t)0);
    } else {
        return spawn_prepare_pipe(
            request, index, target_descriptor, target_descriptor == STDIN_FILENO, native_error);
    }
    if (status != 0) {
        *native_error = status;
        return R_RUNTIME_DARWIN_PROCESS_START_ALLOCATION_FAILED;
    }
    return R_RUNTIME_DARWIN_PROCESS_START_OK;
}

static void spawn_dispose_unsubmitted(RRuntimeDarwinProcessSpawn *request) {
    dispatch_source_t deadline_source;

    if (request == NULL) {
        return;
    }
    deadline_source = request->deadline_source;
    request->deadline_source = NULL;
    if (deadline_source != NULL) {
        dispatch_source_set_event_handler_f(deadline_source, NULL);
        dispatch_source_set_cancel_handler_f(deadline_source, NULL);
        dispatch_set_context(deadline_source, NULL);
        dispatch_source_cancel(deadline_source);
        dispatch_activate(deadline_source);
        dispatch_release(deadline_source);
    }
    spawn_close_child_descriptors(request);
    close_descriptor(&request->current_directory);
    if (request->attributes_initialized) {
        (void)posix_spawnattr_destroy(&request->attributes);
    }
    if (request->file_actions_initialized) {
        (void)posix_spawn_file_actions_destroy(&request->file_actions);
    }
    if (request->child != NULL) {
        RRuntimeDarwinIoHandle *pipes[3] = {NULL, NULL, NULL};

        if (pthread_mutex_lock(&process_service.mutex) != 0) {
            abort();
        }
        child_detach_pipes_locked(request->child, pipes);
        if (pthread_mutex_unlock(&process_service.mutex) != 0) {
            abort();
        }
        child_release_pipes(pipes);
        child_release_reference(request->child);
        request->child = NULL;
    }
    if (pthread_mutex_destroy(&request->mutex) != 0) {
        abort();
    }
    r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinProcessSpawn));
}

RRuntimeDarwinProcessStartResult
r_runtime_darwin_process_lifecycle_start(RRuntimeAllocator *allocator) {
    dispatch_queue_t queue;

    queue = dispatch_queue_create("r.process.spawn", DISPATCH_QUEUE_SERIAL);
    if (queue == NULL) {
        return process_start_result(R_RUNTIME_DARWIN_PROCESS_START_ALLOCATION_FAILED, ENOMEM);
    }
    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (process_service.running || process_service.stopping ||
        process_service.submission_queue != NULL || process_service.children != NULL ||
        process_service.preparation_count != 0U || process_service.active_spawn_count != 0U ||
        process_service.active_wait_count != 0U || process_service.source_count != 0U ||
        process_service.pipe_cleanup_count != 0U) {
        if (pthread_mutex_unlock(&process_service.mutex) != 0) {
            abort();
        }
        dispatch_release(queue);
        return process_start_result(R_RUNTIME_DARWIN_PROCESS_START_RUNTIME_STOPPING, 0);
    }
    process_service.allocator = allocator;
    process_service.submission_queue = queue;
    process_service.running = 1;
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    return process_start_result(R_RUNTIME_DARWIN_PROCESS_START_OK, 0);
}

RRuntimeDarwinProcessPrepareResult
r_runtime_darwin_process_spawn_prepare(RRuntimeAllocator *allocator,
                                       const RRuntimeDarwinProcessSpawnOptions *options) {
    RRuntimeDarwinProcessSpawn *request = NULL;
    RRuntimeDarwinProcessChild *child = NULL;
    dispatch_queue_t timer_queue;
    dispatch_time_t timer_deadline = DISPATCH_TIME_FOREVER;
    uint64_t continuous_deadline_nanoseconds = UINT64_C(0);
    short spawn_flags = (short)(POSIX_SPAWN_CLOEXEC_DEFAULT | POSIX_SPAWN_START_SUSPENDED);
    int forced_error = 0;
    int native_error = 0;
    int status;
    size_t index;
    RRuntimeDarwinProcessStartStatus prepare_status;
    _Bool request_disposable = 0;

    if (options->current_directory < 0 || !stdio_mode_valid(options->input) ||
        !stdio_mode_valid(options->output) || !stdio_mode_valid(options->error) ||
        options->timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return process_prepare_result(NULL, R_RUNTIME_DARWIN_PROCESS_START_INVALID, 0);
    }
    if (options->timeout_nanoseconds != UINT64_C(0)) {
        continuous_deadline_nanoseconds = deadline_from_timeout(options->timeout_nanoseconds);
        timer_deadline = dispatch_time(DISPATCH_TIME_NOW, (int64_t)options->timeout_nanoseconds);
    }
    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (!process_service.running || process_service.stopping ||
        process_service.allocator != allocator) {
        if (pthread_mutex_unlock(&process_service.mutex) != 0) {
            abort();
        }
        return process_prepare_result(NULL, R_RUNTIME_DARWIN_PROCESS_START_RUNTIME_STOPPING, 0);
    }
    process_service.preparation_count += 1U;
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    if (r_runtime_allocator_allocate(
            allocator, sizeof(*request), _Alignof(RRuntimeDarwinProcessSpawn), (void **)&request) !=
            R_RUNTIME_ALLOCATION_OK ||
        r_runtime_allocator_allocate(
            allocator, sizeof(*child), _Alignof(RRuntimeDarwinProcessChild), (void **)&child) !=
            R_RUNTIME_ALLOCATION_OK) {
        native_error = ENOMEM;
        goto cleanup;
    }
    (void)memset(request, 0, sizeof(*request));
    (void)memset(child, 0, sizeof(*child));
    request->allocator = allocator;
    request->options = *options;
    request->child = child;
    request->current_directory = -1;
    child->allocator = allocator;
    child->references = 1U;
    for (index = 0U; index < 3U; ++index) {
        request->child_descriptors[index] = -1;
    }
    if (pthread_mutex_init(&request->mutex, NULL) != 0) {
        native_error = ENOMEM;
        goto cleanup;
    }
    request_disposable = 1;
    status = posix_spawn_file_actions_init(&request->file_actions);
    if (status != 0) {
        native_error = status;
        goto cleanup;
    }
    request->file_actions_initialized = 1;
    status = posix_spawnattr_init(&request->attributes);
    if (status != 0) {
        native_error = status;
        goto cleanup;
    }
    request->attributes_initialized = 1;
    {
        /* R-SLIB-PROC-0003, R-SLIB-SIGNAL-0003: a child starts with no blocked signal, whatever
           the mask of the runtime thread that spawns it (M22-2), and with the default action of
           every signal whose disposition a listener of this process replaced by ignore. */
        sigset_t replaced;
        sigset_t unblocked;

        r_runtime_darwin_signal_replaced_set(&replaced);
        (void)sigemptyset(&unblocked);
        status = posix_spawnattr_setsigdefault(&request->attributes, &replaced);
        if (status == 0) {
            status = posix_spawnattr_setsigmask(&request->attributes, &unblocked);
        }
        if (status != 0) {
            native_error = status;
            goto cleanup;
        }
        spawn_flags = (short)(spawn_flags | POSIX_SPAWN_SETSIGDEF | POSIX_SPAWN_SETSIGMASK);
    }
    status = posix_spawnattr_setflags(&request->attributes, spawn_flags);
    if (status != 0) {
        native_error = status;
        goto cleanup;
    }
    request->current_directory = fcntl(options->current_directory, F_DUPFD_CLOEXEC, 0);
    if (request->current_directory < 0) {
        native_error = errno;
        goto cleanup;
    }
    status = posix_spawn_file_actions_addfchdir(&request->file_actions, request->current_directory);
    if (status == 0 && options->working_directory != NULL) {
        status =
            posix_spawn_file_actions_addchdir(&request->file_actions, options->working_directory);
    }
    if (status != 0) {
        native_error = status;
        goto cleanup;
    }
    prepare_status = spawn_prepare_stdio(request, 0U, STDIN_FILENO, options->input, &native_error);
    if (prepare_status == R_RUNTIME_DARWIN_PROCESS_START_OK) {
        prepare_status =
            spawn_prepare_stdio(request, 1U, STDOUT_FILENO, options->output, &native_error);
    }
    if (prepare_status == R_RUNTIME_DARWIN_PROCESS_START_OK) {
        prepare_status =
            spawn_prepare_stdio(request, 2U, STDERR_FILENO, options->error, &native_error);
    }
    if (prepare_status != R_RUNTIME_DARWIN_PROCESS_START_OK) {
        goto cleanup;
    }
    if (options->timeout_nanoseconds != UINT64_C(0)) {
        timer_queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0U);
        if (!process_testing_take_failure(R_RUNTIME_DARWIN_PROCESS_FAIL_DEADLINE_SOURCE,
                                          &forced_error)) {
            request->deadline_source = dispatch_source_create(
                DISPATCH_SOURCE_TYPE_TIMER, 0U, DISPATCH_TIMER_STRICT, timer_queue);
        }
        if (request->deadline_source == NULL) {
            native_error = forced_error == 0 ? ENOMEM : forced_error;
            goto cleanup;
        }
        request->continuous_deadline_nanoseconds = continuous_deadline_nanoseconds;
        dispatch_set_context(request->deadline_source, request);
        dispatch_source_set_event_handler_f(request->deadline_source, spawn_deadline_fired);
        dispatch_source_set_cancel_handler_f(request->deadline_source, spawn_deadline_cancelled);
        dispatch_source_set_timer(
            request->deadline_source, timer_deadline, DISPATCH_TIME_FOREVER, 0U);
    }
    return process_prepare_result(request, R_RUNTIME_DARWIN_PROCESS_START_OK, 0);

cleanup:
    if (request_disposable) {
        spawn_dispose_unsubmitted(request);
    } else {
        r_runtime_allocator_deallocate(child, _Alignof(RRuntimeDarwinProcessChild));
        r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinProcessSpawn));
    }
    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (process_service.preparation_count == 0U) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    process_service.preparation_count -= 1U;
    if (pthread_cond_broadcast(&process_service.condition) != 0) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    return process_prepare_result(
        NULL, R_RUNTIME_DARWIN_PROCESS_START_ALLOCATION_FAILED, native_error);
}

_Bool r_runtime_darwin_process_spawn_bind(RRuntimeDarwinProcessSpawn *request,
                                          RRuntimeDarwinProcessCommitFn commit,
                                          RRuntimeDarwinProcessCompletionFn completion,
                                          void *context) {
    _Bool bound = 0;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        return 0;
    }
    if (!request->activated && request->commit == NULL && request->completion == NULL) {
        request->commit = commit;
        request->completion = completion;
        request->callback_context = context;
        bound = 1;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return bound;
}

_Bool r_runtime_darwin_process_spawn_activate(RRuntimeDarwinProcessSpawn *request) {
    dispatch_queue_t queue;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        return 0;
    }
    if (request->activated || request->commit == NULL || request->completion == NULL) {
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        return 0;
    }
    request->activated = 1;
    if (request->deadline_source != NULL && !request->deadline_activated) {
        request->deadline_activated = 1;
        dispatch_activate(request->deadline_source);
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (!process_service.running || process_service.preparation_count == 0U ||
        process_service.submission_queue == NULL) {
        if (pthread_mutex_unlock(&process_service.mutex) != 0) {
            abort();
        }
        return 0;
    }
    process_service.preparation_count -= 1U;
    process_service.active_spawn_count += 1U;
    queue = process_service.submission_queue;
    if (pthread_cond_broadcast(&process_service.condition) != 0) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    dispatch_async_f(queue, request, spawn_execute);
    return 1;
}

_Bool r_runtime_darwin_process_spawn_cancel(RRuntimeDarwinProcessSpawn *request,
                                            uint64_t cancellation_sequence) {
    _Bool selected;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        return 0;
    }
    selected = spawn_select_locked(
        request, R_RUNTIME_DARWIN_PROCESS_TERMINAL_CANCELLED, cancellation_sequence, 0, 0, 0);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    spawn_try_deliver(request);
    return selected;
}

_Bool r_runtime_darwin_process_spawn_deadline_expired(RRuntimeDarwinProcessSpawn *request,
                                                      uint64_t deadline_sequence) {
    _Bool selected;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        return 0;
    }
    selected = spawn_select_locked(
        request, R_RUNTIME_DARWIN_PROCESS_TERMINAL_TIMED_OUT, deadline_sequence, 0, 0, 0);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    spawn_try_deliver(request);
    return selected;
}

RRuntimeDarwinProcessSpawnResult
r_runtime_darwin_process_spawn_result(RRuntimeDarwinProcessSpawn *request) {
    RRuntimeDarwinProcessSpawnResult result = {0};

    if (pthread_mutex_lock(&request->mutex) != 0) {
        return result;
    }
    if (request->completion_delivered) {
        result = request->result;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinProcessChild *
r_runtime_darwin_process_spawn_take_child(RRuntimeDarwinProcessSpawn *request) {
    RRuntimeDarwinProcessChild *child = NULL;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        return NULL;
    }
    if (request->completion_delivered && request->result.spawned && !request->child_taken &&
        request->child != NULL) {
        child = request->child;
        request->child = NULL;
        request->child_taken = 1;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return child;
}

void r_runtime_darwin_process_spawn_release(RRuntimeDarwinProcessSpawn *request) {
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (!request->completion_delivered || request->deadline_source != NULL ||
        request->deadline_cancel_pending || request->rollback_source_pending ||
        (request->result.spawned && !request->child_taken)) {
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        abort();
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    spawn_dispose_unsubmitted(request);
}

void r_runtime_darwin_process_spawn_abort(RRuntimeDarwinProcessSpawn **request_slot) {
    RRuntimeDarwinProcessSpawn *request;

    if (request_slot == NULL || *request_slot == NULL) {
        return;
    }
    request = *request_slot;
    *request_slot = NULL;
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->activated || request->commit != NULL || request->completion != NULL ||
        request->terminal_selected) {
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        abort();
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    spawn_dispose_unsubmitted(request);
    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (process_service.preparation_count == 0U) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    process_service.preparation_count -= 1U;
    if (pthread_cond_broadcast(&process_service.condition) != 0) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
}

uint64_t r_runtime_darwin_process_child_identity(const RRuntimeDarwinProcessChild *child) {
    return child->process_id <= (pid_t)0 ? UINT64_C(0) : (uint64_t)child->process_id;
}

RRuntimeDarwinIoHandle *r_runtime_darwin_process_child_take_pipe(RRuntimeDarwinProcessChild *child,
                                                                 RRuntimeDarwinProcessPipe pipe) {
    RRuntimeDarwinIoHandle *result = NULL;

    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (process_service.running && !process_service.stopping && child->view_alive) {
        result = child->pipes[(size_t)pipe];
        child->pipes[(size_t)pipe] = NULL;
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    return result;
}

static RRuntimeDarwinProcessWaitPrepareResult wait_prepare_result(
    RRuntimeDarwinProcessWait *request, RRuntimeDarwinProcessStartStatus status, int native_error) {
    return (RRuntimeDarwinProcessWaitPrepareResult){request, status, native_error};
}

static void wait_finish_preparation(void) {
    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (process_service.preparation_count == 0U) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    process_service.preparation_count -= 1U;
    if (pthread_cond_broadcast(&process_service.condition) != 0) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
}

RRuntimeDarwinProcessWaitPrepareResult r_runtime_darwin_process_wait_prepare(
    RRuntimeAllocator *allocator, RRuntimeDarwinProcessChild *child, uint64_t timeout_nanoseconds) {
    RRuntimeDarwinProcessWait *request = NULL;
    dispatch_queue_t queue;
    dispatch_time_t timer_deadline = DISPATCH_TIME_FOREVER;
    int forced_error = 0;
    RRuntimeDarwinProcessStartStatus failure_status = R_RUNTIME_DARWIN_PROCESS_START_INVALID;
    int native_error = 0;
    _Bool reserved = 0;

    if (timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return wait_prepare_result(NULL, R_RUNTIME_DARWIN_PROCESS_START_INVALID, 0);
    }
    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (!process_service.running || process_service.stopping ||
        process_service.allocator != allocator) {
        if (pthread_mutex_unlock(&process_service.mutex) != 0) {
            abort();
        }
        return wait_prepare_result(NULL, R_RUNTIME_DARWIN_PROCESS_START_RUNTIME_STOPPING, 0);
    }
    process_service.preparation_count += 1U;
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    if (r_runtime_allocator_allocate(
            allocator, sizeof(*request), _Alignof(RRuntimeDarwinProcessWait), (void **)&request) !=
        R_RUNTIME_ALLOCATION_OK) {
        failure_status = R_RUNTIME_DARWIN_PROCESS_START_ALLOCATION_FAILED;
        native_error = ENOMEM;
        goto cleanup;
    }
    (void)memset(request, 0, sizeof(*request));
    request->allocator = allocator;
    request->child = child;
    request->references = 1U;
    if (timeout_nanoseconds != UINT64_C(0)) {
        request->continuous_deadline_nanoseconds = deadline_from_timeout(timeout_nanoseconds);
        timer_deadline = dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout_nanoseconds);
        queue = dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0U);
        if (!process_testing_take_failure(R_RUNTIME_DARWIN_PROCESS_FAIL_WAIT_DEADLINE_SOURCE,
                                          &forced_error)) {
            request->deadline_source = dispatch_source_create(
                DISPATCH_SOURCE_TYPE_TIMER, 0U, DISPATCH_TIMER_STRICT, queue);
        }
        if (request->deadline_source == NULL) {
            failure_status = R_RUNTIME_DARWIN_PROCESS_START_ALLOCATION_FAILED;
            native_error = forced_error == 0 ? ENOMEM : forced_error;
            goto cleanup;
        }
        dispatch_set_context(request->deadline_source, request);
        dispatch_source_set_event_handler_f(request->deadline_source, wait_deadline_fired);
        dispatch_source_set_cancel_handler_f(request->deadline_source, wait_deadline_cancelled);
        dispatch_source_set_timer(
            request->deadline_source, timer_deadline, DISPATCH_TIME_FOREVER, 0U);
    }
    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (!process_service.running || process_service.stopping ||
        process_service.allocator != allocator) {
        failure_status = R_RUNTIME_DARWIN_PROCESS_START_RUNTIME_STOPPING;
    } else if (!child->view_alive || child->wait_request != NULL || child->references == SIZE_MAX) {
        failure_status = R_RUNTIME_DARWIN_PROCESS_START_INVALID;
    } else {
        child->references += 1U;
        child->wait_request = request;
        reserved = 1;
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    if (reserved) {
        return wait_prepare_result(request, R_RUNTIME_DARWIN_PROCESS_START_OK, 0);
    }

cleanup:
    if (request != NULL) {
        request->child = NULL;
        request->references = 0U;
        wait_dispose_unactivated_deadline(request);
        wait_deallocate(request);
    }
    wait_finish_preparation();
    return wait_prepare_result(NULL, failure_status, native_error);
}

_Bool r_runtime_darwin_process_wait_bind(RRuntimeDarwinProcessWait *request,
                                         RRuntimeDarwinProcessWaitCompletionFn completion,
                                         void *context) {
    _Bool bound = 0;

    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        return 0;
    }
    if (!request->bound && !request->activated && request->child != NULL &&
        request->child->wait_request == request) {
        request->completion = completion;
        request->completion_context = context;
        request->bound = 1;
        bound = 1;
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    return bound;
}

_Bool r_runtime_darwin_process_wait_activate(RRuntimeDarwinProcessWait *request) {
    dispatch_source_t deadline_source = NULL;
    dispatch_source_t source_to_cancel = NULL;
    uint64_t now;
    _Bool activated = 0;

    now = continuous_nanoseconds();
    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (process_service.running && process_service.preparation_count != 0U && request->bound &&
        !request->activated && request->child != NULL &&
        (request->child->wait_request == request || request->terminal_selected)) {
        request->activated = 1;
        process_service.preparation_count -= 1U;
        process_service.active_wait_count += 1U;
        deadline_source = request->deadline_source;
        request->deadline_activated = deadline_source != NULL;
        wait_retain_locked(request);
        if (!request->terminal_selected && request->child->reaped &&
            (request->continuous_deadline_nanoseconds == UINT64_C(0) ||
             request->child->reap_continuous_nanoseconds <=
                 request->continuous_deadline_nanoseconds)) {
            source_to_cancel = wait_select_locked(request,
                                                  R_RUNTIME_DARWIN_PROCESS_TERMINAL_NATIVE,
                                                  request->child->reap_sequence,
                                                  0,
                                                  request->child->wait_status,
                                                  1);
        } else if (!request->terminal_selected &&
                   request->continuous_deadline_nanoseconds != UINT64_C(0) &&
                   now >= request->continuous_deadline_nanoseconds) {
            source_to_cancel = wait_select_locked(request,
                                                  R_RUNTIME_DARWIN_PROCESS_TERMINAL_TIMED_OUT,
                                                  r_runtime_darwin_event_sequence_next(),
                                                  0,
                                                  0,
                                                  0);
        } else if (request->terminal_selected && deadline_source != NULL &&
                   !request->deadline_cancel_pending) {
            request->deadline_cancel_pending = 1;
            source_to_cancel = deadline_source;
        }
        activated = 1;
        if (pthread_cond_broadcast(&process_service.condition) != 0) {
            (void)pthread_mutex_unlock(&process_service.mutex);
            abort();
        }
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    if (!activated) {
        return 0;
    }
    if (deadline_source != NULL) {
        dispatch_activate(deadline_source);
    }
    if (source_to_cancel != NULL) {
        dispatch_source_cancel(source_to_cancel);
    }
    wait_try_deliver(request);
    wait_release_reference(request);
    return 1;
}

_Bool r_runtime_darwin_process_wait_cancel(RRuntimeDarwinProcessWait *request,
                                           uint64_t cancellation_sequence) {
    dispatch_source_t source_to_cancel = NULL;
    _Bool selected = 0;
    _Bool retained = 0;

    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        return 0;
    }
    if (!request->completion_delivered && request->references != 0U) {
        wait_retain_locked(request);
        retained = 1;
        source_to_cancel = wait_select_locked(
            request, R_RUNTIME_DARWIN_PROCESS_TERMINAL_CANCELLED, cancellation_sequence, 0, 0, 0);
        selected = request->result.terminal_event == R_RUNTIME_DARWIN_PROCESS_TERMINAL_CANCELLED &&
                   request->result.terminal_event_sequence == cancellation_sequence;
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    if (!retained) {
        return 0;
    }
    if (selected && source_to_cancel != NULL) {
        dispatch_source_cancel(source_to_cancel);
    }
    if (selected) {
        wait_try_deliver(request);
    }
    wait_release_reference(request);
    return selected;
}

RRuntimeDarwinProcessWaitResult
r_runtime_darwin_process_wait_result(RRuntimeDarwinProcessWait *request) {
    RRuntimeDarwinProcessWaitResult result = {0};

    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        return result;
    }
    if (request->completion_delivered) {
        result = request->result;
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    return result;
}

void r_runtime_darwin_process_wait_release(RRuntimeDarwinProcessWait *request) {
    RRuntimeDarwinProcessChild *child;
    _Bool destroy;

    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        return;
    }
    if (!request->completion_delivered || request->deadline_source != NULL ||
        request->deadline_cancel_pending || request->child == NULL ||
        process_service.active_wait_count == 0U) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    child = request->child;
    request->child = NULL;
    process_service.active_wait_count -= 1U;
    destroy = wait_release_reference_locked(request);
    if (pthread_cond_broadcast(&process_service.condition) != 0) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    child_release_reference(child);
    if (destroy) {
        wait_deallocate(request);
    }
}

void r_runtime_darwin_process_wait_abort(RRuntimeDarwinProcessWait **request_slot) {
    RRuntimeDarwinProcessWait *request;
    RRuntimeDarwinProcessChild *child;

    if (request_slot == NULL || *request_slot == NULL) {
        return;
    }
    request = *request_slot;
    *request_slot = NULL;
    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (request->bound || request->activated || request->terminal_selected ||
        request->completion_delivered || request->child == NULL ||
        request->child->wait_request != request || process_service.preparation_count == 0U ||
        request->references != 1U) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    child = request->child;
    child->wait_request = NULL;
    request->child = NULL;
    request->references = 0U;
    process_service.preparation_count -= 1U;
    if (pthread_cond_broadcast(&process_service.condition) != 0) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    wait_dispose_unactivated_deadline(request);
    child_release_reference(child);
    wait_deallocate(request);
}

RRuntimeDarwinProcessChild *
r_runtime_darwin_process_child_operation_retain(RRuntimeDarwinProcessChild *child) {
    RRuntimeDarwinProcessChild *result = NULL;

    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        return NULL;
    }
    if (process_service.running && !process_service.stopping && child->view_alive &&
        child->references != SIZE_MAX) {
        child->references += 1U;
        result = child;
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    return result;
}

void r_runtime_darwin_process_child_operation_release(RRuntimeDarwinProcessChild *child) {
    if (child != NULL) {
        child_release_reference(child);
    }
}

RRuntimeDarwinProcessTerminateResult
r_runtime_darwin_process_child_force_terminate(RRuntimeDarwinProcessChild *child) {
    RRuntimeDarwinProcessTerminateResult result = {0};
    pid_t process_id;

    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        return result;
    }
    if (child->reaped) {
        result.already_terminal = 1;
    }
    process_id = child->process_id;
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    if (result.already_terminal || process_id <= (pid_t)0) {
        result.already_terminal = 1;
        result.terminal_event_sequence = r_runtime_darwin_event_sequence_next();
        return result;
    }
    if (kill(process_id, SIGKILL) == 0) {
        result.accepted = 1;
    } else {
        result.native_error = errno;
        result.already_terminal = result.native_error == ESRCH;
    }
    result.terminal_event_sequence = r_runtime_darwin_event_sequence_next();
    return result;
}

void r_runtime_darwin_process_child_release(RRuntimeDarwinProcessChild *child) {
    RRuntimeDarwinIoHandle *pipes[3] = {NULL, NULL, NULL};
    _Bool destroy = 0;

    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (!child->view_alive) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    child->view_alive = 0;
    child_detach_pipes_locked(child, pipes);
    if (child->reaped && child->source == NULL) {
        destroy = child_unlink_locked(child);
    }
    if (child_release_reference_locked(child)) {
        if (destroy) {
            (void)pthread_mutex_unlock(&process_service.mutex);
            abort();
        }
        destroy = 1;
    }
    if (pthread_cond_broadcast(&process_service.condition) != 0) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    child_release_pipes(pipes);
    if (destroy) {
        child_deallocate(child);
    }
}

static _Bool service_has_unreaped_child_locked(void) {
    RRuntimeDarwinProcessChild *child;

    for (child = process_service.children; child != NULL; child = child->next) {
        if (!child->reaped) {
            return 1;
        }
    }
    return 0;
}

void r_runtime_darwin_process_lifecycle_stop(void) {
    dispatch_queue_t queue;
    RRuntimeDarwinProcessChild *child;

    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    if (!process_service.running || process_service.stopping) {
        (void)pthread_mutex_unlock(&process_service.mutex);
        abort();
    }
    process_service.stopping = 1;
    while (process_service.preparation_count != 0U || process_service.active_spawn_count != 0U ||
           process_service.active_wait_count != 0U) {
        if (pthread_cond_wait(&process_service.condition, &process_service.mutex) != 0) {
            abort();
        }
    }
    for (;;) {
        RRuntimeDarwinIoHandle *pipe = NULL;
        size_t index;

        for (child = process_service.children; child != NULL && pipe == NULL; child = child->next) {
            for (index = 0U; index < 3U; ++index) {
                if (child->pipes[index] != NULL) {
                    pipe = child->pipes[index];
                    child->pipes[index] = NULL;
                    break;
                }
            }
        }
        if (pipe == NULL) {
            break;
        }
        if (pthread_mutex_unlock(&process_service.mutex) != 0) {
            abort();
        }
        service_release_pipe(pipe);
        if (pthread_mutex_lock(&process_service.mutex) != 0) {
            abort();
        }
    }
    for (child = process_service.children; child != NULL; child = child->next) {
        if (!child->reaped && kill(child->process_id, SIGKILL) != 0 && errno != ESRCH) {
            (void)pthread_mutex_unlock(&process_service.mutex);
            abort();
        }
    }
    while (service_has_unreaped_child_locked() || process_service.source_count != 0U ||
           process_service.pipe_cleanup_count != 0U) {
        if (pthread_cond_wait(&process_service.condition, &process_service.mutex) != 0) {
            abort();
        }
    }
    while (process_service.children != NULL) {
        _Bool destroy;

        child = process_service.children;
        destroy = child_unlink_locked(child);
        if (destroy) {
            if (pthread_mutex_unlock(&process_service.mutex) != 0) {
                abort();
            }
            child_deallocate(child);
            if (pthread_mutex_lock(&process_service.mutex) != 0) {
                abort();
            }
        }
    }
    queue = process_service.submission_queue;
    process_service.submission_queue = NULL;
    process_service.allocator = NULL;
    process_service.running = 0;
    process_service.stopping = 0;
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    dispatch_release(queue);
}

#if defined(R_RUNTIME_DARWIN_PROCESS_TESTING)
void r_runtime_darwin_process_child_testing_wait_reaped(RRuntimeDarwinProcessChild *child) {
    if (child == NULL || pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    while (!child->reaped || child->source != NULL) {
        if (pthread_cond_wait(&process_service.condition, &process_service.mutex) != 0) {
            abort();
        }
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
}

size_t r_runtime_darwin_process_child_testing_pipe_count(RRuntimeDarwinProcessChild *child) {
    size_t count = 0U;
    size_t index;

    if (child == NULL || pthread_mutex_lock(&process_service.mutex) != 0) {
        return 0U;
    }
    for (index = 0U; index < 3U; ++index) {
        if (child->pipes[index] != NULL) {
            count += 1U;
        }
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    return count;
}

size_t r_runtime_darwin_process_testing_registry_count(void) {
    RRuntimeDarwinProcessChild *child;
    size_t count = 0U;

    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    for (child = process_service.children; child != NULL; child = child->next) {
        count += 1U;
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    return count;
}

uint64_t r_runtime_darwin_process_testing_reap_count(void) {
    uint64_t count;

    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    count = process_service.reap_count;
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
    return count;
}

void r_runtime_darwin_process_testing_wait_idle(void) {
    if (pthread_mutex_lock(&process_service.mutex) != 0) {
        abort();
    }
    while (process_service.preparation_count != 0U || process_service.active_spawn_count != 0U ||
           process_service.active_wait_count != 0U || process_service.children != NULL ||
           process_service.source_count != 0U || process_service.pipe_cleanup_count != 0U) {
        if (pthread_cond_wait(&process_service.condition, &process_service.mutex) != 0) {
            abort();
        }
    }
    if (pthread_mutex_unlock(&process_service.mutex) != 0) {
        abort();
    }
}
#endif
