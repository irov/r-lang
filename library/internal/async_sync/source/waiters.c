#include "r_library_async_sync_internal.h"

#include "r_runtime_0_1.h"
#include "r_runtime_core.h"

#include <dispatch/dispatch.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

void r_library_internal_async_wait_push(RLibraryAsyncWaitList *list, RLibraryAsyncWaiter *waiter) {
    waiter->next = NULL;
    waiter->previous = list->tail;
    if (list->tail == NULL) {
        list->head = waiter;
    } else {
        list->tail->next = waiter;
    }
    list->tail = waiter;
    list->count += 1U;
    waiter->linked = 1;
}

void r_library_internal_async_wait_remove(RLibraryAsyncWaitList *list,
                                          RLibraryAsyncWaiter *waiter) {
    if (waiter->previous == NULL) {
        list->head = waiter->next;
    } else {
        waiter->previous->next = waiter->next;
    }
    if (waiter->next == NULL) {
        list->tail = waiter->previous;
    } else {
        waiter->next->previous = waiter->previous;
    }
    waiter->next = NULL;
    waiter->previous = NULL;
    list->count -= 1U;
    waiter->linked = 0;
}

_Bool r_library_internal_async_waiter_select(RLibraryAsyncWaiter *waiter) {
    return r_runtime_task_external_try_select_completion(waiter->execution);
}

static void waiter_acknowledge(void *context) {
    r_runtime_task_external_acknowledge(context);
    r_runtime_hosted_work_end();
}

void r_library_internal_async_waiter_finish(RLibraryAsyncWaiter *waiter) {
    r_runtime_hosted_work_begin();
    dispatch_async_f(
        dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0UL), waiter->execution, waiter_acknowledge);
}

void r_library_internal_async_finish_all(RLibraryAsyncWaiter *finished) {
    while (finished != NULL) {
        RLibraryAsyncWaiter *next = finished->finished_next;

        r_library_internal_async_waiter_finish(finished);
        finished = next;
    }
}

_Noreturn void r_library_internal_async_contract_violation(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION,
                    (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
}

_Noreturn void r_library_internal_async_count_overflow(void) {
    r_runtime_panic(R_RUNTIME_PANIC_REFERENCE_COUNT_OVERFLOW,
                    (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
}

RStdAllocError r_library_internal_async_allocation_error(RRuntimeAllocationStatus status) {
    switch (status) {
    case R_RUNTIME_ALLOCATION_EXHAUSTED:
        return R_STD_ALLOC_REFUSAL();
    case R_RUNTIME_ALLOCATION_SIZE_OVERFLOW:
        return R_STD_ALLOC_ERROR_SIZE_OVERFLOW;
    case R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT:
        return R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT;
    case R_RUNTIME_ALLOCATION_OK:
    case R_RUNTIME_ALLOCATION_INVALID:
        r_library_internal_async_contract_violation();
    }
    r_library_internal_async_contract_violation();
}

/*
 * The payload of one waiting operation. The payload moves only before commit, when its waiter is
 * not linked; the task retains the resource until its payload is dropped.
 */
typedef struct RLibraryAsyncPayload {
    RLibraryAsyncWaiter waiter;
    const RLibraryAsyncOperation *operation;
    void *resource;
} RLibraryAsyncPayload;

static void payload_move(void *destination, void *source) {
    RLibraryAsyncPayload *destination_value = destination;
    RLibraryAsyncPayload *source_value = source;

    *destination_value = *source_value;
    source_value->resource = NULL;
}

static void payload_drop(void *value) {
    RLibraryAsyncPayload *payload = value;

    if (payload->resource != NULL) {
        payload->operation->release(payload->resource);
        payload->resource = NULL;
    }
}

/* Readiness precedes the waiter's publication: a release may finish the waiter at once, and the
 * runtime dispatches a cancellation only after this callback returns. */
static void external_start(RRuntimeTaskExternalExecution *execution,
                           void *payload_pointer,
                           void *result_pointer) {
    RLibraryAsyncPayload *payload = payload_pointer;

    payload->waiter.execution = execution;
    payload->waiter.result = result_pointer;
    r_runtime_task_external_start_ready(execution);
    if (payload->operation->begin(payload->resource, &payload->waiter) ==
        R_LIBRARY_ASYNC_BEGIN_FINISHED) {
        r_library_internal_async_waiter_finish(&payload->waiter);
    }
}

/* Runs only when cancellation won the terminal selection, so no finish follows. */
static void external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryAsyncPayload *payload = payload_pointer;

    payload->operation->withdraw(payload->resource, &payload->waiter);
    r_runtime_task_external_acknowledge(execution);
}

static RStdAsyncStartResult start_failure(RRuntimeTaskStartStatus status) {
    RStdAsyncStartResult result = {0};

    switch (status) {
    case R_RUNTIME_TASK_START_ALLOCATION_FAILED:
        result.error = R_STD_ASYNC_START_REFUSAL();
        return result;
    case R_RUNTIME_TASK_START_RUNTIME_STOPPING:
        result.error = R_STD_ASYNC_START_RUNTIME_STOPPING;
        return result;
    case R_RUNTIME_TASK_START_OK:
    case R_RUNTIME_TASK_START_INVALID:
        r_library_internal_async_contract_violation();
    }
    r_library_internal_async_contract_violation();
}

RStdAsyncStartResult r_library_internal_async_start(const RLibraryAsyncOperation *operation,
                                                    void *resource,
                                                    uint32_t mode,
                                                    RRuntimeTypeInfo result) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryAsyncPayload),
        _Alignof(RLibraryAsyncPayload),
        payload_move,
        payload_drop,
    };
    RLibraryAsyncPayload payload;
    RRuntimeTaskPrepareResult prepared;
    RRuntimeTaskStartResult started;
    RStdAsyncStartResult outcome = {0};

    if ((operation == NULL) || (resource == NULL)) {
        r_library_internal_async_contract_violation();
    }
    prepared = r_runtime_task_external_start_prepare(
        payload_type, result, external_start, external_cancel);
    if (prepared.status != R_RUNTIME_TASK_START_OK) {
        return start_failure(prepared.status);
    }
    (void)memset(&payload, 0, sizeof(payload));
    payload.operation = operation;
    payload.resource = resource;
    payload.waiter.mode = mode;
    operation->retain(resource);
    started = r_runtime_task_start_commit(&prepared.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        payload_drop(&payload);
        return start_failure(started.status);
    }
    outcome.is_ok = 1;
    outcome.task = started.task;
    return outcome;
}
