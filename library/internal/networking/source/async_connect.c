#include "r_library_net_internal.h"

#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_darwin_socket.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>

typedef struct RLibraryNetConnectControl {
    pthread_mutex_t mutex;
    RRuntimeDarwinSocketConnect *request;
    RRuntimeTaskExternalExecution *execution;
    void *result;
    RRuntimeDarwinSocketConnectResult native_result;
    int connected_descriptor;
    _Bool callback_released;
    _Bool cancel_reported;
    _Bool completion_selected;
    _Bool finalized;
} RLibraryNetConnectControl;

typedef struct RLibraryNetConnectPayload {
    RStdNetSocketAddress remote;
    /* R-SLIB-NET-0014: a Unix-domain connect carries its converted path; length zero is invalid. */
    struct sockaddr_un unix_remote;
    socklen_t unix_remote_length;
    _Bool unix_domain;
    RStdNetDeadline deadline;
    RStdNetTcpStreamStorage *stream_storage;
    RLibraryNetConnectControl *control;
    RStdNetError immediate_error;
    uint64_t immediate_event_sequence;
    _Bool immediate;
} RLibraryNetConnectPayload;

typedef struct RLibraryNetConnectFinalizeAction {
    RRuntimeDarwinSocketConnectResult native_result;
    int connected_descriptor;
    _Bool ready;
    _Bool completion_selected;
} RLibraryNetConnectFinalizeAction;

static void connect_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION, (RRuntimeSourceSpan){0U, 0U, 0U});
}

static RStdNetError net_error(RStdNetErrorCode code, int64_t native_code) {
    return (RStdNetError){code, native_code};
}

static RStdNetTaskStartResult start_failure(RRuntimeTaskStartStatus status) {
    RStdNetTaskStartResult result = {0};

    switch (status) {
    case R_RUNTIME_TASK_START_ALLOCATION_FAILED:
        result.error = R_STD_ASYNC_START_REFUSAL();
        return result;
    case R_RUNTIME_TASK_START_RUNTIME_STOPPING:
        result.error = R_STD_ASYNC_START_RUNTIME_STOPPING;
        return result;
    case R_RUNTIME_TASK_START_OK:
    case R_RUNTIME_TASK_START_INVALID:
        connect_panic();
    }
    connect_panic();
    return result;
}

static void stream_result_move(void *destination_pointer, void *source_pointer) {
    RStdNetTcpStreamResult *destination = destination_pointer;
    RStdNetTcpStreamResult *source = source_pointer;

    *destination = *source;
    if (source->r_tag == UINT32_C(0)) {
        source->r_payload.r_ok.storage = NULL;
    }
}

static void stream_result_drop(void *value) {
    RStdNetTcpStreamResult *result = value;

    if (result->r_tag == UINT32_C(0)) {
        r_library_internal_net_tcp_stream_drop(&result->r_payload.r_ok);
    }
}

static void unix_stream_result_move(void *destination_pointer, void *source_pointer) {
    RStdNetUnixStreamResult *destination = destination_pointer;
    RStdNetUnixStreamResult *source = source_pointer;

    *destination = *source;
    if (source->r_tag == UINT32_C(0)) {
        source->r_payload.r_ok.storage = NULL;
    }
}

static void unix_stream_result_drop(void *value) {
    RStdNetUnixStreamResult *result = value;

    if (result->r_tag == UINT32_C(0)) {
        r_library_internal_net_unix_stream_drop(&result->r_payload.r_ok);
    }
}

/* The TCP and Unix-domain stream results share one layout; each is written through its type. */
static void stream_result_error(void *result_pointer, _Bool unix_domain, RStdNetError error) {
    if (unix_domain) {
        RStdNetUnixStreamResult *result = result_pointer;

        *result = (RStdNetUnixStreamResult){0};
        result->r_tag = UINT32_C(1);
        result->r_payload.r_error_00000001 = error;
    } else {
        RStdNetTcpStreamResult *result = result_pointer;

        *result = (RStdNetTcpStreamResult){0};
        result->r_tag = UINT32_C(1);
        result->r_payload.r_error_00000001 = error;
    }
}

static void stream_result_value(void *result_pointer,
                                _Bool unix_domain,
                                RStdNetTcpStreamStorage *storage) {
    if (unix_domain) {
        RStdNetUnixStreamResult *result = result_pointer;

        *result = (RStdNetUnixStreamResult){0};
        result->r_payload.r_ok.storage = storage;
    } else {
        RStdNetTcpStreamResult *result = result_pointer;

        *result = (RStdNetTcpStreamResult){0};
        result->r_payload.r_ok.storage = storage;
    }
}

static void connect_control_destroy(RLibraryNetConnectControl *control) {
    RRuntimeDarwinSocketConnect *request = NULL;

    if (control == NULL) {
        return;
    }
    if (pthread_mutex_lock(&control->mutex) != 0) {
        connect_panic();
    }
    request = control->request;
    control->request = NULL;
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        connect_panic();
    }
    if (request != NULL) {
        r_runtime_darwin_socket_connect_abort(&request);
    }
    if (pthread_mutex_destroy(&control->mutex) != 0) {
        connect_panic();
    }
    r_runtime_allocator_deallocate(control, _Alignof(RLibraryNetConnectControl));
}

static void connect_payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryNetConnectPayload *destination = destination_pointer;
    RLibraryNetConnectPayload *source = source_pointer;

    *destination = *source;
    source->stream_storage = NULL;
    source->control = NULL;
}

static void connect_payload_drop(void *value) {
    RLibraryNetConnectPayload *payload = value;

    connect_control_destroy(payload->control);
    payload->control = NULL;
    r_library_internal_net_handle_release(
        payload->stream_storage == NULL ? NULL : &payload->stream_storage->handle);
    payload->stream_storage = NULL;
}

static RLibraryNetConnectControl *connect_control_create(RRuntimeAllocator *allocator) {
    RLibraryNetConnectControl *control = NULL;

    if (allocator == NULL ||
        r_runtime_allocator_allocate(
            allocator, sizeof(*control), _Alignof(RLibraryNetConnectControl), (void **)&control) !=
            R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    (void)memset(control, 0, sizeof(*control));
    control->connected_descriptor = -1;
    if (pthread_mutex_init(&control->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(control, _Alignof(RLibraryNetConnectControl));
        return NULL;
    }
    return control;
}

static RLibraryNetConnectFinalizeAction
connect_finalize_action_locked(RLibraryNetConnectControl *control) {
    RLibraryNetConnectFinalizeAction action = {0};
    const uint64_t cancellation_sequence =
        r_runtime_task_external_cancellation_sequence(control->execution);

    if (control->callback_released &&
        (cancellation_sequence == UINT64_C(0) || control->cancel_reported) && !control->finalized) {
        control->finalized = 1;
        action.native_result = control->native_result;
        action.connected_descriptor = control->connected_descriptor;
        action.ready = 1;
        action.completion_selected = control->completion_selected;
        control->connected_descriptor = -1;
    }
    return action;
}

static void connect_fill_result(RLibraryNetConnectPayload *payload,
                                RLibraryNetConnectFinalizeAction action) {
    RLibraryNetConnectControl *control = payload->control;

    if (!action.completion_selected) {
        return;
    }
    if (action.native_result.terminal_event == R_RUNTIME_DARWIN_SOCKET_TERMINAL_TIMED_OUT) {
        stream_result_error(control->result,
                            payload->unix_domain,
                            net_error(R_STD_NET_ERROR_TIMED_OUT, INT64_C(0)));
    } else if (action.native_result.terminal_event == R_RUNTIME_DARWIN_SOCKET_TERMINAL_NATIVE) {
        if (action.native_result.connected) {
            if (action.connected_descriptor < 0 || payload->stream_storage == NULL) {
                connect_panic();
            }
            r_library_internal_net_tcp_stream_publish(payload->stream_storage,
                                                      action.connected_descriptor);
            stream_result_value(control->result, payload->unix_domain, payload->stream_storage);
            payload->stream_storage = NULL;
        } else {
            stream_result_error(
                control->result,
                payload->unix_domain,
                r_library_internal_net_error_from_native(action.native_result.native_error));
        }
    } else {
        connect_panic();
    }
}

static void connect_finalize(RLibraryNetConnectPayload *payload,
                             RLibraryNetConnectFinalizeAction action) {
    RLibraryNetConnectControl *control;
    RRuntimeTaskExternalExecution *execution;

    if (!action.ready) {
        return;
    }
    control = payload->control;
    execution = control->execution;
    connect_fill_result(payload, action);
    r_runtime_task_external_acknowledge(execution);
}

static void connect_native_completed(RRuntimeDarwinSocketConnect *request, void *context_pointer) {
    RLibraryNetConnectPayload *payload = context_pointer;
    RLibraryNetConnectControl *control = payload->control;
    RRuntimeDarwinSocketConnectResult native_result =
        r_runtime_darwin_socket_connect_result(request);
    RLibraryNetConnectFinalizeAction action;
    _Bool completion_selected = 0;
    int connected_descriptor = -1;

    if (native_result.terminal_event != R_RUNTIME_DARWIN_SOCKET_TERMINAL_CANCELLED) {
        completion_selected = r_runtime_task_external_try_select_completion_at(
            control->execution, native_result.terminal_event_sequence);
    }
    if (native_result.connected && completion_selected) {
        connected_descriptor = r_runtime_darwin_socket_connect_take_descriptor(request);
        if (connected_descriptor < 0) {
            connect_panic();
        }
    }
    if (pthread_mutex_lock(&control->mutex) != 0) {
        connect_panic();
    }
    if (control->request != request || control->callback_released) {
        (void)pthread_mutex_unlock(&control->mutex);
        connect_panic();
    }
    control->request = NULL;
    control->native_result = native_result;
    control->connected_descriptor = connected_descriptor;
    control->callback_released = 1;
    control->completion_selected = completion_selected;
    action = connect_finalize_action_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        connect_panic();
    }
    r_runtime_darwin_socket_connect_release(request);
    connect_finalize(payload, action);
}

static void connect_external_cancel(RRuntimeTaskExternalExecution *execution,
                                    void *payload_pointer) {
    RLibraryNetConnectPayload *payload = payload_pointer;
    RLibraryNetConnectControl *control = payload->control;
    RLibraryNetConnectFinalizeAction action;
    const uint64_t cancellation_sequence = r_runtime_task_external_cancellation_sequence(execution);

    if (control == NULL || control->execution != execution ||
        cancellation_sequence == UINT64_C(0) || pthread_mutex_lock(&control->mutex) != 0) {
        connect_panic();
    }
    if (control->cancel_reported) {
        (void)pthread_mutex_unlock(&control->mutex);
        connect_panic();
    }
    if (control->request != NULL) {
        (void)r_runtime_darwin_socket_connect_cancel(control->request, cancellation_sequence);
    }
    control->cancel_reported = 1;
    action = connect_finalize_action_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        connect_panic();
    }
    connect_finalize(payload, action);
}

static void connect_complete_immediate(RLibraryNetConnectPayload *payload,
                                       RRuntimeTaskExternalExecution *execution,
                                       void *result_pointer) {
    _Bool selected;

    if (payload->immediate_event_sequence == UINT64_C(0)) {
        connect_panic();
    }
    selected = r_runtime_task_external_try_select_completion_at(execution,
                                                                payload->immediate_event_sequence);
    r_runtime_task_external_start_ready(execution);
    if (selected) {
        stream_result_error(result_pointer, payload->unix_domain, payload->immediate_error);
        r_runtime_task_external_acknowledge(execution);
    }
}

static void connect_external_start(RRuntimeTaskExternalExecution *execution,
                                   void *payload_pointer,
                                   void *result_pointer) {
    RLibraryNetConnectPayload *payload = payload_pointer;
    RLibraryNetConnectControl *control = payload->control;

    if (payload->immediate) {
        connect_complete_immediate(payload, execution, result_pointer);
        return;
    }
    if (control == NULL || control->request == NULL) {
        connect_panic();
    }
    control->execution = execution;
    control->result = result_pointer;
    if (!r_runtime_darwin_socket_connect_bind(
            control->request, connect_native_completed, payload)) {
        connect_panic();
    }
    r_runtime_task_external_start_ready(execution);
    if (!r_runtime_task_external_cancel_requested(execution) &&
        !r_runtime_darwin_socket_connect_activate(control->request)) {
        connect_panic();
    }
}

static RRuntimeTaskStartStatus connect_reserve_native(RLibraryNetConnectPayload *payload,
                                                      RRuntimeAllocator *allocator) {
    struct sockaddr_storage native = {0};
    socklen_t native_length = 0;
    int domain = 0;
    uint64_t timeout_nanoseconds = 0U;
    RStdNetError deadline_error = {0};
    RLibraryNetDeadlineStatus deadline_status;
    RRuntimeDarwinSocketPrepareResult preparation;

    if (payload->unix_domain) {
        (void)memcpy(&native, &payload->unix_remote, sizeof(payload->unix_remote));
        native_length = payload->unix_remote_length;
    }
    if (payload->unix_domain ? native_length == 0
                             : !r_library_internal_net_address_to_native(
                                   payload->remote, &native, &native_length, &domain)) {
        payload->immediate = 1;
        payload->immediate_error = net_error(R_STD_NET_ERROR_INVALID_ADDRESS, INT64_C(0));
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return R_RUNTIME_TASK_START_OK;
    }
    (void)domain;
    deadline_status = r_library_internal_net_deadline_timeout(
        payload->deadline, &timeout_nanoseconds, &deadline_error);
    if (deadline_status != R_LIBRARY_NET_DEADLINE_READY) {
        payload->immediate = 1;
        payload->immediate_error = deadline_error;
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return R_RUNTIME_TASK_START_OK;
    }
    preparation = r_runtime_darwin_socket_connect_prepare(
        allocator, (const struct sockaddr *)&native, native_length, timeout_nanoseconds);
    switch (preparation.status) {
    case R_RUNTIME_DARWIN_SOCKET_PREPARE_OK:
        payload->control->request = preparation.request;
        return R_RUNTIME_TASK_START_OK;
    case R_RUNTIME_DARWIN_SOCKET_PREPARE_ALLOCATION_FAILED:
        return R_RUNTIME_TASK_START_ALLOCATION_FAILED;
    case R_RUNTIME_DARWIN_SOCKET_PREPARE_NATIVE_FAILED:
        payload->immediate = 1;
        payload->immediate_error =
            r_library_internal_net_error_from_native(preparation.native_error);
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return R_RUNTIME_TASK_START_OK;
    case R_RUNTIME_DARWIN_SOCKET_PREPARE_INVALID:
        connect_panic();
    }
    connect_panic();
    return R_RUNTIME_TASK_START_INVALID;
}

static RStdNetTaskStartResult connect_start(RLibraryNetConnectPayload payload) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryNetConnectPayload),
        _Alignof(RLibraryNetConnectPayload),
        connect_payload_move,
        connect_payload_drop,
    };
    const RRuntimeTypeInfo result_type = {
        payload.unix_domain ? sizeof(RStdNetUnixStreamResult) : sizeof(RStdNetTcpStreamResult),
        payload.unix_domain ? _Alignof(RStdNetUnixStreamResult) : _Alignof(RStdNetTcpStreamResult),
        payload.unix_domain ? unix_stream_result_move : stream_result_move,
        payload.unix_domain ? unix_stream_result_drop : stream_result_drop,
    };
    RRuntimeTaskPrepareResult task_preparation;
    RRuntimeTaskStartResult started;
    RRuntimeAllocator *allocator;
    RRuntimeTaskStartStatus reserve_status;
    RStdNetTaskStartResult result = {0};

    task_preparation = r_runtime_task_external_start_prepare(
        payload_type, result_type, connect_external_start, connect_external_cancel);
    if (task_preparation.status != R_RUNTIME_TASK_START_OK) {
        return start_failure(task_preparation.status);
    }
    allocator = r_runtime_task_start_allocator(task_preparation.transaction);
    if (allocator == NULL) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        connect_panic();
    }
    payload.stream_storage = r_library_internal_net_tcp_stream_reserve(allocator);
    if (payload.stream_storage == NULL) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        return start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    payload.control = connect_control_create(allocator);
    if (payload.control == NULL) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        connect_payload_drop(&payload);
        return start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    reserve_status = connect_reserve_native(&payload, allocator);
    if (reserve_status != R_RUNTIME_TASK_START_OK) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        connect_payload_drop(&payload);
        return start_failure(reserve_status);
    }
    started = r_runtime_task_start_commit(&task_preparation.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        connect_payload_drop(&payload);
        return start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}

RStdNetTaskStartResult r_library_internal_net_tcp_connect(RStdNetSocketAddress remote,
                                                          RStdNetDeadline deadline) {
    RLibraryNetConnectPayload payload = {0};

    payload.remote = remote;
    payload.deadline = deadline;
    return connect_start(payload);
}

RStdNetTaskStartResult r_library_internal_net_unix_connect(RStdStringView path,
                                                           RStdNetDeadline deadline) {
    RLibraryNetConnectPayload payload = {0};

    payload.unix_domain = 1;
    if (!r_library_internal_net_unix_address(
            path, &payload.unix_remote, &payload.unix_remote_length)) {
        payload.unix_remote_length = 0;
    }
    payload.deadline = deadline;
    return connect_start(payload);
}
