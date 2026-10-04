#ifndef R_RUNTIME_TASK_H
#define R_RUNTIME_TASK_H

#include "r_runtime_allocator.h"
#include "r_runtime_budget.h"
#include "r_runtime_type.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct RRuntimeTask RRuntimeTask;
typedef struct RRuntimeTaskExecution RRuntimeTaskExecution;
typedef struct RRuntimeTaskExternalExecution RRuntimeTaskExternalExecution;

/* Compiler-owned, immovable storage in a resumable frame; members are runtime-private. */
typedef struct RRuntimeTaskScopeEntry {
    RRuntimeTask *task;
    _Bool reserved;
} RRuntimeTaskScopeEntry;

typedef struct RRuntimeTaskScope {
    RRuntimeTaskScopeEntry *entries;
    size_t capacity;
    RRuntimeTask *owner;
    RRuntimeTask *waiter;
    _Bool active;
    _Bool closing;
} RRuntimeTaskScope;

typedef enum RRuntimeTaskStepStatus {
    R_RUNTIME_TASK_STEP_COMPLETED = 0,
    R_RUNTIME_TASK_STEP_SUSPENDED,
    R_RUNTIME_TASK_STEP_CANCELLED
} RRuntimeTaskStepStatus;

/*
 * Body contract: payload and result are frame-owned call-bounded storage. Every normal body return
 * initializes non-void result exactly once, including a return after observing cancellation. The
 * runtime may skip the body when cancellation was already requested before first execution.
 */
typedef void (*RRuntimeTaskBodyFn)(RRuntimeTaskExecution *execution, void *payload, void *result);

/*
 * Resumable step contract: payload is the compiler-generated owned async frame. COMPLETED obeys the
 * ordinary body result contract above. SUSPENDED is valid only immediately after
 * task_execution_await or task_scope_wait registered an incomplete wait; the step shall return
 * without touching its call-bounded execution token again. CANCELLED reports that the step ran its
 * cancellation finalies and initialized no result. A resumed step observes the same payload and
 * result storage and shall continue from its frame-owned state. A cancellation request resumes a
 * suspended step; generated code consumes its awaited task and runs active finalies before
 * returning CANCELLED.
 */
typedef RRuntimeTaskStepStatus (*RRuntimeTaskStepFn)(RRuntimeTaskExecution *execution,
                                                     void *payload,
                                                     void *result);

/*
 * Compiler-generated in-place payload initialization runs synchronously inside the successful
 * commit critical section. It shall be allocation-free, nonblocking, non-failing, shall not
 * re-enter the task runtime, and shall establish the complete payload invariant exactly once.
 * The context is call-bounded and is never observed on a failed commit.
 */
typedef void (*RRuntimeTaskPayloadInitializeFn)(void *payload, const void *context);

/*
 * External start runs synchronously after the task has been published but before commit returns.
 * Adapter-owned native state shall already be reserved in the staged payload before commit. Start
 * binds that state to the committed frame, calls external_start_ready exactly once, and never
 * blocks an executor worker. A pending external-cancel callback is dispatched only after start
 * returns, so start shall not wait for that callback. External cancel only initiates native
 * cancellation; acknowledgement is separate.
 */
typedef void (*RRuntimeTaskExternalStartFn)(RRuntimeTaskExternalExecution *execution,
                                            void *payload,
                                            void *result);
typedef void (*RRuntimeTaskExternalCancelFn)(RRuntimeTaskExternalExecution *execution,
                                             void *payload);

typedef enum RRuntimeExecutorStartStatus {
    R_RUNTIME_EXECUTOR_START_OK = 0,
    R_RUNTIME_EXECUTOR_START_INVALID,
    R_RUNTIME_EXECUTOR_START_ALLOCATION_FAILED,
    R_RUNTIME_EXECUTOR_START_RUNTIME_STOPPING
} RRuntimeExecutorStartStatus;

typedef enum RRuntimeTaskStartStatus {
    R_RUNTIME_TASK_START_OK = 0,
    R_RUNTIME_TASK_START_INVALID,
    R_RUNTIME_TASK_START_ALLOCATION_FAILED,
    R_RUNTIME_TASK_START_RUNTIME_STOPPING
} RRuntimeTaskStartStatus;

typedef enum RRuntimeTaskState {
    R_RUNTIME_TASK_INVALID = 0,
    R_RUNTIME_TASK_PREPARED,
    R_RUNTIME_TASK_QUEUED,
    R_RUNTIME_TASK_RUNNING,
    R_RUNTIME_TASK_COMPLETED,
    R_RUNTIME_TASK_CANCELLED,
    R_RUNTIME_TASK_SUSPENDED
} RRuntimeTaskState;

typedef enum RRuntimeTaskAwaitStatus {
    R_RUNTIME_TASK_AWAIT_OK = 0,
    R_RUNTIME_TASK_AWAIT_INVALID,
    R_RUNTIME_TASK_AWAIT_CANCELLED,
    R_RUNTIME_TASK_AWAIT_WOULD_BLOCK
} RRuntimeTaskAwaitStatus;

typedef enum RRuntimeTaskExecutionAwaitStatus {
    R_RUNTIME_TASK_EXECUTION_AWAIT_OK = 0,
    R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID,
    R_RUNTIME_TASK_EXECUTION_AWAIT_CANCELLED,
    R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED
} RRuntimeTaskExecutionAwaitStatus;

typedef struct RRuntimeTaskPrepareResult {
    RRuntimeTask *transaction;
    RRuntimeTaskStartStatus status;
} RRuntimeTaskPrepareResult;

typedef struct RRuntimeTaskStartResult {
    RRuntimeTask *task;
    RRuntimeTaskStartStatus status;
} RRuntimeTaskStartResult;

/*
 * Ownership: allocator is a shared service that must outlive lifecycle stop. Success starts one
 * fresh eager executor generation. A second or concurrent start reports runtime stopping.
 */
RRuntimeExecutorStartStatus r_runtime_executor_lifecycle_start(RRuntimeAllocator *allocator);

/*
 * Ownership: closes the running generation to new commits, requests cancellation in submission
 * order, and does not return until every published task has acknowledged terminal completion and
 * every provisional start has committed or rolled back. Returns false when no generation exists
 * or when called from an executor worker, which is never permitted to block on its own drain.
 */
_Bool r_runtime_executor_lifecycle_stop(void);
/* Keep the executor available to cleanup, cancelling both current and subsequently committed work.
 */
void r_runtime_executor_cancel_pending(void);
/* True while the calling thread executes a task body, step or external callback. */
_Bool r_runtime_executor_on_worker(void);
/*
 * Process exit from a thread other than the initial one (R-SLIB-PROC-0007): closes the generation
 * to new commits, requests cancellation of every task and returns once no task other than those
 * executing on the calling thread is queued, running, preparing or external, except a task that
 * waits to join the calling thread. Every remaining task then waits on the calling thread; the
 * frames stay allocated because the process ends without resuming them. Only the first caller
 * returns; a later caller waits for the process to end.
 */
_Bool r_runtime_executor_quiesce_for_exit(void);
/* Marks the task executing on this thread, if any, as waiting to join a native thread. */
void r_runtime_executor_join_begin(pthread_t thread);
void r_runtime_executor_join_end(void);

/*
 * Ownership: reserves the complete frame and one provisional start transaction but does not read,
 * move, drop, or otherwise modify the caller's payload. A zero-sized type uses alignment one.
 * payload_type and result_type callbacks are immutable compiler-generated metadata.
 */
RRuntimeTaskPrepareResult r_runtime_task_start_prepare(RRuntimeTypeInfo payload_type,
                                                       RRuntimeTypeInfo result_type,
                                                       RRuntimeTaskBodyFn body);

/*
 * Ownership is identical to task_start_prepare. The complete resumable frame and its one inline
 * waiter slot are reserved before commit; suspension performs no allocation.
 */
RRuntimeTaskPrepareResult r_runtime_task_resumable_start_prepare(RRuntimeTypeInfo payload_type,
                                                                 RRuntimeTypeInfo result_type,
                                                                 RRuntimeTaskStepFn step);

/*
 * Ownership: reserves a two-phase task frame for a native/asynchronous producer. No caller payload
 * is touched before the ordinary start_commit succeeds. The external producer owns its call-bounded
 * payload/result borrows until it acknowledges the selected terminal event.
 */
RRuntimeTaskPrepareResult
r_runtime_task_external_start_prepare(RRuntimeTypeInfo payload_type,
                                      RRuntimeTypeInfo result_type,
                                      RRuntimeTaskExternalStartFn start,
                                      RRuntimeTaskExternalCancelFn cancel);

/*
 * Borrow: returns the executor allocator associated with one still-owned PREPARED transaction.
 * The allocator outlives every committed task in that executor generation. Invalid, committed or
 * consumed transactions return null. This call allocates nothing and does not end preparation.
 */
RRuntimeAllocator *r_runtime_task_start_allocator(const RRuntimeTask *transaction);

/*
 * Ownership: consumes transaction in every outcome. Success move-initializes the frame payload
 * from staged_payload, thereby committing that caller-owned value, and returns the sole task
 * observation right. Failure leaves staged_payload initialized and byte-for-byte unchanged.
 */
RRuntimeTaskStartResult r_runtime_task_start_commit(RRuntimeTask **transaction,
                                                    void *staged_payload);

/*
 * Ownership: consumes transaction in every outcome. Success invokes initialize exactly once on
 * the already reserved runtime-owned payload, after the last recoverable start check and before
 * publication. Failure does not invoke initialize and leaves context untouched. A nonzero payload
 * requires a non-null initialize callback; a zero-sized payload requires initialize to be null.
 */
RRuntimeTaskStartResult r_runtime_task_start_commit_initialize(
    RRuntimeTask **transaction, RRuntimeTaskPayloadInitializeFn initialize, const void *context);

/*
 * Same contract as start_commit_initialize for a resumable task whose sole observer awaits it
 * immediately after commit. When the calling thread has step_stack_bytes of R stack available
 * (plus one call transition) the first step runs on the calling thread before this call
 * returns, so a step that never suspends publishes before the await; otherwise, or when the
 * step suspends, the task continues on the executor exactly as after start_commit_initialize.
 * Observable ordering is unchanged: the caller performs no work between commit and await.
 */
RRuntimeTaskStartResult
r_runtime_task_start_commit_initialize_inline(RRuntimeTask **transaction,
                                              RRuntimeTaskPayloadInitializeFn initialize,
                                              const void *context,
                                              size_t step_stack_bytes);

/*
 * Same contract as start_commit_initialize, except that the committed resumable task is not
 * handed to the executor: the caller shall pass it to r_runtime_task_run_deferred exactly once
 * before it performs any other work (the generated task-group start does so right after bind).
 */
RRuntimeTaskStartResult r_runtime_task_start_commit_initialize_deferred(
    RRuntimeTask **transaction, RRuntimeTaskPayloadInitializeFn initialize, const void *context);

/*
 * Runs the first step of a task committed by start_commit_initialize_deferred on the calling
 * thread when step_stack_bytes of R stack are available, otherwise dispatches it to the executor.
 */
void r_runtime_task_run_deferred(RRuntimeTask *task, size_t step_stack_bytes);

/* Ownership: rolls back and consumes one uncommitted transaction without touching its payload. */
void r_runtime_task_start_abort(RRuntimeTask **transaction);

/*
 * Ownership: consumes one published task observation. Success move-initializes result_storage.
 * A cancelled task has no result. An executor worker never blocks: it reports WOULD_BLOCK and
 * leaves the observation unchanged when the target is incomplete.
 */
RRuntimeTaskAwaitStatus r_runtime_task_await(RRuntimeTask **task, void *result_storage);

/*
 * Compiler-generated resumable steps use this operation instead of blocking await. task and
 * non-void result_storage shall address fields in the owned resumable frame and remain stable
 * across suspension. OK or CANCELLED consumes task exactly as task_await does. SUSPENDED atomically
 * registers the current computation as the task's sole waiter, leaves task and result_storage
 * unchanged, and requires the current step to return R_RUNTIME_TASK_STEP_SUSPENDED. INVALID also
 * leaves both untouched. Completion publishes with release semantics before scheduling the
 * continuation; resumed observation uses acquire. Parent cancellation requests cancellation of
 * the linked awaited task without consuming its frame-owned observation. The parent remains
 * suspended until that task publishes terminal acknowledgement, then resumes exactly once for
 * generated finally cleanup. A linked awaited task retains the parent until terminal publication.
 */
RRuntimeTaskExecutionAwaitStatus r_runtime_task_execution_await(RRuntimeTaskExecution *execution,
                                                                RRuntimeTask **task,
                                                                void *result_storage);

/*
 * Scoped supervision reserves no heap storage. Open initializes frame-owned scope and entries;
 * neither may move before close. A nonzero reservation is made before preparing a child and must
 * be abandoned after start failure, or bound to its committed observation. Bind cannot fail.
 * Supervision retains the child until terminal acknowledgement and observation consumption.
 */
_Bool r_runtime_task_scope_open(RRuntimeTaskExecution *execution,
                                RRuntimeTaskScope *scope,
                                RRuntimeTaskScopeEntry *entries,
                                size_t capacity);
size_t r_runtime_task_scope_reserve(RRuntimeTaskScope *scope);
void r_runtime_task_scope_abandon(RRuntimeTaskScope *scope, size_t reservation);
void r_runtime_task_scope_bind(RRuntimeTaskScope *scope, size_t reservation, RRuntimeTask *task);

/*
 * An empty selection waits for all supervised children. Otherwise waits for the first terminal
 * observation in selection order and returns its zero-based index. Selection is call-bounded;
 * results and checked errors remain owned by the individual observations. SUSPENDED obeys the
 * step contract. Parent cancellation returns CANCELLED until scope_cancel starts mandatory drain;
 * drain waits for acknowledgement despite parent cancellation and does not consume observations.
 */
RRuntimeTaskExecutionAwaitStatus r_runtime_task_scope_wait(RRuntimeTaskExecution *execution,
                                                           RRuntimeTaskScope *scope,
                                                           RRuntimeTask *const *selection,
                                                           size_t count,
                                                           size_t *ready_index);
/*
 * The same wait bounded by an instant of the monotonic clock of std.time::instant. When the
 * instant is reached before the selection is ready, OK is returned with *expired set and no
 * index; a later publication is observed by the next wait. The wakeup allocates nothing.
 */
RRuntimeTaskExecutionAwaitStatus r_runtime_task_scope_wait_until(RRuntimeTaskExecution *execution,
                                                                 RRuntimeTaskScope *scope,
                                                                 RRuntimeTask *const *selection,
                                                                 size_t count,
                                                                 size_t *ready_index,
                                                                 int64_t deadline_seconds,
                                                                 uint32_t deadline_nanoseconds,
                                                                 _Bool *expired);
/*
 * Waits until the group has a free slot (L25): no member occupies it and no start holds it
 * reserved. The waiting task is woken by every publication of a member, as for the other waits.
 * The bounded form behaves like r_runtime_task_scope_wait_until.
 */
RRuntimeTaskExecutionAwaitStatus r_runtime_task_scope_wait_vacancy(RRuntimeTaskExecution *execution,
                                                                   RRuntimeTaskScope *scope);
RRuntimeTaskExecutionAwaitStatus
r_runtime_task_scope_wait_vacancy_until(RRuntimeTaskExecution *execution,
                                        RRuntimeTaskScope *scope,
                                        int64_t deadline_seconds,
                                        uint32_t deadline_nanoseconds,
                                        _Bool *expired);
void r_runtime_task_scope_cancel(RRuntimeTaskScope *scope);

/*
 * The deadline of a task (Core R-STMT-0019): an instant of the monotonic clock of
 * std.time::instant, or none. A task inherits the deadline of the task that prepares it; a
 * deadline block narrows the deadline of the executing task and restores it on every exit.
 */
typedef struct RRuntimeTaskDeadline {
    _Bool active;
    int64_t seconds;
    uint32_t nanoseconds;
} RRuntimeTaskDeadline;

/* Narrows the deadline of the executing task by the given one, which may be inactive, and
 * returns the previous deadline for leave. Outside a task both calls do nothing. */
RRuntimeTaskDeadline r_runtime_task_deadline_enter(RRuntimeTaskDeadline narrowing);
void r_runtime_task_deadline_leave(RRuntimeTaskDeadline previous);

/* Core R-STMT-0020: makes the budget of the executing task a new budget with the given limits
 * under its previous one and returns the previous one for leave, which restores it; a budget
 * that cannot be recorded refuses every charge. Outside a task both calls do nothing. */
RRuntimeBudget *
r_runtime_task_budget_enter(_Bool has_bytes, uint64_t bytes, _Bool has_tasks, uint64_t tasks);
void r_runtime_task_budget_leave(RRuntimeBudget *previous);

/* Narrows the deadline of a standard operation before its start by the deadline of the
 * executing task (R-SLIB-ASYNC-0008): the earlier of both, or the task deadline when the
 * operation has none. */
void r_runtime_task_deadline_narrow(_Bool *active, int64_t *seconds, uint32_t *nanoseconds);

/* R-SLIB-ASYNC-0018: the identifier of the task whose code runs on this thread, from one upward
   in start-commit order, or zero outside every task. Allocation-free and lock-free. */
uint64_t r_runtime_task_current_id(void);

/* Consume this observation only if it belongs to scope. The caller owns the slot exclusively.
 * Generated unwind code clears the slot's initialization flag when this returns true. */
_Bool r_runtime_task_scope_drop(RRuntimeTaskScope *scope, RRuntimeTask **slot);

/* Close succeeds only after all observations are consumed and every child acknowledges completion.
 */
_Bool r_runtime_task_scope_close(RRuntimeTaskScope *scope);

/*
 * Ownership: both operations consume one published task observation. Cancel requests cooperative
 * cancellation before detaching; detach permits execution to continue. The runtime retains frame
 * storage through body/native acknowledgement and drops an unobserved result exactly once.
 */
void r_runtime_task_cancel(RRuntimeTask **task);
void r_runtime_task_detach(RRuntimeTask **task);

/* Ownership: ordinary generated task drop has the same consuming semantics as cancel. */
void r_runtime_task_destroy(RRuntimeTask **task);

RRuntimeTaskState r_runtime_task_state(RRuntimeTask *task);

/* The execution token and its payload/result pointers are body-call-bounded borrows. */
_Bool r_runtime_task_execution_cancel_requested(const RRuntimeTaskExecution *execution);

/*
 * P4.4: begins an awaited call of an async function whose body cannot suspend as an ordinary call
 * on the awaiting task, an optimization under Core R-AM-0003. It succeeds while the executor runs,
 * no drain is in progress, the calling task has no budget and this thread's stack holds
 * stack_bytes (the measured bound of the direct body), and then takes the next task identifier
 * (R-SLIB-ASYNC-0018), so every later task keeps the identifier an ordinary start would have given
 * it; r_runtime_task_current_id reports it until r_runtime_task_direct_end. False leaves nothing
 * changed; the caller then starts the call as a task.
 */
_Bool r_runtime_task_direct_begin(size_t stack_bytes);
void r_runtime_task_direct_end(void);

/*
 * P4.4: whether an awaited standard operation that can complete at once may complete on the
 * awaiting task without starting its task: true while the executor runs, no drain is in progress
 * and the calling task has no budget, the conditions under which its start would succeed
 * uncounted. The operation then takes no task identifier, which R-SLIB-ASYNC-0018 allows: the
 * identifiers stay unique and in start order.
 */
_Bool r_runtime_task_inline_completion_allowed(void);

/*
 * External producer protocol. start_ready publishes fully initialized native state. Completion and
 * cancellation race through one terminal selection. The producer shall acknowledge only after the
 * native backend can no longer access payload/result storage. A completion acknowledgement treats
 * result storage as initialized; a cancellation acknowledgement does not.
 */
void r_runtime_task_external_start_ready(RRuntimeTaskExternalExecution *execution);

/*
 * Selects completion only while no terminal event is selected: the call is itself the event and
 * never replaces a cancellation, so an adapter whose cancel callback runs may rely on no
 * completion following it.
 */
_Bool r_runtime_task_external_try_select_completion(RRuntimeTaskExternalExecution *execution);

/*
 * Selects completion using the sequence captured when the native or adapter terminal event
 * occurred. It may replace an unacknowledged cancellation only when event_sequence is earlier.
 */
_Bool r_runtime_task_external_try_select_completion_at(RRuntimeTaskExternalExecution *execution,
                                                       uint64_t event_sequence);

/*
 * A native adapter calls this only after cancellation selected first and a documented
 * non-cancellable commit requires a completion outcome. An earlier ordinary native event uses
 * try_select_completion_at with its captured sequence instead. Success replaces the
 * unacknowledged cancellation selection with completion so the initialized result is transferred
 * or dropped normally.
 */
_Bool r_runtime_task_external_select_terminal_completion(RRuntimeTaskExternalExecution *execution);
void r_runtime_task_external_acknowledge(RRuntimeTaskExternalExecution *execution);
_Bool r_runtime_task_external_cancel_requested(const RRuntimeTaskExternalExecution *execution);

/* Returns the original cancellation candidate sequence, or zero iff none was ever selected. */
uint64_t
r_runtime_task_external_cancellation_sequence(const RRuntimeTaskExternalExecution *execution);

/*
 * Blocking call pool (Core R-TERM-0015, Library R-SLIB-ASYNC-0017). At most
 * R_RUNTIME_BLOCKING_THREAD_COUNT runtime-owned threads, disjoint from the executor workers and
 * from the filesystem adapter lane, run submitted jobs one at a time in submission order; a thread
 * is added only while every existing one is busy. A producer reserves a slot before its task
 * commits, so submission never allocates or fails. withdraw removes a job that no thread has
 * taken yet; a taken job belongs to its thread until run returns.
 */
#define R_RUNTIME_BLOCKING_THREAD_COUNT 4U
#define R_RUNTIME_BLOCKING_MAX_PENDING_CALLS ((size_t)65536U)

typedef struct RRuntimeBlockingJob RRuntimeBlockingJob;
typedef void (*RRuntimeBlockingRunFn)(RRuntimeBlockingJob *job);

struct RRuntimeBlockingJob {
    RRuntimeBlockingJob *previous;
    RRuntimeBlockingJob *next;
    RRuntimeBlockingRunFn run;
    const RRuntimeTaskExternalExecution *execution;
    unsigned int state;
};

typedef enum RRuntimeBlockingReserveStatus {
    R_RUNTIME_BLOCKING_RESERVED = 0,
    R_RUNTIME_BLOCKING_QUEUE_FULL,
    R_RUNTIME_BLOCKING_THREAD_UNAVAILABLE,
    R_RUNTIME_BLOCKING_STOPPING
} RRuntimeBlockingReserveStatus;

/* Accepts at most capacity waiting jobs until stop; threads start on demand. */
_Bool r_runtime_blocking_start(size_t capacity);
RRuntimeBlockingReserveStatus r_runtime_blocking_reserve(void);
void r_runtime_blocking_unreserve(void);
/* execution names the task that run completes; an exit from run does not wait for that task. */
void r_runtime_blocking_submit(RRuntimeBlockingJob *job,
                               RRuntimeBlockingRunFn run,
                               const RRuntimeTaskExternalExecution *execution);
_Bool r_runtime_blocking_withdraw(RRuntimeBlockingJob *job);
/* Refuses new reservations, lets the threads drain the queue and joins them; on a pool thread,
   which exits the process, it only refuses reservations. */
void r_runtime_blocking_stop(void);

#ifdef __cplusplus
}
#endif

#endif
