#include "r_library_sync_receive_internal.h"

#include "r_library_sync_internal.h"
#include "r_runtime_0_1.h"
#include "r_runtime_task.h"

#include <dispatch/dispatch.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * std.sync::reserve (R-LIB-0016, L30) as an external task, following std.sync::receive: the
 * payload is the channel waiter, the task retains the channel until its payload is dropped, and
 * the acknowledgement runs on a native queue.
 */
typedef struct RLibrarySyncReservePayload {
    RLibrarySyncChannelWaiter waiter;
    RLibrarySyncChannelState *state;
    RRuntimeTaskExternalExecution *execution;
    unsigned char *result;
    size_t tag_offset;
    size_t value_offset;
} RLibrarySyncReservePayload;

static RLibrarySyncReservePayload *reserve_payload(RLibrarySyncChannelWaiter *waiter) {
    return (RLibrarySyncReservePayload *)(void *)waiter;
}

static void reserve_payload_move(void *destination, void *source) {
    RLibrarySyncReservePayload *destination_value = destination;
    RLibrarySyncReservePayload *source_value = source;

    *destination_value = *source_value;
    source_value->state = NULL;
}

static void reserve_payload_drop(void *value) {
    RLibrarySyncReservePayload *payload = value;

    if (payload->state != NULL) {
        r_library_internal_sync_channel_release(payload->state);
        payload->state = NULL;
    }
}

static _Bool reserve_select(RLibrarySyncChannelWaiter *waiter) {
    return r_runtime_task_external_try_select_completion(reserve_payload(waiter)->execution);
}

static void reserve_acknowledge(void *context) {
    r_runtime_task_external_acknowledge(context);
    r_runtime_hosted_work_end();
}

static void reserve_finish(RLibrarySyncChannelWaiter *waiter) {
    RLibrarySyncReservePayload *payload = reserve_payload(waiter);
    RRuntimeTaskExternalExecution *execution = payload->execution;
    const uint32_t tag = waiter->received ? UINT32_C(0) : UINT32_C(1);

    (void)memcpy(payload->result + payload->tag_offset, &tag, sizeof(tag));
    r_runtime_hosted_work_begin();
    dispatch_async_f(
        dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0UL), execution, reserve_acknowledge);
}

static void reserve_external_start(RRuntimeTaskExternalExecution *execution,
                                   void *payload_pointer,
                                   void *result_pointer) {
    RLibrarySyncReservePayload *payload = payload_pointer;

    payload->execution = execution;
    payload->result = result_pointer;
    payload->waiter.value = payload->result + payload->value_offset;
    payload->waiter.select = reserve_select;
    payload->waiter.finish = reserve_finish;
    r_runtime_task_external_start_ready(execution);
    if (r_library_internal_sync_channel_reserve_begin(payload->state, &payload->waiter) ==
        R_LIBRARY_SYNC_RECEIVE_FINISHED) {
        reserve_finish(&payload->waiter);
    }
}

static void reserve_external_cancel(RRuntimeTaskExternalExecution *execution,
                                    void *payload_pointer) {
    RLibrarySyncReservePayload *payload = payload_pointer;

    r_library_internal_sync_channel_reserve_withdraw(payload->state, &payload->waiter);
    r_runtime_task_external_acknowledge(execution);
}

static RStdAsyncStartResult reserve_start_failure(RRuntimeTaskStartStatus status) {
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
        r_library_internal_sync_contract_violation();
    }
    r_library_internal_sync_contract_violation();
}

RStdAsyncStartResult r_library_internal_sync_reserve(const RStdSyncSyncSender *endpoint,
                                                     RStdSyncReserveLayout layout) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibrarySyncReservePayload),
        _Alignof(RLibrarySyncReservePayload),
        reserve_payload_move,
        reserve_payload_drop,
    };
    RLibrarySyncReservePayload payload;
    RRuntimeTaskPrepareResult prepared;
    RRuntimeTaskStartResult started;
    RStdAsyncStartResult result = {0};

    if ((endpoint == NULL) || (endpoint->state == NULL) ||
        (layout.result.size < sizeof(uint32_t)) ||
        (layout.tag_offset > layout.result.size - sizeof(uint32_t)) ||
        (layout.value_offset > layout.result.size - sizeof(RStdSyncPermit))) {
        r_library_internal_sync_contract_violation();
    }
    prepared = r_runtime_task_external_start_prepare(
        payload_type, layout.result, reserve_external_start, reserve_external_cancel);
    if (prepared.status != R_RUNTIME_TASK_START_OK) {
        return reserve_start_failure(prepared.status);
    }
    (void)memset(&payload, 0, sizeof(payload));
    payload.state = endpoint->state;
    payload.tag_offset = layout.tag_offset;
    payload.value_offset = layout.value_offset;
    r_library_internal_sync_channel_retain(payload.state);
    started = r_runtime_task_start_commit(&prepared.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        reserve_payload_drop(&payload);
        return reserve_start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}
