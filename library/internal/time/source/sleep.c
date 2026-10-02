#include "r_library_sleep_internal.h"

#include "r_library_clock_internal.h"
#include "r_library_duration_internal.h"
#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_darwin_timer.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <stddef.h>
#include <stdint.h>

typedef enum RLibraryTimeSleepMode {
    R_LIBRARY_TIME_SLEEP_FOR = 0,
    R_LIBRARY_TIME_SLEEP_UNTIL
} RLibraryTimeSleepMode;

typedef struct RLibraryTimeSleepPayload {
    RLibraryTimeSleepMode mode;
    RStdTimeDuration duration;
    RStdTimeInstant instant;
    RRuntimeDarwinTimer timer;
    RRuntimeTaskExternalExecution *execution;
    RStdTimeTaskResult result;
    uint64_t immediate_event_sequence;
    _Bool timer_bound;
} RLibraryTimeSleepPayload;

static void sleep_payload_move(void *destination, void *source) {
    RLibraryTimeSleepPayload *destination_value = destination;
    RLibraryTimeSleepPayload *source_value = source;

    destination_value->mode = source_value->mode;
    destination_value->duration = source_value->duration;
    destination_value->instant = source_value->instant;
    r_runtime_darwin_timer_initialize(&destination_value->timer);
    r_runtime_darwin_timer_move(&destination_value->timer, &source_value->timer);
    destination_value->execution = NULL;
    destination_value->result = source_value->result;
    destination_value->immediate_event_sequence = source_value->immediate_event_sequence;
    destination_value->timer_bound = source_value->timer_bound;
    source_value->timer_bound = 0;
}

static void sleep_payload_drop(void *value) {
    RLibraryTimeSleepPayload *payload = value;

    r_runtime_darwin_timer_dispose_unsubmitted(&payload->timer);
}

static RStdTimeSleepStartResult sleep_start_failure(RRuntimeTaskStartStatus status) {
    RStdTimeSleepStartResult result = {0};

    switch (status) {
    case R_RUNTIME_TASK_START_ALLOCATION_FAILED:
        result.error = R_STD_ASYNC_START_REFUSAL();
        return result;
    case R_RUNTIME_TASK_START_RUNTIME_STOPPING:
        result.error = R_STD_ASYNC_START_RUNTIME_STOPPING;
        return result;
    case R_RUNTIME_TASK_START_OK:
    case R_RUNTIME_TASK_START_INVALID:
        r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION, (RRuntimeSourceSpan){0U, 0U, 0U});
    }
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION, (RRuntimeSourceSpan){0U, 0U, 0U});
}

static void complete_immediately(RLibraryTimeSleepPayload *payload,
                                 RRuntimeTaskExternalExecution *execution) {
    _Bool selected;

    if (payload->immediate_event_sequence == UINT64_C(0)) {
        r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION, (RRuntimeSourceSpan){0U, 0U, 0U});
    }
    selected = r_runtime_task_external_try_select_completion_at(execution,
                                                                payload->immediate_event_sequence);

    payload->execution = execution;
    r_runtime_task_external_start_ready(execution);
    if (selected) {
        r_runtime_task_external_acknowledge(execution);
    }
}

static void sleep_external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryTimeSleepPayload *payload = payload_pointer;

    if (payload->timer_bound) {
        r_runtime_darwin_timer_cancel(&payload->timer);
    } else {
        r_runtime_task_external_acknowledge(execution);
    }
}

static void sleep_result_error(RStdTimeTaskResult *result, RStdTimeError error) {
    result->r_tag = UINT32_C(1);
    result->r_payload.r_error_00000001 = error;
}

static _Bool sleep_delay(RLibraryTimeSleepPayload *payload,
                         RStdTimeTaskResult *result,
                         uint64_t *seconds,
                         uint32_t *nanoseconds) {
    RStdTimeInstantResult now;

    if (payload->mode == R_LIBRARY_TIME_SLEEP_FOR) {
        RStdTimeInstantResult target;

        if (payload->duration.nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND) {
            sleep_result_error(result, (RStdTimeError){R_STD_TIME_ERROR_INVALID_VALUE, INT64_C(0)});
            return 0;
        }
        if (payload->duration.seconds < 0 ||
            (payload->duration.seconds == 0 && payload->duration.nanoseconds == 0U)) {
            *seconds = 0U;
            *nanoseconds = 0U;
            return 1;
        }
        now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
        if (!now.is_ok) {
            sleep_result_error(result, now.error);
            return 0;
        }
        target = r_library_internal_time_instant_add(now.value, payload->duration);
        if (!target.is_ok) {
            sleep_result_error(result, target.error);
            return 0;
        }
        *seconds = (uint64_t)payload->duration.seconds;
        *nanoseconds = payload->duration.nanoseconds;
        return 1;
    }

    if ((payload->instant.storage_seconds < 0) ||
        (payload->instant.storage_nanoseconds >= R_STD_TIME_NANOSECONDS_PER_SECOND)) {
        sleep_result_error(result, (RStdTimeError){R_STD_TIME_ERROR_INVALID_VALUE, INT64_C(0)});
        return 0;
    }
    now = r_library_internal_time_monotonic_now(r_library_internal_time_darwin_clock_hooks());
    if (!now.is_ok) {
        sleep_result_error(result, now.error);
        return 0;
    }
    if ((payload->instant.storage_seconds < now.value.storage_seconds) ||
        ((payload->instant.storage_seconds == now.value.storage_seconds) &&
         (payload->instant.storage_nanoseconds <= now.value.storage_nanoseconds))) {
        *seconds = 0U;
        *nanoseconds = 0U;
        return 1;
    }
    {
        const RStdTimeDurationTimeResult difference =
            r_library_internal_time_instant_duration(payload->instant, now.value);

        if (!difference.is_ok) {
            sleep_result_error(result, difference.error);
            return 0;
        }
        *seconds = (uint64_t)difference.value.seconds;
        *nanoseconds = difference.value.nanoseconds;
    }
    return 1;
}

static void sleep_external_start(RRuntimeTaskExternalExecution *execution,
                                 void *payload_pointer,
                                 void *result_pointer) {
    RLibraryTimeSleepPayload *payload = payload_pointer;
    RStdTimeTaskResult *result = result_pointer;

    payload->execution = execution;
    *result = payload->result;
    if (payload->timer_bound) {
        r_runtime_darwin_timer_bind_and_activate(&payload->timer, execution);
        return;
    }
    complete_immediately(payload, execution);
}

static RRuntimeTaskStartStatus sleep_reserve_native(RLibraryTimeSleepPayload *payload) {
    RRuntimeDarwinTimerPrepareStatus prepare_status;
    uint64_t seconds = 0U;
    uint32_t nanoseconds = 0U;

    payload->result = (RStdTimeTaskResult){0};
    if (!sleep_delay(payload, &payload->result, &seconds, &nanoseconds)) {
        return R_RUNTIME_TASK_START_OK;
    }
    payload->result.r_tag = UINT32_C(0);
    prepare_status = r_runtime_darwin_timer_prepare(&payload->timer, seconds, nanoseconds);
    switch (prepare_status) {
    case R_RUNTIME_DARWIN_TIMER_PREPARE_OK:
        payload->timer_bound = 1;
        return R_RUNTIME_TASK_START_OK;
    case R_RUNTIME_DARWIN_TIMER_PREPARE_ALLOCATION_FAILED:
        return R_RUNTIME_TASK_START_ALLOCATION_FAILED;
    case R_RUNTIME_DARWIN_TIMER_PREPARE_UNAVAILABLE:
        sleep_result_error(&payload->result,
                           (RStdTimeError){R_STD_TIME_ERROR_UNAVAILABLE, INT64_C(0)});
        return R_RUNTIME_TASK_START_OK;
    case R_RUNTIME_DARWIN_TIMER_PREPARE_INVALID:
        r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION, (RRuntimeSourceSpan){0U, 0U, 0U});
    }
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION, (RRuntimeSourceSpan){0U, 0U, 0U});
}

static RStdTimeSleepStartResult sleep_start(RLibraryTimeSleepPayload *payload) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryTimeSleepPayload),
        _Alignof(RLibraryTimeSleepPayload),
        sleep_payload_move,
        sleep_payload_drop,
    };
    const RRuntimeTypeInfo result_type = {
        sizeof(RStdTimeTaskResult),
        _Alignof(RStdTimeTaskResult),
        NULL,
        NULL,
    };
    RRuntimeTaskPrepareResult prepared = r_runtime_task_external_start_prepare(
        payload_type, result_type, sleep_external_start, sleep_external_cancel);
    RRuntimeTaskStartResult started;
    RRuntimeTaskStartStatus reserve_status;
    RStdTimeSleepStartResult result = {0};

    if (prepared.status != R_RUNTIME_TASK_START_OK) {
        return sleep_start_failure(prepared.status);
    }
    reserve_status = sleep_reserve_native(payload);
    if (reserve_status != R_RUNTIME_TASK_START_OK) {
        r_runtime_task_start_abort(&prepared.transaction);
        r_runtime_darwin_timer_dispose_unsubmitted(&payload->timer);
        return sleep_start_failure(reserve_status);
    }
    if (!payload->timer_bound) {
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
    }
    started = r_runtime_task_start_commit(&prepared.transaction, payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        r_runtime_darwin_timer_dispose_unsubmitted(&payload->timer);
        return sleep_start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

RStdTimeSleepStartResult r_library_internal_time_sleep_for(RStdTimeDuration duration) {
    RLibraryTimeSleepPayload payload;

    payload.mode = R_LIBRARY_TIME_SLEEP_FOR;
    payload.duration = duration;
    payload.instant = (RStdTimeInstant){0};
    r_runtime_darwin_timer_initialize(&payload.timer);
    payload.execution = NULL;
    payload.result = (RStdTimeTaskResult){0};
    payload.timer_bound = 0;
    return sleep_start(&payload);
}

RStdTimeSleepStartResult r_library_internal_time_sleep_until(RStdTimeInstant when) {
    RLibraryTimeSleepPayload payload;

    payload.mode = R_LIBRARY_TIME_SLEEP_UNTIL;
    payload.duration = (RStdTimeDuration){0};
    payload.instant = when;
    r_runtime_darwin_timer_initialize(&payload.timer);
    payload.execution = NULL;
    payload.result = (RStdTimeTaskResult){0};
    payload.timer_bound = 0;
    return sleep_start(&payload);
}
