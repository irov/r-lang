#include "r_std_async.h"

#include "r_runtime_0_1.h"
#include "r_runtime_core.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * One blocking call: the job that the pool links, the task it completes and, after this header,
 * the entry's argument payload. The header stays readable until the task's frame is released, so
 * a cancellation that arrives after a pool thread took the job finds it taken.
 */
typedef struct RLibraryBlockingCall {
    RRuntimeBlockingJob job;
    RRuntimeTaskExternalExecution *execution;
    void *result;
    RRuntimeTypeInfo arguments_type;
    RRuntimeTypeInfo result_type;
    RStdAsyncBlockingEntryFn entry;
    size_t arguments_offset;
    _Bool arguments_initialized;
} RLibraryBlockingCall;

typedef struct RLibraryBlockingStage {
    RRuntimeTypeInfo arguments_type;
    RRuntimeTypeInfo result_type;
    RStdAsyncBlockingEntryFn entry;
    size_t arguments_offset;
    void *staged_arguments;
} RLibraryBlockingStage;

static _Noreturn void blocking_contract_violation(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                    (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
}

static void *call_arguments(RLibraryBlockingCall *call) {
    return (unsigned char *)call + call->arguments_offset;
}

/* The commit of the task moves the staged arguments into the frame; before it the caller keeps
   them. */
static void call_move_initialize(void *destination, void *source) {
    RLibraryBlockingCall *call = destination;
    const RLibraryBlockingStage *stage = source;

    (void)memset(call, 0, sizeof(*call));
    call->arguments_type = stage->arguments_type;
    call->result_type = stage->result_type;
    call->entry = stage->entry;
    call->arguments_offset = stage->arguments_offset;
    if (stage->arguments_type.size != 0U) {
        stage->arguments_type.move_initialize(call_arguments(call), stage->staged_arguments);
        call->arguments_initialized = 1;
    }
}

/* The entry moves every argument it receives out of the payload, so this destroys only the
   arguments of a call that never ran. */
static void call_drop(void *value) {
    RLibraryBlockingCall *call = value;

    if (call->arguments_initialized) {
        call->arguments_initialized = 0;
        call->arguments_type.drop(call_arguments(call));
    }
}

/* On a pool thread: runs the entry unless cancellation came first. A cancellation selected before
   the entry returned destroys the result the entry wrote; the task then reports cancellation. */
static void call_run(RRuntimeBlockingJob *job) {
    RLibraryBlockingCall *call = (RLibraryBlockingCall *)job;
    RRuntimeTaskExternalExecution *execution = call->execution;

    if (!r_runtime_task_external_cancel_requested(execution)) {
        call->entry(call_arguments(call), call->result);
        if (r_runtime_task_external_try_select_completion(execution)) {
            r_runtime_task_external_acknowledge(execution);
            return;
        }
        if ((call->result_type.size != 0U) && (call->result_type.drop != NULL)) {
            call->result_type.drop(call->result);
        }
    }
    r_runtime_task_external_acknowledge(execution);
}

static void
call_start(RRuntimeTaskExternalExecution *execution, void *payload_pointer, void *result_pointer) {
    RLibraryBlockingCall *call = payload_pointer;

    call->execution = execution;
    call->result = result_pointer;
    r_runtime_task_external_start_ready(execution);
    if (r_runtime_task_external_cancel_requested(execution)) {
        r_runtime_blocking_unreserve();
        r_runtime_task_external_acknowledge(execution);
        return;
    }
    r_runtime_blocking_submit(&call->job, call_run, execution);
}

/* On the executor, after the start: a call that no pool thread has taken is removed and
   acknowledged here; a taken call is acknowledged by its pool thread when the entry returns. */
static void call_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryBlockingCall *call = payload_pointer;

    if (call->execution != execution) {
        blocking_contract_violation();
    }
    if (r_runtime_blocking_withdraw(&call->job)) {
        r_runtime_task_external_acknowledge(execution);
    }
}

static RStdAsyncStartResult start_failure(RStdAsyncStartError error) {
    RStdAsyncStartResult result = {0};

    result.error = error;
    return result;
}

static RStdAsyncStartResult task_start_failure(RRuntimeTaskStartStatus status) {
    switch (status) {
    case R_RUNTIME_TASK_START_ALLOCATION_FAILED:
        return start_failure(R_STD_ASYNC_START_REFUSAL());
    case R_RUNTIME_TASK_START_RUNTIME_STOPPING:
        return start_failure(R_STD_ASYNC_START_RUNTIME_STOPPING);
    case R_RUNTIME_TASK_START_OK:
    case R_RUNTIME_TASK_START_INVALID:
        blocking_contract_violation();
    }
    blocking_contract_violation();
}

RStdAsyncStartResult r_std_async_blocking(RRuntimeTypeInfo payload_type,
                                          RRuntimeTypeInfo result_type,
                                          RStdAsyncBlockingEntryFn entry,
                                          void *staged_payload) {
    size_t alignment = _Alignof(RLibraryBlockingCall);
    size_t offset = sizeof(RLibraryBlockingCall);
    RRuntimeTypeInfo call_type;
    RLibraryBlockingStage stage;
    RRuntimeTaskPrepareResult prepared;
    RRuntimeTaskStartResult started;
    RStdAsyncStartResult outcome = {0};

    if (entry == NULL) {
        blocking_contract_violation();
    }
    if (payload_type.size != 0U) {
        const size_t argument_alignment = payload_type.alignment;

        if ((argument_alignment == 0U) ||
            ((argument_alignment & (argument_alignment - 1U)) != 0U) ||
            (payload_type.move_initialize == NULL) || (payload_type.drop == NULL)) {
            blocking_contract_violation();
        }
        if (argument_alignment > alignment) {
            alignment = argument_alignment;
        }
        offset = (offset + argument_alignment - 1U) & ~(argument_alignment - 1U);
        if (offset > SIZE_MAX - payload_type.size) {
            return start_failure(R_STD_ASYNC_START_ALLOCATION_FAILED);
        }
    }
    call_type.size = offset + payload_type.size;
    call_type.alignment = alignment;
    call_type.move_initialize = call_move_initialize;
    call_type.drop = call_drop;
    switch (r_runtime_blocking_reserve()) {
    case R_RUNTIME_BLOCKING_RESERVED:
        break;
    case R_RUNTIME_BLOCKING_STOPPING:
        return start_failure(R_STD_ASYNC_START_RUNTIME_STOPPING);
    case R_RUNTIME_BLOCKING_QUEUE_FULL:
    case R_RUNTIME_BLOCKING_THREAD_UNAVAILABLE:
        return start_failure(R_STD_ASYNC_START_ALLOCATION_FAILED);
    }
    prepared =
        r_runtime_task_external_start_prepare(call_type, result_type, call_start, call_cancel);
    if (prepared.status != R_RUNTIME_TASK_START_OK) {
        r_runtime_blocking_unreserve();
        return task_start_failure(prepared.status);
    }
    stage.arguments_type = payload_type;
    stage.result_type = result_type;
    stage.entry = entry;
    stage.arguments_offset = offset;
    stage.staged_arguments = staged_payload;
    started = r_runtime_task_start_commit(&prepared.transaction, &stage);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        r_runtime_blocking_unreserve();
        return task_start_failure(started.status);
    }
    outcome.is_ok = 1;
    outcome.task = started.task;
    return outcome;
}
