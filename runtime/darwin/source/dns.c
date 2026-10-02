#include "r_runtime_darwin_dns.h"

#include "r_runtime_darwin_event.h"

#include <dispatch/dispatch.h>
#include <dns_sd.h>
#include <limits.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct RRuntimeDarwinDnsAddress {
    struct sockaddr_storage value;
    socklen_t length;
} RRuntimeDarwinDnsAddress;

struct RRuntimeDarwinDnsRequest {
    RRuntimeAllocator *allocator;
    pthread_mutex_t mutex;
    dispatch_queue_t queue;
    dispatch_source_t timer_source;
    DNSServiceRef service;
    RRuntimeDarwinDnsCompletionFn completion;
    void *completion_context;
    RRuntimeDarwinDnsAddress *addresses;
    size_t address_count;
    size_t address_capacity;
    size_t hostname_length;
    size_t references;
    RRuntimeDarwinDnsProtocol protocol;
    RRuntimeDarwinDnsResult result;
    uint64_t pending_cancel_sequence;
    _Bool bound;
    _Bool timer_reserved;
    _Bool timer_configured;
    _Bool timer_activated;
    _Bool activated;
    _Bool terminal_selected;
    _Bool completion_delivered;
    _Bool cancel_scheduled;
    char hostname[];
};

static RRuntimeDarwinDnsPrepareResult dns_prepare_result(RRuntimeDarwinDnsRequest *request,
                                                         RRuntimeDarwinDnsPrepareStatus status) {
    return (RRuntimeDarwinDnsPrepareResult){request, status};
}

static void dns_lock(RRuntimeDarwinDnsRequest *request) {
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
}

static void dns_unlock(RRuntimeDarwinDnsRequest *request) {
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
}

static size_t dns_allocation_size(size_t hostname_length, _Bool *valid) {
    const size_t prefix = offsetof(RRuntimeDarwinDnsRequest, hostname);

    *valid = hostname_length < SIZE_MAX && prefix <= SIZE_MAX - hostname_length - 1U;
    return *valid ? prefix + hostname_length + 1U : 0U;
}

static void dns_request_destroy(RRuntimeDarwinDnsRequest *request) {
    dispatch_queue_t queue;

    if (request == NULL) {
        return;
    }
    if (request->service != NULL || request->timer_source != NULL || request->references != 0U ||
        (request->activated && !request->completion_delivered)) {
        abort();
    }
    queue = request->queue;
    request->queue = NULL;
    r_runtime_allocator_deallocate(request->addresses, _Alignof(RRuntimeDarwinDnsAddress));
    request->addresses = NULL;
    if (pthread_mutex_destroy(&request->mutex) != 0) {
        abort();
    }
    if (queue != NULL) {
        dispatch_release(queue);
    }
    r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinDnsRequest));
}

static void dns_release_reference(RRuntimeDarwinDnsRequest *request) {
    _Bool destroy;

    dns_lock(request);
    if (request->references == 0U) {
        dns_unlock(request);
        abort();
    }
    request->references -= 1U;
    destroy = request->references == 0U;
    dns_unlock(request);
    if (destroy) {
        dns_request_destroy(request);
    }
}

static DNSServiceProtocol dns_native_protocol(RRuntimeDarwinDnsProtocol protocol) {
    switch (protocol) {
    case R_RUNTIME_DARWIN_DNS_PROTOCOL_V4:
        return kDNSServiceProtocol_IPv4;
    case R_RUNTIME_DARWIN_DNS_PROTOCOL_V6:
        return kDNSServiceProtocol_IPv6;
    case R_RUNTIME_DARWIN_DNS_PROTOCOL_ANY:
        return kDNSServiceProtocol_IPv4 | kDNSServiceProtocol_IPv6;
    }
    abort();
}

static _Bool dns_address_equal(const RRuntimeDarwinDnsAddress *left,
                               const RRuntimeDarwinDnsAddress *right) {
    if (left->length != right->length || left->value.ss_family != right->value.ss_family) {
        return 0;
    }
    if (left->value.ss_family == AF_INET) {
        const struct sockaddr_in *left_v4 = (const struct sockaddr_in *)&left->value;
        const struct sockaddr_in *right_v4 = (const struct sockaddr_in *)&right->value;

        return memcmp(&left_v4->sin_addr, &right_v4->sin_addr, sizeof(left_v4->sin_addr)) == 0;
    }
    if (left->value.ss_family == AF_INET6) {
        const struct sockaddr_in6 *left_v6 = (const struct sockaddr_in6 *)&left->value;
        const struct sockaddr_in6 *right_v6 = (const struct sockaddr_in6 *)&right->value;

        return left_v6->sin6_scope_id == right_v6->sin6_scope_id &&
               memcmp(&left_v6->sin6_addr, &right_v6->sin6_addr, sizeof(left_v6->sin6_addr)) == 0;
    }
    return 0;
}

static _Bool dns_store_address(RRuntimeDarwinDnsRequest *request,
                               uint32_t interface_index,
                               const struct sockaddr *address) {
    RRuntimeDarwinDnsAddress candidate = {0};
    RRuntimeDarwinDnsAddress *replacement = NULL;
    size_t index;
    size_t capacity;
    size_t old_size;
    size_t new_size;
    RRuntimeAllocationStatus allocation_status;

    if (address == NULL) {
        return 0;
    }
    if (address->sa_family == AF_INET && request->protocol != R_RUNTIME_DARWIN_DNS_PROTOCOL_V6) {
        const struct sockaddr_in *source = (const struct sockaddr_in *)address;
        struct sockaddr_in *destination = (struct sockaddr_in *)&candidate.value;

        *destination = *source;
        destination->sin_port = 0U;
        candidate.length = (socklen_t)sizeof(*destination);
    } else if (address->sa_family == AF_INET6 &&
               request->protocol != R_RUNTIME_DARWIN_DNS_PROTOCOL_V4) {
        const struct sockaddr_in6 *source = (const struct sockaddr_in6 *)address;
        struct sockaddr_in6 *destination = (struct sockaddr_in6 *)&candidate.value;

        *destination = *source;
        destination->sin6_port = 0U;
        if (destination->sin6_scope_id == 0U &&
            (IN6_IS_ADDR_LINKLOCAL(&destination->sin6_addr) ||
             IN6_IS_ADDR_MC_LINKLOCAL(&destination->sin6_addr))) {
            destination->sin6_scope_id = interface_index;
        }
        candidate.length = (socklen_t)sizeof(*destination);
    } else {
        return 1;
    }
    for (index = 0U; index < request->address_count; ++index) {
        if (dns_address_equal(&request->addresses[index], &candidate)) {
            return 1;
        }
    }
    if (request->address_count < request->address_capacity) {
        request->addresses[request->address_count] = candidate;
        request->address_count += 1U;
        return 1;
    }
    if (request->address_capacity == 0U) {
        capacity = 4U;
    } else {
        if (request->address_capacity > SIZE_MAX / 2U) {
            return 0;
        }
        capacity = request->address_capacity * 2U;
    }
    if (capacity > SIZE_MAX / sizeof(*replacement)) {
        return 0;
    }
    old_size = request->address_capacity * sizeof(*replacement);
    new_size = capacity * sizeof(*replacement);
    allocation_status = r_runtime_allocator_reallocate(request->allocator,
                                                       request->addresses,
                                                       old_size,
                                                       new_size,
                                                       _Alignof(RRuntimeDarwinDnsAddress),
                                                       (void **)&replacement);
    if (allocation_status != R_RUNTIME_ALLOCATION_OK) {
        return 0;
    }
    request->addresses = replacement;
    request->address_capacity = capacity;
    request->addresses[request->address_count] = candidate;
    request->address_count += 1U;
    return 1;
}

static void dns_maybe_deliver_completion(RRuntimeDarwinDnsRequest *request) {
    RRuntimeDarwinDnsCompletionFn completion = NULL;
    void *completion_context = NULL;

    dns_lock(request);
    if (request->terminal_selected && request->service == NULL && request->timer_source == NULL &&
        !request->completion_delivered) {
        if (request->completion == NULL) {
            dns_unlock(request);
            abort();
        }
        request->completion_delivered = 1;
        request->result.address_count = request->address_count;
        completion = request->completion;
        completion_context = request->completion_context;
    }
    dns_unlock(request);
    if (completion != NULL) {
        completion(request, completion_context);
    }
}

static void dns_cancel_timer(dispatch_source_t timer_source, _Bool timer_activated) {
    if (timer_source == NULL) {
        return;
    }
    if (!timer_activated) {
        dispatch_activate(timer_source);
    }
    dispatch_source_cancel(timer_source);
}

static _Bool dns_select_terminal(RRuntimeDarwinDnsRequest *request,
                                 RRuntimeDarwinDnsTerminalEvent terminal_event,
                                 RRuntimeDarwinDnsResultStatus status,
                                 int32_t native_error,
                                 uint64_t event_sequence) {
    DNSServiceRef service = NULL;
    dispatch_source_t timer_source = NULL;
    _Bool timer_activated = 0;
    _Bool first_selection = 0;

    if (event_sequence == UINT64_C(0)) {
        abort();
    }
    dns_lock(request);
    if (request->completion_delivered ||
        (request->terminal_selected && request->result.terminal_event_sequence <= event_sequence)) {
        dns_unlock(request);
        return 0;
    }
    first_selection = !request->terminal_selected;
    request->terminal_selected = 1;
    request->result.terminal_event = terminal_event;
    request->result.status = status;
    request->result.native_error = native_error;
    request->result.terminal_event_sequence = event_sequence;
    if (first_selection) {
        service = request->service;
        request->service = NULL;
        timer_source = request->timer_source;
        timer_activated = request->timer_activated;
    }
    dns_unlock(request);

    if (service != NULL) {
        DNSServiceRefDeallocate(service);
    }
    dns_cancel_timer(timer_source, timer_activated);
    if (timer_source == NULL) {
        dns_maybe_deliver_completion(request);
    }
    return 1;
}

static void dns_timer_cancelled(void *context) {
    RRuntimeDarwinDnsRequest *request = context;
    dispatch_source_t timer_source;

    dns_lock(request);
    timer_source = request->timer_source;
    if (timer_source == NULL) {
        dns_unlock(request);
        abort();
    }
    request->timer_source = NULL;
    dns_unlock(request);
    dispatch_release(timer_source);
    dns_maybe_deliver_completion(request);
}

static void dns_timer_expired(void *context) {
    RRuntimeDarwinDnsRequest *request = context;

    (void)dns_select_terminal(request,
                              R_RUNTIME_DARWIN_DNS_TERMINAL_TIMED_OUT,
                              R_RUNTIME_DARWIN_DNS_RESULT_NATIVE_ERROR,
                              INT32_C(0),
                              r_runtime_darwin_event_sequence_next());
}

static void DNSSD_API dns_address_callback(DNSServiceRef service,
                                           DNSServiceFlags flags,
                                           uint32_t interface_index,
                                           DNSServiceErrorType error_code,
                                           const char *hostname,
                                           const struct sockaddr *address,
                                           uint32_t ttl,
                                           void *context) {
    RRuntimeDarwinDnsRequest *request = context;
    RRuntimeDarwinDnsResultStatus status = R_RUNTIME_DARWIN_DNS_RESULT_ADDRESSES;
    int32_t native_error = INT32_C(0);
    _Bool terminal = 0;

    (void)service;
    (void)hostname;
    (void)ttl;
    if (error_code != kDNSServiceErr_NoError) {
        status = R_RUNTIME_DARWIN_DNS_RESULT_NATIVE_ERROR;
        native_error = (int32_t)error_code;
        terminal = 1;
    } else {
        if ((flags & kDNSServiceFlagsAdd) != 0U &&
            !dns_store_address(request, interface_index, address)) {
            status = R_RUNTIME_DARWIN_DNS_RESULT_ALLOCATION_FAILED;
            terminal = 1;
        } else if ((flags & kDNSServiceFlagsMoreComing) == 0U) {
            terminal = 1;
        }
    }
    if (terminal) {
        (void)dns_select_terminal(request,
                                  R_RUNTIME_DARWIN_DNS_TERMINAL_NATIVE,
                                  status,
                                  native_error,
                                  r_runtime_darwin_event_sequence_next());
    }
}

static void dns_begin(void *context) {
    RRuntimeDarwinDnsRequest *request = context;
    DNSServiceRef service = NULL;
    DNSServiceErrorType native_status;
    dispatch_source_t timer_source;

    dns_lock(request);
    if (request->terminal_selected || !request->activated || !request->bound ||
        (request->timer_reserved && !request->timer_configured)) {
        dns_unlock(request);
        abort();
    }
    timer_source = request->timer_source;
    request->timer_activated = timer_source != NULL;
    dns_unlock(request);
    if (timer_source != NULL) {
        dispatch_activate(timer_source);
    }

    native_status = DNSServiceGetAddrInfo(&service,
                                          0U,
                                          kDNSServiceInterfaceIndexAny,
                                          dns_native_protocol(request->protocol),
                                          request->hostname,
                                          dns_address_callback,
                                          request);
    if (native_status != kDNSServiceErr_NoError) {
        if (service != NULL) {
            DNSServiceRefDeallocate(service);
        }
        (void)dns_select_terminal(request,
                                  R_RUNTIME_DARWIN_DNS_TERMINAL_NATIVE,
                                  R_RUNTIME_DARWIN_DNS_RESULT_NATIVE_ERROR,
                                  (int32_t)native_status,
                                  r_runtime_darwin_event_sequence_next());
        return;
    }
    dns_lock(request);
    if (request->terminal_selected || request->service != NULL) {
        dns_unlock(request);
        DNSServiceRefDeallocate(service);
        return;
    }
    request->service = service;
    dns_unlock(request);
    native_status = DNSServiceSetDispatchQueue(service, request->queue);
    if (native_status != kDNSServiceErr_NoError) {
        (void)dns_select_terminal(request,
                                  R_RUNTIME_DARWIN_DNS_TERMINAL_NATIVE,
                                  R_RUNTIME_DARWIN_DNS_RESULT_NATIVE_ERROR,
                                  (int32_t)native_status,
                                  r_runtime_darwin_event_sequence_next());
    }
}

static void dns_cancel_on_queue(void *context) {
    RRuntimeDarwinDnsRequest *request = context;
    uint64_t cancellation_sequence;

    dns_lock(request);
    cancellation_sequence = request->pending_cancel_sequence;
    dns_unlock(request);
    if (cancellation_sequence != UINT64_C(0)) {
        (void)dns_select_terminal(request,
                                  R_RUNTIME_DARWIN_DNS_TERMINAL_CANCELLED,
                                  R_RUNTIME_DARWIN_DNS_RESULT_NATIVE_ERROR,
                                  INT32_C(0),
                                  cancellation_sequence);
    }
    dns_release_reference(request);
}

RRuntimeDarwinDnsPrepareResult r_runtime_darwin_dns_prepare(RRuntimeAllocator *allocator,
                                                            const char *hostname,
                                                            size_t hostname_length,
                                                            RRuntimeDarwinDnsProtocol protocol,
                                                            _Bool reserve_deadline) {
    RRuntimeDarwinDnsRequest *request = NULL;
    RRuntimeAllocationStatus allocation_status;
    size_t allocation_size;
    _Bool valid_size;

    allocation_size = dns_allocation_size(hostname_length, &valid_size);
    if (!valid_size) {
        return dns_prepare_result(NULL, R_RUNTIME_DARWIN_DNS_PREPARE_ALLOCATION_FAILED);
    }
    allocation_status = r_runtime_allocator_allocate(
        allocator, allocation_size, _Alignof(RRuntimeDarwinDnsRequest), (void **)&request);
    if (allocation_status != R_RUNTIME_ALLOCATION_OK) {
        return dns_prepare_result(NULL, R_RUNTIME_DARWIN_DNS_PREPARE_ALLOCATION_FAILED);
    }
    (void)memset(request, 0, offsetof(RRuntimeDarwinDnsRequest, hostname));
    request->allocator = allocator;
    request->protocol = protocol;
    request->references = 1U;
    request->hostname_length = hostname_length;
    request->timer_reserved = reserve_deadline;
    if (hostname_length != 0U) {
        (void)memcpy(request->hostname, hostname, hostname_length);
    }
    request->hostname[hostname_length] = '\0';
    if (pthread_mutex_init(&request->mutex, NULL) != 0) {
        request->references = 0U;
        r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinDnsRequest));
        return dns_prepare_result(NULL, R_RUNTIME_DARWIN_DNS_PREPARE_ALLOCATION_FAILED);
    }
    request->queue = dispatch_queue_create("org.r-lang.runtime.dns", DISPATCH_QUEUE_SERIAL);
    if (request->queue == NULL) {
        request->references = 0U;
        dns_request_destroy(request);
        return dns_prepare_result(NULL, R_RUNTIME_DARWIN_DNS_PREPARE_ALLOCATION_FAILED);
    }
    if (reserve_deadline) {
        request->timer_source = dispatch_source_create(
            DISPATCH_SOURCE_TYPE_TIMER, 0U, DISPATCH_TIMER_STRICT, request->queue);
        if (request->timer_source == NULL) {
            request->references = 0U;
            dns_request_destroy(request);
            return dns_prepare_result(NULL, R_RUNTIME_DARWIN_DNS_PREPARE_ALLOCATION_FAILED);
        }
        dispatch_set_context(request->timer_source, request);
        dispatch_source_set_event_handler_f(request->timer_source, dns_timer_expired);
        dispatch_source_set_cancel_handler_f(request->timer_source, dns_timer_cancelled);
    }
    return dns_prepare_result(request, R_RUNTIME_DARWIN_DNS_PREPARE_OK);
}

_Bool r_runtime_darwin_dns_bind(RRuntimeDarwinDnsRequest *request,
                                RRuntimeDarwinDnsCompletionFn completion,
                                void *context) {
    _Bool bound = 0;

    dns_lock(request);
    if (!request->bound && !request->activated && !request->completion_delivered) {
        request->completion = completion;
        request->completion_context = context;
        request->bound = 1;
        bound = 1;
    }
    dns_unlock(request);
    return bound;
}

_Bool r_runtime_darwin_dns_set_timeout(RRuntimeDarwinDnsRequest *request,
                                       uint64_t timeout_nanoseconds) {
    dispatch_time_t deadline;
    _Bool configured = 0;

    if (timeout_nanoseconds == 0U || timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return 0;
    }
    deadline = dispatch_time(DISPATCH_TIME_NOW, (int64_t)timeout_nanoseconds);
    if (deadline == DISPATCH_TIME_FOREVER) {
        return 0;
    }
    dns_lock(request);
    if (request->timer_reserved && request->timer_source != NULL && !request->activated &&
        !request->timer_configured) {
        dispatch_source_set_timer(request->timer_source, deadline, DISPATCH_TIME_FOREVER, 0U);
        request->timer_configured = 1;
        configured = 1;
    }
    dns_unlock(request);
    return configured;
}

_Bool r_runtime_darwin_dns_hostname(RRuntimeDarwinDnsRequest *request,
                                    const char **hostname,
                                    size_t *hostname_length) {
    _Bool available = 0;

    dns_lock(request);
    if (!request->activated && !request->completion_delivered) {
        *hostname = request->hostname;
        *hostname_length = request->hostname_length;
        available = 1;
    }
    dns_unlock(request);
    return available;
}

_Bool r_runtime_darwin_dns_activate(RRuntimeDarwinDnsRequest *request) {
    _Bool activated = 0;

    dns_lock(request);
    if (request->bound && !request->activated && !request->terminal_selected &&
        (!request->timer_reserved || request->timer_configured)) {
        request->activated = 1;
        activated = 1;
    }
    dns_unlock(request);
    if (activated) {
        dispatch_async_f(request->queue, request, dns_begin);
    }
    return activated;
}

_Bool r_runtime_darwin_dns_cancel(RRuntimeDarwinDnsRequest *request,
                                  uint64_t cancellation_sequence) {
    _Bool scheduled = 0;

    dns_lock(request);
    if (!request->completion_delivered && !request->cancel_scheduled &&
        request->references != SIZE_MAX) {
        request->pending_cancel_sequence = cancellation_sequence;
        request->cancel_scheduled = 1;
        request->references += 1U;
        request->activated = 1;
        scheduled = 1;
    }
    dns_unlock(request);
    if (scheduled) {
        dispatch_async_f(request->queue, request, dns_cancel_on_queue);
    }
    return scheduled;
}

RRuntimeDarwinDnsResult r_runtime_darwin_dns_result(RRuntimeDarwinDnsRequest *request) {
    RRuntimeDarwinDnsResult result = {0};

    dns_lock(request);
    if (request->completion_delivered) {
        result = request->result;
    }
    dns_unlock(request);
    return result;
}

_Bool r_runtime_darwin_dns_copy_address(RRuntimeDarwinDnsRequest *request,
                                        size_t index,
                                        struct sockaddr_storage *address,
                                        socklen_t *address_length) {
    _Bool copied = 0;

    dns_lock(request);
    if (request->completion_delivered && index < request->address_count) {
        *address = request->addresses[index].value;
        *address_length = request->addresses[index].length;
        copied = 1;
    }
    dns_unlock(request);
    return copied;
}

void r_runtime_darwin_dns_release(RRuntimeDarwinDnsRequest *request) {
    dns_lock(request);
    if (!request->completion_delivered) {
        dns_unlock(request);
        abort();
    }
    dns_unlock(request);
    dns_release_reference(request);
}

void r_runtime_darwin_dns_abort(RRuntimeDarwinDnsRequest **request_pointer) {
    RRuntimeDarwinDnsRequest *request;
    dispatch_source_t timer_source;

    if (request_pointer == NULL || *request_pointer == NULL) {
        return;
    }
    request = *request_pointer;
    *request_pointer = NULL;
    dns_lock(request);
    if (request->activated || request->completion_delivered || request->references != 1U ||
        request->service != NULL) {
        dns_unlock(request);
        abort();
    }
    timer_source = request->timer_source;
    request->timer_source = NULL;
    request->references = 0U;
    dns_unlock(request);
    if (timer_source != NULL) {
        dispatch_source_set_event_handler_f(timer_source, NULL);
        dispatch_source_set_cancel_handler_f(timer_source, NULL);
        dispatch_set_context(timer_source, NULL);
        dispatch_source_cancel(timer_source);
        dispatch_activate(timer_source);
        dispatch_release(timer_source);
    }
    dns_request_destroy(request);
}
