module runtime.tasks;
import runtime.types;
import runtime.types::{SourceSpan, PanicCategory, PanicReport, TypeInfo, Allocator, AllocationStatus,
                       Budget, TaskHandle, ExecutionHandle, ExternalHandle};
import runtime.system;
import runtime.system::{Mutex, Condition, Thread, Queue, Object, TimeSpec, Timebase};
import runtime.heap;
import runtime.panics;
import runtime.stacks;
import runtime.threads;

// The tasks of the darwin target (r_runtime_task.h): an executor over a concurrent Dispatch queue,
// task frames with their payload and result, the one linearized terminal outcome of external
// tasks (R-SLIB-ASYNC-0007), supervision scopes, deadlines and budgets of tasks, and the pool of
// threads that run blocking calls (Core R-TERM-0015).
@header("r_runtime_task.h")
extern "C" {
}

@link(name = "r.runtime", kind = "static")
@header("r_runtime_task.h")
@header("r_runtime_target_abi.h")
@abi("r-runtime-abi")
extern "C" {
    // Compiler-owned, immovable storage in a resumable frame; members are runtime-private.
    @repr(C)
    @c_type(name = "RRuntimeTaskScopeEntry", kind = "typedef")
    struct ScopeEntry {
        @c_field(name = "task")
        raw TaskHandle*? frame;
        c_bool reserved;
    };

    @repr(C)
    @c_type(name = "RRuntimeTaskScope", kind = "typedef")
    struct Scope {
        raw ScopeEntry*? entries;
        c_size capacity;
        raw TaskHandle*? owner;
        raw TaskHandle*? waiter;
        c_bool active;
        c_bool closing;
    };

    @repr(C)
    @c_type(name = "RRuntimeTaskStepStatus", kind = "typedef")
    enum StepStatus : c_uint {
        R_RUNTIME_TASK_STEP_COMPLETED = 0,
        R_RUNTIME_TASK_STEP_SUSPENDED = 1,
        R_RUNTIME_TASK_STEP_CANCELLED = 2,
        R_RUNTIME_TASK_STEP_PANICKED = 3,
    };

    @repr(C)
    @c_type(name = "RRuntimeExecutorStartStatus", kind = "typedef")
    enum ExecutorStartStatus : c_uint {
        R_RUNTIME_EXECUTOR_START_OK = 0,
        R_RUNTIME_EXECUTOR_START_INVALID = 1,
        R_RUNTIME_EXECUTOR_START_ALLOCATION_FAILED = 2,
        R_RUNTIME_EXECUTOR_START_RUNTIME_STOPPING = 3,
    };

    @repr(C)
    @c_type(name = "RRuntimeTaskStartStatus", kind = "typedef")
    enum StartStatus : c_uint {
        R_RUNTIME_TASK_START_OK = 0,
        R_RUNTIME_TASK_START_INVALID = 1,
        R_RUNTIME_TASK_START_ALLOCATION_FAILED = 2,
        R_RUNTIME_TASK_START_RUNTIME_STOPPING = 3,
    };

    @repr(C)
    @c_type(name = "RRuntimeTaskState", kind = "typedef")
    enum TaskState : c_uint {
        R_RUNTIME_TASK_INVALID = 0,
        R_RUNTIME_TASK_PREPARED = 1,
        R_RUNTIME_TASK_QUEUED = 2,
        R_RUNTIME_TASK_RUNNING = 3,
        R_RUNTIME_TASK_COMPLETED = 4,
        R_RUNTIME_TASK_CANCELLED = 5,
        R_RUNTIME_TASK_SUSPENDED = 6,
    };

    @repr(C)
    @c_type(name = "RRuntimeTaskAwaitStatus", kind = "typedef")
    enum AwaitStatus : c_uint {
        R_RUNTIME_TASK_AWAIT_OK = 0,
        R_RUNTIME_TASK_AWAIT_INVALID = 1,
        R_RUNTIME_TASK_AWAIT_CANCELLED = 2,
        R_RUNTIME_TASK_AWAIT_WOULD_BLOCK = 3,
        R_RUNTIME_TASK_AWAIT_PANICKED = 4,
    };

    @repr(C)
    @c_type(name = "RRuntimeTaskExecutionAwaitStatus", kind = "typedef")
    enum ExecutionAwaitStatus : c_uint {
        R_RUNTIME_TASK_EXECUTION_AWAIT_OK = 0,
        R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID = 1,
        R_RUNTIME_TASK_EXECUTION_AWAIT_CANCELLED = 2,
        R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED = 3,
        R_RUNTIME_TASK_EXECUTION_AWAIT_PANICKED = 4,
    };

    @repr(C)
    @c_type(name = "RRuntimeTaskPrepareResult", kind = "typedef")
    struct PrepareResult {
        raw TaskHandle*? transaction;
        StartStatus status;
    };

    @repr(C)
    @c_type(name = "RRuntimeTaskStartResult", kind = "typedef")
    struct StartResult {
        @c_field(name = "task")
        raw TaskHandle*? frame;
        StartStatus status;
    };

    @repr(C)
    @c_type(name = "RRuntimeTaskDeadline", kind = "typedef")
    struct Deadline {
        c_bool active;
        c_int64 seconds;
        c_uint32 nanoseconds;
    };

    @repr(C)
    @c_type(name = "RRuntimeBlockingJob", kind = "struct")
    struct BlockingJob {
        raw BlockingJob*? previous;
        raw BlockingJob*? next;
        raw fn?(raw BlockingJob*?) -> void run;
        raw const ExternalHandle*? execution;
        c_uint state;
    };

    @repr(C)
    @c_type(name = "RRuntimeBlockingReserveStatus", kind = "typedef")
    enum BlockingReserveStatus : c_uint {
        R_RUNTIME_BLOCKING_RESERVED = 0,
        R_RUNTIME_BLOCKING_QUEUE_FULL = 1,
        R_RUNTIME_BLOCKING_THREAD_UNAVAILABLE = 2,
        R_RUNTIME_BLOCKING_STOPPING = 3,
    };

    @c_constant(name = "R_RUNTIME_BLOCKING_THREAD_COUNT")
    const c_uint blocking_thread_count = 4u32 as c_uint;
    @c_constant(name = "R_RUNTIME_GENERATED_FRAME_MAX_BYTES")
    const c_size generated_frame_max_bytes = 262144u64 as c_size;
}

enum Kind {
    computation,
    resumable,
    external,
};

enum Selection {
    unselected,
    completion,
    cancellation,
};

enum ExecutorState {
    stopped,
    running,
    stopping,
};

enum State {
    invalid,
    prepared,
    queued,
    running,
    completed,
    cancelled,
    suspended,
};

// The record behind RRuntimeTaskExecution and RRuntimeTaskExternalExecution.
struct Execution {
    raw Task*? frame = null;
};

// The record behind an RRuntimeTask handle, followed in its allocation by the payload and the
// result of the task; C sees an incomplete type.
struct Task {
    Mutex mutex = {.signature = 0i64 as c_long, .bytes = {}};
    // R-SLIB-ASYNC-0018: given under the executor lock when the start commits.
    u64 id = 0u64;
    Condition condition = {.signature = 0i64 as c_long, .bytes = {}};
    raw Allocator*? allocator = null;
    TypeInfo payload_type = {.size = 0u64 as c_size, .alignment = 1u64 as c_size,
                             .move_initialize = null, .drop_value = null};
    TypeInfo result_type = {.size = 0u64 as c_size, .alignment = 1u64 as c_size,
                            .move_initialize = null, .drop_value = null};
    raw fn?(raw ExecutionHandle*?, raw void*?, raw void*?) -> void body = null;
    raw fn?(raw ExecutionHandle*?, raw void*?, raw void*?) -> c_uint step = null;
    raw fn?(raw ExternalHandle*?, raw void*?, raw void*?) -> void external_start = null;
    raw fn?(raw ExternalHandle*?, raw void*?) -> void external_cancel = null;
    Execution external_execution = {};
    raw Task*? next = null;
    raw Task*? waiter = null;
    raw Task*? waiting_on = null;
    raw Scope*? scope = null;
    raw Scope*? waiting_scope = null;
    // A deadline-bounded scope wait: absolute monotonic nanoseconds while active.
    u64 scope_deadline = 0u64;
    bool scope_deadline_active = false;
    // Core R-STMT-0019: read and written only by code executing this task, and copied into each
    // task this one prepares.
    Deadline deadline = {.active = 0i32 as c_bool, .seconds = 0i64 as c_int64,
                         .nanoseconds = 0u32 as c_uint32};
    // Core R-STMT-0020: the budget charged for the allocations and starts of this task, held by
    // one reference; read and written only by code executing this task. counted while this task
    // counts under the budget of the task that prepared it: from its start until its await
    // completes, which clears it under the task lock, or until its release when nothing awaits it.
    raw Budget*? budget = null;
    bool counted = false;
    usize scope_index = 0usize;
    usize open_scope_count = 0usize;
    usize payload_offset = 0usize;
    usize result_offset = 0usize;
    usize allocation_alignment = 0usize;
    usize references = 0usize;
    Kind kind = Kind::computation;
    State state = State::invalid;
    Selection external_selection = Selection::unselected;
    u64 external_selection_sequence = 0u64;
    u64 external_cancellation_sequence = 0u64;
    atomic bool committed = false;
    atomic bool cancel_requested = false;
    atomic bool terminal_published = false;
    bool preparation_active = false;
    bool observer_attached = false;
    bool observer_cleanup_active = false;
    bool payload_initialized = false;
    bool result_initialized = false;
    bool terminal_decided = false;
    bool external_start_ready = false;
    bool external_start_finished = false;
    bool external_cancel_scheduled = false;
    bool external_acknowledged = false;
    bool resume_pending = false;
    bool resumable_started = false;
    // Core R-ERR-0005 (L39): the task ended with a panic; its report waits for the observer or,
    // when nobody observes it, is delivered to the panic hook once.
    bool panicked = false;
    bool panic_delivered = false;
    bool panic_parked = false;
    // A panic that began where no panic exit tests it, in a drop on a return, break or
    // checked-error path or in a thread-local drop, and was still pending when the step
    // suspended; it is pending again when the step resumes.
    bool panic_stray = false;
    PanicReport panic_report = {.category = PanicCategory::R_RUNTIME_PANIC_EXPLICIT,
                                .span = {.module_id = 0u32 as c_uint32, .start = 0u32 as c_uint32,
                                         .end = 0u32 as c_uint32},
                                .text_length = 0u32 as c_uint32, .text = {}};
    // The task this thread was executing when this one started executing inline over it.
    raw Task*? executing_outer = null;
    // The native thread this task waits to join, while it waits; zero otherwise.
    usize joining_thread = 0usize;
};

// The layout of a task frame.
struct Layout {
    usize payload_offset = 0usize;
    usize result_offset = 0usize;
    usize size = 0usize;
    usize alignment = 0usize;
};

// A waiter that a publication removed from a scope, to resume and release after the locks.
struct Wakeup {
    raw Task*? waiter = null;
    bool dispatch = false;
};

// The outcome of a scope wait decided under the executor lock.
struct WaitOutcome {
    ExecutionAwaitStatus status =
        ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
    bool armed = false;
    u64 remaining = 0u64;
};

const u32 u32_maximum = 4294967295u32;
const u64 u64_maximum = 18446744073709551615u64;
const u64 i64_maximum = 9223372036854775807u64;

// ---- The executor ----

Mutex executor_mutex = {.signature = runtime.system::mutex_signature as c_long, .bytes = {}};
Condition executor_condition = {.signature = runtime.system::condition_signature as c_long,
                                .bytes = {}};
raw Allocator*? executor_allocator = null;
raw Queue*? executor_queue = null;
raw void*? executor_reservation = null;
raw Task*? executor_tasks = null;
raw Task*? executor_tasks_tail = null;
usize preparation_count = 0usize;
usize live_task_count = 0usize;
ExecutorState executor_state = ExecutorState::stopped;
bool executor_draining = false;
// R-SLIB-ASYNC-0018: the identifier of the task committed last; zero before the first. A direct
// call (r_runtime_task_direct_begin) takes its identifier without the executor lock.
atomic u64 last_task_id = 0u64;
// Whether a direct call may run now: the executor runs and no drain is in progress. Written under
// the executor lock wherever those change.
atomic bool direct_open = false;
atomic bool exit_claimed = false;

thread_local u32 worker_depth = 0u32;
// The innermost task this thread executes; outer inline executions chain via executing_outer.
thread_local raw Task*? current_task = null;
// P4.4: the identifier taken by the direct call running on this thread, or zero.
thread_local u64 direct_task_id = 0u64;
// The task whose blocking call the current pool thread runs.
thread_local raw Task*? blocking_running = null;
thread_local bool blocking_worker_thread = false;

// Testing: runs once on the worker that executed a task with a budget (M42-3).
atomic raw fn?() -> void testing_after_end = null;

protected never fail() {
    unsafe {
        runtime.system::abort();
    }
}

protected SourceSpan no_span() {
    return SourceSpan {.module_id = 0u32 as c_uint32, .start = 0u32 as c_uint32,
                       .end = 0u32 as c_uint32};
}

protected PanicReport no_report() {
    return PanicReport {.category = PanicCategory::R_RUNTIME_PANIC_EXPLICIT, .span = no_span(),
                        .text_length = 0u32 as c_uint32, .text = {}};
}

protected Deadline no_deadline() {
    return Deadline {.active = runtime.types::c_bool_of(false), .seconds = 0i64 as c_int64,
                     .nanoseconds = 0u32 as c_uint32};
}

protected void executor_lock() {
    unsafe {
        runtime.system::require_zero(
            runtime.system::pthread_mutex_lock(&executor_mutex as raw Mutex*));
    }
}

protected void executor_unlock() {
    unsafe {
        runtime.system::require_zero(
            runtime.system::pthread_mutex_unlock(&executor_mutex as raw Mutex*));
    }
}

protected void executor_wait() {
    unsafe {
        runtime.system::require_zero(runtime.system::pthread_cond_wait(
            &executor_condition as raw Condition*, &executor_mutex as raw Mutex*));
    }
}

protected void executor_broadcast() {
    unsafe {
        runtime.system::pthread_cond_broadcast(&executor_condition as raw Condition*) as void;
    }
}

protected void task_lock(raw Task*? frame) {
    unsafe {
        runtime.system::require_zero(runtime.system::pthread_mutex_lock(&frame->mutex as raw Mutex*));
    }
}

protected void task_unlock(raw Task*? frame) {
    unsafe {
        runtime.system::require_zero(
            runtime.system::pthread_mutex_unlock(&frame->mutex as raw Mutex*));
    }
}

protected raw Task*? task_of(raw TaskHandle*? handle) {
    unsafe {
        return (handle as raw void*?) as raw Task*?;
    }
}

protected raw Task*? task_of_const(raw const TaskHandle*? handle) {
    unsafe {
        return core::cast_mutable(handle as raw const void*?) as raw Task*?;
    }
}

protected raw TaskHandle*? handle_of(raw Task*? frame) {
    unsafe {
        return (frame as raw void*?) as raw TaskHandle*?;
    }
}

protected raw Execution*? execution_of(raw ExecutionHandle*? handle) {
    unsafe {
        return (handle as raw void*?) as raw Execution*?;
    }
}

protected raw Execution*? execution_of_const(raw const ExecutionHandle*? handle) {
    unsafe {
        return core::cast_mutable(handle as raw const void*?) as raw Execution*?;
    }
}

protected raw ExecutionHandle*? execution_handle(raw Execution*? execution) {
    unsafe {
        return (execution as raw void*?) as raw ExecutionHandle*?;
    }
}

protected raw Execution*? external_of(raw ExternalHandle*? handle) {
    unsafe {
        return (handle as raw void*?) as raw Execution*?;
    }
}

protected raw Execution*? external_of_const(raw const ExternalHandle*? handle) {
    unsafe {
        return core::cast_mutable(handle as raw const void*?) as raw Execution*?;
    }
}

protected raw ExternalHandle*? external_handle(raw Task*? frame) {
    unsafe {
        raw Execution* execution = &frame->external_execution as raw Execution*;
        return (execution as raw void*?) as raw ExternalHandle*?;
    }
}

protected raw Task*? external_task(raw ExternalHandle*? execution) {
    unsafe {
        raw Execution*? record = external_of(execution);
        return record->frame;
    }
}

protected void move_into(TypeInfo type, raw void*? destination, raw void*? source) {
    if ((type.size as usize) == 0usize) {
        return;
    }
    runtime.types::move_value(type, destination, source);
}

protected void drop_value(TypeInfo type, raw void*? value) {
    if ((type.size as usize) == 0usize) {
        return;
    }
    unsafe {
        raw fn?(raw void*?) -> void glue = type.drop_value;
        if (glue != null) {
            glue(value);
        }
    }
}

protected raw void*? task_payload(raw Task*? frame) {
    unsafe {
        if ((frame->payload_type.size as usize) == 0usize) {
            return null;
        }
        return core::offset_bytes(frame, frame->payload_offset);
    }
}

protected raw void*? task_result(raw Task*? frame) {
    unsafe {
        if ((frame->result_type.size as usize) == 0usize) {
            return null;
        }
        return core::offset_bytes(frame, frame->result_offset);
    }
}

protected PrepareResult prepare_result(raw Task*? transaction, StartStatus status) {
    return PrepareResult {.transaction = handle_of(transaction), .status = status};
}

protected StartResult start_result(raw Task*? frame, StartStatus status) {
    return StartResult {.frame = handle_of(frame), .status = status};
}

protected bool unwinding() {
    if (core::atomic_load(&runtime.panics::r_runtime_unwinding_threads,
                          core::memory_order::relaxed) as u32 == 0u32) {
        return false;
    }
    return runtime.types::is_true(runtime.panics::r_runtime_unwinding_current_thread());
}

protected void task_deallocate(raw Task*? frame) {
    unsafe {
        usize alignment = frame->allocation_alignment;
        raw Budget*? budget = frame->budget;
        bool counted = frame->counted;
        bool linked = (frame->waiter != null) || (frame->waiting_on != null) ||
                      (frame->waiting_scope != null) || (frame->scope != null);
        if ((frame->payload_initialized == true) || (frame->result_initialized == true) ||
            (frame->observer_cleanup_active == true) || (frame->references != 0usize) ||
            (linked == true) || (frame->open_scope_count != 0usize) ||
            (frame->resume_pending == true) || (frame->state == State::suspended)) {
            fail();
        }
        runtime.system::require_zero(
            runtime.system::pthread_cond_destroy(&frame->condition as raw Condition*));
        runtime.system::require_zero(
            runtime.system::pthread_mutex_destroy(&frame->mutex as raw Mutex*));
        runtime.heap::r_runtime_allocator_deallocate(frame as raw void*?, alignment as c_size);
        if (counted == true) {
            runtime.heap::r_runtime_budget_return_task(budget);
        }
        runtime.heap::r_runtime_budget_release(budget);
    }
}

protected void task_release_reference(raw Task*? frame) {
    task_lock(frame);
    unsafe {
        if (frame->references == 0usize) {
            fail();
        }
        frame->references -= 1usize;
        bool destroy = frame->references == 0usize;
        task_unlock(frame);
        if (destroy == true) {
            task_deallocate(frame);
        }
    }
}

protected void task_execute(raw Task*? frame, bool on_worker);

@callback
@safety("R-RUNTIME-TASK-WORKER", "context is a queued task")
protected extern "C" void task_worker(raw void* context) {
    unsafe {
        task_execute(context as raw Task*?, true);
    }
}

protected void task_dispatch(raw Task*? frame) {
    unsafe {
        raw Queue*? queue = executor_queue;
        if (queue == null) {
            fail();
        }
        runtime.system::dispatch_async_f(queue, frame as raw void*?, task_worker);
    }
}

protected bool request_resume_locked(raw Task*? frame) {
    unsafe {
        if ((frame->kind != Kind::resumable) || (frame->terminal_decided == true)) {
            return false;
        }
        if (frame->state == State::suspended) {
            frame->state = State::queued;
            frame->resume_pending = false;
            return true;
        }
        if (frame->state == State::running) {
            frame->resume_pending = true;
        }
        return false;
    }
}

protected void wake_waiter(raw Task*? waiter, raw Task*? completed) {
    task_lock(waiter);
    unsafe {
        if (waiter->waiting_on != completed) {
            task_unlock(waiter);
            fail();
        }
        waiter->waiting_on = null;
        bool dispatch_resume = request_resume_locked(waiter);
        task_unlock(waiter);
        if (dispatch_resume == true) {
            task_dispatch(waiter);
        }
    }
    task_release_reference(waiter);
}

// ---- Scope links, protected by the executor lock, always acquired before a task lock ----

protected raw ScopeEntry*? entry_at(raw Scope*? scope, usize index) {
    unsafe {
        return core::offset_bytes(scope->entries, index * sizeof(ScopeEntry)) as raw ScopeEntry*?;
    }
}

protected Wakeup scope_disarm_locked(raw Scope*? scope) {
    unsafe {
        raw Task*? waiter = task_of(scope->waiter);
        if (waiter == null) {
            return Wakeup {};
        }
        scope->waiter = null;
        task_lock(waiter);
        if (waiter->waiting_scope != scope) {
            fail();
        }
        waiter->waiting_scope = null;
        bool dispatch_resume = request_resume_locked(waiter);
        task_unlock(waiter);
        return Wakeup {.waiter = waiter, .dispatch = dispatch_resume};
    }
}

protected void wake(Wakeup wakeup) {
    unsafe {
        if (wakeup.waiter != null) {
            if (wakeup.dispatch == true) {
                task_dispatch(wakeup.waiter);
            }
            task_release_reference(wakeup.waiter);
        }
    }
}

protected void scope_publish(raw Task*? frame) {
    Wakeup wakeup = {};
    bool release_member = false;
    executor_lock();
    unsafe {
        // Load the frame-owned scope only while close is excluded by the same lock.
        bool published = core::atomic_load(&frame->terminal_published, core::memory_order::acquire);
        if ((published == true) && (frame->scope != null)) {
            raw Scope*? scope = frame->scope;
            task_lock(frame);
            if ((frame->observer_attached == false) && (frame->observer_cleanup_active == false)) {
                raw ScopeEntry*? entry = entry_at(scope, frame->scope_index);
                if ((runtime.types::is_true(scope->active) == false) ||
                    (task_of(entry->frame) != frame)) {
                    fail();
                }
                entry->frame = null;
                frame->scope = null;
                release_member = true;
            }
            task_unlock(frame);
            wakeup = scope_disarm_locked(scope);
        }
    }
    executor_unlock();
    wake(wakeup);
    if (release_member == true) {
        task_release_reference(frame);
    }
}

protected void publish_terminal(raw Task*? frame) {
    task_lock(frame);
    unsafe {
        if (core::atomic_load(&frame->terminal_published, core::memory_order::relaxed) == true) {
            task_unlock(frame);
            fail();
        }
        core::atomic_store(&frame->terminal_published, true, core::memory_order::release);
        raw Task*? waiter = frame->waiter;
        frame->waiter = null;
        runtime.system::pthread_cond_broadcast(&frame->condition as raw Condition*) as void;
        task_unlock(frame);
        scope_publish(frame);
        if (waiter != null) {
            wake_waiter(waiter, frame);
        }
    }
}

protected bool external_cancel_schedule_locked(raw Task*? frame) {
    unsafe {
        bool chosen = frame->external_selection == Selection::cancellation;
        if ((frame->kind != Kind::external) || (frame->external_start_ready == false) ||
            (frame->external_start_finished == false) || (chosen == false) ||
            (frame->external_cancel_scheduled == true) || (frame->external_acknowledged == true)) {
            return false;
        }
        if (frame->references == runtime.types::size_maximum) {
            fail();
        }
        frame->references += 1usize;
        frame->external_cancel_scheduled = true;
        return true;
    }
}

@callback
@safety("R-RUNTIME-TASK-EXTERNAL-CANCEL-WORKER", "context is a task retained for its cancel")
protected extern "C" void external_cancel_worker(raw void* context) {
    unsafe {
        raw Task*? frame = context as raw Task*?;
        if (worker_depth == u32_maximum) {
            fail();
        }
        worker_depth += 1u32;
        raw fn?(raw ExternalHandle*?, raw void*?) -> void cancel = frame->external_cancel;
        if (cancel != null) {
            cancel(external_handle(frame), task_payload(frame));
        } else {
            fail();
        }
        worker_depth -= 1u32;
        task_release_reference(frame);
    }
    runtime.threads::r_runtime_thread_local_cleanup_current();
    runtime.threads::r_runtime_hosted_work_end();
}

protected void external_cancel_dispatch(raw Task*? frame) {
    unsafe {
        raw Queue*? queue = executor_queue;
        if (queue == null) {
            fail();
        }
        runtime.threads::r_runtime_hosted_work_begin();
        runtime.system::dispatch_async_f(queue, frame as raw void*?, external_cancel_worker);
    }
}

protected void preparation_finish_locked(raw Task*? frame) {
    unsafe {
        if ((frame->preparation_active == false) || (preparation_count == 0usize)) {
            fail();
        }
        frame->preparation_active = false;
        preparation_count -= 1usize;
    }
    executor_broadcast();
}

protected void request_cancellation(raw Task*? frame) {
    bool dispatch_cancel = false;
    bool dispatch_resume = false;
    task_lock(frame);
    unsafe {
        if (frame->terminal_decided == false) {
            if (frame->kind == Kind::external) {
                frame->external_selection_sequence =
                    runtime.types::r_runtime_darwin_event_sequence_next() as u64;
                frame->external_cancellation_sequence = frame->external_selection_sequence;
                frame->external_selection = Selection::cancellation;
                frame->terminal_decided = true;
                dispatch_cancel = external_cancel_schedule_locked(frame);
            }
            core::atomic_store(&frame->cancel_requested, true, core::memory_order::release);
            dispatch_resume = request_resume_locked(frame);
        }
    }
    task_unlock(frame);
    if (dispatch_cancel == true) {
        external_cancel_dispatch(frame);
    }
    if (dispatch_resume == true) {
        task_dispatch(frame);
    }
}

protected void executor_remove_task(raw Task*? frame, bool cleanup_thread_local) {
    executor_lock();
    unsafe {
        raw Task*? previous = null;
        raw Task*? cursor = executor_tasks;
        while ((cursor != null) && (cursor != frame)) {
            previous = cursor;
            cursor = cursor->next;
        }
        if ((cursor == null) || (live_task_count == 0usize)) {
            fail();
        }
        if (previous == null) {
            executor_tasks = frame->next;
        } else {
            previous->next = frame->next;
        }
        if (executor_tasks_tail == frame) {
            executor_tasks_tail = previous;
        }
        frame->next = null;
    }
    executor_unlock();
    task_release_reference(frame);
    if (cleanup_thread_local == true) {
        runtime.threads::r_runtime_thread_local_cleanup_current();
    }
    executor_lock();
    unsafe {
        live_task_count -= 1usize;
    }
    executor_broadcast();
    executor_unlock();
    runtime.threads::r_runtime_hosted_work_end();
}

// The status a step returned (RRuntimeTaskStepStatus); any other value breaks the step contract.
protected StepStatus step_status_of(c_uint value) {
    u32 status = value as u32;
    if (status == 0u32) {
        return StepStatus::R_RUNTIME_TASK_STEP_COMPLETED;
    }
    if (status == 1u32) {
        return StepStatus::R_RUNTIME_TASK_STEP_SUSPENDED;
    }
    if (status == 2u32) {
        return StepStatus::R_RUNTIME_TASK_STEP_CANCELLED;
    }
    if (status == 3u32) {
        return StepStatus::R_RUNTIME_TASK_STEP_PANICKED;
    }
    fail();
}

// Runs a step or body with this thread executing the task; returns the status of the step, or
// completed for a body.
protected StepStatus run_body(raw Task*? frame) {
    unsafe {
        Execution execution = {.frame = frame};
        raw ExecutionHandle*? handle = execution_handle(&execution as raw Execution*);
        if (frame->kind == Kind::resumable) {
            raw fn?(raw ExecutionHandle*?, raw void*?, raw void*?) -> c_uint step = frame->step;
            if (step == null) {
                fail();
            }
            return step_status_of(step(handle, task_payload(frame), task_result(frame)));
        }
        raw fn?(raw ExecutionHandle*?, raw void*?, raw void*?) -> void body = frame->body;
        if (body == null) {
            fail();
        }
        body(handle, task_payload(frame), task_result(frame));
    }
    return StepStatus::R_RUNTIME_TASK_STEP_COMPLETED;
}

// Runs one queued task to its next suspension or terminal state. on_worker marks the executor
// callback: it owns the thread's stack bounds and the per-task thread-local cleanup. The inline
// caller (start_commit_initialize_inline) runs on a thread that already executes R code, so its
// thread-locals stay live and its bounds are already established.
protected void task_execute(raw Task*? frame, bool on_worker) {
    if (on_worker == true) {
        if (runtime.types::is_true(runtime.stacks::r_runtime_stack_initialize_current_thread()) ==
            false) {
            runtime.panics::r_runtime_panic(12u32 as c_uint, no_span());
        }
    }
    unsafe {
        if (core::atomic_load(&frame->committed, core::memory_order::acquire) == false) {
            fail();
        }
        task_lock(frame);
        if (frame->state != State::queued) {
            fail();
        }
        frame->state = State::running;
        frame->resume_pending = false;
        bool cancelled = core::atomic_load(&frame->cancel_requested, core::memory_order::acquire);
        bool resumed = (frame->kind == Kind::resumable) && (frame->resumable_started == true);
        bool execute_body = (cancelled == false) || (resumed == true);
        if ((execute_body == true) && (frame->kind == Kind::resumable)) {
            frame->resumable_started = true;
        }
        task_unlock(frame);

        StepStatus step_status = StepStatus::R_RUNTIME_TASK_STEP_COMPLETED;
        if (execute_body == true) {
            if (worker_depth == u32_maximum) {
                fail();
            }
            worker_depth += 1u32;
            frame->executing_outer = current_task;
            current_task = frame;
            raw Budget*? outer_budget = runtime.heap::r_runtime_budget_swap_current(frame->budget);
            if (frame->panic_stray == true) {
                frame->panic_stray = false;
                runtime.panics::r_runtime_panic_resume(
                    &frame->panic_report as raw const PanicReport*);
            }
            step_status = run_body(frame);
            if (step_status == StepStatus::R_RUNTIME_TASK_STEP_PANICKED) {
                // R-ERR-0005: thread-local and frame drops below run as cleanup of the panic; a
                // panic that begins in them is a second panic (R-ERR-0008).
                runtime.panics::r_runtime_unwind_cleanup_enter();
            }
            runtime.heap::r_runtime_budget_swap_current(outer_budget) as void;
            current_task = frame->executing_outer;
            frame->executing_outer = null;
            if (on_worker == true) {
                runtime.threads::r_runtime_thread_local_cleanup_current();
            }
            // L39 (R-ERR-0009): a panic still pending here began where no panic exit tests it. A
            // suspended step keeps it in its task, so that this thread runs other tasks without
            // it; a step that ended ends the task with it, and the result it wrote is dropped.
            if ((step_status != StepStatus::R_RUNTIME_TASK_STEP_PANICKED) && (unwinding() == true)) {
                if (step_status == StepStatus::R_RUNTIME_TASK_STEP_SUSPENDED) {
                    PanicReport stray = no_report();
                    runtime.panics::r_runtime_panic_take(&stray as raw PanicReport*) as void;
                    if (frame->panic_parked == true) {
                        // It began on the way of a parked panic: a second panic (R-ERR-0008).
                        runtime.panics::r_runtime_panic_second(
                            &frame->panic_report as raw const PanicReport*,
                            stray.category as c_uint, stray.span);
                    }
                    frame->panic_report = stray;
                    frame->panic_stray = true;
                } else {
                    runtime.panics::r_runtime_unwind_cleanup_enter();
                    if ((step_status == StepStatus::R_RUNTIME_TASK_STEP_COMPLETED) &&
                        ((frame->result_type.size as usize) != 0usize)) {
                        drop_value(frame->result_type, task_result(frame));
                    }
                    step_status = StepStatus::R_RUNTIME_TASK_STEP_PANICKED;
                }
            }
            worker_depth -= 1u32;
        }

        bool resumable_ran = (frame->kind == Kind::resumable) && (execute_body == true);
        if ((resumable_ran == true) && (step_status == StepStatus::R_RUNTIME_TASK_STEP_SUSPENDED)) {
            task_lock(frame);
            bool cancel = core::atomic_load(&frame->cancel_requested, core::memory_order::acquire);
            bool unwaited = (frame->waiting_on == null) && (frame->waiting_scope == null);
            bool idle = (cancel == false) && (unwaited == true) && (frame->resume_pending == false);
            if ((frame->terminal_decided == true) || (idle == true)) {
                task_unlock(frame);
                fail();
            }
            bool dispatch_resume = ((cancel == true) && (unwaited == true)) ||
                                   (frame->resume_pending == true);
            if (dispatch_resume == true) {
                frame->state = State::queued;
                frame->resume_pending = false;
            } else {
                frame->state = State::suspended;
            }
            task_unlock(frame);
            if (dispatch_resume == true) {
                task_dispatch(frame);
            }
            return;
        }
        if ((resumable_ran == true) && (step_status != StepStatus::R_RUNTIME_TASK_STEP_COMPLETED) &&
            (step_status != StepStatus::R_RUNTIME_TASK_STEP_CANCELLED) &&
            (step_status != StepStatus::R_RUNTIME_TASK_STEP_PANICKED)) {
            fail();
        }

        if ((frame->open_scope_count != 0usize) || (frame->waiting_scope != null)) {
            fail();
        }
        if (frame->payload_initialized == true) {
            drop_value(frame->payload_type, task_payload(frame));
            frame->payload_initialized = false;
        }
        if (step_status == StepStatus::R_RUNTIME_TASK_STEP_PANICKED) {
            runtime.panics::r_runtime_unwind_cleanup_leave();
            if (runtime.types::is_true(runtime.panics::r_runtime_panic_take(
                    &frame->panic_report as raw PanicReport*)) == false) {
                fail();
            }
            frame->panicked = true;
        }

        task_lock(frame);
        bool step_completed = (frame->kind != Kind::resumable) ||
                              (step_status == StepStatus::R_RUNTIME_TASK_STEP_COMPLETED);
        frame->result_initialized = (execute_body == true) && (frame->panicked == false) &&
                                   (step_completed == true) &&
                                   ((frame->result_type.size as usize) != 0usize);
        if (frame->panicked == true) {
            // A panic is the terminal outcome even when cancellation was requested; an observer
            // that cancelled or detached leaves the report to the hook.
            frame->state = State::completed;
        } else {
            bool cancel = core::atomic_load(&frame->cancel_requested, core::memory_order::acquire);
            bool step_cancelled = (frame->kind == Kind::resumable) &&
                                  (step_status == StepStatus::R_RUNTIME_TASK_STEP_CANCELLED);
            if ((cancel == true) || (step_cancelled == true)) {
                frame->state = State::cancelled;
            } else {
                frame->state = State::completed;
            }
        }
        frame->resume_pending = false;
        frame->terminal_decided = true;
        bool drop_result = (frame->result_initialized == true) &&
                           ((frame->state == State::cancelled) || (frame->observer_attached == false));
        if (drop_result == true) {
            frame->result_initialized = false;
        }
        bool deliver_panic = (frame->panicked == true) && (frame->observer_attached == false) &&
                             (frame->panic_delivered == false);
        if (deliver_panic == true) {
            frame->panic_delivered = true;
        }
        task_unlock(frame);

        if (drop_result == true) {
            drop_value(frame->result_type, task_result(frame));
        }
        if (deliver_panic == true) {
            runtime.panics::r_runtime_panic_deliver(&frame->panic_report as raw const PanicReport*);
        }

        publish_terminal(frame);
        if ((on_worker == true) && (frame->budget != null)) {
            // An unarmed hook costs one atomic load.
            if (core::atomic_load(&testing_after_end, core::memory_order::acquire) != null) {
                raw fn?() -> void hook =
                    core::atomic_exchange(&testing_after_end, null, core::memory_order::acq_rel);
                if (hook != null) {
                    hook();
                }
            }
        }
        executor_remove_task(frame, on_worker);
    }
}

// ---- Lifecycle of the executor ----

@safety("R-RUNTIME-EXECUTOR-START", "allocator outlives the generation")
extern "C" ExecutorStartStatus r_runtime_executor_lifecycle_start(raw Allocator*? allocator) {
    executor_lock();
    unsafe {
        if (executor_state != ExecutorState::stopped) {
            executor_unlock();
            return ExecutorStartStatus::R_RUNTIME_EXECUTOR_START_RUNTIME_STOPPING;
        }
        raw void*? reservation = null;
        AllocationStatus allocation_status = runtime.heap::r_runtime_allocator_allocate(
            allocator, 1u64 as c_size, 1u64 as c_size, &reservation as raw (raw void*?)*);
        if (allocation_status != AllocationStatus::R_RUNTIME_ALLOCATION_OK) {
            executor_unlock();
            return ExecutorStartStatus::R_RUNTIME_EXECUTOR_START_ALLOCATION_FAILED;
        }
        raw Queue*? queue = runtime.system::dispatch_queue_create(
            runtime.system::c_string("dev.r-lang.executor\0"),
            runtime.system::_dispatch_queue_attr_concurrent);
        if (queue == null) {
            runtime.heap::r_runtime_allocator_deallocate(reservation, 1u64 as c_size);
            executor_unlock();
            return ExecutorStartStatus::R_RUNTIME_EXECUTOR_START_ALLOCATION_FAILED;
        }
        executor_allocator = allocator;
        executor_queue = queue;
        executor_reservation = reservation;
        executor_tasks = null;
        executor_tasks_tail = null;
        preparation_count = 0usize;
        live_task_count = 0usize;
        executor_state = ExecutorState::running;
        executor_draining = false;
    }
    core::atomic_store(&direct_open, true, core::memory_order::release);
    executor_unlock();
    return ExecutorStartStatus::R_RUNTIME_EXECUTOR_START_OK;
}

@safety("R-RUNTIME-EXECUTOR-CANCEL-PENDING", "None")
extern "C" void r_runtime_executor_cancel_pending() {
    if (worker_depth != 0u32) {
        fail();
    }
    executor_lock();
    unsafe {
        if (executor_state != ExecutorState::running) {
            fail();
        }
        executor_draining = true;
        core::atomic_store(&direct_open, false, core::memory_order::release);
        raw Task*? frame = executor_tasks;
        while (frame != null) {
            request_cancellation(frame);
            frame = frame->next;
        }
    }
    executor_unlock();
}

@safety("R-RUNTIME-EXECUTOR-ON-WORKER", "None")
extern "C" c_bool r_runtime_executor_on_worker() {
    return runtime.types::c_bool_of(worker_depth != 0u32);
}

@safety("R-RUNTIME-EXECUTOR-JOIN-BEGIN", "thread is a native thread")
extern "C" void r_runtime_executor_join_begin(raw Thread*? thread) {
    raw Task*? frame = current_task;
    unsafe {
        if (frame != null) {
            task_lock(frame);
            frame->joining_thread = core::pointer_address(thread);
            task_unlock(frame);
        }
    }
}

@safety("R-RUNTIME-EXECUTOR-JOIN-END", "None")
extern "C" void r_runtime_executor_join_end() {
    raw Task*? frame = current_task;
    unsafe {
        if (frame != null) {
            task_lock(frame);
            frame->joining_thread = 0usize;
            task_unlock(frame);
        }
    }
}

// ---- Deadlines and budgets of tasks ----

protected bool deadline_before(Deadline left, Deadline right) {
    i64 left_seconds = left.seconds as i64;
    i64 right_seconds = right.seconds as i64;
    bool tie = (left_seconds == right_seconds) &&
               ((left.nanoseconds as u32) < (right.nanoseconds as u32));
    return (left_seconds < right_seconds) || (tie == true);
}

@safety("R-RUNTIME-TASK-DEADLINE-ENTER", "None")
extern "C" Deadline r_runtime_task_deadline_enter(Deadline narrowing) {
    raw Task*? frame = current_task;
    unsafe {
        if (frame == null) {
            return no_deadline();
        }
        Deadline previous = frame->deadline;
        bool narrower = (runtime.types::is_true(previous.active) == false) ||
                        (deadline_before(narrowing, previous) == true);
        if ((runtime.types::is_true(narrowing.active) == true) && (narrower == true)) {
            frame->deadline = narrowing;
        }
        return previous;
    }
}

@safety("R-RUNTIME-TASK-DEADLINE-LEAVE", "previous was returned by the matching enter")
extern "C" void r_runtime_task_deadline_leave(Deadline previous) {
    raw Task*? frame = current_task;
    unsafe {
        if (frame != null) {
            frame->deadline = previous;
        }
    }
}

@safety("R-RUNTIME-TASK-BUDGET-ENTER", "None")
extern "C" raw Budget*? r_runtime_task_budget_enter(c_bool has_bytes, c_uint64 bytes,
                                                    c_bool has_tasks, c_uint64 task_limit) {
    raw Task*? frame = current_task;
    unsafe {
        if (frame == null) {
            return null;
        }
        raw Budget*? previous = frame->budget;
        raw Budget*? entered =
            runtime.heap::r_runtime_budget_create(previous, has_bytes, bytes, has_tasks, task_limit);
        if (entered == null) {
            entered = runtime.heap::r_runtime_budget_refusing();
        }
        frame->budget = entered;
        runtime.heap::r_runtime_budget_swap_current(entered) as void;
        return previous;
    }
}

@safety("R-RUNTIME-TASK-BUDGET-LEAVE", "previous was returned by the matching enter")
extern "C" void r_runtime_task_budget_leave(raw Budget*? previous) {
    raw Task*? frame = current_task;
    unsafe {
        if (frame == null) {
            return;
        }
        raw Budget*? left = frame->budget;
        frame->budget = previous;
        runtime.heap::r_runtime_budget_swap_current(previous) as void;
        runtime.heap::r_runtime_budget_release(left);
    }
}

@safety("R-RUNTIME-TASK-DEADLINE-NARROW", "the three places are null or writable")
extern "C" void r_runtime_task_deadline_narrow(raw c_bool*? active, raw c_int64*? seconds,
                                               raw c_uint32*? nanoseconds) {
    raw Task*? frame = current_task;
    unsafe {
        if ((frame == null) || (active == null) || (seconds == null) || (nanoseconds == null)) {
            return;
        }
        Deadline inherited = frame->deadline;
        if (runtime.types::is_true(inherited.active) == false) {
            return;
        }
        Deadline given = {.active = runtime.types::c_bool_of(true), .seconds = *seconds,
                          .nanoseconds = *nanoseconds};
        if ((runtime.types::is_true(*active) == false) || (deadline_before(inherited, given) == true)) {
            *active = runtime.types::c_bool_of(true);
            *seconds = inherited.seconds;
            *nanoseconds = inherited.nanoseconds;
        }
    }
}

// R-SLIB-ASYNC-0018: the task whose code runs on this thread, a call of the blocking pool and a
// direct call (P4.4) included, or zero outside every task. The identifier never changes after the
// commit that published the running task, so reading it needs no lock.
@safety("R-RUNTIME-TASK-CURRENT-ID", "None")
extern "C" c_uint64 r_runtime_task_current_id() {
    if (direct_task_id != 0u64) {
        return direct_task_id as c_uint64;
    }
    unsafe {
        raw Task*? running = blocking_running;
        if (running != null) {
            return running->id as c_uint64;
        }
        raw Task*? frame = current_task;
        if (frame == null) {
            return 0u64 as c_uint64;
        }
        return frame->id as c_uint64;
    }
}

protected bool on_current_thread(raw Task*? frame) {
    unsafe {
        if (blocking_running == frame) {
            return true;
        }
        raw Task*? cursor = current_task;
        while (cursor != null) {
            if (cursor == frame) {
                return true;
            }
            cursor = cursor->executing_outer;
        }
        return false;
    }
}

// Every task that is not executing on this thread is suspended or waits to join this thread, and
// nothing else is pending.
protected bool quiescent_for_exit_locked() {
    unsafe {
        if (preparation_count != 0usize) {
            return false;
        }
        raw Task*? frame = executor_tasks;
        while (frame != null) {
            if (on_current_thread(frame) == false) {
                usize exiting = core::pointer_address(runtime.system::pthread_self());
                task_lock(frame);
                bool parked = (frame->state == State::suspended) && (frame->resume_pending == false);
                bool joining = (frame->joining_thread == exiting) && (frame->state == State::running);
                bool suspended = (frame->kind != Kind::external) &&
                                 ((parked == true) || (joining == true));
                task_unlock(frame);
                if (suspended == false) {
                    return false;
                }
            }
            frame = frame->next;
        }
        return true;
    }
}

protected void sleep_briefly() {
    TimeSpec interval = {.seconds = 0i64 as c_long, .nanoseconds = 1000000i64 as c_long};
    unsafe {
        runtime.system::nanosleep(&interval as raw const TimeSpec*, null) as void;
    }
}

@safety("R-RUNTIME-EXECUTOR-QUIESCE-FOR-EXIT", "None")
extern "C" c_bool r_runtime_executor_quiesce_for_exit() {
    if (core::atomic_exchange(&exit_claimed, true, core::memory_order::acq_rel) == true) {
        while (true) {
            sleep_briefly();
        }
    }
    executor_lock();
    unsafe {
        if (executor_state == ExecutorState::running) {
            executor_state = ExecutorState::stopping;
        }
        executor_draining = true;
        core::atomic_store(&direct_open, false, core::memory_order::release);
        raw Task*? frame = executor_tasks;
        while (frame != null) {
            if (on_current_thread(frame) == false) {
                request_cancellation(frame);
            }
            frame = frame->next;
        }
    }
    executor_unlock();
    while (true) {
        executor_lock();
        bool quiescent = quiescent_for_exit_locked();
        executor_unlock();
        if (quiescent == true) {
            return runtime.types::c_bool_of(true);
        }
        sleep_briefly();
    }
}

@safety("R-RUNTIME-EXECUTOR-STOP", "None")
extern "C" c_bool r_runtime_executor_lifecycle_stop() {
    if (worker_depth != 0u32) {
        return runtime.types::c_bool_of(false);
    }
    executor_lock();
    unsafe {
        if (executor_state == ExecutorState::stopped) {
            executor_unlock();
            return runtime.types::c_bool_of(false);
        }
        if (executor_state == ExecutorState::stopping) {
            while (executor_state == ExecutorState::stopping) {
                executor_wait();
            }
            executor_unlock();
            return runtime.types::c_bool_of(true);
        }

        executor_state = ExecutorState::stopping;
        core::atomic_store(&direct_open, false, core::memory_order::release);
        raw Task*? frame = executor_tasks;
        while (frame != null) {
            request_cancellation(frame);
            frame = frame->next;
        }
        while ((preparation_count != 0usize) || (live_task_count != 0usize)) {
            executor_wait();
        }
        raw Queue*? queue = executor_queue;
        raw void*? reservation = executor_reservation;
        executor_allocator = null;
        executor_queue = null;
        executor_reservation = null;
        executor_unlock();

        runtime.system::dispatch_release(Object {.queue = queue});
        runtime.heap::r_runtime_allocator_deallocate(reservation, 1u64 as c_size);

        executor_lock();
        if (executor_state != ExecutorState::stopping) {
            fail();
        }
        executor_state = ExecutorState::stopped;
    }
    executor_broadcast();
    executor_unlock();
    return runtime.types::c_bool_of(true);
}

// ---- Starting tasks ----

protected bool lay_out(TypeInfo payload_type, TypeInfo result_type, Layout* frame) {
    usize payload_size = payload_type.size as usize;
    usize result_size = result_type.size as usize;
    usize payload_offset = 0usize;
    if (runtime.types::align_up(sizeof(Task), payload_type.alignment as usize, &payload_offset) ==
        false) {
        return false;
    }
    if (payload_size > runtime.types::size_maximum - payload_offset) {
        return false;
    }
    usize result_offset = 0usize;
    if (runtime.types::align_up(payload_offset + payload_size, result_type.alignment as usize,
                                &result_offset) == false) {
        return false;
    }
    if (result_size > runtime.types::size_maximum - result_offset) {
        return false;
    }
    usize alignment = alignof(Task);
    if ((payload_type.alignment as usize) > alignment) {
        alignment = payload_type.alignment as usize;
    }
    if ((result_type.alignment as usize) > alignment) {
        alignment = result_type.alignment as usize;
    }
    *frame = Layout {.payload_offset = payload_offset, .result_offset = result_offset,
                     .size = result_offset + result_size, .alignment = alignment};
    return true;
}

// Gives back the preparation of a start whose frame could not be made.
protected void abandon_preparation() {
    executor_lock();
    unsafe {
        if (preparation_count == 0usize) {
            fail();
        }
        preparation_count -= 1usize;
    }
    executor_broadcast();
    executor_unlock();
    runtime.threads::r_runtime_hosted_work_end();
}

protected PrepareResult task_start_frame(
    TypeInfo payload_type, TypeInfo result_type, Kind kind,
    raw fn?(raw ExecutionHandle*?, raw void*?, raw void*?) -> void body,
    raw fn?(raw ExecutionHandle*?, raw void*?, raw void*?) -> c_uint step,
    raw fn?(raw ExternalHandle*?, raw void*?, raw void*?) -> void external_start,
    raw fn?(raw ExternalHandle*?, raw void*?) -> void external_cancel) {
    executor_lock();
    unsafe {
        if (executor_state != ExecutorState::running) {
            executor_unlock();
            return prepare_result(null, StartStatus::R_RUNTIME_TASK_START_RUNTIME_STOPPING);
        }
        raw Allocator*? allocator = executor_allocator;
        if (preparation_count == runtime.types::size_maximum) {
            executor_unlock();
            fail();
        }
        preparation_count += 1usize;
        runtime.threads::r_runtime_hosted_work_begin();
        executor_unlock();

        Layout shape = {};
        raw void*? storage = null;
        AllocationStatus allocation_status = AllocationStatus::R_RUNTIME_ALLOCATION_SIZE_OVERFLOW;
        if (lay_out(payload_type, result_type, &shape) == true) {
            allocation_status = runtime.heap::r_runtime_allocator_allocate(
                allocator, shape.size as c_size, shape.alignment as c_size,
                &storage as raw (raw void*?)*);
        }
        if (allocation_status != AllocationStatus::R_RUNTIME_ALLOCATION_OK) {
            abandon_preparation();
            return prepare_result(null, StartStatus::R_RUNTIME_TASK_START_ALLOCATION_FAILED);
        }

        raw Task*? frame = storage as raw Task*?;
        *frame = Task {.allocator = allocator, .payload_type = payload_type,
                      .result_type = result_type, .body = body, .step = step,
                      .external_start = external_start, .external_cancel = external_cancel,
                      .payload_offset = shape.payload_offset,
                      .result_offset = shape.result_offset,
                      .allocation_alignment = shape.alignment, .references = 1usize,
                      .kind = kind, .state = State::prepared, .preparation_active = true};
        frame->external_execution.frame = frame;
        if (runtime.system::pthread_mutex_init(&frame->mutex as raw Mutex*, null) != 0i32 as c_int) {
            runtime.heap::r_runtime_allocator_deallocate(storage, shape.alignment as c_size);
            abandon_preparation();
            return prepare_result(null, StartStatus::R_RUNTIME_TASK_START_ALLOCATION_FAILED);
        }
        if (runtime.system::pthread_cond_init(&frame->condition as raw Condition*, null) !=
            0i32 as c_int) {
            runtime.system::pthread_mutex_destroy(&frame->mutex as raw Mutex*) as void;
            runtime.heap::r_runtime_allocator_deallocate(storage, shape.alignment as c_size);
            abandon_preparation();
            return prepare_result(null, StartStatus::R_RUNTIME_TASK_START_ALLOCATION_FAILED);
        }
        // Core R-STMT-0019: everything started inside a deadline block inherits its deadline.
        raw Task*? outer = current_task;
        if (outer != null) {
            frame->deadline = outer->deadline;
        }
        return prepare_result(frame, StartStatus::R_RUNTIME_TASK_START_OK);
    }
}

// Core R-STMT-0020: a task prepared under a budget receives it, and an R task counts one task under
// it until the task is destroyed; a start beyond the limit is refused before the frame is
// allocated, with the refusal noted for the start error. Standard operations do not count.
protected PrepareResult task_start_prepare(
    TypeInfo payload_type, TypeInfo result_type, Kind kind,
    raw fn?(raw ExecutionHandle*?, raw void*?, raw void*?) -> void body,
    raw fn?(raw ExecutionHandle*?, raw void*?, raw void*?) -> c_uint step,
    raw fn?(raw ExternalHandle*?, raw void*?, raw void*?) -> void external_start,
    raw fn?(raw ExternalHandle*?, raw void*?) -> void external_cancel) {
    raw Budget*? budget = runtime.heap::r_runtime_budget_current();
    unsafe {
        bool counted = (budget != null) && (kind != Kind::external);
        if (counted == true) {
            if (runtime.types::is_true(runtime.heap::r_runtime_budget_charge_task(budget)) ==
                false) {
                return prepare_result(null, StartStatus::R_RUNTIME_TASK_START_ALLOCATION_FAILED);
            }
        }
        PrepareResult prepared = task_start_frame(payload_type, result_type, kind, body, step,
                                                  external_start, external_cancel);
        raw Task*? frame = task_of(prepared.transaction);
        if (frame == null) {
            if (counted == true) {
                runtime.heap::r_runtime_budget_return_task(budget);
            }
            return prepared;
        }
        runtime.heap::r_runtime_budget_retain(budget);
        frame->budget = budget;
        frame->counted = counted;
        return prepared;
    }
}

@safety("R-RUNTIME-TASK-START-ALLOCATOR", "transaction is null or a task handle")
extern "C" raw Allocator*? r_runtime_task_start_allocator(raw const TaskHandle*? transaction) {
    raw Task*? frame = task_of_const(transaction);
    unsafe {
        if (frame == null) {
            return null;
        }
        bool committed = core::atomic_load(&frame->committed, core::memory_order::acquire);
        if ((committed == true) || (frame->preparation_active == false) ||
            (frame->state != State::prepared)) {
            return null;
        }
        return frame->allocator;
    }
}

@safety("R-RUNTIME-TASK-START-PREPARE", "the type callbacks are immutable generated metadata")
extern "C" PrepareResult r_runtime_task_start_prepare(
    TypeInfo payload_type, TypeInfo result_type,
    raw fn?(raw ExecutionHandle*?, raw void*?, raw void*?) -> void body) {
    return task_start_prepare(payload_type, result_type, Kind::computation, body, null, null, null);
}

@safety("R-RUNTIME-TASK-RESUMABLE-START-PREPARE",
        "the type callbacks are immutable generated metadata")
extern "C" PrepareResult r_runtime_task_resumable_start_prepare(
    TypeInfo payload_type, TypeInfo result_type,
    raw fn?(raw ExecutionHandle*?, raw void*?, raw void*?) -> c_uint step) {
    return task_start_prepare(payload_type, result_type, Kind::resumable, null, step, null, null);
}

@safety("R-RUNTIME-TASK-EXTERNAL-START-PREPARE",
        "the type callbacks are immutable generated metadata")
extern "C" PrepareResult r_runtime_task_external_start_prepare(
    TypeInfo payload_type, TypeInfo result_type,
    raw fn?(raw ExternalHandle*?, raw void*?, raw void*?) -> void start,
    raw fn?(raw ExternalHandle*?, raw void*?) -> void cancel) {
    return task_start_prepare(payload_type, result_type, Kind::external, null, null, start, cancel);
}

// inline_mode: 0 dispatches now; 1 runs the first step on this thread when the stack allows; 2
// leaves the task queued for r_runtime_task_run_deferred.
protected StartResult task_start_commit(raw (raw TaskHandle*?)*? transaction,
                                        raw void*? staged_payload,
                                        raw fn?(raw void*?, raw const void*?) -> void initialize,
                                        raw const void*? context, bool initialize_in_place,
                                        u32 inline_mode, usize inline_step_stack_bytes) {
    unsafe {
        raw Task*? frame = task_of(*transaction);
        *transaction = null;

        executor_lock();
        if (executor_state != ExecutorState::running) {
            preparation_finish_locked(frame);
            executor_unlock();
            frame->references = 0usize;
            task_deallocate(frame);
            runtime.threads::r_runtime_hosted_work_end();
            return start_result(null, StartStatus::R_RUNTIME_TASK_START_RUNTIME_STOPPING);
        }

        if (initialize_in_place == true) {
            if (initialize != null) {
                initialize(task_payload(frame), context);
            }
        } else {
            move_into(frame->payload_type, task_payload(frame), staged_payload);
        }
        frame->payload_initialized = (frame->payload_type.size as usize) != 0usize;
        frame->observer_attached = true;
        frame->references = 2usize;
        if (frame->kind == Kind::external) {
            frame->state = State::running;
        } else {
            frame->state = State::queued;
        }
        preparation_finish_locked(frame);
        if (executor_tasks_tail == null) {
            executor_tasks = frame;
        } else {
            executor_tasks_tail->next = frame;
        }
        executor_tasks_tail = frame;
        if (live_task_count == runtime.types::size_maximum) {
            fail();
        }
        live_task_count += 1usize;
        u64 previous_id = core::atomic_fetch_add(&last_task_id, 1u64, core::memory_order::relaxed);
        if (previous_id == u64_maximum) {
            fail();
        }
        frame->id = previous_id + 1u64;
        core::atomic_store(&frame->committed, true, core::memory_order::release);
        if (executor_draining == true) {
            request_cancellation(frame);
        }
        executor_unlock();

        if (frame->kind != Kind::external) {
            bool inline_step = (inline_mode == 1u32) && (frame->kind == Kind::resumable);
            bool fits = false;
            if (inline_step == true) {
                fits = runtime.types::is_true(
                    runtime.stacks::r_runtime_stack_can_require(inline_step_stack_bytes as c_size));
            }
            if (fits == true) {
                task_execute(frame, false);
            } else {
                if (inline_mode != 2u32) {
                    task_dispatch(frame);
                }
            }
        }
        if (frame->kind == Kind::external) {
            raw fn?(raw ExternalHandle*?, raw void*?, raw void*?) -> void start =
                frame->external_start;
            if (start != null) {
                start(external_handle(frame), task_payload(frame), task_result(frame));
            } else {
                fail();
            }
            task_lock(frame);
            if ((frame->external_start_ready == false) || (frame->external_start_finished == true)) {
                task_unlock(frame);
                fail();
            }
            frame->external_start_finished = true;
            bool dispatch_cancel = external_cancel_schedule_locked(frame);
            task_unlock(frame);
            if (dispatch_cancel == true) {
                external_cancel_dispatch(frame);
            }
        }
        return start_result(frame, StartStatus::R_RUNTIME_TASK_START_OK);
    }
}

@safety("R-RUNTIME-TASK-START-COMMIT", "transaction holds a prepared task; payload is staged")
extern "C" StartResult r_runtime_task_start_commit(raw (raw TaskHandle*?)*? transaction,
                                                   raw void*? staged_payload) {
    return task_start_commit(transaction, staged_payload, null, null, false, 0u32, 0usize);
}

@safety("R-RUNTIME-TASK-START-COMMIT-INITIALIZE", "transaction holds a prepared task")
extern "C" StartResult r_runtime_task_start_commit_initialize(
    raw (raw TaskHandle*?)*? transaction,
    raw fn?(raw void*?, raw const void*?) -> void initialize, raw const void*? context) {
    return task_start_commit(transaction, null, initialize, context, true, 0u32, 0usize);
}

@safety("R-RUNTIME-TASK-START-COMMIT-INITIALIZE-INLINE", "transaction holds a prepared task")
extern "C" StartResult r_runtime_task_start_commit_initialize_inline(
    raw (raw TaskHandle*?)*? transaction,
    raw fn?(raw void*?, raw const void*?) -> void initialize, raw const void*? context,
    c_size step_stack_bytes) {
    return task_start_commit(transaction, null, initialize, context, true, 1u32,
                             step_stack_bytes as usize);
}

@safety("R-RUNTIME-TASK-START-COMMIT-INITIALIZE-DEFERRED", "transaction holds a prepared task")
extern "C" StartResult r_runtime_task_start_commit_initialize_deferred(
    raw (raw TaskHandle*?)*? transaction,
    raw fn?(raw void*?, raw const void*?) -> void initialize, raw const void*? context) {
    return task_start_commit(transaction, null, initialize, context, true, 2u32, 0usize);
}

@safety("R-RUNTIME-TASK-RUN-DEFERRED", "task was committed deferred and not yet run")
extern "C" void r_runtime_task_run_deferred(raw TaskHandle*? handle, c_size step_stack_bytes) {
    raw Task*? frame = task_of(handle);
    unsafe {
        if (frame == null) {
            fail();
        }
        bool committed = core::atomic_load(&frame->committed, core::memory_order::acquire);
        if ((committed == false) || (frame->kind != Kind::resumable)) {
            fail();
        }
    }
    if (runtime.types::is_true(runtime.stacks::r_runtime_stack_can_require(step_stack_bytes)) ==
        true) {
        task_execute(frame, false);
    } else {
        task_dispatch(frame);
    }
}

protected bool inline_completion_allowed() {
    // A budget counts tasks and charges frames (Core R-STMT-0020); its starts keep the ordinary
    // path. A drain or a stopping executor would refuse or cancel the start.
    unsafe {
        if (runtime.heap::r_runtime_budget_current() != null) {
            return false;
        }
    }
    return core::atomic_load(&direct_open, core::memory_order::acquire);
}

@safety("R-RUNTIME-TASK-INLINE-COMPLETION-ALLOWED", "None")
extern "C" c_bool r_runtime_task_inline_completion_allowed() {
    return runtime.types::c_bool_of(inline_completion_allowed());
}

@safety("R-RUNTIME-TASK-DIRECT-BEGIN", "None")
extern "C" c_bool r_runtime_task_direct_begin(c_size stack_bytes) {
    // The call runs on this stack, so it needs the measured bound of the direct body (R-FUNC-0004).
    if (inline_completion_allowed() == false) {
        return runtime.types::c_bool_of(false);
    }
    if (runtime.types::is_true(runtime.stacks::r_runtime_stack_can_require(stack_bytes)) == false) {
        return runtime.types::c_bool_of(false);
    }
    u64 identifier = core::atomic_fetch_add(&last_task_id, 1u64, core::memory_order::relaxed);
    if (identifier == u64_maximum) {
        fail();
    }
    direct_task_id = identifier + 1u64;
    return runtime.types::c_bool_of(true);
}

@safety("R-RUNTIME-TASK-DIRECT-END", "None")
extern "C" void r_runtime_task_direct_end() {
    direct_task_id = 0u64;
}

// Rolls back and releases a transaction that never committed.
protected void abort_uncommitted(raw Task*? frame) {
    executor_lock();
    preparation_finish_locked(frame);
    executor_unlock();
    unsafe {
        frame->references = 0usize;
    }
    task_deallocate(frame);
    runtime.threads::r_runtime_hosted_work_end();
}

// Consumes one observation of task: cancel requests cooperative cancellation first.
protected void consume_task(raw Task*? frame, bool cancel) {
    unsafe {
        if (core::atomic_load(&frame->committed, core::memory_order::acquire) == false) {
            abort_uncommitted(frame);
            return;
        }
        if (cancel == true) {
            request_cancellation(frame);
        }
        task_lock(frame);
        if (frame->observer_attached == false) {
            task_unlock(frame);
            return;
        }
        frame->observer_attached = false;
        bool drop_result = frame->result_initialized;
        if (drop_result == true) {
            frame->result_initialized = false;
            frame->observer_cleanup_active = true;
        }
        bool deliver_panic = (frame->terminal_decided == true) && (frame->panicked == true) &&
                             (frame->panic_delivered == false);
        if (deliver_panic == true) {
            frame->panic_delivered = true;
        }
        task_unlock(frame);
        if (drop_result == true) {
            drop_value(frame->result_type, task_result(frame));
            task_lock(frame);
            frame->observer_cleanup_active = false;
            task_unlock(frame);
        }
        if (deliver_panic == true) {
            // R-MEM-0017: the report of a task that nobody observes goes to the panic hook.
            runtime.panics::r_runtime_panic_deliver(&frame->panic_report as raw const PanicReport*);
        }
    }
    scope_publish(frame);
    task_release_reference(frame);
}

protected void consume_observer(raw (raw TaskHandle*?)*? slot, bool cancel) {
    unsafe {
        if (slot == null) {
            return;
        }
        raw Task*? frame = task_of(*slot);
        if (frame == null) {
            return;
        }
        *slot = null;
        consume_task(frame, cancel);
    }
}

@safety("R-RUNTIME-TASK-START-ABORT", "transaction is null or holds a task of this caller")
extern "C" void r_runtime_task_start_abort(raw (raw TaskHandle*?)*? transaction) {
    unsafe {
        if (transaction == null) {
            return;
        }
        raw Task*? frame = task_of(*transaction);
        if (frame == null) {
            return;
        }
        *transaction = null;
        if (core::atomic_load(&frame->committed, core::memory_order::acquire) == true) {
            consume_task(frame, true);
            return;
        }
        abort_uncommitted(frame);
    }
}

// ---- Awaiting tasks ----

@safety("R-RUNTIME-TASK-AWAIT", "the slot holds a published observation; storage fits the result")
extern "C" AwaitStatus r_runtime_task_await(raw (raw TaskHandle*?)*? task_slot,
                                            raw void*? result_storage) {
    unsafe {
        raw Task*? frame = task_of(*task_slot);
        task_lock(frame);
        bool published = core::atomic_load(&frame->terminal_published, core::memory_order::acquire);
        if ((published == false) && (worker_depth != 0u32)) {
            task_unlock(frame);
            return AwaitStatus::R_RUNTIME_TASK_AWAIT_WOULD_BLOCK;
        }
        while (core::atomic_load(&frame->terminal_published, core::memory_order::acquire) == false) {
            runtime.system::require_zero(runtime.system::pthread_cond_wait(
                &frame->condition as raw Condition*, &frame->mutex as raw Mutex*));
        }
        if (frame->observer_attached == false) {
            fail();
        }
        frame->observer_attached = false;
        *task_slot = null;
        // Core R-STMT-0020: an awaited task stops counting under its budget when its await
        // completes, also while the worker that ran it still holds it (M42-3).
        bool return_count = frame->counted;
        frame->counted = false;
        AwaitStatus status = AwaitStatus::R_RUNTIME_TASK_AWAIT_OK;
        bool completed = frame->state == State::completed;
        if ((completed == true) && (frame->panicked == true)) {
            // R-FUNC-0012: the observed panic is re-raised at the await.
            frame->panic_delivered = true;
            runtime.panics::r_runtime_panic_resume(&frame->panic_report as raw const PanicReport*);
            status = AwaitStatus::R_RUNTIME_TASK_AWAIT_PANICKED;
        } else {
            if (completed == true) {
                if ((frame->result_type.size as usize) != 0usize) {
                    if (frame->result_initialized == false) {
                        fail();
                    }
                    move_into(frame->result_type, result_storage, task_result(frame));
                    frame->result_initialized = false;
                }
            } else {
                if (frame->state != State::cancelled) {
                    fail();
                }
                if (frame->result_initialized == true) {
                    fail();
                }
                status = AwaitStatus::R_RUNTIME_TASK_AWAIT_CANCELLED;
            }
        }
        task_unlock(frame);
        if (return_count == true) {
            runtime.heap::r_runtime_budget_return_task(frame->budget);
        }
        scope_publish(frame);
        task_release_reference(frame);
        return status;
    }
}

protected ExecutionAwaitStatus await_terminal(raw (raw TaskHandle*?)*? task_slot,
                                              raw void*? result_storage) {
    AwaitStatus status = r_runtime_task_await(task_slot, result_storage);
    if (status == AwaitStatus::R_RUNTIME_TASK_AWAIT_OK) {
        return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_OK;
    }
    if (status == AwaitStatus::R_RUNTIME_TASK_AWAIT_CANCELLED) {
        return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_CANCELLED;
    }
    if (status == AwaitStatus::R_RUNTIME_TASK_AWAIT_PANICKED) {
        return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_PANICKED;
    }
    return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
}

protected bool arm_waiter(raw Task*? parent, raw Task*? awaited) {
    bool armed = false;
    task_lock(parent);
    unsafe {
        bool idle = (parent->waiting_on == null) && (parent->waiting_scope == null);
        if ((parent->kind == Kind::resumable) && (parent->state == State::running) &&
            (parent->terminal_decided == false) && (idle == true)) {
            if (parent->references == runtime.types::size_maximum) {
                task_unlock(parent);
                fail();
            }
            parent->references += 1usize;
            parent->waiting_on = awaited;
            armed = true;
        }
    }
    task_unlock(parent);
    return armed;
}

protected void disarm_waiter(raw Task*? parent, raw Task*? awaited) {
    task_lock(parent);
    unsafe {
        if (parent->waiting_on != awaited) {
            task_unlock(parent);
            fail();
        }
        parent->waiting_on = null;
    }
    task_unlock(parent);
    task_release_reference(parent);
}

@safety("R-RUNTIME-TASK-EXECUTION-AWAIT", "the slot and storage are fields of the owned frame")
extern "C" ExecutionAwaitStatus r_runtime_task_execution_await(
    raw ExecutionHandle*? execution, raw (raw TaskHandle*?)*? task_slot,
    raw void*? result_storage) {
    unsafe {
        raw Execution*? running = execution_of(execution);
        if ((running == null) || (task_slot == null)) {
            return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
        }
        raw Task*? parent = running->frame;
        raw Task*? awaited = task_of(*task_slot);
        if ((awaited == null) || (parent == null) || (parent == awaited)) {
            return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
        }
        if (core::atomic_load(&awaited->committed, core::memory_order::acquire) == false) {
            return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
        }
        if ((awaited->result_type.size as usize) != 0usize) {
            if (result_storage == null) {
                return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
            }
            usize mask = (awaited->result_type.alignment as usize) - 1usize;
            if ((core::pointer_address(result_storage) & mask) != 0usize) {
                return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
            }
        }

        task_lock(parent);
        if ((parent->kind != Kind::resumable) || (parent->state != State::running) ||
            (parent->terminal_decided == true)) {
            task_unlock(parent);
            return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
        }
        bool already_registered = parent->waiting_on == awaited;
        bool elsewhere = (parent->waiting_on != null) && (already_registered == false);
        if ((parent->waiting_scope != null) || (elsewhere == true)) {
            task_unlock(parent);
            return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
        }
        task_unlock(parent);

        if (core::atomic_load(&parent->cancel_requested, core::memory_order::acquire) == true) {
            request_cancellation(awaited);
        }

        task_lock(awaited);
        if (core::atomic_load(&awaited->terminal_published, core::memory_order::acquire) == true) {
            task_unlock(awaited);
            return await_terminal(task_slot, result_storage);
        }
        if (awaited->waiter != null) {
            bool same_waiter = (awaited->waiter == parent) && (already_registered == true);
            task_unlock(awaited);
            if (same_waiter == true) {
                return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED;
            }
            return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
        }
        task_unlock(awaited);

        if (already_registered == true) {
            return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
        }
        if (arm_waiter(parent, awaited) == false) {
            return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
        }

        task_lock(awaited);
        if (core::atomic_load(&awaited->terminal_published, core::memory_order::acquire) == true) {
            task_unlock(awaited);
            disarm_waiter(parent, awaited);
            return await_terminal(task_slot, result_storage);
        }
        if (awaited->waiter != null) {
            task_unlock(awaited);
            disarm_waiter(parent, awaited);
            return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
        }
        awaited->waiter = parent;
        task_unlock(awaited);
        return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED;
    }
}

@safety("R-RUNTIME-TASK-PANIC-PARK", "execution is the executing task; a panic is pending")
extern "C" void r_runtime_task_panic_park(raw ExecutionHandle*? execution) {
    unsafe {
        raw Execution*? running = execution_of(execution);
        if (running == null) {
            fail();
        }
        raw Task*? frame = running->frame;
        if (frame == null) {
            fail();
        }
        if (frame->panic_parked == true) {
            // R-ERR-0005: a panic that begins in a finally block run for a panic.
            PanicReport second = no_report();
            if (runtime.types::is_true(
                    runtime.panics::r_runtime_panic_take(&second as raw PanicReport*)) == false) {
                fail();
            }
            runtime.panics::r_runtime_panic_second(&frame->panic_report as raw const PanicReport*,
                                                   second.category as c_uint, second.span);
        }
        if (runtime.types::is_true(runtime.panics::r_runtime_panic_take(
                &frame->panic_report as raw PanicReport*)) == false) {
            fail();
        }
        frame->panic_parked = true;
    }
}

@safety("R-RUNTIME-TASK-PANIC-UNPARK", "execution is the executing task with a parked panic")
extern "C" void r_runtime_task_panic_unpark(raw ExecutionHandle*? execution) {
    unsafe {
        raw Execution*? running = execution_of(execution);
        if (running == null) {
            fail();
        }
        raw Task*? frame = running->frame;
        if (frame == null) {
            fail();
        }
        if (frame->panic_parked == false) {
            fail();
        }
        frame->panic_parked = false;
        runtime.panics::r_runtime_panic_resume(&frame->panic_report as raw const PanicReport*);
    }
}

@safety("R-RUNTIME-TASK-CANCEL", "task is null or an observation slot")
extern "C" void r_runtime_task_cancel(raw (raw TaskHandle*?)*? frame) {
    consume_observer(frame, true);
}

@safety("R-RUNTIME-TASK-DETACH", "task is null or an observation slot")
extern "C" void r_runtime_task_detach(raw (raw TaskHandle*?)*? frame) {
    consume_observer(frame, false);
}

@safety("R-RUNTIME-TASK-DESTROY", "task is null or an observation slot")
extern "C" void r_runtime_task_destroy(raw (raw TaskHandle*?)*? frame) {
    consume_observer(frame, true);
}

// ---- External tasks (R-SLIB-ASYNC-0007) ----

@safety("R-RUNTIME-TASK-EXTERNAL-START-READY", "execution is a committed external task")
extern "C" void r_runtime_task_external_start_ready(raw ExternalHandle*? execution) {
    raw Task*? frame = external_task(execution);
    unsafe {
        if (frame == null) {
            fail();
        }
        task_lock(frame);
        bool committed = core::atomic_load(&frame->committed, core::memory_order::acquire);
        if ((frame->external_start_ready == true) || (frame->external_acknowledged == true) ||
            (committed == false)) {
            task_unlock(frame);
            fail();
        }
        frame->external_start_ready = true;
        bool dispatch_cancel = external_cancel_schedule_locked(frame);
        task_unlock(frame);
        if (dispatch_cancel == true) {
            external_cancel_dispatch(frame);
        }
    }
}

// The selection is itself the event, so its sequence is taken under the task lock and a selected
// cancellation is never replaced. Taking the sequence before the lock let a cancellation chosen in
// between be replaced while its cancel callback, which acknowledges a withdrawn waiter, was
// already scheduled: the waiter was acknowledged twice (P4.1-8).
@safety("R-RUNTIME-TASK-EXTERNAL-SELECT", "execution is a started external task")
extern "C" c_bool r_runtime_task_external_try_select_completion(raw ExternalHandle*? execution) {
    raw Task*? frame = external_task(execution);
    bool selected = false;
    task_lock(frame);
    unsafe {
        if ((frame->external_acknowledged == false) &&
            (frame->external_selection == Selection::unselected)) {
            frame->external_selection = Selection::completion;
            frame->external_selection_sequence =
                runtime.types::r_runtime_darwin_event_sequence_next() as u64;
            frame->terminal_decided = true;
            selected = true;
        }
    }
    task_unlock(frame);
    return runtime.types::c_bool_of(selected);
}

@safety("R-RUNTIME-TASK-EXTERNAL-SELECT-AT", "execution is a started external task")
extern "C" c_bool r_runtime_task_external_try_select_completion_at(raw ExternalHandle*? execution,
                                                                   c_uint64 event_sequence) {
    raw Task*? frame = external_task(execution);
    bool selected = false;
    task_lock(frame);
    unsafe {
        bool open = frame->external_acknowledged == false;
        if ((open == true) && (frame->external_selection == Selection::unselected)) {
            frame->external_selection = Selection::completion;
            frame->external_selection_sequence = event_sequence as u64;
            frame->terminal_decided = true;
            selected = true;
        } else {
            bool earlier = (frame->external_selection == Selection::cancellation) &&
                           ((event_sequence as u64) < frame->external_selection_sequence);
            if ((open == true) && (earlier == true)) {
                frame->external_selection = Selection::completion;
                frame->external_selection_sequence = event_sequence as u64;
                selected = true;
            }
        }
    }
    task_unlock(frame);
    return runtime.types::c_bool_of(selected);
}

@safety("R-RUNTIME-TASK-EXTERNAL-SELECT-TERMINAL", "execution is a started external task")
extern "C" c_bool r_runtime_task_external_select_terminal_completion(
    raw ExternalHandle*? execution) {
    raw Task*? frame = external_task(execution);
    bool selected = false;
    task_lock(frame);
    unsafe {
        if ((frame->external_acknowledged == false) &&
            (frame->external_selection == Selection::cancellation)) {
            frame->external_selection = Selection::completion;
            frame->external_selection_sequence =
                runtime.types::r_runtime_darwin_event_sequence_next() as u64;
            selected = true;
        }
    }
    task_unlock(frame);
    return runtime.types::c_bool_of(selected);
}

@safety("R-RUNTIME-TASK-EXTERNAL-ACKNOWLEDGE", "execution has a selected terminal event")
extern "C" void r_runtime_task_external_acknowledge(raw ExternalHandle*? execution) {
    raw Task*? frame = external_task(execution);
    unsafe {
        if (frame == null) {
            fail();
        }
        task_lock(frame);
        if ((frame->external_start_ready == false) || (frame->external_acknowledged == true) ||
            (frame->external_selection == Selection::unselected)) {
            task_unlock(frame);
            fail();
        }
        frame->external_acknowledged = true;
        if (frame->external_selection == Selection::completion) {
            frame->state = State::completed;
            frame->result_initialized = (frame->result_type.size as usize) != 0usize;
        } else {
            frame->state = State::cancelled;
        }
        bool drop_payload = frame->payload_initialized;
        frame->payload_initialized = false;
        bool drop_result = (frame->result_initialized == true) &&
                           ((frame->state == State::cancelled) || (frame->observer_attached == false));
        if (drop_result == true) {
            frame->result_initialized = false;
        }
        task_unlock(frame);

        if (drop_payload == true) {
            drop_value(frame->payload_type, task_payload(frame));
        }
        if (drop_result == true) {
            drop_value(frame->result_type, task_result(frame));
        }
    }
    publish_terminal(frame);
    executor_remove_task(frame, true);
}

// L39 (R-ERR-0009): an external task whose native work ran R code that panicked ends with that
// panic, which is the terminal outcome even after a selected cancellation, as for a resumable
// task: an observer re-raises it at its await, and without one the report goes to the panic hook.
@safety("R-RUNTIME-TASK-EXTERNAL-ACKNOWLEDGE-PANIC", "execution is started; report is a report")
extern "C" void r_runtime_task_external_acknowledge_panic(raw ExternalHandle*? execution,
                                                          raw const PanicReport*? report) {
    raw Task*? frame = external_task(execution);
    unsafe {
        if ((frame == null) || (report == null)) {
            fail();
        }
        task_lock(frame);
        if ((frame->external_start_ready == false) || (frame->external_acknowledged == true)) {
            task_unlock(frame);
            fail();
        }
        if (frame->external_selection == Selection::unselected) {
            frame->external_selection = Selection::completion;
            frame->external_selection_sequence =
                runtime.types::r_runtime_darwin_event_sequence_next() as u64;
            frame->terminal_decided = true;
        }
        frame->external_acknowledged = true;
        frame->state = State::completed;
        frame->panicked = true;
        frame->panic_report = *report;
        frame->result_initialized = false;
        bool drop_payload = frame->payload_initialized;
        frame->payload_initialized = false;
        bool deliver_panic = (frame->observer_attached == false) && (frame->panic_delivered == false);
        if (deliver_panic == true) {
            frame->panic_delivered = true;
        }
        task_unlock(frame);

        if (drop_payload == true) {
            drop_value(frame->payload_type, task_payload(frame));
        }
        if (deliver_panic == true) {
            runtime.panics::r_runtime_panic_deliver(&frame->panic_report as raw const PanicReport*);
        }
    }
    publish_terminal(frame);
    executor_remove_task(frame, true);
}

@safety("R-RUNTIME-TASK-EXTERNAL-CANCEL-REQUESTED", "execution is a started external task")
extern "C" c_bool r_runtime_task_external_cancel_requested(raw const ExternalHandle*? execution) {
    unsafe {
        raw Execution*? record = external_of_const(execution);
        raw Task*? frame = record->frame;
        return runtime.types::c_bool_of(
            core::atomic_load(&frame->cancel_requested, core::memory_order::acquire));
    }
}

@safety("R-RUNTIME-TASK-EXTERNAL-CANCELLATION-SEQUENCE", "execution is a started external task")
extern "C" c_uint64 r_runtime_task_external_cancellation_sequence(
    raw const ExternalHandle*? execution) {
    unsafe {
        raw Execution*? record = external_of_const(execution);
        raw Task*? frame = record->frame;
        task_lock(frame);
        u64 sequence = frame->external_cancellation_sequence;
        task_unlock(frame);
        return sequence as c_uint64;
    }
}

protected TaskState state_value(State state) {
    switch (state) {
    case State::invalid:
        return TaskState::R_RUNTIME_TASK_INVALID;
    case State::prepared:
        return TaskState::R_RUNTIME_TASK_PREPARED;
    case State::queued:
        return TaskState::R_RUNTIME_TASK_QUEUED;
    case State::running:
        return TaskState::R_RUNTIME_TASK_RUNNING;
    case State::completed:
        return TaskState::R_RUNTIME_TASK_COMPLETED;
    case State::cancelled:
        return TaskState::R_RUNTIME_TASK_CANCELLED;
    case State::suspended:
        return TaskState::R_RUNTIME_TASK_SUSPENDED;
    }
}

@safety("R-RUNTIME-TASK-STATE", "task is a live task")
extern "C" TaskState r_runtime_task_state(raw TaskHandle*? handle) {
    raw Task*? frame = task_of(handle);
    task_lock(frame);
    unsafe {
        State state = frame->state;
        task_unlock(frame);
        return state_value(state);
    }
}

@safety("R-RUNTIME-TASK-EXECUTION-CANCEL-REQUESTED", "execution is the executing task")
extern "C" c_bool r_runtime_task_execution_cancel_requested(raw const ExecutionHandle*? execution) {
    unsafe {
        raw Execution*? record = execution_of_const(execution);
        raw Task*? frame = record->frame;
        return runtime.types::c_bool_of(
            core::atomic_load(&frame->cancel_requested, core::memory_order::acquire));
    }
}

// ---- The blocking call pool (Core R-TERM-0015, Library R-SLIB-ASYNC-0017) ----
//
// The jobs form one doubly-linked FIFO, so withdrawing a waiting job costs no search. A job holds
// no pool state after run returns: its producer acknowledges its task from run, and a
// cancellation that arrives later finds the job taken and leaves it alone.

const u32 job_queued = 1u32;
const u32 job_taken = 2u32;

Mutex blocking_mutex = {.signature = runtime.system::mutex_signature as c_long, .bytes = {}};
Condition blocking_condition = {.signature = runtime.system::condition_signature as c_long,
                                .bytes = {}};
(raw Thread*?)[4] blocking_workers = {null, null, null, null};
u32 blocking_created = 0u32;
u32 blocking_waiting = 0u32;
raw BlockingJob*? blocking_head = null;
raw BlockingJob*? blocking_tail = null;
usize blocking_queued = 0usize;
usize blocking_reserved = 0usize;
usize blocking_capacity = 0usize;
bool blocking_accepting = false;

protected void blocking_lock() {
    unsafe {
        runtime.system::require_zero(
            runtime.system::pthread_mutex_lock(&blocking_mutex as raw Mutex*));
    }
}

protected void blocking_unlock() {
    unsafe {
        runtime.system::require_zero(
            runtime.system::pthread_mutex_unlock(&blocking_mutex as raw Mutex*));
    }
}

protected void blocking_unlink_locked(raw BlockingJob*? job) {
    unsafe {
        raw BlockingJob*? previous = job->previous;
        raw BlockingJob*? following = job->next;
        if (previous == null) {
            blocking_head = following;
        } else {
            previous->next = following;
        }
        if (following == null) {
            blocking_tail = previous;
        } else {
            following->previous = previous;
        }
        job->previous = null;
        job->next = null;
        blocking_queued -= 1usize;
    }
}

// A pool thread runs R entries, so it establishes its stack bounds once and cleans its
// thread-local state after every job.
@callback
@safety("R-RUNTIME-BLOCKING-WORKER", "runs as the entry of a pool thread")
protected extern "C" raw void*? blocking_worker(raw void*? context) {
    blocking_worker_thread = true;
    unsafe {
        runtime.system::pthread_setname_np(runtime.system::c_string("r.blocking\0")) as void;
    }
    if (runtime.types::is_true(runtime.stacks::r_runtime_stack_initialize_current_thread()) ==
        false) {
        runtime.panics::r_runtime_panic(12u32 as c_uint, no_span());
    }
    blocking_lock();
    unsafe {
        while (true) {
            while ((blocking_head == null) && (blocking_accepting == true)) {
                blocking_waiting += 1u32;
                runtime.system::require_zero(runtime.system::pthread_cond_wait(
                    &blocking_condition as raw Condition*, &blocking_mutex as raw Mutex*));
                blocking_waiting -= 1u32;
            }
            raw BlockingJob*? job = blocking_head;
            if (job == null) {
                break;
            }
            blocking_unlink_locked(job);
            job->state = job_taken as c_uint;
            blocking_unlock();
            runtime.stacks::r_runtime_stack_require(generated_frame_max_bytes, no_span());
            raw const ExternalHandle*? execution = job->execution;
            if (execution != null) {
                raw Execution*? record = external_of_const(execution);
                blocking_running = record->frame;
            }
            raw fn?(raw BlockingJob*?) -> void run = job->run;
            if (run != null) {
                run(job);
            } else {
                fail();
            }
            blocking_running = null;
            runtime.threads::r_runtime_thread_local_cleanup_current();
            blocking_lock();
        }
    }
    blocking_unlock();
    return context;
}

@safety("R-RUNTIME-BLOCKING-START", "None")
extern "C" c_bool r_runtime_blocking_start(c_size capacity) {
    bool started = false;
    blocking_lock();
    unsafe {
        if ((blocking_accepting == false) && (blocking_created == 0u32) &&
            ((capacity as usize) != 0usize)) {
            blocking_capacity = capacity as usize;
            blocking_queued = 0usize;
            blocking_reserved = 0usize;
            blocking_accepting = true;
            started = true;
        }
    }
    blocking_unlock();
    return runtime.types::c_bool_of(started);
}

@safety("R-RUNTIME-BLOCKING-RESERVE", "None")
extern "C" BlockingReserveStatus r_runtime_blocking_reserve() {
    BlockingReserveStatus status = BlockingReserveStatus::R_RUNTIME_BLOCKING_RESERVED;
    blocking_lock();
    unsafe {
        if (blocking_accepting == false) {
            status = BlockingReserveStatus::R_RUNTIME_BLOCKING_STOPPING;
        } else {
            if (blocking_queued + blocking_reserved >= blocking_capacity) {
                status = BlockingReserveStatus::R_RUNTIME_BLOCKING_QUEUE_FULL;
            } else {
                blocking_reserved += 1usize;
                // Every waiting or reserved job without an idle thread may add one more thread.
                bool short = (blocking_created < (blocking_thread_count as u32)) &&
                             (blocking_queued + blocking_reserved > (blocking_waiting as usize));
                if (short == true) {
                    raw (raw Thread*?)* slot =
                        &blocking_workers[blocking_created as usize] as raw (raw Thread*?)*;
                    if (runtime.system::pthread_create(slot, null, blocking_worker, null) ==
                        0i32 as c_int) {
                        blocking_created += 1u32;
                    } else {
                        if (blocking_created == 0u32) {
                            blocking_reserved -= 1usize;
                            status = BlockingReserveStatus::R_RUNTIME_BLOCKING_THREAD_UNAVAILABLE;
                        }
                    }
                }
            }
        }
    }
    blocking_unlock();
    return status;
}

@safety("R-RUNTIME-BLOCKING-UNRESERVE", "a reservation of this caller is held")
extern "C" void r_runtime_blocking_unreserve() {
    blocking_lock();
    unsafe {
        if (blocking_reserved == 0usize) {
            blocking_unlock();
            fail();
        }
        blocking_reserved -= 1usize;
    }
    blocking_unlock();
}

@safety("R-RUNTIME-BLOCKING-SUBMIT", "a reservation is held; job lives until run returns")
extern "C" void r_runtime_blocking_submit(raw BlockingJob*? job,
                                          raw fn?(raw BlockingJob*?) -> void run,
                                          raw const ExternalHandle*? execution) {
    blocking_lock();
    unsafe {
        if ((blocking_reserved == 0usize) || (blocking_accepting == false)) {
            blocking_unlock();
            fail();
        }
        blocking_reserved -= 1usize;
        blocking_queued += 1usize;
        job->run = run;
        job->execution = execution;
        job->state = job_queued as c_uint;
        job->next = null;
        job->previous = blocking_tail;
        if (blocking_tail == null) {
            blocking_head = job;
        } else {
            blocking_tail->next = job;
        }
        blocking_tail = job;
        if (runtime.system::pthread_cond_signal(&blocking_condition as raw Condition*) !=
            0i32 as c_int) {
            blocking_unlock();
            fail();
        }
    }
    blocking_unlock();
}

@safety("R-RUNTIME-BLOCKING-WITHDRAW", "job was submitted")
extern "C" c_bool r_runtime_blocking_withdraw(raw BlockingJob*? job) {
    bool withdrawn = false;
    blocking_lock();
    unsafe {
        if ((job->state as u32) == job_queued) {
            blocking_unlink_locked(job);
            job->state = 0u32 as c_uint;
            withdrawn = true;
        }
    }
    blocking_unlock();
    return runtime.types::c_bool_of(withdrawn);
}

@safety("R-RUNTIME-BLOCKING-STOP", "None")
extern "C" void r_runtime_blocking_stop() {
    blocking_lock();
    unsafe {
        blocking_accepting = false;
        u32 created = blocking_created;
        if (blocking_worker_thread == true) {
            created = 0u32;
        }
        if (runtime.system::pthread_cond_broadcast(&blocking_condition as raw Condition*) !=
            0i32 as c_int) {
            blocking_unlock();
            fail();
        }
        blocking_unlock();
        u32 index = 0u32;
        while (index < created) {
            if (runtime.system::pthread_join(blocking_workers[index as usize], null) !=
                0i32 as c_int) {
                fail();
            }
            index += 1u32;
        }
        if (created != 0u32) {
            blocking_lock();
            blocking_created = 0u32;
            blocking_unlock();
        }
    }
}

// Testing: hook runs once on the worker that executed a task with a budget, after the end of the
// task is published and before the executor releases the task (M42-3); null disarms it.
@safety("R-RUNTIME-TASK-TESTING-AFTER-END", "hook is null or may run on any worker")
extern "C" void r_runtime_darwin_task_testing_after_end(raw fn?() -> void hook) {
    core::atomic_store(&testing_after_end, hook, core::memory_order::release);
}

// ---- Supervision scopes ----

@safety("R-RUNTIME-TASK-SCOPE-OPEN", "scope and entries are frame-owned storage of the owner")
extern "C" c_bool r_runtime_task_scope_open(raw ExecutionHandle*? execution, raw Scope*? scope,
                                            raw ScopeEntry*? entries, c_size capacity) {
    unsafe {
        raw Execution*? running = execution_of(execution);
        usize count = capacity as usize;
        if ((running == null) || (scope == null) || (entries == null) || (count == 0usize) ||
            (count > runtime.types::size_maximum / sizeof(ScopeEntry))) {
            return runtime.types::c_bool_of(false);
        }
        raw Task*? owner = running->frame;
        if (owner == null) {
            return runtime.types::c_bool_of(false);
        }
        executor_lock();
        task_lock(owner);
        if ((owner->kind != Kind::resumable) || (owner->state != State::running) ||
            (owner->terminal_decided == true) ||
            (owner->references == runtime.types::size_maximum) ||
            (owner->open_scope_count == runtime.types::size_maximum)) {
            task_unlock(owner);
            executor_unlock();
            return runtime.types::c_bool_of(false);
        }
        owner->references += 1usize;
        owner->open_scope_count += 1usize;
        task_unlock(owner);
        runtime.types::memset(entries as raw void*?, 0i32 as c_int,
                              (count * sizeof(ScopeEntry)) as c_size) as void;
        *scope = Scope {.entries = entries, .capacity = capacity, .owner = handle_of(owner),
                        .waiter = null, .active = runtime.types::c_bool_of(true),
                        .closing = runtime.types::c_bool_of(false)};
        executor_unlock();
        return runtime.types::c_bool_of(true);
    }
}

@safety("R-RUNTIME-TASK-SCOPE-RESERVE", "scope is null or an open scope")
extern "C" c_size r_runtime_task_scope_reserve(raw Scope*? scope) {
    usize reservation = 0usize;
    executor_lock();
    unsafe {
        bool open = false;
        if (scope != null) {
            open = (runtime.types::is_true(scope->active) == true) &&
                   (runtime.types::is_true(scope->closing) == false);
        }
        if (open == true) {
            usize capacity = scope->capacity as usize;
            usize index = 0usize;
            while (index < capacity) {
                raw ScopeEntry*? entry = entry_at(scope, index);
                if ((entry->frame == null) && (runtime.types::is_true(entry->reserved) == false)) {
                    entry->reserved = runtime.types::c_bool_of(true);
                    reservation = index + 1usize;
                    break;
                }
                index += 1usize;
            }
        }
    }
    executor_unlock();
    return reservation as c_size;
}

@safety("R-RUNTIME-TASK-SCOPE-ABANDON", "reservation is a held reservation of scope")
extern "C" void r_runtime_task_scope_abandon(raw Scope*? scope, c_size reservation) {
    executor_lock();
    unsafe {
        usize slot = reservation as usize;
        if ((scope == null) || (runtime.types::is_true(scope->active) == false) ||
            (slot == 0usize) || (slot > (scope->capacity as usize))) {
            fail();
        }
        raw ScopeEntry*? entry = entry_at(scope, slot - 1usize);
        if ((runtime.types::is_true(entry->reserved) == false) || (entry->frame != null)) {
            fail();
        }
        entry->reserved = runtime.types::c_bool_of(false);
    }
    executor_unlock();
}

@safety("R-RUNTIME-TASK-SCOPE-BIND", "reservation is held; task is its committed observation")
extern "C" void r_runtime_task_scope_bind(raw Scope*? scope, c_size reservation,
                                          raw TaskHandle*? handle) {
    raw Task*? frame = task_of(handle);
    executor_lock();
    unsafe {
        usize slot = reservation as usize;
        if ((scope == null) || (runtime.types::is_true(scope->active) == false) ||
            (runtime.types::is_true(scope->closing) == true) || (frame == null) ||
            (task_of(scope->owner) == frame) || (slot == 0usize) ||
            (slot > (scope->capacity as usize))) {
            fail();
        }
        raw ScopeEntry*? entry = entry_at(scope, slot - 1usize);
        if ((runtime.types::is_true(entry->reserved) == false) || (entry->frame != null)) {
            fail();
        }
        task_lock(frame);
        bool committed = core::atomic_load(&frame->committed, core::memory_order::acquire);
        if ((committed == false) || (frame->scope != null) || (frame->observer_attached == false) ||
            (frame->references == runtime.types::size_maximum)) {
            fail();
        }
        frame->references += 1usize;
        frame->scope = scope;
        frame->scope_index = slot - 1usize;
        entry->frame = handle;
        entry->reserved = runtime.types::c_bool_of(false);
        task_unlock(frame);
    }
    executor_unlock();
}

// The monotonic clock of std.time::instant: continuous Mach time in nanoseconds, the exact
// quotient of ticks times numer by denom, saturated.
protected u64 monotonic_now() {
    unsafe {
        Timebase timebase = {.numer = 0u32 as c_uint32, .denom = 0u32 as c_uint32};
        if (runtime.system::mach_timebase_info(&timebase as raw Timebase*) != 0i32 as c_int) {
            fail();
        }
        u64 numer = timebase.numer as u64;
        u64 denom = timebase.denom as u64;
        if (denom == 0u64) {
            fail();
        }
        u64 ticks = runtime.system::mach_continuous_time() as u64;
        // ticks = quotient * denom + remainder, so ticks * numer / denom is
        // quotient * numer + remainder * numer / denom; remainder * numer < 2^64.
        u64 quotient = ticks / denom;
        u64 remainder = ticks % denom;
        u64 tail = (remainder * numer) / denom;
        auto (low, high) = core::widening_mul(quotient, numer);
        bool overflow = high != 0u64;
        bool beyond = low > u64_maximum - tail;
        if ((overflow == true) || (beyond == true)) {
            return u64_maximum;
        }
        return low + tail;
    }
}

// A one-shot wakeup armed by a deadline-bounded wait. It resumes the owner only while that owner
// still waits on a scope with a reached deadline, so a stale wakeup has no effect.
@callback
@safety("R-RUNTIME-TASK-SCOPE-DEADLINE", "context is a task retained for this wakeup")
protected extern "C" void scope_deadline_fired(raw void* context) {
    unsafe {
        raw Task*? owner = context as raw Task*?;
        Wakeup wakeup = {};
        executor_lock();
        task_lock(owner);
        raw Scope*? scope = owner->waiting_scope;
        bool due = false;
        if ((scope != null) && (owner->scope_deadline_active == true)) {
            if (task_of(scope->waiter) == owner) {
                due = monotonic_now() >= owner->scope_deadline;
            }
        }
        task_unlock(owner);
        if (due == true) {
            wakeup = scope_disarm_locked(scope);
        }
        executor_unlock();
        wake(wakeup);
        task_release_reference(owner);
    }
}

protected WaitOutcome outcome(ExecutionAwaitStatus status) {
    return WaitOutcome {.status = status};
}

// The decision of a scope wait, made under the executor lock.
protected WaitOutcome scope_wait_locked(raw Scope*? scope, raw Task*? owner,
                                        raw const (raw TaskHandle*?)*? selection, usize count,
                                        raw c_size*? ready_index, bool vacancy, bool bounded,
                                        u64 deadline, raw c_bool*? expired) {
    unsafe {
        bool closing = runtime.types::is_true(scope->closing);
        if ((closing == false) &&
            (core::atomic_load(&owner->cancel_requested, core::memory_order::acquire) == true)) {
            return outcome(ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_CANCELLED);
        }
        bool ready = (count == 0usize) && (vacancy == false);
        usize capacity = scope->capacity as usize;
        usize index = 0usize;
        while (index < capacity) {
            raw ScopeEntry*? entry = entry_at(scope, index);
            if (runtime.types::is_true(entry->reserved) == true) {
                return outcome(ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID);
            }
            if (vacancy == true) {
                // A slot is free once no member occupies it; a detached member releases its slot
                // when its publication removes the scope link.
                if (entry->frame == null) {
                    ready = true;
                }
            } else {
                if ((count == 0usize) && (entry->frame != null)) {
                    raw Task*? member = task_of(entry->frame);
                    task_lock(member);
                    // A detached observation is ready only after publication removes its scope
                    // link.
                    bool published =
                        core::atomic_load(&member->terminal_published, core::memory_order::acquire);
                    if ((published == false) || (member->observer_attached == false)) {
                        ready = false;
                    }
                    task_unlock(member);
                }
            }
            index += 1usize;
        }
        usize selected = 0usize;
        usize position = 0usize;
        while (position < count) {
            raw const (raw TaskHandle*?)*? item =
                core::offset_bytes(selection, position * sizeof(raw TaskHandle*?)) as
                raw const (raw TaskHandle*?)*?;
            raw Task*? member = task_of(*item);
            if (member == null) {
                return outcome(ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID);
            }
            if (member->scope != scope) {
                return outcome(ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID);
            }
            task_lock(member);
            bool observed = member->observer_attached;
            task_unlock(member);
            if (observed == false) {
                return outcome(ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID);
            }
            if (ready == false) {
                if (core::atomic_load(&member->terminal_published, core::memory_order::acquire) ==
                    true) {
                    selected = position;
                    ready = true;
                }
            }
            position += 1usize;
        }
        if (ready == true) {
            if (count != 0usize) {
                *ready_index = selected as c_size;
            }
            return outcome(ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_OK);
        }
        u64 remaining = 0u64;
        if (bounded == true) {
            u64 now = monotonic_now();
            if (now >= deadline) {
                *expired = runtime.types::c_bool_of(true);
                return outcome(ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_OK);
            }
            remaining = deadline - now;
        }
        task_lock(owner);
        if ((owner->references > runtime.types::size_maximum - 2usize) || (scope->waiter != null)) {
            fail();
        }
        owner->references += 1usize;
        owner->waiting_scope = scope;
        owner->scope_deadline_active = bounded;
        owner->scope_deadline = 0u64;
        if (bounded == true) {
            owner->scope_deadline = deadline;
            // The wakeup retains the owner until it has run.
            owner->references += 1usize;
        }
        scope->waiter = handle_of(owner);
        task_unlock(owner);
        return WaitOutcome {.status = ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED,
                            .armed = bounded, .remaining = remaining};
    }
}

// Waits for all members (empty selection), for the first terminal member of a selection, or,
// with vacancy, until a slot of the group is free (L25).
protected ExecutionAwaitStatus scope_wait(raw ExecutionHandle*? execution, raw Scope*? scope,
                                          raw const (raw TaskHandle*?)*? selection, usize count,
                                          raw c_size*? ready_index, bool vacancy, bool bounded,
                                          u64 deadline, raw c_bool*? expired) {
    unsafe {
        raw Execution*? running = execution_of(execution);
        bool bad_selection = (count != 0usize) &&
                             ((vacancy == true) || (selection == null) || (ready_index == null));
        if ((running == null) || (scope == null) || (bad_selection == true) ||
            ((bounded == true) && (expired == null))) {
            return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
        }
        raw Task*? owner = running->frame;
        if (owner == null) {
            return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
        }
        if (expired != null) {
            *expired = runtime.types::c_bool_of(false);
        }
        // M26-6: a task cancelled while it awaited another task can reach this wait still
        // registered as the waiter of that task. When the awaited task has published its end, its
        // wakeup is on its way: the wait suspends until it arrives. When the awaited task is a
        // running member of this scope, the barrier, which waits for every member, takes the wait
        // over.
        task_lock(owner);
        raw Task*? awaited = owner->waiting_on;
        task_unlock(owner);
        if (awaited != null) {
            bool released = false;
            bool in_flight = false;
            task_lock(awaited);
            if (awaited->waiter == owner) {
                if ((count == 0usize) && (vacancy == false) && (awaited->scope == scope)) {
                    awaited->waiter = null;
                    released = true;
                }
            } else {
                in_flight =
                    core::atomic_load(&awaited->terminal_published, core::memory_order::acquire);
            }
            task_unlock(awaited);
            if (in_flight == true) {
                return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED;
            }
            if (released == true) {
                task_lock(owner);
                if (owner->waiting_on == awaited) {
                    owner->waiting_on = null;
                }
                task_unlock(owner);
                task_release_reference(owner);
            }
        }
        executor_lock();
        task_lock(owner);
        bool closing = runtime.types::is_true(scope->closing);
        if (((closing == true) && (count != 0usize)) ||
            (runtime.types::is_true(scope->active) == false) || (task_of(scope->owner) != owner) ||
            (owner->kind != Kind::resumable) || (owner->state != State::running) ||
            (owner->terminal_decided == true) || (owner->waiting_on != null)) {
            task_unlock(owner);
            executor_unlock();
            return ExecutionAwaitStatus::R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID;
        }
        raw Task*? previous_waiter = null;
        raw Scope*? waiting = owner->waiting_scope;
        if (waiting != null) {
            if (task_of(waiting->waiter) != owner) {
                fail();
            }
            waiting->waiter = null;
            owner->waiting_scope = null;
            previous_waiter = owner;
        }
        task_unlock(owner);

        WaitOutcome decided = scope_wait_locked(scope, owner, selection, count, ready_index,
                                                vacancy, bounded, deadline, expired);
        executor_unlock();
        if (decided.armed == true) {
            u64 delay = decided.remaining;
            if (delay > i64_maximum) {
                delay = i64_maximum;
            }
            // The global queue of DISPATCH_QUEUE_PRIORITY_DEFAULT is that of QOS_CLASS_DEFAULT
            // (dispatch/queue.h).
            runtime.system::dispatch_after_f(
                runtime.system::dispatch_time(runtime.system::time_now as c_uint64,
                                              delay as c_int64),
                runtime.system::dispatch_get_global_queue(
                    runtime.system::priority_default as c_intptr, 0u64 as c_uintptr),
                owner as raw void*?, scope_deadline_fired);
        }
        if (previous_waiter != null) {
            task_release_reference(previous_waiter);
        }
        return decided.status;
    }
}

@safety("R-RUNTIME-TASK-SCOPE-WAIT", "scope is open by the executing task; selection is bounded")
extern "C" ExecutionAwaitStatus r_runtime_task_scope_wait(
    raw ExecutionHandle*? execution, raw Scope*? scope, raw const (raw TaskHandle*?)*? selection,
    c_size count, raw c_size*? ready_index) {
    return scope_wait(execution, scope, selection, count as usize, ready_index, false, false,
                      0u64, null);
}

// The deadline of a bounded wait: the earlier of the given instant and the deadline of the waiting
// task (Core R-STMT-0018/0019), in nanoseconds of the monotonic clock. An instant before the
// clock's origin has passed; one beyond its range never arrives.
protected u64 bounded_deadline(raw ExecutionHandle*? execution, c_int64 deadline_seconds,
                               c_uint32 deadline_nanoseconds) {
    i64 seconds = deadline_seconds as i64;
    u32 nanoseconds = deadline_nanoseconds as u32;
    unsafe {
        raw Execution*? running = execution_of(execution);
        if (running != null) {
            raw Task*? frame = running->frame;
            if (frame != null) {
                Deadline inherited = frame->deadline;
                i64 own_seconds = inherited.seconds as i64;
                u32 own_nanoseconds = inherited.nanoseconds as u32;
                bool earlier = (own_seconds < seconds) ||
                               ((own_seconds == seconds) && (own_nanoseconds < nanoseconds));
                if ((runtime.types::is_true(inherited.active) == true) && (earlier == true)) {
                    seconds = own_seconds;
                    nanoseconds = own_nanoseconds;
                }
            }
        }
    }
    if (seconds > 0i64) {
        auto (low, high) = core::widening_mul(seconds as u64, 1000000000u64);
        bool overflow = high != 0u64;
        bool beyond = low > u64_maximum - (nanoseconds as u64);
        if ((overflow == true) || (beyond == true)) {
            return u64_maximum;
        }
        return low + (nanoseconds as u64);
    }
    if (seconds == 0i64) {
        return nanoseconds as u64;
    }
    return 0u64;
}

@safety("R-RUNTIME-TASK-SCOPE-WAIT-UNTIL", "scope is open by the executing task; expired is writable")
extern "C" ExecutionAwaitStatus r_runtime_task_scope_wait_until(
    raw ExecutionHandle*? execution, raw Scope*? scope, raw const (raw TaskHandle*?)*? selection,
    c_size count, raw c_size*? ready_index, c_int64 deadline_seconds,
    c_uint32 deadline_nanoseconds, raw c_bool*? expired) {
    u64 deadline = bounded_deadline(execution, deadline_seconds, deadline_nanoseconds);
    return scope_wait(execution, scope, selection, count as usize, ready_index, false, true,
                      deadline, expired);
}

@safety("R-RUNTIME-TASK-SCOPE-WAIT-VACANCY", "scope is open by the executing task")
extern "C" ExecutionAwaitStatus r_runtime_task_scope_wait_vacancy(raw ExecutionHandle*? execution,
                                                                  raw Scope*? scope) {
    return scope_wait(execution, scope, null, 0usize, null, true, false, 0u64, null);
}

@safety("R-RUNTIME-TASK-SCOPE-WAIT-VACANCY-UNTIL",
        "scope is open by the executing task; expired is writable")
extern "C" ExecutionAwaitStatus r_runtime_task_scope_wait_vacancy_until(
    raw ExecutionHandle*? execution, raw Scope*? scope, c_int64 deadline_seconds,
    c_uint32 deadline_nanoseconds, raw c_bool*? expired) {
    u64 deadline = bounded_deadline(execution, deadline_seconds, deadline_nanoseconds);
    return scope_wait(execution, scope, null, 0usize, null, true, true, deadline, expired);
}

@safety("R-RUNTIME-TASK-SCOPE-CANCEL", "scope is open")
extern "C" void r_runtime_task_scope_cancel(raw Scope*? scope) {
    executor_lock();
    unsafe {
        if ((scope == null) || (runtime.types::is_true(scope->active) == false)) {
            fail();
        }
        scope->closing = runtime.types::c_bool_of(true);
        usize capacity = scope->capacity as usize;
        usize index = 0usize;
        while (index < capacity) {
            raw ScopeEntry*? entry = entry_at(scope, index);
            if (entry->frame != null) {
                request_cancellation(task_of(entry->frame));
            }
            index += 1usize;
        }
    }
    executor_unlock();
}

@safety("R-RUNTIME-TASK-SCOPE-CLOSE", "scope is null or a scope of the executing task")
extern "C" c_bool r_runtime_task_scope_close(raw Scope*? scope) {
    executor_lock();
    unsafe {
        if ((scope == null) || (runtime.types::is_true(scope->active) == false) ||
            (scope->waiter != null)) {
            executor_unlock();
            return runtime.types::c_bool_of(false);
        }
        usize capacity = scope->capacity as usize;
        usize index = 0usize;
        while (index < capacity) {
            raw ScopeEntry*? entry = entry_at(scope, index);
            if ((entry->frame != null) || (runtime.types::is_true(entry->reserved) == true)) {
                executor_unlock();
                return runtime.types::c_bool_of(false);
            }
            index += 1usize;
        }
        raw Task*? owner = task_of(scope->owner);
        task_lock(owner);
        if (owner->open_scope_count == 0usize) {
            fail();
        }
        owner->open_scope_count -= 1usize;
        task_unlock(owner);
        scope->active = runtime.types::c_bool_of(false);
        scope->owner = null;
        executor_unlock();
        task_release_reference(owner);
        return runtime.types::c_bool_of(true);
    }
}

// Consume this observation only if it belongs to scope. The caller owns the slot exclusively.
@safety("R-RUNTIME-TASK-SCOPE-DROP", "slot is an observation slot owned by the caller")
extern "C" c_bool r_runtime_task_scope_drop(raw Scope*? scope, raw (raw TaskHandle*?)*? slot) {
    unsafe {
        if ((scope == null) || (slot == null)) {
            return runtime.types::c_bool_of(false);
        }
        raw Task*? frame = task_of(*slot);
        if (frame == null) {
            return runtime.types::c_bool_of(false);
        }
        executor_lock();
        bool member = (runtime.types::is_true(scope->active) == true) && (frame->scope == scope);
        executor_unlock();
        if (member == false) {
            return runtime.types::c_bool_of(false);
        }
    }
    // The observation keeps the member alive after releasing the executor lock.
    consume_observer(slot, true);
    return runtime.types::c_bool_of(true);
}
