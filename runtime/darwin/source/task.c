#include "r_runtime_task.h"

#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"

#include <dispatch/dispatch.h>
#include <limits.h>
#include <mach/mach_time.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef enum RRuntimeExecutorState {
    R_RUNTIME_EXECUTOR_STOPPED = 0,
    R_RUNTIME_EXECUTOR_RUNNING,
    R_RUNTIME_EXECUTOR_STOPPING
} RRuntimeExecutorState;

typedef enum RRuntimeTaskKind {
    R_RUNTIME_TASK_KIND_COMPUTATION = 0,
    R_RUNTIME_TASK_KIND_RESUMABLE,
    R_RUNTIME_TASK_KIND_EXTERNAL
} RRuntimeTaskKind;

typedef enum RRuntimeTaskExternalSelection {
    R_RUNTIME_TASK_EXTERNAL_SELECTION_NONE = 0,
    R_RUNTIME_TASK_EXTERNAL_SELECTION_COMPLETION,
    R_RUNTIME_TASK_EXTERNAL_SELECTION_CANCELLATION
} RRuntimeTaskExternalSelection;

struct RRuntimeTaskExecution {
    RRuntimeTask *task;
};

struct RRuntimeTaskExternalExecution {
    RRuntimeTask *task;
};

struct RRuntimeTask {
    pthread_mutex_t mutex;
    /* R-SLIB-ASYNC-0018: given under the executor lock when the start commits. */
    uint64_t id;
    pthread_cond_t condition;
    RRuntimeAllocator *allocator;
    RRuntimeTypeInfo payload_type;
    RRuntimeTypeInfo result_type;
    RRuntimeTaskBodyFn body;
    RRuntimeTaskStepFn step;
    RRuntimeTaskExternalStartFn external_start;
    RRuntimeTaskExternalCancelFn external_cancel;
    RRuntimeTaskExternalExecution external_execution;
    RRuntimeTask *next;
    RRuntimeTask *waiter;
    RRuntimeTask *waiting_on;
    RRuntimeTaskScope *scope;
    RRuntimeTaskScope *waiting_scope;
    /* A deadline-bounded scope wait: absolute monotonic nanoseconds while active. */
    uint64_t scope_deadline;
    _Bool scope_deadline_active;
    /* Core R-STMT-0019: read and written only by code executing this task, and copied into
     * each task this one prepares. */
    RRuntimeTaskDeadline deadline;
    /* Core R-STMT-0020: the budget charged for the allocations and starts of this task, held by
     * one reference; read and written only by code executing this task. counted when this task
     * counts under the budget of the task that prepared it. */
    RRuntimeBudget *budget;
    _Bool counted;
    size_t scope_index;
    size_t open_scope_count;
    size_t payload_offset;
    size_t result_offset;
    size_t allocation_alignment;
    size_t references;
    RRuntimeTaskKind kind;
    RRuntimeTaskState state;
    RRuntimeTaskExternalSelection external_selection;
    uint64_t external_selection_sequence;
    uint64_t external_cancellation_sequence;
    _Atomic _Bool committed;
    _Atomic _Bool cancel_requested;
    _Atomic _Bool terminal_published;
    _Bool preparation_active;
    _Bool observer_attached;
    _Bool observer_cleanup_active;
    _Bool payload_initialized;
    _Bool result_initialized;
    _Bool terminal_decided;
    _Bool external_start_ready;
    _Bool external_start_finished;
    _Bool external_cancel_scheduled;
    _Bool external_acknowledged;
    _Bool resume_pending;
    _Bool resumable_started;
    /* The task this thread was executing when this one started executing inline over it. */
    RRuntimeTask *executing_outer;
    /* The native thread this task waits to join, while it waits; zero otherwise. */
    uintptr_t joining_thread;
};

typedef struct RRuntimeExecutor {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    RRuntimeAllocator *allocator;
    dispatch_queue_t queue;
    void *reservation;
    RRuntimeTask *tasks;
    RRuntimeTask *tasks_tail;
    size_t preparation_count;
    size_t live_task_count;
    /* R-SLIB-ASYNC-0018: the identifier of the task committed last; zero before the first. */
    uint64_t last_task_id;
    RRuntimeExecutorState state;
} RRuntimeExecutor;

static _Bool r_runtime_executor_draining;

static RRuntimeExecutor r_runtime_executor = {
    PTHREAD_MUTEX_INITIALIZER,
    PTHREAD_COND_INITIALIZER,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    0U,
    0U,
    UINT64_C(0),
    R_RUNTIME_EXECUTOR_STOPPED,
};

static _Thread_local unsigned int r_runtime_executor_worker_depth;
/* The innermost task this thread executes; outer inline executions chain via executing_outer. */
static _Thread_local RRuntimeTask *r_runtime_executor_current_task;
/* The external execution whose blocking call the current pool thread runs (blocking_pool.inc). */
static _Thread_local const RRuntimeTaskExternalExecution *r_runtime_blocking_running;
static _Thread_local _Bool r_runtime_blocking_worker_thread;
static atomic_bool r_runtime_executor_exit_claimed;

static void executor_lock(void) {
    if (pthread_mutex_lock(&r_runtime_executor.mutex) != 0) {
        abort();
    }
}

static void executor_unlock(void) {
    if (pthread_mutex_unlock(&r_runtime_executor.mutex) != 0) {
        abort();
    }
}

static void task_lock(RRuntimeTask *task) {
    if (pthread_mutex_lock(&task->mutex) != 0) {
        abort();
    }
}

static void task_unlock(RRuntimeTask *task) {
    if (pthread_mutex_unlock(&task->mutex) != 0) {
        abort();
    }
}

static _Bool align_up(size_t value, size_t alignment, size_t *result) {
    const size_t mask = alignment - 1U;

    if (value > SIZE_MAX - mask) {
        return 0;
    }
    *result = (value + mask) & ~mask;
    return 1;
}

static void move_initialize(RRuntimeTypeInfo type, void *destination, void *source) {
    if (type.size == 0U) {
        return;
    }
    if (type.move_initialize != NULL) {
        type.move_initialize(destination, source);
    } else {
        (void)memcpy(destination, source, type.size);
    }
}

static void drop_value(RRuntimeTypeInfo type, void *value) {
    if (type.size != 0U && type.drop != NULL) {
        type.drop(value);
    }
}

static void *task_payload(RRuntimeTask *task) {
    if (task->payload_type.size == 0U) {
        return NULL;
    }
    return (unsigned char *)task + task->payload_offset;
}

static void *task_result(RRuntimeTask *task) {
    if (task->result_type.size == 0U) {
        return NULL;
    }
    return (unsigned char *)task + task->result_offset;
}

static RRuntimeTaskPrepareResult prepare_result(RRuntimeTask *transaction,
                                                RRuntimeTaskStartStatus status) {
    RRuntimeTaskPrepareResult result;

    result.transaction = transaction;
    result.status = status;
    return result;
}

static RRuntimeTaskStartResult start_result(RRuntimeTask *task, RRuntimeTaskStartStatus status) {
    RRuntimeTaskStartResult result;

    result.task = task;
    result.status = status;
    return result;
}

static void task_deallocate(RRuntimeTask *task) {
    const size_t alignment = task->allocation_alignment;
    RRuntimeBudget *budget = task->budget;
    const _Bool counted = task->counted;

    if (task->payload_initialized || task->result_initialized || task->observer_cleanup_active ||
        task->references != 0U || task->waiter != NULL || task->waiting_on != NULL ||
        task->waiting_scope != NULL || task->scope != NULL || task->open_scope_count != 0U ||
        task->resume_pending || task->state == R_RUNTIME_TASK_SUSPENDED) {
        abort();
    }
    if (pthread_cond_destroy(&task->condition) != 0) {
        abort();
    }
    if (pthread_mutex_destroy(&task->mutex) != 0) {
        abort();
    }
    r_runtime_allocator_deallocate(task, alignment);
    if (counted) {
        r_runtime_budget_return_task(budget);
    }
    r_runtime_budget_release(budget);
}

static void task_release_reference(RRuntimeTask *task) {
    _Bool destroy;

    task_lock(task);
    if (task->references == 0U) {
        abort();
    }
    task->references -= 1U;
    destroy = task->references == 0U;
    task_unlock(task);
    if (destroy) {
        task_deallocate(task);
    }
}

static void task_worker(void *context);

static void task_dispatch(RRuntimeTask *task) {
    dispatch_queue_t queue = r_runtime_executor.queue;

    if (queue == NULL) {
        abort();
    }
    dispatch_async_f(queue, task, task_worker);
}

static _Bool task_request_resume_locked(RRuntimeTask *task) {
    if (task->kind != R_RUNTIME_TASK_KIND_RESUMABLE || task->terminal_decided) {
        return 0;
    }
    if (task->state == R_RUNTIME_TASK_SUSPENDED) {
        task->state = R_RUNTIME_TASK_QUEUED;
        task->resume_pending = 0;
        return 1;
    }
    if (task->state == R_RUNTIME_TASK_RUNNING) {
        task->resume_pending = 1;
    }
    return 0;
}

static void task_wake_waiter(RRuntimeTask *waiter, RRuntimeTask *completed) {
    _Bool dispatch_resume;

    task_lock(waiter);
    if (waiter->waiting_on != completed) {
        task_unlock(waiter);
        abort();
    }
    waiter->waiting_on = NULL;
    dispatch_resume = task_request_resume_locked(waiter);
    task_unlock(waiter);
    if (dispatch_resume) {
        task_dispatch(waiter);
    }
    task_release_reference(waiter);
}

static void task_scope_publish(RRuntimeTask *task);

static void task_publish_terminal(RRuntimeTask *task) {
    RRuntimeTask *waiter;

    task_lock(task);
    if (atomic_load_explicit(&task->terminal_published, memory_order_relaxed)) {
        task_unlock(task);
        abort();
    }
    atomic_store_explicit(&task->terminal_published, 1, memory_order_release);
    waiter = task->waiter;
    task->waiter = NULL;
    (void)pthread_cond_broadcast(&task->condition);
    task_unlock(task);
    task_scope_publish(task);
    if (waiter != NULL) {
        task_wake_waiter(waiter, task);
    }
}

static _Bool external_cancel_schedule_locked(RRuntimeTask *task) {
    if (task->kind != R_RUNTIME_TASK_KIND_EXTERNAL || !task->external_start_ready ||
        !task->external_start_finished ||
        task->external_selection != R_RUNTIME_TASK_EXTERNAL_SELECTION_CANCELLATION ||
        task->external_cancel_scheduled || task->external_acknowledged) {
        return 0;
    }
    if (task->references == SIZE_MAX) {
        abort();
    }
    task->references += 1U;
    task->external_cancel_scheduled = 1;
    return 1;
}

static void external_cancel_worker(void *context) {
    RRuntimeTask *task = context;

    if (r_runtime_executor_worker_depth == UINT_MAX) {
        abort();
    }
    r_runtime_executor_worker_depth += 1U;
    task->external_cancel(&task->external_execution, task_payload(task));
    r_runtime_executor_worker_depth -= 1U;
    task_release_reference(task);
    r_runtime_thread_local_cleanup_current();
    r_runtime_hosted_work_end();
}

static void external_cancel_dispatch(RRuntimeTask *task) {
    if (r_runtime_executor.queue == NULL) {
        abort();
    }
    r_runtime_hosted_work_begin();
    dispatch_async_f(r_runtime_executor.queue, task, external_cancel_worker);
}

static void preparation_finish_locked(RRuntimeTask *task) {
    if (!task->preparation_active || r_runtime_executor.preparation_count == 0U) {
        abort();
    }
    task->preparation_active = 0;
    r_runtime_executor.preparation_count -= 1U;
    (void)pthread_cond_broadcast(&r_runtime_executor.condition);
}

static void request_cancellation(RRuntimeTask *task) {
    _Bool dispatch_cancel = 0;
    _Bool dispatch_resume = 0;

    task_lock(task);
    if (!task->terminal_decided) {
        if (task->kind == R_RUNTIME_TASK_KIND_EXTERNAL) {
            task->external_selection_sequence = r_runtime_darwin_event_sequence_next();
            task->external_cancellation_sequence = task->external_selection_sequence;
            task->external_selection = R_RUNTIME_TASK_EXTERNAL_SELECTION_CANCELLATION;
            task->terminal_decided = 1;
            dispatch_cancel = external_cancel_schedule_locked(task);
        }
        atomic_store_explicit(&task->cancel_requested, 1, memory_order_release);
        dispatch_resume = task_request_resume_locked(task);
    }
    task_unlock(task);
    if (dispatch_cancel) {
        external_cancel_dispatch(task);
    }
    if (dispatch_resume) {
        task_dispatch(task);
    }
}

static void executor_remove_task(RRuntimeTask *task, _Bool cleanup_thread_local) {
    RRuntimeTask *previous = NULL;
    RRuntimeTask *cursor;

    executor_lock();
    cursor = r_runtime_executor.tasks;
    while (cursor != NULL && cursor != task) {
        previous = cursor;
        cursor = cursor->next;
    }
    if (cursor == NULL || r_runtime_executor.live_task_count == 0U) {
        abort();
    }
    if (previous == NULL) {
        r_runtime_executor.tasks = task->next;
    } else {
        previous->next = task->next;
    }
    if (r_runtime_executor.tasks_tail == task) {
        r_runtime_executor.tasks_tail = previous;
    }
    task->next = NULL;
    executor_unlock();
    task_release_reference(task);
    if (cleanup_thread_local)
        r_runtime_thread_local_cleanup_current();
    executor_lock();
    r_runtime_executor.live_task_count -= 1U;
    (void)pthread_cond_broadcast(&r_runtime_executor.condition);
    executor_unlock();
    r_runtime_hosted_work_end();
}

/* M27-5: an address-sanitizer build gives generated steps frames many times the measured
   bounds that decide whether a first step may run on the calling thread, so that build always
   dispatches it; running it inline is only an optimization. */
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define R_RUNTIME_INLINE_STEPS 0
#endif
#endif
#if !defined(R_RUNTIME_INLINE_STEPS) && defined(__SANITIZE_ADDRESS__)
#define R_RUNTIME_INLINE_STEPS 0
#endif
#ifndef R_RUNTIME_INLINE_STEPS
#define R_RUNTIME_INLINE_STEPS 1
#endif

/*
 * Runs one queued task to its next suspension or terminal state. on_worker marks the executor
 * callback: it owns the thread's stack bounds and the per-task thread-local cleanup. The inline
 * caller (start_commit_initialize_inline) runs on a thread that already executes R code, so
 * its thread-locals stay live and its bounds are already established.
 */
static void task_execute(RRuntimeTask *task, _Bool on_worker) {
    RRuntimeTaskExecution execution;
    _Bool execute_body;
    _Bool drop_result = 0;
    _Bool dispatch_resume = 0;
    RRuntimeTaskStepStatus step_status = R_RUNTIME_TASK_STEP_COMPLETED;
    RRuntimeBudget *outer_budget;

    if (on_worker && !r_runtime_stack_initialize_current_thread()) {
        r_runtime_panic(R_RUNTIME_PANIC_STACK_EXHAUSTION,
                        (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
    }
    if (!atomic_load_explicit(&task->committed, memory_order_acquire)) {
        abort();
    }
    task_lock(task);
    if (task->state != R_RUNTIME_TASK_QUEUED) {
        abort();
    }
    task->state = R_RUNTIME_TASK_RUNNING;
    task->resume_pending = 0;
    execute_body = !atomic_load_explicit(&task->cancel_requested, memory_order_acquire) ||
                   ((task->kind == R_RUNTIME_TASK_KIND_RESUMABLE) && task->resumable_started);
    if (execute_body && (task->kind == R_RUNTIME_TASK_KIND_RESUMABLE)) {
        task->resumable_started = 1;
    }
    task_unlock(task);

    if (execute_body) {
        execution.task = task;
        if (r_runtime_executor_worker_depth == UINT_MAX) {
            abort();
        }
        r_runtime_executor_worker_depth += 1U;
        task->executing_outer = r_runtime_executor_current_task;
        r_runtime_executor_current_task = task;
        outer_budget = r_runtime_budget_swap_current(task->budget);
        if (task->kind == R_RUNTIME_TASK_KIND_RESUMABLE) {
            step_status = task->step(&execution, task_payload(task), task_result(task));
        } else {
            task->body(&execution, task_payload(task), task_result(task));
        }
        (void)r_runtime_budget_swap_current(outer_budget);
        r_runtime_executor_current_task = task->executing_outer;
        task->executing_outer = NULL;
        if (on_worker) {
            r_runtime_thread_local_cleanup_current();
        }
        r_runtime_executor_worker_depth -= 1U;
    }

    if (task->kind == R_RUNTIME_TASK_KIND_RESUMABLE && execute_body &&
        step_status == R_RUNTIME_TASK_STEP_SUSPENDED) {
        task_lock(task);
        if (task->terminal_decided ||
            (task->waiting_on == NULL && task->waiting_scope == NULL && !task->resume_pending &&
             !atomic_load_explicit(&task->cancel_requested, memory_order_acquire))) {
            task_unlock(task);
            abort();
        }
        if (task->resume_pending ||
            ((task->waiting_on == NULL && task->waiting_scope == NULL) &&
             atomic_load_explicit(&task->cancel_requested, memory_order_acquire))) {
            task->state = R_RUNTIME_TASK_QUEUED;
            task->resume_pending = 0;
            dispatch_resume = 1;
        } else {
            task->state = R_RUNTIME_TASK_SUSPENDED;
        }
        task_unlock(task);
        if (dispatch_resume) {
            task_dispatch(task);
        }
        return;
    }
    if (task->kind == R_RUNTIME_TASK_KIND_RESUMABLE && execute_body &&
        (step_status != R_RUNTIME_TASK_STEP_COMPLETED) &&
        (step_status != R_RUNTIME_TASK_STEP_CANCELLED)) {
        abort();
    }

    if (task->open_scope_count != 0U || task->waiting_scope != NULL) {
        abort();
    }
    if (task->payload_initialized) {
        drop_value(task->payload_type, task_payload(task));
        task->payload_initialized = 0;
    }

    task_lock(task);
    task->result_initialized = execute_body &&
                               ((task->kind != R_RUNTIME_TASK_KIND_RESUMABLE) ||
                                (step_status == R_RUNTIME_TASK_STEP_COMPLETED)) &&
                               (task->result_type.size != 0U);
    if (atomic_load_explicit(&task->cancel_requested, memory_order_acquire) ||
        ((task->kind == R_RUNTIME_TASK_KIND_RESUMABLE) &&
         (step_status == R_RUNTIME_TASK_STEP_CANCELLED))) {
        task->state = R_RUNTIME_TASK_CANCELLED;
    } else {
        task->state = R_RUNTIME_TASK_COMPLETED;
    }
    task->resume_pending = 0;
    task->terminal_decided = 1;
    if (task->result_initialized &&
        (task->state == R_RUNTIME_TASK_CANCELLED || !task->observer_attached)) {
        task->result_initialized = 0;
        drop_result = 1;
    }
    task_unlock(task);

    if (drop_result) {
        drop_value(task->result_type, task_result(task));
    }

    task_publish_terminal(task);
    executor_remove_task(task, on_worker);
}

static void task_worker(void *context) {
    task_execute(context, 1);
}

RRuntimeExecutorStartStatus r_runtime_executor_lifecycle_start(RRuntimeAllocator *allocator) {
    RRuntimeAllocationStatus allocation_status;
    dispatch_queue_t queue;
    void *reservation = NULL;

    executor_lock();
    if (r_runtime_executor.state != R_RUNTIME_EXECUTOR_STOPPED) {
        executor_unlock();
        return R_RUNTIME_EXECUTOR_START_RUNTIME_STOPPING;
    }
    allocation_status =
        r_runtime_allocator_allocate(allocator, 1U, _Alignof(unsigned char), &reservation);
    if (allocation_status != R_RUNTIME_ALLOCATION_OK) {
        executor_unlock();
        return R_RUNTIME_EXECUTOR_START_ALLOCATION_FAILED;
    }
    queue = dispatch_queue_create("dev.r-lang.executor", DISPATCH_QUEUE_CONCURRENT);
    if (queue == NULL) {
        r_runtime_allocator_deallocate(reservation, _Alignof(unsigned char));
        executor_unlock();
        return R_RUNTIME_EXECUTOR_START_ALLOCATION_FAILED;
    }
    r_runtime_executor.allocator = allocator;
    r_runtime_executor.queue = queue;
    r_runtime_executor.reservation = reservation;
    r_runtime_executor.tasks = NULL;
    r_runtime_executor.tasks_tail = NULL;
    r_runtime_executor.preparation_count = 0U;
    r_runtime_executor.live_task_count = 0U;
    r_runtime_executor.state = R_RUNTIME_EXECUTOR_RUNNING;
    r_runtime_executor_draining = 0;
    executor_unlock();
    return R_RUNTIME_EXECUTOR_START_OK;
}

void r_runtime_executor_cancel_pending(void) {
    if (r_runtime_executor_worker_depth != 0U)
        abort();
    executor_lock();
    if (r_runtime_executor.state != R_RUNTIME_EXECUTOR_RUNNING)
        abort();
    r_runtime_executor_draining = 1;
    for (RRuntimeTask *task = r_runtime_executor.tasks; task != NULL; task = task->next)
        request_cancellation(task);
    executor_unlock();
}

_Bool r_runtime_executor_on_worker(void) {
    return r_runtime_executor_worker_depth != 0U;
}

static uintptr_t executor_thread_token(pthread_t thread) {
    uintptr_t token = (uintptr_t)0U;

    _Static_assert(sizeof(thread) <= sizeof(token), "pthread_t must fit in a thread token");
    (void)memcpy(&token, &thread, sizeof(thread));
    return token;
}

void r_runtime_executor_join_begin(pthread_t thread) {
    RRuntimeTask *task = r_runtime_executor_current_task;

    if (task != NULL) {
        task_lock(task);
        task->joining_thread = executor_thread_token(thread);
        task_unlock(task);
    }
}

void r_runtime_executor_join_end(void) {
    RRuntimeTask *task = r_runtime_executor_current_task;

    if (task != NULL) {
        task_lock(task);
        task->joining_thread = (uintptr_t)0U;
        task_unlock(task);
    }
}

static _Bool task_deadline_before(RRuntimeTaskDeadline left, RRuntimeTaskDeadline right) {
    return left.seconds < right.seconds ||
           (left.seconds == right.seconds && left.nanoseconds < right.nanoseconds);
}

RRuntimeTaskDeadline r_runtime_task_deadline_enter(RRuntimeTaskDeadline narrowing) {
    RRuntimeTask *task = r_runtime_executor_current_task;
    RRuntimeTaskDeadline previous = {0};

    if (task != NULL) {
        previous = task->deadline;
        if (narrowing.active && (!previous.active || task_deadline_before(narrowing, previous))) {
            task->deadline = narrowing;
        }
    }
    return previous;
}

void r_runtime_task_deadline_leave(RRuntimeTaskDeadline previous) {
    RRuntimeTask *task = r_runtime_executor_current_task;

    if (task != NULL) {
        task->deadline = previous;
    }
}

RRuntimeBudget *
r_runtime_task_budget_enter(_Bool has_bytes, uint64_t bytes, _Bool has_tasks, uint64_t tasks) {
    RRuntimeTask *task = r_runtime_executor_current_task;
    RRuntimeBudget *previous;
    RRuntimeBudget *entered;

    if (task == NULL) {
        return NULL;
    }
    previous = task->budget;
    entered = r_runtime_budget_create(previous, has_bytes, bytes, has_tasks, tasks);
    if (entered == NULL) {
        entered = r_runtime_budget_refusing();
    }
    task->budget = entered;
    (void)r_runtime_budget_swap_current(entered);
    return previous;
}

void r_runtime_task_budget_leave(RRuntimeBudget *previous) {
    RRuntimeTask *task = r_runtime_executor_current_task;
    RRuntimeBudget *left;

    if (task == NULL) {
        return;
    }
    left = task->budget;
    task->budget = previous;
    (void)r_runtime_budget_swap_current(previous);
    r_runtime_budget_release(left);
}

void r_runtime_task_deadline_narrow(_Bool *active, int64_t *seconds, uint32_t *nanoseconds) {
    const RRuntimeTask *task = r_runtime_executor_current_task;

    if (task == NULL || !task->deadline.active || active == NULL || seconds == NULL ||
        nanoseconds == NULL) {
        return;
    }
    if (!*active ||
        task_deadline_before(task->deadline, (RRuntimeTaskDeadline){1, *seconds, *nanoseconds})) {
        *active = 1;
        *seconds = task->deadline.seconds;
        *nanoseconds = task->deadline.nanoseconds;
    }
}

/* R-SLIB-ASYNC-0018: the task whose code runs on this thread, a call of the blocking pool
   included, or zero outside every task. The identifier never changes after the commit that
   published the running task, so reading it needs no lock. */
uint64_t r_runtime_task_current_id(void) {
    if (r_runtime_blocking_running != NULL) {
        return r_runtime_blocking_running->task->id;
    }
    return r_runtime_executor_current_task == NULL ? UINT64_C(0)
                                                   : r_runtime_executor_current_task->id;
}

static _Bool executor_task_on_current_thread(const RRuntimeTask *task) {
    if (r_runtime_blocking_running == &task->external_execution) {
        return 1;
    }
    for (const RRuntimeTask *cursor = r_runtime_executor_current_task; cursor != NULL;
         cursor = cursor->executing_outer) {
        if (cursor == task) {
            return 1;
        }
    }
    return 0;
}

/* Every task that is not executing on this thread is suspended or waits to join this thread,
   and nothing else is pending. */
static _Bool executor_quiescent_for_exit_locked(void) {
    const uintptr_t exiting_thread = executor_thread_token(pthread_self());

    if (r_runtime_executor.preparation_count != 0U) {
        return 0;
    }
    for (RRuntimeTask *task = r_runtime_executor.tasks; task != NULL; task = task->next) {
        _Bool suspended;

        if (executor_task_on_current_thread(task)) {
            continue;
        }
        task_lock(task);
        suspended =
            (task->kind != R_RUNTIME_TASK_KIND_EXTERNAL) &&
            (((task->state == R_RUNTIME_TASK_SUSPENDED) && !task->resume_pending) ||
             ((task->state == R_RUNTIME_TASK_RUNNING) && (task->joining_thread == exiting_thread)));
        task_unlock(task);
        if (!suspended) {
            return 0;
        }
    }
    return 1;
}

_Bool r_runtime_executor_quiesce_for_exit(void) {
    const struct timespec interval = {0, 1000000L};

    if (atomic_exchange_explicit(&r_runtime_executor_exit_claimed, 1, memory_order_acq_rel)) {
        for (;;) {
            (void)nanosleep(&interval, NULL);
        }
    }
    executor_lock();
    if (r_runtime_executor.state == R_RUNTIME_EXECUTOR_RUNNING) {
        r_runtime_executor.state = R_RUNTIME_EXECUTOR_STOPPING;
    }
    r_runtime_executor_draining = 1;
    for (RRuntimeTask *task = r_runtime_executor.tasks; task != NULL; task = task->next) {
        if (!executor_task_on_current_thread(task)) {
            request_cancellation(task);
        }
    }
    executor_unlock();
    for (;;) {
        _Bool quiescent;

        executor_lock();
        quiescent = executor_quiescent_for_exit_locked();
        executor_unlock();
        if (quiescent) {
            return 1;
        }
        (void)nanosleep(&interval, NULL);
    }
}

_Bool r_runtime_executor_lifecycle_stop(void) {
    dispatch_queue_t queue;
    void *reservation;
    RRuntimeTask *task;

    if (r_runtime_executor_worker_depth != 0U) {
        return 0;
    }
    executor_lock();
    if (r_runtime_executor.state == R_RUNTIME_EXECUTOR_STOPPED) {
        executor_unlock();
        return 0;
    }
    if (r_runtime_executor.state == R_RUNTIME_EXECUTOR_STOPPING) {
        while (r_runtime_executor.state == R_RUNTIME_EXECUTOR_STOPPING) {
            if (pthread_cond_wait(&r_runtime_executor.condition, &r_runtime_executor.mutex) != 0) {
                abort();
            }
        }
        executor_unlock();
        return 1;
    }

    r_runtime_executor.state = R_RUNTIME_EXECUTOR_STOPPING;
    for (task = r_runtime_executor.tasks; task != NULL; task = task->next) {
        request_cancellation(task);
    }
    while (r_runtime_executor.preparation_count != 0U || r_runtime_executor.live_task_count != 0U) {
        if (pthread_cond_wait(&r_runtime_executor.condition, &r_runtime_executor.mutex) != 0) {
            abort();
        }
    }
    queue = r_runtime_executor.queue;
    reservation = r_runtime_executor.reservation;
    r_runtime_executor.allocator = NULL;
    r_runtime_executor.queue = NULL;
    r_runtime_executor.reservation = NULL;
    executor_unlock();

    dispatch_release(queue);
    r_runtime_allocator_deallocate(reservation, _Alignof(unsigned char));

    executor_lock();
    if (r_runtime_executor.state != R_RUNTIME_EXECUTOR_STOPPING) {
        abort();
    }
    r_runtime_executor.state = R_RUNTIME_EXECUTOR_STOPPED;
    (void)pthread_cond_broadcast(&r_runtime_executor.condition);
    executor_unlock();
    return 1;
}

static RRuntimeTaskPrepareResult task_start_frame(RRuntimeTypeInfo payload_type,
                                                  RRuntimeTypeInfo result_type,
                                                  RRuntimeTaskKind kind,
                                                  RRuntimeTaskBodyFn body,
                                                  RRuntimeTaskStepFn step,
                                                  RRuntimeTaskExternalStartFn external_start,
                                                  RRuntimeTaskExternalCancelFn external_cancel) {
    RRuntimeAllocator *allocator;
    RRuntimeTask *task = NULL;
    RRuntimeAllocationStatus allocation_status;
    size_t payload_offset;
    size_t result_offset;
    size_t allocation_size;
    size_t allocation_alignment;

    executor_lock();
    if (r_runtime_executor.state != R_RUNTIME_EXECUTOR_RUNNING) {
        executor_unlock();
        return prepare_result(NULL, R_RUNTIME_TASK_START_RUNTIME_STOPPING);
    }
    allocator = r_runtime_executor.allocator;
    if (r_runtime_executor.preparation_count == SIZE_MAX) {
        executor_unlock();
        abort();
    }
    r_runtime_executor.preparation_count += 1U;
    r_runtime_hosted_work_begin();
    executor_unlock();

    if (!align_up(sizeof(RRuntimeTask), payload_type.alignment, &payload_offset) ||
        payload_type.size > SIZE_MAX - payload_offset) {
        allocation_status = R_RUNTIME_ALLOCATION_SIZE_OVERFLOW;
    } else if (!align_up(
                   payload_offset + payload_type.size, result_type.alignment, &result_offset) ||
               result_type.size > SIZE_MAX - result_offset) {
        allocation_status = R_RUNTIME_ALLOCATION_SIZE_OVERFLOW;
    } else {
        allocation_size = result_offset + result_type.size;
        allocation_alignment = _Alignof(RRuntimeTask);
        if (payload_type.alignment > allocation_alignment) {
            allocation_alignment = payload_type.alignment;
        }
        if (result_type.alignment > allocation_alignment) {
            allocation_alignment = result_type.alignment;
        }
        allocation_status = r_runtime_allocator_allocate(
            allocator, allocation_size, allocation_alignment, (void **)&task);
    }
    if (allocation_status != R_RUNTIME_ALLOCATION_OK) {
        executor_lock();
        if (r_runtime_executor.preparation_count == 0U) {
            abort();
        }
        r_runtime_executor.preparation_count -= 1U;
        (void)pthread_cond_broadcast(&r_runtime_executor.condition);
        executor_unlock();
        r_runtime_hosted_work_end();
        return prepare_result(NULL, R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }

    (void)memset(task, 0, sizeof(*task));
    if (pthread_mutex_init(&task->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(task, allocation_alignment);
        executor_lock();
        r_runtime_executor.preparation_count -= 1U;
        (void)pthread_cond_broadcast(&r_runtime_executor.condition);
        executor_unlock();
        r_runtime_hosted_work_end();
        return prepare_result(NULL, R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    if (pthread_cond_init(&task->condition, NULL) != 0) {
        (void)pthread_mutex_destroy(&task->mutex);
        r_runtime_allocator_deallocate(task, allocation_alignment);
        executor_lock();
        r_runtime_executor.preparation_count -= 1U;
        (void)pthread_cond_broadcast(&r_runtime_executor.condition);
        executor_unlock();
        r_runtime_hosted_work_end();
        return prepare_result(NULL, R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    task->payload_type = payload_type;
    task->result_type = result_type;
    task->allocator = allocator;
    task->body = body;
    task->step = step;
    task->external_start = external_start;
    task->external_cancel = external_cancel;
    task->external_execution.task = task;
    task->payload_offset = payload_offset;
    task->result_offset = result_offset;
    task->allocation_alignment = allocation_alignment;
    task->references = 1U;
    task->kind = kind;
    task->state = R_RUNTIME_TASK_PREPARED;
    task->preparation_active = 1;
    /* Core R-STMT-0019: everything started inside a deadline block inherits its deadline. */
    if (r_runtime_executor_current_task != NULL) {
        task->deadline = r_runtime_executor_current_task->deadline;
    }
    atomic_init(&task->committed, 0);
    atomic_init(&task->cancel_requested, 0);
    atomic_init(&task->terminal_published, 0);
    return prepare_result(task, R_RUNTIME_TASK_START_OK);
}

/* Core R-STMT-0020: a task prepared under a budget receives it, and an R task counts one task
   under it until the task is destroyed; a start beyond the limit is refused before the frame is
   allocated, with the refusal noted for the start error. Standard operations do not count. */
static RRuntimeTaskPrepareResult task_start_prepare(RRuntimeTypeInfo payload_type,
                                                    RRuntimeTypeInfo result_type,
                                                    RRuntimeTaskKind kind,
                                                    RRuntimeTaskBodyFn body,
                                                    RRuntimeTaskStepFn step,
                                                    RRuntimeTaskExternalStartFn external_start,
                                                    RRuntimeTaskExternalCancelFn external_cancel) {
    RRuntimeBudget *budget = r_runtime_budget_current();
    const _Bool counted = (budget != NULL) && (kind != R_RUNTIME_TASK_KIND_EXTERNAL);
    RRuntimeTaskPrepareResult prepared;

    if (counted && !r_runtime_budget_charge_task(budget)) {
        return prepare_result(NULL, R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    prepared = task_start_frame(
        payload_type, result_type, kind, body, step, external_start, external_cancel);
    if (prepared.transaction == NULL) {
        if (counted) {
            r_runtime_budget_return_task(budget);
        }
        return prepared;
    }
    r_runtime_budget_retain(budget);
    prepared.transaction->budget = budget;
    prepared.transaction->counted = counted;
    return prepared;
}

RRuntimeAllocator *r_runtime_task_start_allocator(const RRuntimeTask *transaction) {
    if ((transaction == NULL) || !transaction->preparation_active ||
        (transaction->state != R_RUNTIME_TASK_PREPARED) ||
        atomic_load_explicit(&transaction->committed, memory_order_acquire)) {
        return NULL;
    }
    return transaction->allocator;
}

RRuntimeTaskPrepareResult r_runtime_task_start_prepare(RRuntimeTypeInfo payload_type,
                                                       RRuntimeTypeInfo result_type,
                                                       RRuntimeTaskBodyFn body) {
    return task_start_prepare(
        payload_type, result_type, R_RUNTIME_TASK_KIND_COMPUTATION, body, NULL, NULL, NULL);
}

RRuntimeTaskPrepareResult r_runtime_task_resumable_start_prepare(RRuntimeTypeInfo payload_type,
                                                                 RRuntimeTypeInfo result_type,
                                                                 RRuntimeTaskStepFn step) {
    return task_start_prepare(
        payload_type, result_type, R_RUNTIME_TASK_KIND_RESUMABLE, NULL, step, NULL, NULL);
}

RRuntimeTaskPrepareResult
r_runtime_task_external_start_prepare(RRuntimeTypeInfo payload_type,
                                      RRuntimeTypeInfo result_type,
                                      RRuntimeTaskExternalStartFn start,
                                      RRuntimeTaskExternalCancelFn cancel) {
    return task_start_prepare(
        payload_type, result_type, R_RUNTIME_TASK_KIND_EXTERNAL, NULL, NULL, start, cancel);
}

static RRuntimeTaskStartResult task_start_commit(RRuntimeTask **transaction,
                                                 void *staged_payload,
                                                 RRuntimeTaskPayloadInitializeFn initialize,
                                                 const void *context,
                                                 _Bool initialize_in_place,
                                                 unsigned inline_mode,
                                                 size_t inline_step_stack_bytes) {
    RRuntimeTask *task;

    task = *transaction;
    *transaction = NULL;

    executor_lock();
    if (r_runtime_executor.state != R_RUNTIME_EXECUTOR_RUNNING) {
        preparation_finish_locked(task);
        executor_unlock();
        task->references = 0U;
        task_deallocate(task);
        r_runtime_hosted_work_end();
        return start_result(NULL, R_RUNTIME_TASK_START_RUNTIME_STOPPING);
    }

    if (initialize_in_place) {
        if (initialize != NULL) {
            initialize(task_payload(task), context);
        }
    } else {
        move_initialize(task->payload_type, task_payload(task), staged_payload);
    }
    task->payload_initialized = task->payload_type.size != 0U;
    task->observer_attached = 1;
    task->references = 2U;
    task->state =
        task->kind == R_RUNTIME_TASK_KIND_EXTERNAL ? R_RUNTIME_TASK_RUNNING : R_RUNTIME_TASK_QUEUED;
    preparation_finish_locked(task);
    if (r_runtime_executor.tasks_tail == NULL) {
        r_runtime_executor.tasks = task;
    } else {
        r_runtime_executor.tasks_tail->next = task;
    }
    r_runtime_executor.tasks_tail = task;
    if (r_runtime_executor.live_task_count == SIZE_MAX) {
        abort();
    }
    r_runtime_executor.live_task_count += 1U;
    if (r_runtime_executor.last_task_id == UINT64_MAX) {
        abort();
    }
    r_runtime_executor.last_task_id += 1U;
    task->id = r_runtime_executor.last_task_id;
    atomic_store_explicit(&task->committed, 1, memory_order_release);
    if (r_runtime_executor_draining)
        request_cancellation(task);
    executor_unlock();
    if (task->kind != R_RUNTIME_TASK_KIND_EXTERNAL) {
        /* inline_mode: 0 dispatch now; 1 run the first step on this thread when the stack
           allows; 2 leave the task queued for r_runtime_task_run_deferred. */
        if (R_RUNTIME_INLINE_STEPS && inline_mode == 1U &&
            task->kind == R_RUNTIME_TASK_KIND_RESUMABLE &&
            r_runtime_stack_can_require(inline_step_stack_bytes)) {
            task_execute(task, 0);
        } else if (inline_mode != 2U) {
            task_dispatch(task);
        }
    }
    if (task->kind == R_RUNTIME_TASK_KIND_EXTERNAL) {
        _Bool dispatch_cancel;

        task->external_start(&task->external_execution, task_payload(task), task_result(task));
        task_lock(task);
        if (!task->external_start_ready || task->external_start_finished) {
            task_unlock(task);
            abort();
        }
        task->external_start_finished = 1;
        dispatch_cancel = external_cancel_schedule_locked(task);
        task_unlock(task);
        if (dispatch_cancel) {
            external_cancel_dispatch(task);
        }
    }
    return start_result(task, R_RUNTIME_TASK_START_OK);
}

RRuntimeTaskStartResult r_runtime_task_start_commit(RRuntimeTask **transaction,
                                                    void *staged_payload) {
    return task_start_commit(transaction, staged_payload, NULL, NULL, 0, 0U, 0U);
}

RRuntimeTaskStartResult r_runtime_task_start_commit_initialize(
    RRuntimeTask **transaction, RRuntimeTaskPayloadInitializeFn initialize, const void *context) {
    return task_start_commit(transaction, NULL, initialize, context, 1, 0U, 0U);
}

RRuntimeTaskStartResult
r_runtime_task_start_commit_initialize_inline(RRuntimeTask **transaction,
                                              RRuntimeTaskPayloadInitializeFn initialize,
                                              const void *context,
                                              size_t step_stack_bytes) {
    return task_start_commit(transaction, NULL, initialize, context, 1, 1U, step_stack_bytes);
}

RRuntimeTaskStartResult r_runtime_task_start_commit_initialize_deferred(
    RRuntimeTask **transaction, RRuntimeTaskPayloadInitializeFn initialize, const void *context) {
    return task_start_commit(transaction, NULL, initialize, context, 1, 2U, 0U);
}

void r_runtime_task_run_deferred(RRuntimeTask *task, size_t step_stack_bytes) {
    if (task == NULL || !atomic_load_explicit(&task->committed, memory_order_acquire) ||
        task->kind != R_RUNTIME_TASK_KIND_RESUMABLE) {
        abort();
    }
    if (R_RUNTIME_INLINE_STEPS && r_runtime_stack_can_require(step_stack_bytes)) {
        task_execute(task, 0);
    } else {
        task_dispatch(task);
    }
}

void r_runtime_task_start_abort(RRuntimeTask **transaction) {
    RRuntimeTask *task;

    if (transaction == NULL || *transaction == NULL) {
        return;
    }
    task = *transaction;
    *transaction = NULL;
    if (atomic_load_explicit(&task->committed, memory_order_acquire)) {
        r_runtime_task_cancel(&task);
        return;
    }
    executor_lock();
    preparation_finish_locked(task);
    executor_unlock();
    task->references = 0U;
    task_deallocate(task);
    r_runtime_hosted_work_end();
}

RRuntimeTaskAwaitStatus r_runtime_task_await(RRuntimeTask **task_slot, void *result_storage) {
    RRuntimeTask *task;
    RRuntimeTaskAwaitStatus status;

    task = *task_slot;

    task_lock(task);
    if (!atomic_load_explicit(&task->terminal_published, memory_order_acquire) &&
        r_runtime_executor_worker_depth != 0U) {
        task_unlock(task);
        return R_RUNTIME_TASK_AWAIT_WOULD_BLOCK;
    }
    while (!atomic_load_explicit(&task->terminal_published, memory_order_acquire)) {
        if (pthread_cond_wait(&task->condition, &task->mutex) != 0) {
            abort();
        }
    }
    if (!task->observer_attached) {
        abort();
    }
    task->observer_attached = 0;
    *task_slot = NULL;
    if (task->state == R_RUNTIME_TASK_COMPLETED) {
        if (task->result_type.size != 0U) {
            if (!task->result_initialized) {
                abort();
            }
            move_initialize(task->result_type, result_storage, task_result(task));
            task->result_initialized = 0;
        }
        status = R_RUNTIME_TASK_AWAIT_OK;
    } else if (task->state == R_RUNTIME_TASK_CANCELLED) {
        if (task->result_initialized) {
            abort();
        }
        status = R_RUNTIME_TASK_AWAIT_CANCELLED;
    } else {
        abort();
    }
    task_unlock(task);
    task_scope_publish(task);
    task_release_reference(task);
    return status;
}

static RRuntimeTaskExecutionAwaitStatus task_execution_await_terminal(RRuntimeTask **task_slot,
                                                                      void *result_storage) {
    const RRuntimeTaskAwaitStatus status = r_runtime_task_await(task_slot, result_storage);

    switch (status) {
    case R_RUNTIME_TASK_AWAIT_OK:
        return R_RUNTIME_TASK_EXECUTION_AWAIT_OK;
    case R_RUNTIME_TASK_AWAIT_CANCELLED:
        return R_RUNTIME_TASK_EXECUTION_AWAIT_CANCELLED;
    case R_RUNTIME_TASK_AWAIT_INVALID:
    case R_RUNTIME_TASK_AWAIT_WOULD_BLOCK:
    default:
        return R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
    }
}

static _Bool task_arm_waiter(RRuntimeTask *parent, RRuntimeTask *awaited) {
    _Bool armed = 0;

    task_lock(parent);
    if (parent->kind == R_RUNTIME_TASK_KIND_RESUMABLE && parent->state == R_RUNTIME_TASK_RUNNING &&
        !parent->terminal_decided && parent->waiting_on == NULL && parent->waiting_scope == NULL) {
        if (parent->references == SIZE_MAX) {
            task_unlock(parent);
            abort();
        }
        parent->references += 1U;
        parent->waiting_on = awaited;
        armed = 1;
    }
    task_unlock(parent);
    return armed;
}

static void task_disarm_waiter(RRuntimeTask *parent, RRuntimeTask *awaited) {
    task_lock(parent);
    if (parent->waiting_on != awaited) {
        task_unlock(parent);
        abort();
    }
    parent->waiting_on = NULL;
    task_unlock(parent);
    task_release_reference(parent);
}

RRuntimeTaskExecutionAwaitStatus r_runtime_task_execution_await(RRuntimeTaskExecution *execution,
                                                                RRuntimeTask **task_slot,
                                                                void *result_storage) {
    RRuntimeTask *parent;
    RRuntimeTask *awaited;
    _Bool already_registered;

    if (execution == NULL || execution->task == NULL || task_slot == NULL || *task_slot == NULL) {
        return R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
    }
    parent = execution->task;
    awaited = *task_slot;
    if (parent == awaited || !atomic_load_explicit(&awaited->committed, memory_order_acquire) ||
        (awaited->result_type.size != 0U &&
         (result_storage == NULL ||
          ((uintptr_t)result_storage & (uintptr_t)(awaited->result_type.alignment - 1U)) != 0U))) {
        return R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
    }

    task_lock(parent);
    if (parent->kind != R_RUNTIME_TASK_KIND_RESUMABLE || parent->state != R_RUNTIME_TASK_RUNNING ||
        parent->terminal_decided) {
        task_unlock(parent);
        return R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
    }
    already_registered = parent->waiting_on == awaited;
    if (parent->waiting_scope != NULL || (parent->waiting_on != NULL && !already_registered)) {
        task_unlock(parent);
        return R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
    }
    task_unlock(parent);

    if (atomic_load_explicit(&parent->cancel_requested, memory_order_acquire)) {
        request_cancellation(awaited);
    }

    task_lock(awaited);
    if (atomic_load_explicit(&awaited->terminal_published, memory_order_acquire)) {
        task_unlock(awaited);
        return task_execution_await_terminal(task_slot, result_storage);
    }
    if (awaited->waiter != NULL) {
        const _Bool same_waiter = awaited->waiter == parent && already_registered;

        task_unlock(awaited);
        return same_waiter ? R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED
                           : R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
    }
    task_unlock(awaited);

    if (already_registered || !task_arm_waiter(parent, awaited)) {
        return R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
    }

    task_lock(awaited);
    if (atomic_load_explicit(&awaited->terminal_published, memory_order_acquire)) {
        task_unlock(awaited);
        task_disarm_waiter(parent, awaited);
        return task_execution_await_terminal(task_slot, result_storage);
    }
    if (awaited->waiter != NULL) {
        task_unlock(awaited);
        task_disarm_waiter(parent, awaited);
        return R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
    }
    awaited->waiter = parent;
    task_unlock(awaited);
    return R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED;
}

static void consume_observer(RRuntimeTask **task_slot, _Bool cancel) {
    RRuntimeTask *task;
    _Bool drop_result = 0;

    if (task_slot == NULL || *task_slot == NULL) {
        return;
    }
    task = *task_slot;
    *task_slot = NULL;
    if (!atomic_load_explicit(&task->committed, memory_order_acquire)) {
        r_runtime_task_start_abort(&task);
        return;
    }
    if (cancel) {
        request_cancellation(task);
    }
    task_lock(task);
    if (!task->observer_attached) {
        task_unlock(task);
        return;
    }
    task->observer_attached = 0;
    if (task->result_initialized) {
        task->result_initialized = 0;
        drop_result = 1;
        task->observer_cleanup_active = 1;
    }
    task_unlock(task);
    if (drop_result) {
        drop_value(task->result_type, task_result(task));
        task_lock(task);
        task->observer_cleanup_active = 0;
        task_unlock(task);
    }
    task_scope_publish(task);
    task_release_reference(task);
}

void r_runtime_task_cancel(RRuntimeTask **task) {
    consume_observer(task, 1);
}

void r_runtime_task_detach(RRuntimeTask **task) {
    consume_observer(task, 0);
}

void r_runtime_task_destroy(RRuntimeTask **task) {
    consume_observer(task, 1);
}

static RRuntimeTask *external_task(RRuntimeTaskExternalExecution *execution) {
    RRuntimeTask *task;

    task = execution->task;
    return task;
}

void r_runtime_task_external_start_ready(RRuntimeTaskExternalExecution *execution) {
    RRuntimeTask *task = external_task(execution);
    _Bool dispatch_cancel;

    if (task == NULL) {
        abort();
    }
    task_lock(task);
    if (task->external_start_ready || task->external_acknowledged ||
        !atomic_load_explicit(&task->committed, memory_order_acquire)) {
        task_unlock(task);
        abort();
    }
    task->external_start_ready = 1;
    dispatch_cancel = external_cancel_schedule_locked(task);
    task_unlock(task);
    if (dispatch_cancel) {
        external_cancel_dispatch(task);
    }
}

/*
 * The selection is itself the event, so its sequence is taken under the task lock and a selected
 * cancellation is never replaced. Taking the sequence before the lock let a cancellation chosen in
 * between be replaced while its cancel callback, which acknowledges a withdrawn waiter, was
 * already scheduled: the waiter was acknowledged twice (P4.1-8).
 */
_Bool r_runtime_task_external_try_select_completion(RRuntimeTaskExternalExecution *execution) {
    RRuntimeTask *task = external_task(execution);
    _Bool selected = 0;

    task_lock(task);
    if (!task->external_acknowledged &&
        task->external_selection == R_RUNTIME_TASK_EXTERNAL_SELECTION_NONE) {
        task->external_selection = R_RUNTIME_TASK_EXTERNAL_SELECTION_COMPLETION;
        task->external_selection_sequence = r_runtime_darwin_event_sequence_next();
        task->terminal_decided = 1;
        selected = 1;
    }
    task_unlock(task);
    return selected;
}

_Bool r_runtime_task_external_try_select_completion_at(RRuntimeTaskExternalExecution *execution,
                                                       uint64_t event_sequence) {
    RRuntimeTask *task = external_task(execution);
    _Bool selected = 0;

    task_lock(task);
    if (!task->external_acknowledged &&
        task->external_selection == R_RUNTIME_TASK_EXTERNAL_SELECTION_NONE) {
        task->external_selection = R_RUNTIME_TASK_EXTERNAL_SELECTION_COMPLETION;
        task->external_selection_sequence = event_sequence;
        task->terminal_decided = 1;
        selected = 1;
    } else if (!task->external_acknowledged &&
               task->external_selection == R_RUNTIME_TASK_EXTERNAL_SELECTION_CANCELLATION &&
               event_sequence < task->external_selection_sequence) {
        task->external_selection = R_RUNTIME_TASK_EXTERNAL_SELECTION_COMPLETION;
        task->external_selection_sequence = event_sequence;
        selected = 1;
    }
    task_unlock(task);
    return selected;
}

_Bool r_runtime_task_external_select_terminal_completion(RRuntimeTaskExternalExecution *execution) {
    RRuntimeTask *task = external_task(execution);
    _Bool selected = 0;

    task_lock(task);
    if (!task->external_acknowledged &&
        task->external_selection == R_RUNTIME_TASK_EXTERNAL_SELECTION_CANCELLATION) {
        task->external_selection = R_RUNTIME_TASK_EXTERNAL_SELECTION_COMPLETION;
        task->external_selection_sequence = r_runtime_darwin_event_sequence_next();
        selected = 1;
    }
    task_unlock(task);
    return selected;
}

void r_runtime_task_external_acknowledge(RRuntimeTaskExternalExecution *execution) {
    RRuntimeTask *task = external_task(execution);
    _Bool drop_payload;
    _Bool drop_result = 0;

    if (task == NULL) {
        abort();
    }
    task_lock(task);
    if (!task->external_start_ready || task->external_acknowledged ||
        task->external_selection == R_RUNTIME_TASK_EXTERNAL_SELECTION_NONE) {
        task_unlock(task);
        abort();
    }
    task->external_acknowledged = 1;
    if (task->external_selection == R_RUNTIME_TASK_EXTERNAL_SELECTION_COMPLETION) {
        task->state = R_RUNTIME_TASK_COMPLETED;
        task->result_initialized = task->result_type.size != 0U;
    } else {
        task->state = R_RUNTIME_TASK_CANCELLED;
    }
    drop_payload = task->payload_initialized;
    task->payload_initialized = 0;
    if (task->result_initialized &&
        (task->state == R_RUNTIME_TASK_CANCELLED || !task->observer_attached)) {
        task->result_initialized = 0;
        drop_result = 1;
    }
    task_unlock(task);

    if (drop_payload) {
        drop_value(task->payload_type, task_payload(task));
    }
    if (drop_result) {
        drop_value(task->result_type, task_result(task));
    }

    task_publish_terminal(task);
    executor_remove_task(task, 1);
}

_Bool r_runtime_task_external_cancel_requested(const RRuntimeTaskExternalExecution *execution) {
    return atomic_load_explicit(&execution->task->cancel_requested, memory_order_acquire);
}

uint64_t
r_runtime_task_external_cancellation_sequence(const RRuntimeTaskExternalExecution *execution) {
    RRuntimeTask *task;
    uint64_t sequence;

    task = execution->task;
    task_lock(task);
    sequence = task->external_cancellation_sequence;
    task_unlock(task);
    return sequence;
}

RRuntimeTaskState r_runtime_task_state(RRuntimeTask *task) {
    RRuntimeTaskState state;

    task_lock(task);
    state = task->state;
    task_unlock(task);
    return state;
}

_Bool r_runtime_task_execution_cancel_requested(const RRuntimeTaskExecution *execution) {
    return atomic_load_explicit(&execution->task->cancel_requested, memory_order_acquire);
}

#include "blocking_pool.inc"
#include "task_scope.inc"
