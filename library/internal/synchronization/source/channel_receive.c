#include "r_library_sync_receive_internal.h"

#include "r_library_sync_internal.h"
#include "r_runtime_0_1.h"
#include "r_runtime_task.h"

#include <dispatch/dispatch.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/*
 * std.sync::receive (R-LIB-0016) as an external task. The payload is the channel waiter; the task
 * retains the channel until its payload is dropped. A sender finishes the waiter under the
 * protocol of r_library_sync_internal.h, and the acknowledgement always runs on a native queue:
 * a sender may be an R task on an executor worker, and acknowledging there would clean up that
 * worker's thread-local storage in the middle of its step.
 */
typedef struct RLibrarySyncReceivePayload {
    RLibrarySyncChannelWaiter waiter;
    RLibrarySyncChannelState *state;
    RRuntimeTaskExternalExecution *execution;
    unsigned char *result;
    size_t tag_offset;
    size_t value_offset;
} RLibrarySyncReceivePayload;

static RLibrarySyncReceivePayload *receive_payload(RLibrarySyncChannelWaiter *waiter) {
    return (RLibrarySyncReceivePayload *)(void *)waiter;
}

/* The payload moves only before commit, when its waiter is not linked. */
static void receive_payload_move(void *destination, void *source) {
    RLibrarySyncReceivePayload *destination_value = destination;
    RLibrarySyncReceivePayload *source_value = source;

    *destination_value = *source_value;
    source_value->state = NULL;
}

static void receive_payload_drop(void *value) {
    RLibrarySyncReceivePayload *payload = value;

    if (payload->state != NULL) {
        r_library_internal_sync_channel_release(payload->state);
        payload->state = NULL;
    }
}

static _Bool receive_select(RLibrarySyncChannelWaiter *waiter) {
    return r_runtime_task_external_try_select_completion(receive_payload(waiter)->execution);
}

static void receive_acknowledge(void *context) {
    r_runtime_task_external_acknowledge(context);
    r_runtime_hosted_work_end();
}

static void receive_finish(RLibrarySyncChannelWaiter *waiter) {
    RLibrarySyncReceivePayload *payload = receive_payload(waiter);
    RRuntimeTaskExternalExecution *execution = payload->execution;
    const uint32_t tag = waiter->received ? UINT32_C(1) : UINT32_C(0);

    (void)memcpy(payload->result + payload->tag_offset, &tag, sizeof(tag));
    r_runtime_hosted_work_begin();
    dispatch_async_f(
        dispatch_get_global_queue(QOS_CLASS_DEFAULT, 0UL), execution, receive_acknowledge);
}

/* Readiness precedes the waiter's publication: a sender may finish the waiter at once, and the
 * runtime dispatches a cancellation only after this callback returns. */
static void receive_external_start(RRuntimeTaskExternalExecution *execution,
                                   void *payload_pointer,
                                   void *result_pointer) {
    RLibrarySyncReceivePayload *payload = payload_pointer;

    payload->execution = execution;
    payload->result = result_pointer;
    payload->waiter.value = payload->result + payload->value_offset;
    payload->waiter.select = receive_select;
    payload->waiter.finish = receive_finish;
    r_runtime_task_external_start_ready(execution);
    if (r_library_internal_sync_channel_receive_begin(payload->state, &payload->waiter) ==
        R_LIBRARY_SYNC_RECEIVE_FINISHED) {
        receive_finish(&payload->waiter);
    }
}

/* Runs only when cancellation won the terminal selection, so no finish follows. */
static void receive_external_cancel(RRuntimeTaskExternalExecution *execution,
                                    void *payload_pointer) {
    RLibrarySyncReceivePayload *payload = payload_pointer;

    r_library_internal_sync_channel_receive_withdraw(payload->state, &payload->waiter);
    r_runtime_task_external_acknowledge(execution);
}

static RStdSyncReceiveStartResult receive_start_failure(RRuntimeTaskStartStatus status) {
    RStdSyncReceiveStartResult result = {0};

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

RStdSyncReceiveStartResult r_library_internal_sync_receive(const RStdSyncReceiver *endpoint,
                                                           RStdSyncReceiveLayout layout) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibrarySyncReceivePayload),
        _Alignof(RLibrarySyncReceivePayload),
        receive_payload_move,
        receive_payload_drop,
    };
    RLibrarySyncReceivePayload payload;
    RRuntimeTaskPrepareResult prepared;
    RRuntimeTaskStartResult started;
    RStdSyncReceiveStartResult result = {0};

    if ((endpoint == NULL) || (endpoint->state == NULL) ||
        (layout.result.size < sizeof(uint32_t)) ||
        (layout.tag_offset > layout.result.size - sizeof(uint32_t)) ||
        (layout.value_offset > layout.result.size)) {
        r_library_internal_sync_contract_violation();
    }
    prepared = r_runtime_task_external_start_prepare(
        payload_type, layout.result, receive_external_start, receive_external_cancel);
    if (prepared.status != R_RUNTIME_TASK_START_OK) {
        return receive_start_failure(prepared.status);
    }
    (void)memset(&payload, 0, sizeof(payload));
    payload.state = endpoint->state;
    payload.tag_offset = layout.tag_offset;
    payload.value_offset = layout.value_offset;
    r_library_internal_sync_channel_retain(payload.state);
    started = r_runtime_task_start_commit(&prepared.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        receive_payload_drop(&payload);
        return receive_start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}
