#include "r_library_net_internal.h"

#include "r_runtime_0_1.h"
#include "r_runtime_array.h"
#include "r_runtime_darwin_dns.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <dns_sd.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

typedef enum RLibraryNetResolveHostKind {
    R_LIBRARY_NET_RESOLVE_HOST_INVALID = 0,
    R_LIBRARY_NET_RESOLVE_HOST_DNS,
    R_LIBRARY_NET_RESOLVE_HOST_NUMERIC
} RLibraryNetResolveHostKind;

typedef struct RLibraryNetResolveControl {
    pthread_mutex_t mutex;
    RRuntimeDarwinDnsRequest *request;
    RRuntimeTaskExternalExecution *execution;
    RStdNetResolveResult *result;
    RStdNetResolveResult pending_result;
    _Bool pending_result_initialized;
    _Bool callback_released;
    _Bool cancel_reported;
    _Bool completion_selected;
    _Bool finalized;
} RLibraryNetResolveControl;

typedef struct RLibraryNetResolvePayload {
    RRuntimeAllocator *allocator;
    RLibraryNetResolveControl *control;
    uint16_t port;
    RStdNetFamily family;
    RStdNetDeadline deadline;
} RLibraryNetResolvePayload;

typedef struct RLibraryNetResolveFinalizeAction {
    _Bool ready;
} RLibraryNetResolveFinalizeAction;

static void resolve_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION, (RRuntimeSourceSpan){0U, 0U, 0U});
}

static RStdNetError net_error(RStdNetErrorCode code, int64_t native_code) {
    return (RStdNetError){code, native_code};
}

static RRuntimeTypeInfo socket_address_type(void) {
    return (RRuntimeTypeInfo){
        sizeof(RStdNetSocketAddress),
        _Alignof(RStdNetSocketAddress),
        NULL,
        NULL,
    };
}

static void clear_array(RRuntimeArray *array) {
    *array = (RRuntimeArray){0};
}

static void resolve_result_move(void *destination_pointer, void *source_pointer) {
    RStdNetResolveResult *destination = destination_pointer;
    RStdNetResolveResult *source = source_pointer;

    *destination = *source;
    if (source->r_tag == UINT32_C(0)) {
        clear_array(&source->r_payload.r_ok);
    }
}

static void resolve_result_drop(void *value) {
    RStdNetResolveResult *result = value;

    if (result->r_tag == UINT32_C(0)) {
        r_runtime_array_destroy(&result->r_payload.r_ok);
    }
}

static RRuntimeTypeInfo resolve_result_type(void) {
    return (RRuntimeTypeInfo){
        sizeof(RStdNetResolveResult),
        _Alignof(RStdNetResolveResult),
        resolve_result_move,
        resolve_result_drop,
    };
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
        resolve_panic();
    }
    resolve_panic();
    return result;
}

static void resolve_control_lock(RLibraryNetResolveControl *control) {
    if (pthread_mutex_lock(&control->mutex) != 0) {
        resolve_panic();
    }
}

static void resolve_control_unlock(RLibraryNetResolveControl *control) {
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        resolve_panic();
    }
}

static RLibraryNetResolveControl *resolve_control_create(RRuntimeAllocator *allocator) {
    RLibraryNetResolveControl *control = NULL;

    if (allocator == NULL ||
        r_runtime_allocator_allocate(
            allocator, sizeof(*control), _Alignof(RLibraryNetResolveControl), (void **)&control) !=
            R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    (void)memset(control, 0, sizeof(*control));
    if (pthread_mutex_init(&control->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(control, _Alignof(RLibraryNetResolveControl));
        return NULL;
    }
    return control;
}

static void resolve_control_destroy(RLibraryNetResolveControl *control) {
    RRuntimeDarwinDnsRequest *request;

    if (control == NULL) {
        return;
    }
    resolve_control_lock(control);
    request = control->request;
    control->request = NULL;
    resolve_control_unlock(control);
    if (request != NULL) {
        r_runtime_darwin_dns_abort(&request);
    }
    if (control->pending_result_initialized) {
        resolve_result_drop(&control->pending_result);
        control->pending_result_initialized = 0;
    }
    if (pthread_mutex_destroy(&control->mutex) != 0) {
        resolve_panic();
    }
    r_runtime_allocator_deallocate(control, _Alignof(RLibraryNetResolveControl));
}

static void resolve_payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryNetResolvePayload *destination = destination_pointer;
    RLibraryNetResolvePayload *source = source_pointer;

    *destination = *source;
    source->control = NULL;
}

static void resolve_payload_drop(void *value) {
    RLibraryNetResolvePayload *payload = value;

    resolve_control_destroy(payload->control);
    payload->control = NULL;
}

static _Bool is_ascii_alphanumeric(uint8_t byte) {
    return (byte >= UINT8_C('a') && byte <= UINT8_C('z')) ||
           (byte >= UINT8_C('A') && byte <= UINT8_C('Z')) ||
           (byte >= UINT8_C('0') && byte <= UINT8_C('9'));
}

static RLibraryNetResolveHostKind classify_host(RStdStringView host,
                                                RStdNetIpAddress *numeric_address) {
    size_t index;
    size_t dns_length;
    size_t label_start;
    _Bool has_colon = 0;
    _Bool has_dot = 0;
    _Bool only_decimal_and_dot = 1;

    if (host.length == 0U || (host.data == NULL && host.length != 0U)) {
        return R_LIBRARY_NET_RESOLVE_HOST_INVALID;
    }
    for (index = 0U; index < host.length; ++index) {
        const uint8_t byte = host.data[index];

        if (byte == UINT8_C(0)) {
            return R_LIBRARY_NET_RESOLVE_HOST_INVALID;
        }
        has_colon = has_colon || byte == UINT8_C(':');
        has_dot = has_dot || byte == UINT8_C('.');
        if (!((byte >= UINT8_C('0') && byte <= UINT8_C('9')) || byte == UINT8_C('.'))) {
            only_decimal_and_dot = 0;
        }
    }
    if (has_colon || (has_dot && only_decimal_and_dot)) {
        const RStdNetIpAddressResult parsed = r_library_internal_net_parse_ip(host);

        if (parsed.status != R_STD_NET_CALL_SUCCESS) {
            return R_LIBRARY_NET_RESOLVE_HOST_INVALID;
        }
        *numeric_address = parsed.value;
        return R_LIBRARY_NET_RESOLVE_HOST_NUMERIC;
    }

    dns_length = host.length;
    if (host.data[dns_length - 1U] == UINT8_C('.')) {
        dns_length -= 1U;
    }
    if (dns_length == 0U || dns_length > 253U) {
        return R_LIBRARY_NET_RESOLVE_HOST_INVALID;
    }
    label_start = 0U;
    for (index = 0U; index <= dns_length; ++index) {
        if (index == dns_length || host.data[index] == UINT8_C('.')) {
            const size_t label_length = index - label_start;

            if (label_length == 0U || label_length > 63U ||
                !is_ascii_alphanumeric(host.data[label_start]) ||
                !is_ascii_alphanumeric(host.data[index - 1U])) {
                return R_LIBRARY_NET_RESOLVE_HOST_INVALID;
            }
            label_start = index + 1U;
        } else if (!is_ascii_alphanumeric(host.data[index]) && host.data[index] != UINT8_C('-')) {
            return R_LIBRARY_NET_RESOLVE_HOST_INVALID;
        }
    }
    return R_LIBRARY_NET_RESOLVE_HOST_DNS;
}

static _Bool family_accepts(RStdNetFamily family, RStdNetIpAddressKind kind) {
    if (family == R_STD_NET_FAMILY_ANY) {
        return 1;
    }
    return (family == R_STD_NET_FAMILY_V4 && kind == R_STD_NET_IP_ADDRESS_V4) ||
           (family == R_STD_NET_FAMILY_V6 && kind == R_STD_NET_IP_ADDRESS_V6);
}

static RStdNetResolveResult resolve_error(RStdNetError error) {
    RStdNetResolveResult result = {0};

    result.r_tag = UINT32_C(1);
    result.r_payload.r_error_00000001 = error;
    return result;
}

static RStdNetResolveResult numeric_result(RLibraryNetResolvePayload *payload,
                                           RStdNetIpAddress address) {
    RStdNetResolveResult result = {0};
    RStdNetSocketAddress socket_address = {address, payload->port, 0U};

    if (!family_accepts(payload->family, address.kind)) {
        return resolve_error(net_error(R_STD_NET_ERROR_NAME_NOT_FOUND, INT64_C(0)));
    }
    if (r_runtime_array_with_capacity(
            &result.r_payload.r_ok, payload->allocator, socket_address_type(), 1U) !=
            R_RUNTIME_ARRAY_OK ||
        r_runtime_array_push(&result.r_payload.r_ok, &socket_address) != R_RUNTIME_ARRAY_OK) {
        r_runtime_array_destroy(&result.r_payload.r_ok);
        return resolve_error(net_error(R_STD_NET_ERROR_RESOURCE_EXHAUSTED, INT64_C(0)));
    }
    return result;
}

static RStdNetError dns_native_error(int32_t native_code) {
    switch (native_code) {
    case kDNSServiceErr_NoSuchName:
    case kDNSServiceErr_NoSuchRecord:
        return net_error(R_STD_NET_ERROR_NAME_NOT_FOUND, native_code);
    case kDNSServiceErr_NoMemory:
        return net_error(R_STD_NET_ERROR_RESOURCE_EXHAUSTED, native_code);
    case kDNSServiceErr_Transient:
    case kDNSServiceErr_ServiceNotRunning:
    case kDNSServiceErr_Timeout:
    case kDNSServiceErr_DefunctConnection:
    case kDNSServiceErr_StaleData:
        return net_error(R_STD_NET_ERROR_TEMPORARY_FAILURE, native_code);
    case kDNSServiceErr_NoRouter:
        return net_error(R_STD_NET_ERROR_NETWORK_UNREACHABLE, native_code);
    case kDNSServiceErr_Firewall:
    case kDNSServiceErr_Refused:
    case kDNSServiceErr_NoAuth:
    case kDNSServiceErr_PolicyDenied:
    case kDNSServiceErr_NotPermitted:
        return net_error(R_STD_NET_ERROR_PERMISSION_DENIED, native_code);
    case kDNSServiceErr_Unsupported:
    case kDNSServiceErr_Incompatible:
        return net_error(R_STD_NET_ERROR_UNSUPPORTED, native_code);
    default:
        return net_error(R_STD_NET_ERROR_OTHER, native_code);
    }
}

static RStdNetResolveResult native_result(RLibraryNetResolvePayload *payload,
                                          RRuntimeDarwinDnsRequest *request,
                                          RRuntimeDarwinDnsResult native) {
    RStdNetResolveResult result = {0};
    size_t index;

    if (native.terminal_event == R_RUNTIME_DARWIN_DNS_TERMINAL_TIMED_OUT) {
        return resolve_error(net_error(R_STD_NET_ERROR_TIMED_OUT, INT64_C(0)));
    }
    if (native.terminal_event != R_RUNTIME_DARWIN_DNS_TERMINAL_NATIVE) {
        resolve_panic();
    }
    if (native.status == R_RUNTIME_DARWIN_DNS_RESULT_ALLOCATION_FAILED) {
        return resolve_error(net_error(R_STD_NET_ERROR_RESOURCE_EXHAUSTED, INT64_C(0)));
    }
    if (native.status == R_RUNTIME_DARWIN_DNS_RESULT_NATIVE_ERROR) {
        return resolve_error(dns_native_error(native.native_error));
    }
    if (native.status != R_RUNTIME_DARWIN_DNS_RESULT_ADDRESSES) {
        resolve_panic();
    }
    if (native.address_count == 0U) {
        return resolve_error(net_error(R_STD_NET_ERROR_NAME_NOT_FOUND, INT64_C(0)));
    }
    if (r_runtime_array_with_capacity(&result.r_payload.r_ok,
                                      payload->allocator,
                                      socket_address_type(),
                                      native.address_count) != R_RUNTIME_ARRAY_OK) {
        return resolve_error(net_error(R_STD_NET_ERROR_RESOURCE_EXHAUSTED, INT64_C(0)));
    }
    for (index = 0U; index < native.address_count; ++index) {
        struct sockaddr_storage native_address = {0};
        socklen_t native_length = 0;
        RStdNetSocketAddress address = {0};

        if (!r_runtime_darwin_dns_copy_address(request, index, &native_address, &native_length) ||
            !r_library_internal_net_address_from_native(
                (const struct sockaddr *)&native_address, native_length, &address) ||
            !family_accepts(payload->family, address.address.kind)) {
            r_runtime_array_destroy(&result.r_payload.r_ok);
            return resolve_error(net_error(R_STD_NET_ERROR_INVALID_ADDRESS, INT64_C(0)));
        }
        address.port = payload->port;
        if (r_runtime_array_push(&result.r_payload.r_ok, &address) != R_RUNTIME_ARRAY_OK) {
            r_runtime_array_destroy(&result.r_payload.r_ok);
            return resolve_error(net_error(R_STD_NET_ERROR_RESOURCE_EXHAUSTED, INT64_C(0)));
        }
    }
    return result;
}

static RLibraryNetResolveFinalizeAction
resolve_finalize_action_locked(RLibraryNetResolveControl *control) {
    RLibraryNetResolveFinalizeAction action = {0};
    const uint64_t cancellation_sequence =
        r_runtime_task_external_cancellation_sequence(control->execution);

    if (control->callback_released &&
        (cancellation_sequence == UINT64_C(0) || control->cancel_reported) && !control->finalized) {
        control->finalized = 1;
        if (control->completion_selected) {
            if (!control->pending_result_initialized || control->result == NULL) {
                resolve_panic();
            }
            *control->result = control->pending_result;
            if (control->pending_result.r_tag == UINT32_C(0)) {
                clear_array(&control->pending_result.r_payload.r_ok);
            }
            control->pending_result_initialized = 0;
        }
        action.ready = 1;
    }
    return action;
}

static void resolve_finalize(RLibraryNetResolveControl *control,
                             RLibraryNetResolveFinalizeAction action) {
    if (action.ready) {
        r_runtime_task_external_acknowledge(control->execution);
    }
}

static void resolve_native_completed(RRuntimeDarwinDnsRequest *request, void *context_pointer) {
    RLibraryNetResolvePayload *payload = context_pointer;
    RLibraryNetResolveControl *control = payload->control;
    const RRuntimeDarwinDnsResult completed = r_runtime_darwin_dns_result(request);
    RStdNetResolveResult prepared_result = {0};
    RLibraryNetResolveFinalizeAction action;
    _Bool completion_selected = 0;

    if (completed.terminal_event != R_RUNTIME_DARWIN_DNS_TERMINAL_CANCELLED) {
        completion_selected = r_runtime_task_external_try_select_completion_at(
            control->execution, completed.terminal_event_sequence);
    }
    if (completion_selected) {
        prepared_result = native_result(payload, request, completed);
    }
    resolve_control_lock(control);
    if (control->request != request || control->callback_released ||
        control->pending_result_initialized) {
        resolve_control_unlock(control);
        resolve_panic();
    }
    control->request = NULL;
    control->callback_released = 1;
    control->completion_selected = completion_selected;
    if (completion_selected) {
        control->pending_result = prepared_result;
        control->pending_result_initialized = 1;
    }
    action = resolve_finalize_action_locked(control);
    resolve_control_unlock(control);
    r_runtime_darwin_dns_release(request);
    resolve_finalize(control, action);
}

static void resolve_external_cancel(RRuntimeTaskExternalExecution *execution,
                                    void *payload_pointer) {
    RLibraryNetResolvePayload *payload = payload_pointer;
    RLibraryNetResolveControl *control = payload->control;
    RLibraryNetResolveFinalizeAction action;
    const uint64_t cancellation_sequence = r_runtime_task_external_cancellation_sequence(execution);

    if (control == NULL || control->execution != execution ||
        cancellation_sequence == UINT64_C(0)) {
        resolve_panic();
    }
    resolve_control_lock(control);
    if (control->cancel_reported) {
        resolve_control_unlock(control);
        resolve_panic();
    }
    if (control->request != NULL) {
        (void)r_runtime_darwin_dns_cancel(control->request, cancellation_sequence);
    }
    control->cancel_reported = 1;
    action = resolve_finalize_action_locked(control);
    resolve_control_unlock(control);
    resolve_finalize(control, action);
}

static void resolve_abort_request(RLibraryNetResolveControl *control) {
    RRuntimeDarwinDnsRequest *request;

    resolve_control_lock(control);
    request = control->request;
    control->request = NULL;
    resolve_control_unlock(control);
    if (request == NULL) {
        resolve_panic();
    }
    r_runtime_darwin_dns_abort(&request);
}

static void resolve_complete_immediate(RLibraryNetResolvePayload *payload,
                                       RRuntimeTaskExternalExecution *execution,
                                       RStdNetResolveResult prepared_result) {
    RLibraryNetResolveControl *control = payload->control;
    const uint64_t event_sequence = r_runtime_darwin_event_sequence_next();
    const _Bool completion_selected =
        r_runtime_task_external_try_select_completion_at(execution, event_sequence);
    RLibraryNetResolveFinalizeAction action;

    resolve_control_lock(control);
    control->callback_released = 1;
    control->completion_selected = completion_selected;
    if (completion_selected) {
        control->pending_result = prepared_result;
        control->pending_result_initialized = 1;
    }
    resolve_control_unlock(control);
    if (!completion_selected) {
        resolve_result_drop(&prepared_result);
    }
    r_runtime_task_external_start_ready(execution);
    resolve_control_lock(control);
    action = resolve_finalize_action_locked(control);
    resolve_control_unlock(control);
    resolve_finalize(control, action);
}

static void resolve_external_start(RRuntimeTaskExternalExecution *execution,
                                   void *payload_pointer,
                                   void *result_pointer) {
    RLibraryNetResolvePayload *payload = payload_pointer;
    RLibraryNetResolveControl *control = payload->control;
    const char *hostname = NULL;
    size_t hostname_length = 0U;
    RStdNetIpAddress numeric_address = {0};
    RLibraryNetResolveHostKind host_kind;
    RLibraryNetDeadlineStatus deadline_status;
    RStdNetError deadline_error = {0};
    uint64_t timeout_nanoseconds = 0U;

    if (control == NULL || control->request == NULL ||
        !r_runtime_darwin_dns_hostname(control->request, &hostname, &hostname_length)) {
        resolve_panic();
    }
    control->execution = execution;
    control->result = result_pointer;
    host_kind = classify_host((RStdStringView){(const uint8_t *)hostname, hostname_length},
                              &numeric_address);
    if (host_kind == R_LIBRARY_NET_RESOLVE_HOST_INVALID) {
        resolve_abort_request(control);
        resolve_complete_immediate(
            payload, execution, resolve_error(net_error(R_STD_NET_ERROR_INVALID_NAME, INT64_C(0))));
        return;
    }
    if (host_kind == R_LIBRARY_NET_RESOLVE_HOST_NUMERIC) {
        resolve_abort_request(control);
        resolve_complete_immediate(payload, execution, numeric_result(payload, numeric_address));
        return;
    }
    deadline_status = r_library_internal_net_deadline_timeout(
        payload->deadline, &timeout_nanoseconds, &deadline_error);
    if (deadline_status != R_LIBRARY_NET_DEADLINE_READY) {
        resolve_abort_request(control);
        resolve_complete_immediate(payload, execution, resolve_error(deadline_error));
        return;
    }
    if (payload->deadline.has_value &&
        !r_runtime_darwin_dns_set_timeout(control->request, timeout_nanoseconds)) {
        resolve_panic();
    }
    if (!r_runtime_darwin_dns_bind(control->request, resolve_native_completed, payload)) {
        resolve_panic();
    }
    r_runtime_task_external_start_ready(execution);
    if (!r_runtime_task_external_cancel_requested(execution) &&
        !r_runtime_darwin_dns_activate(control->request)) {
        resolve_panic();
    }
}

static RRuntimeDarwinDnsProtocol resolve_protocol(RStdNetFamily family) {
    switch (family) {
    case R_STD_NET_FAMILY_ANY:
        return R_RUNTIME_DARWIN_DNS_PROTOCOL_ANY;
    case R_STD_NET_FAMILY_V4:
        return R_RUNTIME_DARWIN_DNS_PROTOCOL_V4;
    case R_STD_NET_FAMILY_V6:
        return R_RUNTIME_DARWIN_DNS_PROTOCOL_V6;
    }
    resolve_panic();
    return R_RUNTIME_DARWIN_DNS_PROTOCOL_ANY;
}

RStdNetTaskStartResult r_library_internal_net_resolve(RStdStringView host,
                                                      uint16_t port,
                                                      RStdNetFamily family,
                                                      RStdNetDeadline deadline) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryNetResolvePayload),
        _Alignof(RLibraryNetResolvePayload),
        resolve_payload_move,
        resolve_payload_drop,
    };
    RLibraryNetResolvePayload payload = {0};
    RRuntimeTaskPrepareResult task_preparation;
    RRuntimeTaskStartResult started;
    RRuntimeDarwinDnsPrepareResult dns_preparation;
    RStdNetTaskStartResult result = {0};

    task_preparation = r_runtime_task_external_start_prepare(
        payload_type, resolve_result_type(), resolve_external_start, resolve_external_cancel);
    if (task_preparation.status != R_RUNTIME_TASK_START_OK) {
        return start_failure(task_preparation.status);
    }
    payload.allocator = r_runtime_task_start_allocator(task_preparation.transaction);
    payload.port = port;
    payload.family = family;
    payload.deadline = deadline;
    if (payload.allocator == NULL) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        resolve_panic();
    }
    payload.control = resolve_control_create(payload.allocator);
    if (payload.control == NULL) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        return start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    dns_preparation = r_runtime_darwin_dns_prepare(payload.allocator,
                                                   (const char *)host.data,
                                                   host.length,
                                                   resolve_protocol(family),
                                                   deadline.has_value);
    if (dns_preparation.status != R_RUNTIME_DARWIN_DNS_PREPARE_OK) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        resolve_payload_drop(&payload);
        if (dns_preparation.status == R_RUNTIME_DARWIN_DNS_PREPARE_ALLOCATION_FAILED) {
            return start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
        }
        resolve_panic();
    }
    payload.control->request = dns_preparation.request;
    started = r_runtime_task_start_commit(&task_preparation.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        resolve_payload_drop(&payload);
        return start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}
