#include "r_runtime_task.h"

#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define R_TASK_TEST_REQUIRE(condition, message)                                                    \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "task test failure: %s\n", (message));                           \
            return 0;                                                                              \
        }                                                                                          \
    } while (0)

static _Atomic size_t r_task_test_thread_local_cleanup_calls;

static void test_thread_local_cleanup(void) {
    (void)atomic_fetch_add_explicit(
        &r_task_test_thread_local_cleanup_calls, 1U, memory_order_relaxed);
}

typedef struct RTaskTestTracker {
    _Atomic size_t payload_moves;
    _Atomic size_t payload_drops;
    _Atomic size_t result_moves;
    _Atomic size_t result_drops;
    _Atomic size_t body_calls;
} RTaskTestTracker;

typedef struct RTaskTestPayload {
    RTaskTestTracker *tracker;
    int value;
} RTaskTestPayload;

typedef struct RTaskTestTerminalOrderContext {
    _Atomic _Bool body_entered;
    _Atomic _Bool allow_return;
    _Atomic size_t payload_drops;
    _Atomic int state_at_payload_drop;
    RRuntimeTask *task;
} RTaskTestTerminalOrderContext;

typedef struct RTaskTestTerminalOrderPayload {
    RTaskTestTerminalOrderContext *context;
} RTaskTestTerminalOrderPayload;

typedef struct RTaskTestInitializeContext {
    RTaskTestTracker *tracker;
    int value;
    _Atomic size_t *calls;
    size_t payload_alignment;
    _Atomic _Bool *payload_aligned;
} RTaskTestInitializeContext;

typedef struct RTaskTestResult {
    RTaskTestTracker *tracker;
    int value;
} RTaskTestResult;

typedef struct RTaskTestStopContext {
    _Atomic _Bool returned;
    _Bool stopped;
} RTaskTestStopContext;

typedef struct RTaskTestParallelContext {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    size_t entered;
    _Bool release;
    _Atomic size_t payload_drops;
} RTaskTestParallelContext;

typedef struct RTaskTestParallelPayload {
    RTaskTestParallelContext *context;
    int value;
} RTaskTestParallelPayload;

typedef struct RTaskTestCancelContext {
    _Atomic _Bool entered;
    _Atomic _Bool saw_cancel;
    _Atomic _Bool allow_return;
    _Atomic _Bool body_returned;
    _Atomic size_t payload_drops;
    _Atomic size_t result_drops;
} RTaskTestCancelContext;

typedef struct RTaskTestCancelPayload {
    RTaskTestCancelContext *context;
} RTaskTestCancelPayload;

typedef struct RTaskTestCancelResult {
    RTaskTestCancelContext *context;
} RTaskTestCancelResult;

typedef struct RTaskTestDrainContext {
    _Atomic size_t payload_drops;
    _Atomic size_t body_entries;
    _Atomic size_t cancellation_acknowledgements;
} RTaskTestDrainContext;

typedef struct RTaskTestDrainPayload {
    RTaskTestDrainContext *context;
} RTaskTestDrainPayload;

typedef struct RTaskTestExternalCommitContext {
    _Atomic _Bool started;
    _Atomic _Bool completion_selected;
    _Atomic size_t payload_drops;
    _Atomic size_t result_drops;
} RTaskTestExternalCommitContext;

typedef struct RTaskTestExternalCommitPayload {
    RTaskTestExternalCommitContext *context;
    RRuntimeTaskExternalExecution *execution;
    void *result;
} RTaskTestExternalCommitPayload;

typedef struct RTaskTestExternalCommitResult {
    RTaskTestExternalCommitContext *context;
} RTaskTestExternalCommitResult;

typedef struct RTaskTestSequencedExternalContext {
    uint64_t candidate_sequence;
    _Atomic uint64_t cancellation_sequence;
    _Atomic _Bool started;
    _Atomic _Bool selected;
    _Atomic size_t payload_drops;
    _Atomic size_t result_drops;
} RTaskTestSequencedExternalContext;

typedef struct RTaskTestSequencedExternalPayload {
    RTaskTestSequencedExternalContext *context;
    RRuntimeTaskExternalExecution *execution;
    void *result;
} RTaskTestSequencedExternalPayload;

typedef struct RTaskTestSequencedExternalResult {
    RTaskTestSequencedExternalContext *context;
} RTaskTestSequencedExternalResult;

typedef struct RTaskTestStartAckContext {
    pthread_t stop_thread;
    _Atomic _Bool stop_returned;
    _Atomic _Bool acknowledged_in_start;
    _Atomic size_t cancel_callbacks;
    _Atomic size_t payload_drops;
} RTaskTestStartAckContext;

typedef struct RTaskTestStartAckPayload {
    RTaskTestStartAckContext *context;
} RTaskTestStartAckPayload;

typedef struct RTaskTestNestedPayload {
    RRuntimeTask *inner;
} RTaskTestNestedPayload;

typedef struct RTaskTestResumableContext {
    _Atomic _Bool child_entered;
    _Atomic _Bool child_cancel_seen;
    _Atomic _Bool release_child;
    _Atomic _Bool child_returned;
    _Atomic _Bool outer_registered;
    _Atomic _Bool allow_outer_return;
    _Atomic size_t outer_step_calls;
    _Atomic size_t active_steps;
    _Atomic size_t maximum_active_steps;
    _Atomic size_t child_payload_drops;
    _Atomic size_t child_result_moves;
    _Atomic size_t child_result_drops;
    _Atomic size_t outer_payload_drops;
    _Atomic size_t outer_result_initializations;
    _Atomic size_t outer_result_moves;
    _Atomic size_t outer_result_drops;
    _Atomic uint32_t outer_cancel_finally_trace;
    _Atomic size_t outer_cancel_finally_entries;
    _Atomic _Bool outer_finally_saw_child_ack;
    _Bool hold_outer_return;
    _Bool hold_child_cancellation;
} RTaskTestResumableContext;

typedef struct RTaskTestResumableChildPayload {
    RTaskTestResumableContext *context;
    int value;
} RTaskTestResumableChildPayload;

typedef struct RTaskTestResumableChildResult {
    RTaskTestResumableContext *context;
    int value;
} RTaskTestResumableChildResult;

typedef struct RTaskTestResumableOuterPayload {
    RTaskTestResumableContext *context;
    RRuntimeTask *child;
    RTaskTestResumableChildResult child_result;
} RTaskTestResumableOuterPayload;

typedef struct RTaskTestResumableOuterResult {
    RTaskTestResumableContext *context;
    int value;
} RTaskTestResumableOuterResult;

typedef struct RTaskTestRaceControl {
    RTaskTestResumableContext *context;
    RRuntimeTask **outer;
    _Atomic size_t ready;
    _Atomic _Bool go;
} RTaskTestRaceControl;

typedef struct RTaskTestAwaitMisuseContext {
    _Atomic _Bool first_entered;
    _Atomic _Bool release_first;
    _Atomic _Bool release_second;
    _Atomic int self_status;
    _Atomic int second_status;
    _Atomic size_t payload_drops;
} RTaskTestAwaitMisuseContext;

typedef struct RTaskTestAwaitMisuseChildPayload {
    RTaskTestAwaitMisuseContext *context;
    _Atomic _Bool *release;
} RTaskTestAwaitMisuseChildPayload;

typedef struct RTaskTestAwaitMisuseOuterPayload {
    RTaskTestAwaitMisuseContext *context;
    RRuntimeTask *self;
    RRuntimeTask *first;
    RRuntimeTask *second;
} RTaskTestAwaitMisuseOuterPayload;

static RRuntimeTypeInfo void_type(void) {
    RRuntimeTypeInfo type = {0U, 1U, NULL, NULL};

    return type;
}

static void tracker_initialize(RTaskTestTracker *tracker) {
    atomic_init(&tracker->payload_moves, 0U);
    atomic_init(&tracker->payload_drops, 0U);
    atomic_init(&tracker->result_moves, 0U);
    atomic_init(&tracker->result_drops, 0U);
    atomic_init(&tracker->body_calls, 0U);
}

static void value_payload_move(void *destination, void *source) {
    RTaskTestPayload *destination_value = destination;
    RTaskTestPayload *source_value = source;

    *destination_value = *source_value;
    (void)atomic_fetch_add_explicit(
        &destination_value->tracker->payload_moves, 1U, memory_order_relaxed);
    source_value->tracker = NULL;
    source_value->value = 0;
}

static void value_payload_drop(void *value) {
    RTaskTestPayload *payload = value;

    (void)atomic_fetch_add_explicit(&payload->tracker->payload_drops, 1U, memory_order_relaxed);
    payload->tracker = NULL;
}

static void value_payload_initialize(void *payload_pointer, const void *context_pointer) {
    RTaskTestPayload *payload = payload_pointer;
    const RTaskTestInitializeContext *context = context_pointer;

    payload->tracker = context->tracker;
    payload->value = context->value;
    atomic_store_explicit(context->payload_aligned,
                          (uintptr_t)payload_pointer % context->payload_alignment == 0U,
                          memory_order_relaxed);
    (void)atomic_fetch_add_explicit(context->calls, 1U, memory_order_relaxed);
}

static void value_result_move(void *destination, void *source) {
    RTaskTestResult *destination_value = destination;
    RTaskTestResult *source_value = source;

    *destination_value = *source_value;
    (void)atomic_fetch_add_explicit(
        &destination_value->tracker->result_moves, 1U, memory_order_relaxed);
    source_value->tracker = NULL;
    source_value->value = 0;
}

static void value_result_drop(void *value) {
    RTaskTestResult *result = value;

    (void)atomic_fetch_add_explicit(&result->tracker->result_drops, 1U, memory_order_relaxed);
    result->tracker = NULL;
}

static void
value_body(RRuntimeTaskExecution *execution, void *payload_pointer, void *result_pointer) {
    RTaskTestPayload *payload = payload_pointer;
    RTaskTestResult *result = result_pointer;

    (void)execution;
    (void)atomic_fetch_add_explicit(&payload->tracker->body_calls, 1U, memory_order_relaxed);
    result->tracker = payload->tracker;
    result->value = payload->value + 1;
}

static void *stop_worker(void *context_pointer) {
    RTaskTestStopContext *context = context_pointer;

    context->stopped = r_runtime_executor_lifecycle_stop();
    atomic_store_explicit(&context->returned, 1, memory_order_release);
    return NULL;
}

static _Bool test_start_failures_and_two_phase_commit(RRuntimeAllocator *allocator) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RTaskTestPayload),
        _Alignof(RTaskTestPayload),
        value_payload_move,
        value_payload_drop,
    };
    const RRuntimeTypeInfo result_type = {
        sizeof(RTaskTestResult),
        _Alignof(RTaskTestResult),
        value_result_move,
        value_result_drop,
    };
    RTaskTestTracker tracker;
    RTaskTestPayload payload;
    RTaskTestPayload original;
    RRuntimeTaskPrepareResult prepared;
    RRuntimeTaskPrepareResult initialized_prepared;
    RRuntimeTaskStartResult started;
    RRuntimeTaskStartResult initialized_started;
    RTaskTestInitializeContext initialize_context;
    _Atomic size_t initialize_calls;
    _Atomic _Bool payload_aligned;
    RTaskTestStopContext stop_context;
    pthread_t stop_thread;

    tracker_initialize(&tracker);
    payload.tracker = &tracker;
    payload.value = 41;
    original = payload;

    prepared = r_runtime_task_start_prepare(payload_type, result_type, value_body);
    R_TASK_TEST_REQUIRE(prepared.status == R_RUNTIME_TASK_START_RUNTIME_STOPPING,
                        "prepare before lifecycle start did not report runtime_stopping");
    R_TASK_TEST_REQUIRE(r_runtime_task_start_allocator(prepared.transaction) == NULL,
                        "failed prepare exposed a start allocator");
    R_TASK_TEST_REQUIRE(memcmp(&payload, &original, sizeof(payload)) == 0,
                        "stopped prepare changed the payload");

    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) ==
                            R_RUNTIME_EXECUTOR_START_ALLOCATION_FAILED,
                        "executor reservation failure was not reported");
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) ==
                            R_RUNTIME_EXECUTOR_START_OK,
                        "executor failed to start");
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) ==
                            R_RUNTIME_EXECUTOR_START_RUNTIME_STOPPING,
                        "duplicate executor start was accepted");

    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    prepared = r_runtime_task_start_prepare(payload_type, result_type, value_body);
    R_TASK_TEST_REQUIRE(prepared.status == R_RUNTIME_TASK_START_ALLOCATION_FAILED,
                        "task frame allocation failure was not reported");
    R_TASK_TEST_REQUIRE(memcmp(&payload, &original, sizeof(payload)) == 0,
                        "allocation failure changed the payload");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&tracker.payload_moves, memory_order_relaxed) == 0U,
                        "allocation failure invoked payload move");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&tracker.body_calls, memory_order_relaxed) == 0U,
                        "allocation failure ran the task body");
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));

    prepared = r_runtime_task_start_prepare(payload_type, result_type, value_body);
    R_TASK_TEST_REQUIRE(prepared.status == R_RUNTIME_TASK_START_OK, "transaction prepare failed");
    initialized_prepared = r_runtime_task_start_prepare(payload_type, result_type, value_body);
    R_TASK_TEST_REQUIRE(initialized_prepared.status == R_RUNTIME_TASK_START_OK,
                        "in-place transaction prepare failed");
    R_TASK_TEST_REQUIRE(r_runtime_task_state(prepared.transaction) == R_RUNTIME_TASK_PREPARED,
                        "prepared transaction has the wrong state");
    R_TASK_TEST_REQUIRE(r_runtime_task_start_allocator(prepared.transaction) == allocator &&
                            r_runtime_task_start_allocator(initialized_prepared.transaction) ==
                                allocator,
                        "prepared transaction did not expose its generation allocator");
    initialize_context.tracker = &tracker;
    initialize_context.value = 73;
    atomic_init(&initialize_calls, 0U);
    initialize_context.calls = &initialize_calls;
    initialize_context.payload_alignment = _Alignof(RTaskTestPayload);
    atomic_init(&payload_aligned, 0);
    initialize_context.payload_aligned = &payload_aligned;
    atomic_init(&stop_context.returned, 0);
    stop_context.stopped = 0;
    R_TASK_TEST_REQUIRE(pthread_create(&stop_thread, NULL, stop_worker, &stop_context) == 0,
                        "could not start lifecycle-stop thread");
    for (;;) {
        RRuntimeTaskPrepareResult probe =
            r_runtime_task_start_prepare(payload_type, result_type, value_body);

        if (probe.status == R_RUNTIME_TASK_START_RUNTIME_STOPPING) {
            break;
        }
        R_TASK_TEST_REQUIRE(probe.status == R_RUNTIME_TASK_START_OK,
                            "stop-race probe had an unexpected status");
        r_runtime_task_start_abort(&probe.transaction);
        (void)sched_yield();
    }
    R_TASK_TEST_REQUIRE(!atomic_load_explicit(&stop_context.returned, memory_order_acquire),
                        "lifecycle stop did not retain the prepared transaction");
    initialized_started = r_runtime_task_start_commit_initialize(
        &initialized_prepared.transaction, value_payload_initialize, &initialize_context);
    R_TASK_TEST_REQUIRE(initialized_started.status == R_RUNTIME_TASK_START_RUNTIME_STOPPING,
                        "in-place commit during stop did not roll back");
    R_TASK_TEST_REQUIRE(initialized_started.task == NULL &&
                            initialized_prepared.transaction == NULL,
                        "failed in-place commit published a task");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&initialize_calls, memory_order_relaxed) == 0U,
                        "failed in-place commit invoked its initializer");
    started = r_runtime_task_start_commit(&prepared.transaction, &payload);
    R_TASK_TEST_REQUIRE(started.status == R_RUNTIME_TASK_START_RUNTIME_STOPPING,
                        "commit during stop did not roll back");
    R_TASK_TEST_REQUIRE(started.task == NULL && prepared.transaction == NULL,
                        "failed commit published a task");
    R_TASK_TEST_REQUIRE(memcmp(&payload, &original, sizeof(payload)) == 0,
                        "runtime_stopping commit changed the payload");
    R_TASK_TEST_REQUIRE(pthread_join(stop_thread, NULL) == 0,
                        "could not join lifecycle-stop thread");
    R_TASK_TEST_REQUIRE(stop_context.stopped, "lifecycle stop returned false");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&tracker.payload_moves, memory_order_relaxed) == 0U,
                        "failed transaction invoked payload move");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&tracker.payload_drops, memory_order_relaxed) == 0U,
                        "failed transaction dropped the caller payload");
    return 1;
}

static _Bool test_in_place_payload_initialize(RRuntimeAllocator *allocator) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RTaskTestPayload),
        _Alignof(RTaskTestPayload),
        value_payload_move,
        value_payload_drop,
    };
    const RRuntimeTypeInfo result_type = {
        sizeof(RTaskTestResult),
        _Alignof(RTaskTestResult),
        value_result_move,
        value_result_drop,
    };
    RTaskTestTracker tracker;
    RTaskTestInitializeContext context;
    _Atomic size_t initialize_calls;
    _Atomic _Bool payload_aligned;
    RTaskTestResult result = {NULL, 0};
    RRuntimeTaskPrepareResult prepared;
    RRuntimeTaskStartResult started;

    tracker_initialize(&tracker);
    context.tracker = &tracker;
    context.value = 41;
    atomic_init(&initialize_calls, 0U);
    context.calls = &initialize_calls;
    context.payload_alignment = _Alignof(RTaskTestPayload);
    atomic_init(&payload_aligned, 0);
    context.payload_aligned = &payload_aligned;
    prepared = r_runtime_task_start_prepare(payload_type, result_type, value_body);
    R_TASK_TEST_REQUIRE(prepared.status == R_RUNTIME_TASK_START_OK,
                        "in-place payload prepare failed");
    started = r_runtime_task_start_commit_initialize(
        &prepared.transaction, value_payload_initialize, &context);
    R_TASK_TEST_REQUIRE(started.status == R_RUNTIME_TASK_START_OK && started.task != NULL,
                        "in-place payload commit failed");
    R_TASK_TEST_REQUIRE(r_runtime_task_start_allocator(started.task) == NULL,
                        "committed task exposed a provisional allocator borrow");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&initialize_calls, memory_order_relaxed) == 1U,
                        "in-place payload initializer count is wrong");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&payload_aligned, memory_order_relaxed),
                        "in-place payload initializer received misaligned storage");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&tracker.payload_moves, memory_order_relaxed) == 0U,
                        "in-place payload commit used the ordinary move callback");
    R_TASK_TEST_REQUIRE(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
                        "in-place payload await failed");
    R_TASK_TEST_REQUIRE(result.tracker == &tracker && result.value == 42,
                        "in-place payload returned the wrong result");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&tracker.payload_drops, memory_order_relaxed) == 1U,
                        "in-place payload was not dropped exactly once");
    value_result_drop(&result);
    (void)allocator;
    return 1;
}

static _Bool test_await_and_result_transfer(RRuntimeAllocator *allocator) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RTaskTestPayload),
        _Alignof(RTaskTestPayload),
        value_payload_move,
        value_payload_drop,
    };
    const RRuntimeTypeInfo result_type = {
        sizeof(RTaskTestResult),
        _Alignof(RTaskTestResult),
        value_result_move,
        value_result_drop,
    };
    RTaskTestTracker tracker;
    RTaskTestPayload payload;
    RTaskTestResult result = {NULL, 0};
    RRuntimeTaskPrepareResult prepared;
    RRuntimeTaskStartResult started;

    tracker_initialize(&tracker);
    payload.tracker = &tracker;
    payload.value = 41;
    prepared = r_runtime_task_start_prepare(payload_type, result_type, value_body);
    R_TASK_TEST_REQUIRE(prepared.status == R_RUNTIME_TASK_START_OK, "value prepare failed");
    started = r_runtime_task_start_commit(&prepared.transaction, &payload);
    R_TASK_TEST_REQUIRE(started.status == R_RUNTIME_TASK_START_OK && started.task != NULL,
                        "value commit failed");
    R_TASK_TEST_REQUIRE(payload.tracker == NULL,
                        "successful commit did not consume the staged payload");
    R_TASK_TEST_REQUIRE(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
                        "await failed");
    R_TASK_TEST_REQUIRE(started.task == NULL, "await did not consume the observation");
    R_TASK_TEST_REQUIRE(result.tracker == &tracker && result.value == 42,
                        "await returned the wrong result");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&tracker.payload_moves, memory_order_relaxed) == 1U,
                        "payload move count is wrong");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&tracker.payload_drops, memory_order_relaxed) == 1U,
                        "payload drop count is wrong");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&tracker.result_moves, memory_order_relaxed) == 1U,
                        "result move count is wrong");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&tracker.result_drops, memory_order_relaxed) == 0U,
                        "observed result was dropped by the runtime");
    value_result_drop(&result);
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&tracker.result_drops, memory_order_relaxed) == 1U,
                        "result owner drop count is wrong");
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_stop(), "value executor did not stop");
    (void)allocator;
    return 1;
}

static void terminal_order_payload_move(void *destination, void *source) {
    RTaskTestTerminalOrderPayload *destination_value = destination;
    RTaskTestTerminalOrderPayload *source_value = source;

    *destination_value = *source_value;
    source_value->context = NULL;
}

static void terminal_order_payload_drop(void *value) {
    RTaskTestTerminalOrderPayload *payload = value;
    RTaskTestTerminalOrderContext *context = payload->context;

    atomic_store_explicit(&context->state_at_payload_drop,
                          (int)r_runtime_task_state(context->task),
                          memory_order_relaxed);
    (void)atomic_fetch_add_explicit(&context->payload_drops, 1U, memory_order_release);
    payload->context = NULL;
}

static void
terminal_order_body(RRuntimeTaskExecution *execution, void *payload_pointer, void *result_pointer) {
    RTaskTestTerminalOrderPayload *payload = payload_pointer;
    RTaskTestTerminalOrderContext *context = payload->context;

    (void)execution;
    (void)result_pointer;
    atomic_store_explicit(&context->body_entered, 1, memory_order_release);
    while (!atomic_load_explicit(&context->allow_return, memory_order_acquire)) {
        (void)sched_yield();
    }
}

static _Bool test_terminal_publication_after_payload_drop(RRuntimeAllocator *allocator) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RTaskTestTerminalOrderPayload),
        _Alignof(RTaskTestTerminalOrderPayload),
        terminal_order_payload_move,
        terminal_order_payload_drop,
    };
    RTaskTestTerminalOrderContext context;
    RTaskTestTerminalOrderPayload payload = {&context};
    RRuntimeTaskPrepareResult prepared;
    RRuntimeTaskStartResult started;

    atomic_init(&context.body_entered, 0);
    atomic_init(&context.allow_return, 0);
    atomic_init(&context.payload_drops, 0U);
    atomic_init(&context.state_at_payload_drop, (int)R_RUNTIME_TASK_PREPARED);
    context.task = NULL;
    prepared = r_runtime_task_start_prepare(payload_type, void_type(), terminal_order_body);
    R_TASK_TEST_REQUIRE(prepared.status == R_RUNTIME_TASK_START_OK,
                        "terminal-order prepare failed");
    started = r_runtime_task_start_commit(&prepared.transaction, &payload);
    R_TASK_TEST_REQUIRE(started.status == R_RUNTIME_TASK_START_OK && started.task != NULL,
                        "terminal-order commit failed");
    context.task = started.task;
    while (!atomic_load_explicit(&context.body_entered, memory_order_acquire)) {
        (void)sched_yield();
    }
    atomic_store_explicit(&context.allow_return, 1, memory_order_release);
    R_TASK_TEST_REQUIRE(r_runtime_task_await(&started.task, NULL) == R_RUNTIME_TASK_AWAIT_OK,
                        "terminal-order await failed");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.payload_drops, memory_order_acquire) == 1U,
                        "terminal publication became observable before payload/frame drop");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.state_at_payload_drop,
                                             memory_order_relaxed) == (int)R_RUNTIME_TASK_RUNNING,
                        "payload/frame drop did not precede terminal state selection");
    (void)allocator;
    return 1;
}

static void parallel_payload_move(void *destination, void *source) {
    RTaskTestParallelPayload *destination_value = destination;
    RTaskTestParallelPayload *source_value = source;

    *destination_value = *source_value;
    source_value->context = NULL;
    source_value->value = 0;
}

static void parallel_payload_drop(void *value) {
    RTaskTestParallelPayload *payload = value;

    (void)atomic_fetch_add_explicit(&payload->context->payload_drops, 1U, memory_order_relaxed);
    payload->context = NULL;
}

static void
parallel_body(RRuntimeTaskExecution *execution, void *payload_pointer, void *result_pointer) {
    RTaskTestParallelPayload *payload = payload_pointer;
    RTaskTestParallelContext *context = payload->context;
    int *result = result_pointer;

    (void)execution;
    *result = -1;
    if (pthread_mutex_lock(&context->mutex) != 0) {
        return;
    }
    context->entered += 1U;
    (void)pthread_cond_broadcast(&context->condition);
    while (!context->release) {
        if (pthread_cond_wait(&context->condition, &context->mutex) != 0) {
            (void)pthread_mutex_unlock(&context->mutex);
            return;
        }
    }
    (void)pthread_mutex_unlock(&context->mutex);
    *result = payload->value;
}

static _Bool test_eager_parallel_progress(RRuntimeAllocator *allocator) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RTaskTestParallelPayload),
        _Alignof(RTaskTestParallelPayload),
        parallel_payload_move,
        parallel_payload_drop,
    };
    const RRuntimeTypeInfo result_type = {sizeof(int), _Alignof(int), NULL, NULL};
    RTaskTestParallelContext context;
    RTaskTestParallelPayload payloads[2];
    RRuntimeTask *tasks[2] = {NULL, NULL};
    int results[2] = {0, 0};
    size_t index;

    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) ==
                            R_RUNTIME_EXECUTOR_START_OK,
                        "parallel executor failed to start");
    R_TASK_TEST_REQUIRE(pthread_mutex_init(&context.mutex, NULL) == 0,
                        "parallel mutex initialization failed");
    R_TASK_TEST_REQUIRE(pthread_cond_init(&context.condition, NULL) == 0,
                        "parallel condition initialization failed");
    context.entered = 0U;
    context.release = 0;
    atomic_init(&context.payload_drops, 0U);
    for (index = 0U; index < 2U; ++index) {
        RRuntimeTaskPrepareResult prepared;
        RRuntimeTaskStartResult started;

        payloads[index].context = &context;
        payloads[index].value = (int)(index + 10U);
        prepared = r_runtime_task_start_prepare(payload_type, result_type, parallel_body);
        R_TASK_TEST_REQUIRE(prepared.status == R_RUNTIME_TASK_START_OK, "parallel prepare failed");
        started = r_runtime_task_start_commit(&prepared.transaction, &payloads[index]);
        R_TASK_TEST_REQUIRE(started.status == R_RUNTIME_TASK_START_OK, "parallel commit failed");
        tasks[index] = started.task;
    }
    R_TASK_TEST_REQUIRE(pthread_mutex_lock(&context.mutex) == 0, "parallel mutex lock failed");
    while (context.entered != 2U) {
        R_TASK_TEST_REQUIRE(pthread_cond_wait(&context.condition, &context.mutex) == 0,
                            "parallel entry wait failed");
    }
    context.release = 1;
    (void)pthread_cond_broadcast(&context.condition);
    R_TASK_TEST_REQUIRE(pthread_mutex_unlock(&context.mutex) == 0, "parallel mutex unlock failed");
    for (index = 0U; index < 2U; ++index) {
        R_TASK_TEST_REQUIRE(r_runtime_task_await(&tasks[index], &results[index]) ==
                                R_RUNTIME_TASK_AWAIT_OK,
                            "parallel await failed");
        R_TASK_TEST_REQUIRE(results[index] == (int)(index + 10U), "parallel result is wrong");
    }
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.payload_drops, memory_order_relaxed) == 2U,
                        "parallel payloads were not dropped exactly once");
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_stop(), "parallel executor did not stop");
    R_TASK_TEST_REQUIRE(pthread_cond_destroy(&context.condition) == 0,
                        "parallel condition destruction failed");
    R_TASK_TEST_REQUIRE(pthread_mutex_destroy(&context.mutex) == 0,
                        "parallel mutex destruction failed");
    return 1;
}

static void cancel_payload_move(void *destination, void *source) {
    RTaskTestCancelPayload *destination_value = destination;
    RTaskTestCancelPayload *source_value = source;

    *destination_value = *source_value;
    source_value->context = NULL;
}

static void cancel_payload_drop(void *value) {
    RTaskTestCancelPayload *payload = value;

    (void)atomic_fetch_add_explicit(&payload->context->payload_drops, 1U, memory_order_relaxed);
    payload->context = NULL;
}

static void cancel_result_drop(void *value) {
    RTaskTestCancelResult *result = value;

    (void)atomic_fetch_add_explicit(&result->context->result_drops, 1U, memory_order_relaxed);
    result->context = NULL;
}

static void
cancel_body(RRuntimeTaskExecution *execution, void *payload_pointer, void *result_pointer) {
    RTaskTestCancelPayload *payload = payload_pointer;
    RTaskTestCancelResult *result = result_pointer;
    RTaskTestCancelContext *context = payload->context;

    atomic_store_explicit(&context->entered, 1, memory_order_release);
    while (!r_runtime_task_execution_cancel_requested(execution)) {
        (void)sched_yield();
    }
    atomic_store_explicit(&context->saw_cancel, 1, memory_order_release);
    while (!atomic_load_explicit(&context->allow_return, memory_order_acquire)) {
        (void)sched_yield();
    }
    result->context = context;
    atomic_store_explicit(&context->body_returned, 1, memory_order_release);
}

static void
detach_body(RRuntimeTaskExecution *execution, void *payload_pointer, void *result_pointer) {
    RTaskTestCancelPayload *payload = payload_pointer;
    RTaskTestCancelResult *result = result_pointer;
    RTaskTestCancelContext *context = payload->context;

    atomic_store_explicit(&context->entered, 1, memory_order_release);
    while (!atomic_load_explicit(&context->allow_return, memory_order_acquire)) {
        (void)sched_yield();
    }
    atomic_store_explicit(&context->saw_cancel,
                          r_runtime_task_execution_cancel_requested(execution),
                          memory_order_release);
    result->context = context;
    atomic_store_explicit(&context->body_returned, 1, memory_order_release);
}

static void cancel_context_initialize(RTaskTestCancelContext *context) {
    atomic_init(&context->entered, 0);
    atomic_init(&context->saw_cancel, 0);
    atomic_init(&context->allow_return, 0);
    atomic_init(&context->body_returned, 0);
    atomic_init(&context->payload_drops, 0U);
    atomic_init(&context->result_drops, 0U);
}

static RRuntimeTask *start_cancel_task(RTaskTestCancelContext *context, RRuntimeTaskBodyFn body) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RTaskTestCancelPayload),
        _Alignof(RTaskTestCancelPayload),
        cancel_payload_move,
        cancel_payload_drop,
    };
    const RRuntimeTypeInfo result_type = {
        sizeof(RTaskTestCancelResult),
        _Alignof(RTaskTestCancelResult),
        NULL,
        cancel_result_drop,
    };
    RTaskTestCancelPayload payload = {context};
    RRuntimeTaskPrepareResult prepared =
        r_runtime_task_start_prepare(payload_type, result_type, body);
    RRuntimeTaskStartResult started;

    if (prepared.status != R_RUNTIME_TASK_START_OK) {
        return NULL;
    }
    started = r_runtime_task_start_commit(&prepared.transaction, &payload);
    return started.status == R_RUNTIME_TASK_START_OK ? started.task : NULL;
}

static _Bool test_cancel_acknowledgement_and_detach(RRuntimeAllocator *allocator) {
    RTaskTestCancelContext cancel_context;
    RTaskTestCancelContext detach_context;
    RRuntimeTask *task;

    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) ==
                            R_RUNTIME_EXECUTOR_START_OK,
                        "cancel executor failed to start");
    cancel_context_initialize(&cancel_context);
    task = start_cancel_task(&cancel_context, cancel_body);
    R_TASK_TEST_REQUIRE(task != NULL, "cancel task failed to start");
    while (!atomic_load_explicit(&cancel_context.entered, memory_order_acquire)) {
        (void)sched_yield();
    }
    r_runtime_task_cancel(&task);
    R_TASK_TEST_REQUIRE(task == NULL, "cancel did not consume the observation");
    while (!atomic_load_explicit(&cancel_context.saw_cancel, memory_order_acquire)) {
        (void)sched_yield();
    }
    R_TASK_TEST_REQUIRE(!atomic_load_explicit(&cancel_context.body_returned, memory_order_acquire),
                        "cancel published completion before acknowledgement");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&cancel_context.payload_drops, memory_order_relaxed) ==
                            0U,
                        "cancel released payload before acknowledgement");
    atomic_store_explicit(&cancel_context.allow_return, 1, memory_order_release);
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_stop(), "cancel executor did not drain");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&cancel_context.body_returned, memory_order_acquire),
                        "cancelled body did not acknowledge");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&cancel_context.payload_drops, memory_order_relaxed) ==
                            1U,
                        "cancelled payload drop count is wrong");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&cancel_context.result_drops, memory_order_relaxed) ==
                            1U,
                        "cancelled result drop count is wrong");

    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) ==
                            R_RUNTIME_EXECUTOR_START_OK,
                        "detach executor failed to start");
    cancel_context_initialize(&detach_context);
    task = start_cancel_task(&detach_context, detach_body);
    R_TASK_TEST_REQUIRE(task != NULL, "detach task failed to start");
    while (!atomic_load_explicit(&detach_context.entered, memory_order_acquire)) {
        (void)sched_yield();
    }
    r_runtime_task_detach(&task);
    R_TASK_TEST_REQUIRE(task == NULL, "detach did not consume the observation");
    atomic_store_explicit(&detach_context.allow_return, 1, memory_order_release);
    while (atomic_load_explicit(&detach_context.result_drops, memory_order_acquire) == 0U) {
        (void)sched_yield();
    }
    R_TASK_TEST_REQUIRE(!atomic_load_explicit(&detach_context.saw_cancel, memory_order_acquire),
                        "detach requested cancellation");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&detach_context.payload_drops, memory_order_relaxed) ==
                            1U,
                        "detached payload drop count is wrong");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&detach_context.result_drops, memory_order_relaxed) ==
                            1U,
                        "detached result drop count is wrong");
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_stop(), "detach executor did not stop");
    return 1;
}

static void external_commit_payload_move(void *destination, void *source) {
    RTaskTestExternalCommitPayload *destination_value = destination;
    RTaskTestExternalCommitPayload *source_value = source;

    *destination_value = *source_value;
    destination_value->execution = NULL;
    destination_value->result = NULL;
    source_value->context = NULL;
}

static void external_commit_payload_drop(void *value) {
    RTaskTestExternalCommitPayload *payload = value;

    (void)atomic_fetch_add_explicit(&payload->context->payload_drops, 1U, memory_order_relaxed);
    payload->context = NULL;
}

static void external_commit_result_drop(void *value) {
    RTaskTestExternalCommitResult *result = value;

    (void)atomic_fetch_add_explicit(&result->context->result_drops, 1U, memory_order_relaxed);
    result->context = NULL;
}

static void external_commit_start(RRuntimeTaskExternalExecution *execution,
                                  void *payload_pointer,
                                  void *result_pointer) {
    RTaskTestExternalCommitPayload *payload = payload_pointer;

    payload->execution = execution;
    payload->result = result_pointer;
    r_runtime_task_external_start_ready(execution);
    atomic_store_explicit(&payload->context->started, 1, memory_order_release);
}

static void external_commit_cancel(RRuntimeTaskExternalExecution *execution,
                                   void *payload_pointer) {
    RTaskTestExternalCommitPayload *payload = payload_pointer;
    RTaskTestExternalCommitResult *result = payload->result;
    _Bool selected;

    if (payload->execution != execution || result == NULL) {
        return;
    }
    result->context = payload->context;
    selected = r_runtime_task_external_select_terminal_completion(execution);
    atomic_store_explicit(&payload->context->completion_selected, selected, memory_order_release);
    r_runtime_task_external_acknowledge(execution);
}

static _Bool test_external_committed_completion_overrides_cancel(RRuntimeAllocator *allocator) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RTaskTestExternalCommitPayload),
        _Alignof(RTaskTestExternalCommitPayload),
        external_commit_payload_move,
        external_commit_payload_drop,
    };
    const RRuntimeTypeInfo result_type = {
        sizeof(RTaskTestExternalCommitResult),
        _Alignof(RTaskTestExternalCommitResult),
        NULL,
        external_commit_result_drop,
    };
    RTaskTestExternalCommitContext context;
    RTaskTestExternalCommitPayload payload = {&context, NULL, NULL};
    RRuntimeTaskPrepareResult prepared;
    RRuntimeTaskStartResult started;

    atomic_init(&context.started, 0);
    atomic_init(&context.completion_selected, 0);
    atomic_init(&context.payload_drops, 0U);
    atomic_init(&context.result_drops, 0U);
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) ==
                            R_RUNTIME_EXECUTOR_START_OK,
                        "external commit executor failed to start");
    prepared = r_runtime_task_external_start_prepare(
        payload_type, result_type, external_commit_start, external_commit_cancel);
    R_TASK_TEST_REQUIRE(prepared.status == R_RUNTIME_TASK_START_OK,
                        "external commit prepare failed");
    started = r_runtime_task_start_commit(&prepared.transaction, &payload);
    R_TASK_TEST_REQUIRE(started.status == R_RUNTIME_TASK_START_OK && started.task != NULL,
                        "external commit start failed");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.started, memory_order_acquire),
                        "external producer was not started eagerly");
    r_runtime_task_cancel(&started.task);
    R_TASK_TEST_REQUIRE(started.task == NULL, "external cancel did not consume observation");
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_stop(),
                        "external committed completion did not acknowledge");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.completion_selected, memory_order_acquire),
                        "external committed completion did not replace cancellation");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.payload_drops, memory_order_relaxed) == 1U,
                        "external committed payload drop count is wrong");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.result_drops, memory_order_relaxed) == 1U,
                        "unobserved committed result was not dropped exactly once");
    return 1;
}

static void sequenced_external_payload_move(void *destination, void *source) {
    RTaskTestSequencedExternalPayload *destination_value = destination;
    RTaskTestSequencedExternalPayload *source_value = source;

    *destination_value = *source_value;
    destination_value->execution = NULL;
    destination_value->result = NULL;
    source_value->context = NULL;
}

static void sequenced_external_payload_drop(void *value) {
    RTaskTestSequencedExternalPayload *payload = value;

    (void)atomic_fetch_add_explicit(&payload->context->payload_drops, 1U, memory_order_relaxed);
    payload->context = NULL;
}

static void sequenced_external_result_drop(void *value) {
    RTaskTestSequencedExternalResult *result = value;

    (void)atomic_fetch_add_explicit(&result->context->result_drops, 1U, memory_order_relaxed);
    result->context = NULL;
}

static void sequenced_external_start(RRuntimeTaskExternalExecution *execution,
                                     void *payload_pointer,
                                     void *result_pointer) {
    RTaskTestSequencedExternalPayload *payload = payload_pointer;

    payload->execution = execution;
    payload->result = result_pointer;
    r_runtime_task_external_start_ready(execution);
    atomic_store_explicit(&payload->context->started, 1, memory_order_release);
}

static void sequenced_external_cancel(RRuntimeTaskExternalExecution *execution,
                                      void *payload_pointer) {
    RTaskTestSequencedExternalPayload *payload = payload_pointer;
    RTaskTestSequencedExternalResult *result = payload->result;
    RTaskTestSequencedExternalContext *context = payload->context;
    uint64_t candidate_sequence = context->candidate_sequence;
    const uint64_t cancellation_sequence = r_runtime_task_external_cancellation_sequence(execution);
    _Bool selected;

    if (payload->execution != execution || result == NULL || cancellation_sequence == UINT64_C(0)) {
        return;
    }
    if (candidate_sequence == UINT64_C(0)) {
        candidate_sequence = r_runtime_darwin_event_sequence_next();
    }
    selected = r_runtime_task_external_try_select_completion_at(execution, candidate_sequence);
    if (selected) {
        result->context = context;
    }
    atomic_store_explicit(
        &context->cancellation_sequence, cancellation_sequence, memory_order_release);
    atomic_store_explicit(&context->selected, selected, memory_order_release);
    r_runtime_task_external_acknowledge(execution);
}

static _Bool test_external_event_sequence_order(RRuntimeAllocator *allocator,
                                                _Bool completion_first) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RTaskTestSequencedExternalPayload),
        _Alignof(RTaskTestSequencedExternalPayload),
        sequenced_external_payload_move,
        sequenced_external_payload_drop,
    };
    const RRuntimeTypeInfo result_type = {
        sizeof(RTaskTestSequencedExternalResult),
        _Alignof(RTaskTestSequencedExternalResult),
        NULL,
        sequenced_external_result_drop,
    };
    RTaskTestSequencedExternalContext context;
    RTaskTestSequencedExternalPayload payload = {&context, NULL, NULL};
    RRuntimeTaskPrepareResult prepared;
    RRuntimeTaskStartResult started;
    uint64_t cancellation_sequence;

    context.candidate_sequence =
        completion_first ? r_runtime_darwin_event_sequence_next() : UINT64_C(0);
    atomic_init(&context.cancellation_sequence, UINT64_C(0));
    atomic_init(&context.started, 0);
    atomic_init(&context.selected, 0);
    atomic_init(&context.payload_drops, 0U);
    atomic_init(&context.result_drops, 0U);
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) ==
                            R_RUNTIME_EXECUTOR_START_OK,
                        "sequenced external executor failed to start");
    prepared = r_runtime_task_external_start_prepare(
        payload_type, result_type, sequenced_external_start, sequenced_external_cancel);
    R_TASK_TEST_REQUIRE(prepared.status == R_RUNTIME_TASK_START_OK,
                        "sequenced external prepare failed");
    started = r_runtime_task_start_commit(&prepared.transaction, &payload);
    R_TASK_TEST_REQUIRE(started.status == R_RUNTIME_TASK_START_OK && started.task != NULL,
                        "sequenced external start failed");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.started, memory_order_acquire),
                        "sequenced external producer was not started eagerly");
    r_runtime_task_cancel(&started.task);
    R_TASK_TEST_REQUIRE(started.task == NULL, "sequenced external cancel retained observer");
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_stop(),
                        "sequenced external producer did not acknowledge");
    cancellation_sequence =
        atomic_load_explicit(&context.cancellation_sequence, memory_order_acquire);
    R_TASK_TEST_REQUIRE(cancellation_sequence != UINT64_C(0),
                        "sequenced external cancellation has no sequence");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.selected, memory_order_acquire) ==
                            completion_first,
                        "sequenced external winner ignored event order");
    if (completion_first) {
        R_TASK_TEST_REQUIRE(context.candidate_sequence < cancellation_sequence,
                            "completion-first sequence order is invalid");
        R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.result_drops, memory_order_relaxed) == 1U,
                            "unobserved sequenced completion result was not dropped exactly once");
    } else {
        R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.result_drops, memory_order_relaxed) == 0U,
                            "cancelled sequenced result was treated as initialized");
    }
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.payload_drops, memory_order_relaxed) == 1U,
                        "sequenced external payload was not dropped exactly once");
    return 1;
}

static void start_ack_payload_move(void *destination, void *source) {
    RTaskTestStartAckPayload *destination_value = destination;
    RTaskTestStartAckPayload *source_value = source;

    *destination_value = *source_value;
    source_value->context = NULL;
}

static void start_ack_payload_drop(void *value) {
    RTaskTestStartAckPayload *payload = value;

    (void)atomic_fetch_add_explicit(&payload->context->payload_drops, 1U, memory_order_relaxed);
}

static void *start_ack_stop_worker(void *context_pointer) {
    RTaskTestStartAckContext *context = context_pointer;

    if (!r_runtime_executor_lifecycle_stop()) {
        return NULL;
    }
    atomic_store_explicit(&context->stop_returned, 1, memory_order_release);
    return NULL;
}

static void start_ack_external_start(RRuntimeTaskExternalExecution *execution,
                                     void *payload_pointer,
                                     void *result_pointer) {
    RTaskTestStartAckPayload *payload = payload_pointer;
    RTaskTestStartAckContext *context = payload->context;

    (void)result_pointer;
    r_runtime_task_external_start_ready(execution);
    if (pthread_create(&context->stop_thread, NULL, start_ack_stop_worker, context) != 0) {
        abort();
    }
    while (!r_runtime_task_external_cancel_requested(execution)) {
        (void)sched_yield();
    }
    if (r_runtime_task_external_cancellation_sequence(execution) == UINT64_C(0)) {
        abort();
    }
    r_runtime_task_external_acknowledge(execution);
    atomic_store_explicit(&context->acknowledged_in_start, 1, memory_order_release);
}

static void start_ack_external_cancel(RRuntimeTaskExternalExecution *execution,
                                      void *payload_pointer) {
    RTaskTestStartAckPayload *payload = payload_pointer;

    (void)execution;
    (void)atomic_fetch_add_explicit(&payload->context->cancel_callbacks, 1U, memory_order_relaxed);
}

static _Bool test_start_acknowledgement_suppresses_cancel_callback(RRuntimeAllocator *allocator) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RTaskTestStartAckPayload),
        _Alignof(RTaskTestStartAckPayload),
        start_ack_payload_move,
        start_ack_payload_drop,
    };
    RTaskTestStartAckContext context;
    RTaskTestStartAckPayload payload = {&context};
    RRuntimeTaskPrepareResult prepared;
    RRuntimeTaskStartResult started;

    atomic_init(&context.stop_returned, 0);
    atomic_init(&context.acknowledged_in_start, 0);
    atomic_init(&context.cancel_callbacks, 0U);
    atomic_init(&context.payload_drops, 0U);
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) ==
                            R_RUNTIME_EXECUTOR_START_OK,
                        "start-ack executor failed to start");
    prepared = r_runtime_task_external_start_prepare(
        payload_type, void_type(), start_ack_external_start, start_ack_external_cancel);
    R_TASK_TEST_REQUIRE(prepared.status == R_RUNTIME_TASK_START_OK, "start-ack prepare failed");
    started = r_runtime_task_start_commit(&prepared.transaction, &payload);
    R_TASK_TEST_REQUIRE(started.status == R_RUNTIME_TASK_START_OK && started.task != NULL,
                        "start-ack commit failed");
    R_TASK_TEST_REQUIRE(pthread_join(context.stop_thread, NULL) == 0,
                        "start-ack stop thread did not join");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.stop_returned, memory_order_acquire),
                        "start-ack lifecycle stop did not return");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.acknowledged_in_start, memory_order_acquire),
                        "start-ack producer did not acknowledge cancellation inside start");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.cancel_callbacks, memory_order_relaxed) == 0U,
                        "start-ack scheduled a post-ack cancel callback");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.payload_drops, memory_order_relaxed) == 1U,
                        "start-ack payload was not dropped exactly once");
    R_TASK_TEST_REQUIRE(r_runtime_task_await(&started.task, NULL) == R_RUNTIME_TASK_AWAIT_CANCELLED,
                        "start-ack task did not publish cancellation");
    return 1;
}

static void resumable_context_initialize(RTaskTestResumableContext *context,
                                         _Bool hold_outer_return,
                                         _Bool hold_child_cancellation,
                                         _Bool release_child) {
    atomic_init(&context->child_entered, 0);
    atomic_init(&context->child_cancel_seen, 0);
    atomic_init(&context->release_child, release_child);
    atomic_init(&context->child_returned, 0);
    atomic_init(&context->outer_registered, 0);
    atomic_init(&context->allow_outer_return, !hold_outer_return);
    atomic_init(&context->outer_step_calls, 0U);
    atomic_init(&context->active_steps, 0U);
    atomic_init(&context->maximum_active_steps, 0U);
    atomic_init(&context->child_payload_drops, 0U);
    atomic_init(&context->child_result_moves, 0U);
    atomic_init(&context->child_result_drops, 0U);
    atomic_init(&context->outer_payload_drops, 0U);
    atomic_init(&context->outer_result_initializations, 0U);
    atomic_init(&context->outer_result_moves, 0U);
    atomic_init(&context->outer_result_drops, 0U);
    atomic_init(&context->outer_cancel_finally_trace, UINT32_C(0));
    atomic_init(&context->outer_cancel_finally_entries, 0U);
    atomic_init(&context->outer_finally_saw_child_ack, 0);
    context->hold_outer_return = hold_outer_return;
    context->hold_child_cancellation = hold_child_cancellation;
}

static void resumable_child_payload_move(void *destination, void *source) {
    RTaskTestResumableChildPayload *destination_value = destination;
    RTaskTestResumableChildPayload *source_value = source;

    *destination_value = *source_value;
    source_value->context = NULL;
    source_value->value = 0;
}

static void resumable_child_payload_drop(void *value) {
    RTaskTestResumableChildPayload *payload = value;

    (void)atomic_fetch_add_explicit(
        &payload->context->child_payload_drops, 1U, memory_order_relaxed);
    payload->context = NULL;
}

static void resumable_child_result_move(void *destination, void *source) {
    RTaskTestResumableChildResult *destination_value = destination;
    RTaskTestResumableChildResult *source_value = source;

    *destination_value = *source_value;
    (void)atomic_fetch_add_explicit(
        &destination_value->context->child_result_moves, 1U, memory_order_relaxed);
    source_value->context = NULL;
    source_value->value = 0;
}

static void resumable_child_result_drop(void *value) {
    RTaskTestResumableChildResult *result = value;

    (void)atomic_fetch_add_explicit(&result->context->child_result_drops, 1U, memory_order_relaxed);
    result->context = NULL;
    result->value = 0;
}

static void resumable_outer_payload_move(void *destination, void *source) {
    RTaskTestResumableOuterPayload *destination_value = destination;
    RTaskTestResumableOuterPayload *source_value = source;

    *destination_value = *source_value;
    source_value->context = NULL;
    source_value->child = NULL;
    source_value->child_result.context = NULL;
    source_value->child_result.value = 0;
}

static void resumable_outer_payload_drop(void *value) {
    RTaskTestResumableOuterPayload *payload = value;
    RTaskTestResumableContext *context = payload->context;

    r_runtime_task_destroy(&payload->child);
    (void)atomic_fetch_add_explicit(&context->outer_payload_drops, 1U, memory_order_relaxed);
    payload->context = NULL;
}

static void resumable_outer_result_move(void *destination, void *source) {
    RTaskTestResumableOuterResult *destination_value = destination;
    RTaskTestResumableOuterResult *source_value = source;

    *destination_value = *source_value;
    (void)atomic_fetch_add_explicit(
        &destination_value->context->outer_result_moves, 1U, memory_order_relaxed);
    source_value->context = NULL;
    source_value->value = 0;
}

static void resumable_outer_result_drop(void *value) {
    RTaskTestResumableOuterResult *result = value;

    (void)atomic_fetch_add_explicit(&result->context->outer_result_drops, 1U, memory_order_relaxed);
    result->context = NULL;
    result->value = 0;
}

static void resumable_child_body(RRuntimeTaskExecution *execution,
                                 void *payload_pointer,
                                 void *result_pointer) {
    RTaskTestResumableChildPayload *payload = payload_pointer;
    RTaskTestResumableChildResult *result = result_pointer;
    RTaskTestResumableContext *context = payload->context;

    atomic_store_explicit(&context->child_entered, 1, memory_order_release);
    for (;;) {
        const _Bool cancel_requested = r_runtime_task_execution_cancel_requested(execution);

        if (cancel_requested) {
            atomic_store_explicit(&context->child_cancel_seen, 1, memory_order_release);
        }
        if (atomic_load_explicit(&context->release_child, memory_order_acquire) ||
            (cancel_requested && !context->hold_child_cancellation)) {
            break;
        }
        (void)sched_yield();
    }
    result->context = context;
    result->value = payload->value + 1;
    atomic_store_explicit(&context->child_returned, 1, memory_order_release);
}

static void resumable_record_step_enter(RTaskTestResumableContext *context) {
    const size_t active =
        atomic_fetch_add_explicit(&context->active_steps, 1U, memory_order_relaxed) + 1U;
    size_t maximum = atomic_load_explicit(&context->maximum_active_steps, memory_order_relaxed);

    while (maximum < active &&
           !atomic_compare_exchange_weak_explicit(&context->maximum_active_steps,
                                                  &maximum,
                                                  active,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
    }
}

static void resumable_record_step_leave(RTaskTestResumableContext *context) {
    if (atomic_fetch_sub_explicit(&context->active_steps, 1U, memory_order_relaxed) == 0U) {
        abort();
    }
}

static void resumable_initialize_outer_result(RTaskTestResumableContext *context,
                                              RTaskTestResumableOuterResult *result,
                                              int value) {
    result->context = context;
    result->value = value;
    (void)atomic_fetch_add_explicit(
        &context->outer_result_initializations, 1U, memory_order_relaxed);
}

static RRuntimeTaskStepStatus resumable_outer_step(RRuntimeTaskExecution *execution,
                                                   void *payload_pointer,
                                                   void *result_pointer) {
    RTaskTestResumableOuterPayload *payload = payload_pointer;
    RTaskTestResumableOuterResult *result = result_pointer;
    RTaskTestResumableContext *context = payload->context;
    RRuntimeTaskExecutionAwaitStatus status;

    resumable_record_step_enter(context);
    (void)atomic_fetch_add_explicit(&context->outer_step_calls, 1U, memory_order_relaxed);
    status = r_runtime_task_execution_await(execution, &payload->child, &payload->child_result);
    if (status == R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED) {
        atomic_store_explicit(&context->outer_registered, 1, memory_order_release);
        if (context->hold_outer_return) {
            while (!atomic_load_explicit(&context->allow_outer_return, memory_order_acquire)) {
                (void)sched_yield();
            }
        }
        resumable_record_step_leave(context);
        return R_RUNTIME_TASK_STEP_SUSPENDED;
    }
    if (r_runtime_task_execution_cancel_requested(execution)) {
        uint32_t trace;

        if (status == R_RUNTIME_TASK_EXECUTION_AWAIT_OK) {
            resumable_child_result_drop(&payload->child_result);
        } else if (status != R_RUNTIME_TASK_EXECUTION_AWAIT_CANCELLED) {
            abort();
        }
        atomic_store_explicit(&context->outer_finally_saw_child_ack,
                              atomic_load_explicit(&context->child_returned, memory_order_acquire),
                              memory_order_release);
        (void)atomic_fetch_add_explicit(
            &context->outer_cancel_finally_entries, 1U, memory_order_relaxed);
        trace = atomic_load_explicit(&context->outer_cancel_finally_trace, memory_order_relaxed);
        atomic_store_explicit(&context->outer_cancel_finally_trace,
                              trace * UINT32_C(10) + UINT32_C(1),
                              memory_order_relaxed);
        trace = atomic_load_explicit(&context->outer_cancel_finally_trace, memory_order_relaxed);
        atomic_store_explicit(&context->outer_cancel_finally_trace,
                              trace * UINT32_C(10) + UINT32_C(2),
                              memory_order_relaxed);
        resumable_record_step_leave(context);
        return R_RUNTIME_TASK_STEP_CANCELLED;
    }
    if (status == R_RUNTIME_TASK_EXECUTION_AWAIT_OK) {
        resumable_initialize_outer_result(context, result, payload->child_result.value);
        resumable_child_result_drop(&payload->child_result);
    } else if (status == R_RUNTIME_TASK_EXECUTION_AWAIT_CANCELLED) {
        resumable_initialize_outer_result(context, result, -2);
    } else {
        resumable_initialize_outer_result(context, result, -3);
    }
    resumable_record_step_leave(context);
    return R_RUNTIME_TASK_STEP_COMPLETED;
}

static RRuntimeTask *start_resumable_child(RTaskTestResumableContext *context) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RTaskTestResumableChildPayload),
        _Alignof(RTaskTestResumableChildPayload),
        resumable_child_payload_move,
        resumable_child_payload_drop,
    };
    const RRuntimeTypeInfo result_type = {
        sizeof(RTaskTestResumableChildResult),
        _Alignof(RTaskTestResumableChildResult),
        resumable_child_result_move,
        resumable_child_result_drop,
    };
    RTaskTestResumableChildPayload payload = {context, 41};
    RRuntimeTaskPrepareResult prepared =
        r_runtime_task_start_prepare(payload_type, result_type, resumable_child_body);
    RRuntimeTaskStartResult started;

    if (prepared.status != R_RUNTIME_TASK_START_OK) {
        return NULL;
    }
    started = r_runtime_task_start_commit(&prepared.transaction, &payload);
    return started.status == R_RUNTIME_TASK_START_OK ? started.task : NULL;
}

static RRuntimeTask *start_resumable_outer(RTaskTestResumableContext *context,
                                           RRuntimeTask **child) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RTaskTestResumableOuterPayload),
        _Alignof(RTaskTestResumableOuterPayload),
        resumable_outer_payload_move,
        resumable_outer_payload_drop,
    };
    const RRuntimeTypeInfo result_type = {
        sizeof(RTaskTestResumableOuterResult),
        _Alignof(RTaskTestResumableOuterResult),
        resumable_outer_result_move,
        resumable_outer_result_drop,
    };
    RTaskTestResumableOuterPayload payload = {context, *child, {NULL, 0}};
    RRuntimeTaskPrepareResult prepared =
        r_runtime_task_resumable_start_prepare(payload_type, result_type, resumable_outer_step);
    RRuntimeTaskStartResult started;

    if (prepared.status != R_RUNTIME_TASK_START_OK) {
        return NULL;
    }
    started = r_runtime_task_start_commit(&prepared.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        return NULL;
    }
    *child = NULL;
    return started.task;
}

static _Bool start_resumable_pair(RTaskTestResumableContext *context,
                                  RRuntimeTask **outer,
                                  RRuntimeTask **raw_child) {
    RRuntimeTask *child = start_resumable_child(context);

    if (child == NULL) {
        return 0;
    }
    *raw_child = child;
    *outer = start_resumable_outer(context, &child);
    if (*outer == NULL) {
        r_runtime_task_destroy(&child);
        return 0;
    }
    return 1;
}

static _Bool test_resumable_suspend_resume(RRuntimeAllocator *allocator,
                                           _Bool complete_before_step_return) {
    RTaskTestResumableContext context;
    RRuntimeTask *outer = NULL;
    RRuntimeTask *raw_child = NULL;
    RTaskTestResumableOuterResult result = {NULL, 0};

    resumable_context_initialize(&context, complete_before_step_return, 0, 0);
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) ==
                            R_RUNTIME_EXECUTOR_START_OK,
                        "resumable executor failed to start");
    R_TASK_TEST_REQUIRE(start_resumable_pair(&context, &outer, &raw_child),
                        "resumable task pair failed to start");
    while (!atomic_load_explicit(&context.child_entered, memory_order_acquire) ||
           !atomic_load_explicit(&context.outer_registered, memory_order_acquire)) {
        (void)sched_yield();
    }
    if (complete_before_step_return) {
        R_TASK_TEST_REQUIRE(r_runtime_task_state(outer) == R_RUNTIME_TASK_RUNNING,
                            "held resumable step was not running");
        atomic_store_explicit(&context.release_child, 1, memory_order_release);
        while (!atomic_load_explicit(&context.child_returned, memory_order_acquire) ||
               r_runtime_task_state(raw_child) != R_RUNTIME_TASK_COMPLETED) {
            (void)sched_yield();
        }
        R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.outer_step_calls, memory_order_relaxed) ==
                                1U,
                            "completion race ran a second step concurrently");
        atomic_store_explicit(&context.allow_outer_return, 1, memory_order_release);
    } else {
        while (r_runtime_task_state(outer) != R_RUNTIME_TASK_SUSPENDED) {
            (void)sched_yield();
        }
        atomic_store_explicit(&context.release_child, 1, memory_order_release);
    }
    R_TASK_TEST_REQUIRE(r_runtime_task_await(&outer, &result) == R_RUNTIME_TASK_AWAIT_OK,
                        "resumable await failed");
    R_TASK_TEST_REQUIRE(result.context == &context && result.value == 42,
                        "resumable result is wrong");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.outer_step_calls, memory_order_relaxed) == 2U,
                        "resumable task did not execute exactly two steps");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.maximum_active_steps, memory_order_relaxed) ==
                            1U,
                        "resumable task executed concurrent steps");
    R_TASK_TEST_REQUIRE(
        atomic_load_explicit(&context.child_payload_drops, memory_order_relaxed) == 1U &&
            atomic_load_explicit(&context.outer_payload_drops, memory_order_relaxed) == 1U,
        "resumable payload was not dropped exactly once");
    R_TASK_TEST_REQUIRE(
        atomic_load_explicit(&context.child_result_moves, memory_order_relaxed) == 1U &&
            atomic_load_explicit(&context.child_result_drops, memory_order_relaxed) == 1U,
        "resumable child result ownership is wrong");
    R_TASK_TEST_REQUIRE(
        atomic_load_explicit(&context.outer_result_moves, memory_order_relaxed) == 1U &&
            atomic_load_explicit(&context.outer_result_drops, memory_order_relaxed) == 0U,
        "resumable outer result ownership is wrong");
    resumable_outer_result_drop(&result);
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.outer_result_drops, memory_order_relaxed) ==
                            1U,
                        "resumable observed result was not dropped exactly once");
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_stop(), "resumable executor did not stop");
    return 1;
}

static _Bool test_resumable_inline_ready(RRuntimeAllocator *allocator) {
    RTaskTestResumableContext context;
    RRuntimeTask *child;
    RRuntimeTask *outer;
    RTaskTestResumableOuterResult result = {NULL, 0};

    resumable_context_initialize(&context, 0, 0, 1);
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) ==
                            R_RUNTIME_EXECUTOR_START_OK,
                        "inline-ready executor failed to start");
    child = start_resumable_child(&context);
    R_TASK_TEST_REQUIRE(child != NULL, "inline-ready child failed to start");
    while (r_runtime_task_state(child) != R_RUNTIME_TASK_COMPLETED) {
        (void)sched_yield();
    }
    outer = start_resumable_outer(&context, &child);
    R_TASK_TEST_REQUIRE(outer != NULL, "inline-ready outer failed to start");
    R_TASK_TEST_REQUIRE(r_runtime_task_await(&outer, &result) == R_RUNTIME_TASK_AWAIT_OK,
                        "inline-ready outer await failed");
    R_TASK_TEST_REQUIRE(result.value == 42, "inline-ready result is wrong");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.outer_step_calls, memory_order_relaxed) == 1U,
                        "inline-ready child suspended the outer task");
    R_TASK_TEST_REQUIRE(!atomic_load_explicit(&context.outer_registered, memory_order_relaxed),
                        "inline-ready child registered a waiter");
    resumable_outer_result_drop(&result);
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_stop(), "inline-ready executor did not stop");
    return 1;
}

static void *resumable_race_cancel_worker(void *context_pointer) {
    RTaskTestRaceControl *control = context_pointer;

    (void)atomic_fetch_add_explicit(&control->ready, 1U, memory_order_release);
    while (!atomic_load_explicit(&control->go, memory_order_acquire)) {
        (void)sched_yield();
    }
    r_runtime_task_cancel(control->outer);
    return NULL;
}

static void *resumable_race_complete_worker(void *context_pointer) {
    RTaskTestRaceControl *control = context_pointer;

    (void)atomic_fetch_add_explicit(&control->ready, 1U, memory_order_release);
    while (!atomic_load_explicit(&control->go, memory_order_acquire)) {
        (void)sched_yield();
    }
    atomic_store_explicit(&control->context->release_child, 1, memory_order_release);
    return NULL;
}

static _Bool test_resumable_cancel_completion_race(RRuntimeAllocator *allocator) {
    enum {
        R_TASK_TEST_RESUMABLE_RACE_COUNT = 16
    };
    size_t index;

    for (index = 0U; index < R_TASK_TEST_RESUMABLE_RACE_COUNT; ++index) {
        RTaskTestResumableContext context;
        RTaskTestRaceControl control;
        RRuntimeTask *outer = NULL;
        RRuntimeTask *raw_child = NULL;
        pthread_t cancel_thread;
        pthread_t complete_thread;

        resumable_context_initialize(&context, 0, 0, 0);
        R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) ==
                                R_RUNTIME_EXECUTOR_START_OK,
                            "resumable race executor failed to start");
        R_TASK_TEST_REQUIRE(start_resumable_pair(&context, &outer, &raw_child),
                            "resumable race pair failed to start");
        while (!atomic_load_explicit(&context.child_entered, memory_order_acquire) ||
               r_runtime_task_state(outer) != R_RUNTIME_TASK_SUSPENDED) {
            (void)sched_yield();
        }
        control.context = &context;
        control.outer = &outer;
        atomic_init(&control.ready, 0U);
        atomic_init(&control.go, 0);
        R_TASK_TEST_REQUIRE(
            pthread_create(&cancel_thread, NULL, resumable_race_cancel_worker, &control) == 0,
            "resumable race cancel thread failed to start");
        R_TASK_TEST_REQUIRE(
            pthread_create(&complete_thread, NULL, resumable_race_complete_worker, &control) == 0,
            "resumable race completion thread failed to start");
        while (atomic_load_explicit(&control.ready, memory_order_acquire) != 2U) {
            (void)sched_yield();
        }
        atomic_store_explicit(&control.go, 1, memory_order_release);
        R_TASK_TEST_REQUIRE(pthread_join(cancel_thread, NULL) == 0,
                            "resumable race cancel thread did not join");
        R_TASK_TEST_REQUIRE(pthread_join(complete_thread, NULL) == 0,
                            "resumable race completion thread did not join");
        R_TASK_TEST_REQUIRE(outer == NULL, "resumable race cancel retained observer");
        R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_stop(),
                            "resumable race executor did not drain");
        R_TASK_TEST_REQUIRE(
            atomic_load_explicit(&context.maximum_active_steps, memory_order_relaxed) == 1U,
            "resumable race executed concurrent parent steps");
        R_TASK_TEST_REQUIRE(
            atomic_load_explicit(&context.child_payload_drops, memory_order_relaxed) == 1U &&
                atomic_load_explicit(&context.outer_payload_drops, memory_order_relaxed) == 1U,
            "resumable race payload drop count is wrong");
        R_TASK_TEST_REQUIRE(
            atomic_load_explicit(&context.child_result_drops, memory_order_relaxed) == 1U &&
                atomic_load_explicit(&context.child_result_moves, memory_order_relaxed) <= 1U,
            "resumable race child result ownership is wrong");
        R_TASK_TEST_REQUIRE(
            atomic_load_explicit(&context.outer_result_moves, memory_order_relaxed) == 0U &&
                atomic_load_explicit(&context.outer_result_drops, memory_order_relaxed) ==
                    atomic_load_explicit(&context.outer_result_initializations,
                                         memory_order_relaxed),
            "resumable race outer result ownership is wrong");
        (void)raw_child;
    }
    return 1;
}

static _Bool test_resumable_cancel_retains_waiter_and_drains(RRuntimeAllocator *allocator) {
    RTaskTestResumableContext context;
    RTaskTestStopContext stop_context;
    RRuntimeTask *outer = NULL;
    RRuntimeTask *raw_child = NULL;
    pthread_t stop_thread;

    resumable_context_initialize(&context, 1, 1, 0);
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) ==
                            R_RUNTIME_EXECUTOR_START_OK,
                        "resumable drain executor failed to start");
    R_TASK_TEST_REQUIRE(start_resumable_pair(&context, &outer, &raw_child),
                        "resumable drain pair failed to start");
    while (!atomic_load_explicit(&context.child_entered, memory_order_acquire) ||
           !atomic_load_explicit(&context.outer_registered, memory_order_acquire)) {
        (void)sched_yield();
    }
    R_TASK_TEST_REQUIRE(r_runtime_task_state(outer) == R_RUNTIME_TASK_RUNNING,
                        "resumable drain did not hold the first suspension return");
    r_runtime_task_cancel(&outer);
    R_TASK_TEST_REQUIRE(outer == NULL, "suspended cancel retained parent observer");
    atomic_store_explicit(&context.allow_outer_return, 1, memory_order_release);
    while (!atomic_load_explicit(&context.child_cancel_seen, memory_order_acquire)) {
        (void)sched_yield();
    }
    R_TASK_TEST_REQUIRE(
        atomic_load_explicit(&context.outer_cancel_finally_entries, memory_order_relaxed) == 0U,
        "resumable cancellation entered finally before child acknowledgement");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.outer_payload_drops, memory_order_relaxed) ==
                            0U,
                        "resumable cancellation dropped its frame before child acknowledgement");
    R_TASK_TEST_REQUIRE(
        atomic_load_explicit(&context.outer_step_calls, memory_order_relaxed) == 2U,
        "cancel-during-suspend-return did not perform exactly one acknowledgement retry");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.child_payload_drops, memory_order_relaxed) ==
                            0U,
                        "suspended cancel released child before native-style acknowledgement");
    atomic_init(&stop_context.returned, 0);
    stop_context.stopped = 0;
    R_TASK_TEST_REQUIRE(pthread_create(&stop_thread, NULL, stop_worker, &stop_context) == 0,
                        "resumable drain stop thread failed to start");
    for (;;) {
        RRuntimeTaskPrepareResult probe =
            r_runtime_task_resumable_start_prepare(void_type(), void_type(), resumable_outer_step);

        if (probe.status == R_RUNTIME_TASK_START_RUNTIME_STOPPING) {
            break;
        }
        R_TASK_TEST_REQUIRE(probe.status == R_RUNTIME_TASK_START_OK,
                            "resumable drain probe had an unexpected status");
        r_runtime_task_start_abort(&probe.transaction);
        (void)sched_yield();
    }
    R_TASK_TEST_REQUIRE(!atomic_load_explicit(&stop_context.returned, memory_order_acquire),
                        "root drain returned while child retained the parent waiter");
    atomic_store_explicit(&context.release_child, 1, memory_order_release);
    R_TASK_TEST_REQUIRE(pthread_join(stop_thread, NULL) == 0,
                        "resumable drain stop thread did not join");
    R_TASK_TEST_REQUIRE(stop_context.stopped, "resumable drain lifecycle stop failed");
    R_TASK_TEST_REQUIRE(
        atomic_load_explicit(&context.child_payload_drops, memory_order_relaxed) == 1U &&
            atomic_load_explicit(&context.outer_payload_drops, memory_order_relaxed) == 1U,
        "resumable drain payloads were not dropped exactly once");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.child_result_drops, memory_order_relaxed) ==
                            1U,
                        "resumable drain child result was not dropped exactly once");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.maximum_active_steps, memory_order_relaxed) ==
                            1U,
                        "resumable drain dispatched concurrent parent steps");
    R_TASK_TEST_REQUIRE(
        atomic_load_explicit(&context.outer_cancel_finally_trace, memory_order_relaxed) ==
            UINT32_C(12),
        "resumable cancellation did not run nested finalies exactly once in LIFO order");
    R_TASK_TEST_REQUIRE(
        atomic_load_explicit(&context.outer_cancel_finally_entries, memory_order_relaxed) == 1U,
        "resumable cancellation entered its finalies more than once");
    R_TASK_TEST_REQUIRE(
        atomic_load_explicit(&context.outer_finally_saw_child_ack, memory_order_acquire),
        "resumable cancellation ran finally before child acknowledgement");
    (void)raw_child;
    return 1;
}

static void await_misuse_child_payload_move(void *destination, void *source) {
    RTaskTestAwaitMisuseChildPayload *destination_value = destination;
    RTaskTestAwaitMisuseChildPayload *source_value = source;

    *destination_value = *source_value;
    source_value->context = NULL;
    source_value->release = NULL;
}

static void await_misuse_child_payload_drop(void *value) {
    RTaskTestAwaitMisuseChildPayload *payload = value;

    (void)atomic_fetch_add_explicit(&payload->context->payload_drops, 1U, memory_order_relaxed);
    payload->context = NULL;
    payload->release = NULL;
}

static void await_misuse_child_body(RRuntimeTaskExecution *execution,
                                    void *payload_pointer,
                                    void *result_pointer) {
    RTaskTestAwaitMisuseChildPayload *payload = payload_pointer;

    (void)result_pointer;
    if (payload->release == &payload->context->release_first) {
        atomic_store_explicit(&payload->context->first_entered, 1, memory_order_release);
    }
    while (!atomic_load_explicit(payload->release, memory_order_acquire) &&
           !r_runtime_task_execution_cancel_requested(execution)) {
        (void)sched_yield();
    }
}

static void await_misuse_outer_payload_move(void *destination, void *source) {
    RTaskTestAwaitMisuseOuterPayload *destination_value = destination;
    RTaskTestAwaitMisuseOuterPayload *source_value = source;

    *destination_value = *source_value;
    source_value->context = NULL;
    source_value->self = NULL;
    source_value->first = NULL;
    source_value->second = NULL;
}

static void await_misuse_outer_payload_drop(void *value) {
    RTaskTestAwaitMisuseOuterPayload *payload = value;

    payload->self = NULL;
    r_runtime_task_destroy(&payload->first);
    r_runtime_task_destroy(&payload->second);
    (void)atomic_fetch_add_explicit(&payload->context->payload_drops, 1U, memory_order_relaxed);
    payload->context = NULL;
}

static RRuntimeTaskStepStatus await_misuse_outer_step(RRuntimeTaskExecution *execution,
                                                      void *payload_pointer,
                                                      void *result_pointer) {
    RTaskTestAwaitMisuseOuterPayload *payload = payload_pointer;
    RTaskTestAwaitMisuseContext *context = payload->context;
    RRuntimeTaskExecutionAwaitStatus status;

    if (atomic_load_explicit(&context->self_status, memory_order_relaxed) < 0) {
        status = r_runtime_task_execution_await(execution, &payload->self, NULL);
        atomic_store_explicit(&context->self_status, (int)status, memory_order_relaxed);
    }
    status = r_runtime_task_execution_await(execution, &payload->first, NULL);
    if (status == R_RUNTIME_TASK_EXECUTION_AWAIT_SUSPENDED) {
        const RRuntimeTaskExecutionAwaitStatus second_status =
            r_runtime_task_execution_await(execution, &payload->second, NULL);

        atomic_store_explicit(&context->second_status, (int)second_status, memory_order_release);
        return R_RUNTIME_TASK_STEP_SUSPENDED;
    }
    *(int *)result_pointer = (int)status;
    return R_RUNTIME_TASK_STEP_COMPLETED;
}

static RRuntimeTask *start_await_misuse_child(RTaskTestAwaitMisuseContext *context,
                                              _Atomic _Bool *release) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RTaskTestAwaitMisuseChildPayload),
        _Alignof(RTaskTestAwaitMisuseChildPayload),
        await_misuse_child_payload_move,
        await_misuse_child_payload_drop,
    };
    RTaskTestAwaitMisuseChildPayload payload = {context, release};
    RRuntimeTaskPrepareResult prepared =
        r_runtime_task_start_prepare(payload_type, void_type(), await_misuse_child_body);
    RRuntimeTaskStartResult started;

    if (prepared.status != R_RUNTIME_TASK_START_OK) {
        return NULL;
    }
    started = r_runtime_task_start_commit(&prepared.transaction, &payload);
    return started.status == R_RUNTIME_TASK_START_OK ? started.task : NULL;
}

static _Bool test_resumable_await_misuse_is_invalid(RRuntimeAllocator *allocator) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RTaskTestAwaitMisuseOuterPayload),
        _Alignof(RTaskTestAwaitMisuseOuterPayload),
        await_misuse_outer_payload_move,
        await_misuse_outer_payload_drop,
    };
    const RRuntimeTypeInfo result_type = {sizeof(int), _Alignof(int), NULL, NULL};
    RTaskTestAwaitMisuseContext context;
    RTaskTestAwaitMisuseOuterPayload payload;
    RRuntimeTaskPrepareResult prepared;
    RRuntimeTaskStartResult started;
    int result = -1;

    atomic_init(&context.first_entered, 0);
    atomic_init(&context.release_first, 0);
    atomic_init(&context.release_second, 0);
    atomic_init(&context.self_status, -1);
    atomic_init(&context.second_status, -1);
    atomic_init(&context.payload_drops, 0U);
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) ==
                            R_RUNTIME_EXECUTOR_START_OK,
                        "await misuse executor failed to start");
    payload.context = &context;
    payload.self = NULL;
    payload.first = start_await_misuse_child(&context, &context.release_first);
    payload.second = start_await_misuse_child(&context, &context.release_second);
    R_TASK_TEST_REQUIRE(payload.first != NULL && payload.second != NULL,
                        "await misuse children failed to start");
    prepared =
        r_runtime_task_resumable_start_prepare(payload_type, result_type, await_misuse_outer_step);
    R_TASK_TEST_REQUIRE(prepared.status == R_RUNTIME_TASK_START_OK,
                        "await misuse outer prepare failed");
    payload.self = prepared.transaction;
    started = r_runtime_task_start_commit(&prepared.transaction, &payload);
    R_TASK_TEST_REQUIRE(started.status == R_RUNTIME_TASK_START_OK && started.task != NULL,
                        "await misuse outer commit failed");
    while (!atomic_load_explicit(&context.first_entered, memory_order_acquire) ||
           atomic_load_explicit(&context.second_status, memory_order_acquire) < 0) {
        (void)sched_yield();
    }
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.self_status, memory_order_relaxed) ==
                            (int)R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID,
                        "self await was not rejected");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.second_status, memory_order_relaxed) ==
                            (int)R_RUNTIME_TASK_EXECUTION_AWAIT_INVALID,
                        "second different awaited task was not rejected");
    atomic_store_explicit(&context.release_first, 1, memory_order_release);
    R_TASK_TEST_REQUIRE(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
                        "await misuse parent did not resume");
    R_TASK_TEST_REQUIRE(result == (int)R_RUNTIME_TASK_EXECUTION_AWAIT_OK,
                        "await misuse first task was not consumed after resume");
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_stop(), "await misuse executor did not drain");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.payload_drops, memory_order_relaxed) == 3U,
                        "await misuse payload drop count is wrong");
    return 1;
}

static void drain_payload_move(void *destination, void *source) {
    RTaskTestDrainPayload *destination_value = destination;
    RTaskTestDrainPayload *source_value = source;

    *destination_value = *source_value;
    source_value->context = NULL;
}

static void drain_payload_drop(void *value) {
    RTaskTestDrainPayload *payload = value;

    (void)atomic_fetch_add_explicit(&payload->context->payload_drops, 1U, memory_order_relaxed);
    payload->context = NULL;
}

static void drain_body(RRuntimeTaskExecution *execution, void *payload_pointer, void *result) {
    RTaskTestDrainPayload *payload = payload_pointer;

    (void)result;
    (void)atomic_fetch_add_explicit(&payload->context->body_entries, 1U, memory_order_release);
    while (!r_runtime_task_execution_cancel_requested(execution)) {
        (void)sched_yield();
    }
    (void)atomic_fetch_add_explicit(
        &payload->context->cancellation_acknowledgements, 1U, memory_order_relaxed);
}

static void nested_payload_move(void *destination, void *source) {
    RTaskTestNestedPayload *destination_value = destination;
    RTaskTestNestedPayload *source_value = source;

    *destination_value = *source_value;
    source_value->inner = NULL;
}

static void nested_payload_drop(void *value) {
    RTaskTestNestedPayload *payload = value;

    r_runtime_task_destroy(&payload->inner);
}

static void
nested_body(RRuntimeTaskExecution *execution, void *payload_pointer, void *result_pointer) {
    RTaskTestNestedPayload *payload = payload_pointer;
    int *result = result_pointer;

    (void)execution;
    *result = (int)r_runtime_task_await(&payload->inner, NULL);
    if (r_runtime_executor_lifecycle_stop()) {
        *result = -1;
    }
    r_runtime_task_destroy(&payload->inner);
}

static _Bool test_nonblocking_worker_await_and_root_drain(RRuntimeAllocator *allocator) {
    enum {
        R_TASK_TEST_DRAIN_COUNT = 8
    };
    const RRuntimeTypeInfo drain_payload_type = {
        sizeof(RTaskTestDrainPayload),
        _Alignof(RTaskTestDrainPayload),
        drain_payload_move,
        drain_payload_drop,
    };
    const RRuntimeTypeInfo nested_payload_type = {
        sizeof(RTaskTestNestedPayload),
        _Alignof(RTaskTestNestedPayload),
        nested_payload_move,
        nested_payload_drop,
    };
    const RRuntimeTypeInfo int_type = {sizeof(int), _Alignof(int), NULL, NULL};
    RTaskTestDrainContext context;
    RRuntimeTask *inner;
    RRuntimeTask *outer;
    RTaskTestDrainPayload inner_payload;
    RTaskTestNestedPayload outer_payload;
    RRuntimeTask *drain_tasks[R_TASK_TEST_DRAIN_COUNT];
    int await_status = -1;
    size_t index;

    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_start(allocator) ==
                            R_RUNTIME_EXECUTOR_START_OK,
                        "drain executor failed to start");
    atomic_init(&context.payload_drops, 0U);
    atomic_init(&context.body_entries, 0U);
    atomic_init(&context.cancellation_acknowledgements, 0U);

    inner_payload.context = &context;
    {
        RRuntimeTaskPrepareResult prepared =
            r_runtime_task_start_prepare(drain_payload_type, void_type(), drain_body);
        RRuntimeTaskStartResult started;

        R_TASK_TEST_REQUIRE(prepared.status == R_RUNTIME_TASK_START_OK, "inner prepare failed");
        started = r_runtime_task_start_commit(&prepared.transaction, &inner_payload);
        R_TASK_TEST_REQUIRE(started.status == R_RUNTIME_TASK_START_OK, "inner commit failed");
        inner = started.task;
    }
    outer_payload.inner = inner;
    {
        RRuntimeTaskPrepareResult prepared =
            r_runtime_task_start_prepare(nested_payload_type, int_type, nested_body);
        RRuntimeTaskStartResult started;

        R_TASK_TEST_REQUIRE(prepared.status == R_RUNTIME_TASK_START_OK, "outer prepare failed");
        started = r_runtime_task_start_commit(&prepared.transaction, &outer_payload);
        R_TASK_TEST_REQUIRE(started.status == R_RUNTIME_TASK_START_OK, "outer commit failed");
        outer = started.task;
    }
    R_TASK_TEST_REQUIRE(r_runtime_task_await(&outer, &await_status) == R_RUNTIME_TASK_AWAIT_OK,
                        "outer await failed");
    R_TASK_TEST_REQUIRE(await_status == (int)R_RUNTIME_TASK_AWAIT_WOULD_BLOCK,
                        "executor worker performed a blocking await");

    for (index = 0U; index < R_TASK_TEST_DRAIN_COUNT; ++index) {
        RTaskTestDrainPayload payload = {&context};
        RRuntimeTaskPrepareResult prepared =
            r_runtime_task_start_prepare(drain_payload_type, void_type(), drain_body);
        RRuntimeTaskStartResult started;

        R_TASK_TEST_REQUIRE(prepared.status == R_RUNTIME_TASK_START_OK, "drain prepare failed");
        started = r_runtime_task_start_commit(&prepared.transaction, &payload);
        R_TASK_TEST_REQUIRE(started.status == R_RUNTIME_TASK_START_OK, "drain commit failed");
        drain_tasks[index] = started.task;
        r_runtime_task_detach(&drain_tasks[index]);
    }
    while (atomic_load_explicit(&context.body_entries, memory_order_acquire) == 0U) {
        (void)sched_yield();
    }
    R_TASK_TEST_REQUIRE(r_runtime_executor_lifecycle_stop(), "root drain did not stop");
    R_TASK_TEST_REQUIRE(atomic_load_explicit(&context.payload_drops, memory_order_relaxed) ==
                            R_TASK_TEST_DRAIN_COUNT + 1U,
                        "root drain did not release every task payload");
    R_TASK_TEST_REQUIRE(
        atomic_load_explicit(&context.cancellation_acknowledgements, memory_order_relaxed) >= 1U,
        "root drain did not receive a cancellation acknowledgement");
    R_TASK_TEST_REQUIRE(!r_runtime_executor_lifecycle_stop(),
                        "stopped executor reported a second lifecycle");
    return 1;
}

#include "task_budget_tests.inc"
#include "task_deadline_tests.inc"
#include "task_scope_tests.inc"

int main(void) {
    RRuntimeAllocator allocator;

    atomic_init(&r_task_test_thread_local_cleanup_calls, 0U);
    r_runtime_thread_local_cleanup_install(test_thread_local_cleanup);
    r_runtime_allocator_initialize(&allocator);
    if (!test_start_failures_and_two_phase_commit(&allocator)) {
        return 1;
    }
    if (r_runtime_executor_lifecycle_start(&allocator) != R_RUNTIME_EXECUTOR_START_OK) {
        (void)fputs("task test failure: value executor restart failed\n", stderr);
        return 1;
    }
    if (!test_in_place_payload_initialize(&allocator) ||
        !test_terminal_publication_after_payload_drop(&allocator) ||
        !test_await_and_result_transfer(&allocator) || !test_eager_parallel_progress(&allocator) ||
        !test_cancel_acknowledgement_and_detach(&allocator) ||
        !test_external_committed_completion_overrides_cancel(&allocator) ||
        !test_external_event_sequence_order(&allocator, 1) ||
        !test_external_event_sequence_order(&allocator, 0) ||
        !test_start_acknowledgement_suppresses_cancel_callback(&allocator) ||
        !test_resumable_inline_ready(&allocator) || !test_resumable_suspend_resume(&allocator, 0) ||
        !test_resumable_suspend_resume(&allocator, 1) ||
        !test_resumable_cancel_completion_race(&allocator) ||
        !test_resumable_cancel_retains_waiter_and_drains(&allocator) ||
        !test_resumable_await_misuse_is_invalid(&allocator) ||
        !test_nonblocking_worker_await_and_root_drain(&allocator) ||
        !test_task_scope(&allocator, 1, 0) || !test_task_scope(&allocator, 0, 0) ||
        !test_task_scope(&allocator, 0, 1) || !test_task_deadline(&allocator) ||
        !test_task_budget(&allocator)) {
        return 1;
    }
    if (atomic_load_explicit(&r_task_test_thread_local_cleanup_calls, memory_order_relaxed) == 0U) {
        (void)fputs("task test failure: worker cleanup callback was not invoked\n", stderr);
        return 1;
    }
    (void)puts("runtime_darwin_task_ok");
    return 0;
}
