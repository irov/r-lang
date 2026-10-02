#include "r_runtime_allocator.h"
#include "r_runtime_array.h"
#include "r_runtime_task.h"
#include "r_std_net.h"
#include "r_std_time.h"

#include <arpa/inet.h>
#include <dispatch/dispatch.h>
#include <dns_sd.h>
#include <netinet/in.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef enum RTestResolveScenario {
    R_TEST_RESOLVE_ORDERED = 0,
    R_TEST_RESOLVE_MISSING,
    R_TEST_RESOLVE_TRANSIENT,
    R_TEST_RESOLVE_PENDING
} RTestResolveScenario;

struct _DNSServiceRef_t {
    DNSServiceGetAddrInfoReply callback;
    void *context;
    dispatch_queue_t queue;
    DNSServiceProtocol protocol;
    RTestResolveScenario scenario;
    _Bool in_callback;
    _Bool deallocated;
};

static _Atomic size_t fake_native_starts;
static _Atomic size_t fake_native_deallocations;
static RRuntimeAllocator fake_dns_allocator;

static RTestResolveScenario fake_scenario(const char *hostname) {
    if (strcmp(hostname, "ordered.test") == 0) {
        return R_TEST_RESOLVE_ORDERED;
    }
    if (strcmp(hostname, "missing.test") == 0) {
        return R_TEST_RESOLVE_MISSING;
    }
    if (strcmp(hostname, "transient.test") == 0) {
        return R_TEST_RESOLVE_TRANSIENT;
    }
    return R_TEST_RESOLVE_PENDING;
}

DNSServiceErrorType DNSSD_API DNSServiceGetAddrInfo(DNSServiceRef *service,
                                                    DNSServiceFlags flags,
                                                    uint32_t interface_index,
                                                    DNSServiceProtocol protocol,
                                                    const char *hostname,
                                                    DNSServiceGetAddrInfoReply callback,
                                                    void *context) {
    DNSServiceRef result;

    (void)flags;
    (void)interface_index;
    if (service == NULL || hostname == NULL || callback == NULL) {
        return kDNSServiceErr_BadParam;
    }
    if (r_runtime_allocator_allocate(&fake_dns_allocator,
                                     sizeof(*result),
                                     _Alignof(struct _DNSServiceRef_t),
                                     (void **)&result) != R_RUNTIME_ALLOCATION_OK) {
        return kDNSServiceErr_NoMemory;
    }
    (void)memset(result, 0, sizeof(*result));
    result->callback = callback;
    result->context = context;
    result->protocol = protocol;
    result->scenario = fake_scenario(hostname);
    *service = result;
    (void)atomic_fetch_add_explicit(&fake_native_starts, 1U, memory_order_relaxed);
    return kDNSServiceErr_NoError;
}

static void fake_invoke(DNSServiceRef service,
                        DNSServiceFlags flags,
                        DNSServiceErrorType error,
                        const struct sockaddr *address) {
    service->in_callback = 1;
    service->callback(
        service, flags, 0U, error, "ordered.test.", address, UINT32_C(60), service->context);
    service->in_callback = 0;
}

static void fake_callbacks(void *context) {
    DNSServiceRef service = context;
    struct sockaddr_in first = {0};
    struct sockaddr_in duplicate = {0};
    struct sockaddr_in6 second = {0};

    if (service->scenario == R_TEST_RESOLVE_MISSING) {
        fake_invoke(service, 0U, kDNSServiceErr_NoSuchName, NULL);
    } else if (service->scenario == R_TEST_RESOLVE_TRANSIENT) {
        fake_invoke(service, 0U, kDNSServiceErr_Transient, NULL);
    } else if (service->scenario == R_TEST_RESOLVE_ORDERED) {
        first.sin_family = AF_INET;
        first.sin_addr.s_addr = htonl(UINT32_C(0xc0000201));
        duplicate = first;
        second.sin6_family = AF_INET6;
        second.sin6_addr.s6_addr[0] = UINT8_C(0x20);
        second.sin6_addr.s6_addr[1] = UINT8_C(0x01);
        second.sin6_addr.s6_addr[2] = UINT8_C(0x0d);
        second.sin6_addr.s6_addr[3] = UINT8_C(0xb8);
        second.sin6_addr.s6_addr[15] = UINT8_C(1);
        if ((service->protocol & kDNSServiceProtocol_IPv4) != 0U) {
            fake_invoke(service,
                        kDNSServiceFlagsAdd | kDNSServiceFlagsMoreComing,
                        kDNSServiceErr_NoError,
                        (const struct sockaddr *)&first);
        }
        if (!service->deallocated && (service->protocol & kDNSServiceProtocol_IPv4) != 0U) {
            fake_invoke(service,
                        kDNSServiceFlagsAdd | kDNSServiceFlagsMoreComing,
                        kDNSServiceErr_NoError,
                        (const struct sockaddr *)&duplicate);
        }
        if (!service->deallocated && (service->protocol & kDNSServiceProtocol_IPv6) != 0U) {
            fake_invoke(service,
                        kDNSServiceFlagsAdd,
                        kDNSServiceErr_NoError,
                        (const struct sockaddr *)&second);
        } else if (!service->deallocated) {
            fake_invoke(service, 0U, kDNSServiceErr_NoError, NULL);
        }
    }
    if (service->deallocated) {
        r_runtime_allocator_deallocate(service, _Alignof(struct _DNSServiceRef_t));
    }
}

DNSServiceErrorType DNSSD_API DNSServiceSetDispatchQueue(DNSServiceRef service,
                                                         dispatch_queue_t queue) {
    if (service == NULL || queue == NULL) {
        return kDNSServiceErr_BadParam;
    }
    service->queue = queue;
    if (service->scenario != R_TEST_RESOLVE_PENDING) {
        dispatch_async_f(queue, service, fake_callbacks);
    }
    return kDNSServiceErr_NoError;
}

void DNSSD_API DNSServiceRefDeallocate(DNSServiceRef service) {
    if (service == NULL || service->deallocated) {
        abort();
    }
    service->deallocated = 1;
    (void)atomic_fetch_add_explicit(&fake_native_deallocations, 1U, memory_order_relaxed);
    if (!service->in_callback && service->scenario == R_TEST_RESOLVE_PENDING) {
        r_runtime_allocator_deallocate(service, _Alignof(struct _DNSServiceRef_t));
    }
}

static RStdStringView string_view(const char *text) {
    return (RStdStringView){(const uint8_t *)text, strlen(text)};
}

static int await_result(RStdNetTaskStartResult started, RStdNetResolveResult *result) {
    if (!started.is_ok || started.task == NULL) {
        return 0;
    }
    return r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static void destroy_result(RStdNetResolveResult *result) {
    r_runtime_array_destroy(&result->value);
    *result = (RStdNetResolveResult){0};
}

static int test_numeric_and_family(void) {
    const RStdNetDeadline expired = {1, {0, 0U}};
    RStdNetResolveResult result = {0};
    const RStdNetSocketAddress *address;

    R_TEST_CHECK(await_result(
        r_std_net_resolve(string_view("192.0.2.9"), 443U, R_STD_NET_FAMILY_ANY, expired), &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(0) && result.value.length == 1U);
    address = r_runtime_array_get(&result.value, 0U);
    R_TEST_CHECK(address != NULL && address->address.kind == R_STD_NET_IP_ADDRESS_V4);
    R_TEST_CHECK(address->address.bytes.v4[0] == UINT8_C(192) &&
                 address->address.bytes.v4[1] == UINT8_C(0) &&
                 address->address.bytes.v4[2] == UINT8_C(2) &&
                 address->address.bytes.v4[3] == UINT8_C(9));
    R_TEST_CHECK(address->port == 443U && address->scope_id == 0U);
    destroy_result(&result);

    R_TEST_CHECK(await_result(
        r_std_net_resolve(string_view("192.0.2.9"), 80U, R_STD_NET_FAMILY_V6, expired), &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(1) &&
                 result.error.code == R_STD_NET_ERROR_NAME_NOT_FOUND &&
                 result.error.native_code == 0 && result.value.data == NULL);
    return 0;
}

static int test_validation_precedence(void) {
    static const uint8_t embedded_nul[] = {'a', 0U, 'b'};
    static const char *const invalid_names[] = {
        "",
        "1.2.3.999",
        "a..b",
        "-a.test",
        "a-.test",
        "a_b.test",
        ".",
    };
    const RStdNetDeadline expired = {1, {0, 0U}};
    size_t index;
    size_t starts = atomic_load_explicit(&fake_native_starts, memory_order_relaxed);

    for (index = 0U; index < sizeof(invalid_names) / sizeof(invalid_names[0]); ++index) {
        RStdNetResolveResult result = {0};

        R_TEST_CHECK(
            await_result(r_std_net_resolve(
                             string_view(invalid_names[index]), 53U, R_STD_NET_FAMILY_ANY, expired),
                         &result));
        R_TEST_CHECK(result.r_tag == UINT32_C(1) &&
                     result.error.code == R_STD_NET_ERROR_INVALID_NAME &&
                     result.error.native_code == 0);
    }
    {
        RStdNetResolveResult result = {0};
        const RStdStringView host = {embedded_nul, sizeof(embedded_nul)};

        R_TEST_CHECK(
            await_result(r_std_net_resolve(host, 53U, R_STD_NET_FAMILY_ANY, expired), &result));
        R_TEST_CHECK(result.r_tag == UINT32_C(1) &&
                     result.error.code == R_STD_NET_ERROR_INVALID_NAME);
    }
    {
        RStdNetResolveResult result = {0};

        R_TEST_CHECK(await_result(
            r_std_net_resolve(string_view("valid.test"), 53U, R_STD_NET_FAMILY_ANY, expired),
            &result));
        R_TEST_CHECK(result.r_tag == UINT32_C(1) &&
                     result.error.code == R_STD_NET_ERROR_TIMED_OUT &&
                     result.error.native_code == 0);
    }
    R_TEST_CHECK(atomic_load_explicit(&fake_native_starts, memory_order_relaxed) == starts);
    return 0;
}

static int test_dns_results(void) {
    RStdNetResolveResult result = {0};
    const RStdNetSocketAddress *first;
    const RStdNetSocketAddress *second;

    R_TEST_CHECK(await_result(
        r_std_net_resolve(
            string_view("ordered.test"), 8443U, R_STD_NET_FAMILY_ANY, (RStdNetDeadline){0}),
        &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(0) && result.value.length == 2U);
    first = r_runtime_array_get(&result.value, 0U);
    second = r_runtime_array_get(&result.value, 1U);
    R_TEST_CHECK(first != NULL && second != NULL);
    R_TEST_CHECK(first->address.kind == R_STD_NET_IP_ADDRESS_V4 && first->port == 8443U);
    R_TEST_CHECK(second->address.kind == R_STD_NET_IP_ADDRESS_V6 && second->port == 8443U);
    R_TEST_CHECK(first->address.bytes.v4[0] == UINT8_C(192) &&
                 first->address.bytes.v4[3] == UINT8_C(1));
    R_TEST_CHECK(second->address.bytes.v6[0] == UINT8_C(0x20) &&
                 second->address.bytes.v6[15] == UINT8_C(1));
    destroy_result(&result);

    R_TEST_CHECK(await_result(
        r_std_net_resolve(
            string_view("ordered.test"), 80U, R_STD_NET_FAMILY_V4, (RStdNetDeadline){0}),
        &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(0) && result.value.length == 1U);
    destroy_result(&result);

    R_TEST_CHECK(await_result(
        r_std_net_resolve(
            string_view("missing.test"), 80U, R_STD_NET_FAMILY_ANY, (RStdNetDeadline){0}),
        &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(1) &&
                 result.error.code == R_STD_NET_ERROR_NAME_NOT_FOUND &&
                 result.error.native_code == kDNSServiceErr_NoSuchName);

    R_TEST_CHECK(await_result(
        r_std_net_resolve(
            string_view("transient.test"), 80U, R_STD_NET_FAMILY_ANY, (RStdNetDeadline){0}),
        &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(1) &&
                 result.error.code == R_STD_NET_ERROR_TEMPORARY_FAILURE &&
                 result.error.native_code == kDNSServiceErr_Transient);
    return 0;
}

static int test_allocation_failures(RRuntimeAllocator *allocator) {
    static const uint8_t host_bytes[] = "ordered.test";
    const RStdStringView host = {host_bytes, sizeof(host_bytes) - 1U};
    uint64_t fail_at;

    for (fail_at = UINT64_C(1); fail_at <= UINT64_C(3); ++fail_at) {
        RStdNetTaskStartResult started;

        r_runtime_allocator_set_failure(allocator, fail_at);
        started = r_std_net_resolve(host, 80U, R_STD_NET_FAMILY_ANY, (RStdNetDeadline){0});
        R_TEST_CHECK(!started.is_ok && started.task == NULL);
        R_TEST_CHECK(started.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
        R_TEST_CHECK(memcmp(host.data, "ordered.test", host.length) == 0);
    }
    for (fail_at = UINT64_C(4); fail_at <= UINT64_C(5); ++fail_at) {
        RStdNetResolveResult result = {0};
        RStdNetTaskStartResult started;

        r_runtime_allocator_set_failure(allocator, fail_at);
        started = r_std_net_resolve(host, 80U, R_STD_NET_FAMILY_ANY, (RStdNetDeadline){0});
        R_TEST_CHECK(await_result(started, &result));
        R_TEST_CHECK(result.r_tag == UINT32_C(1) &&
                     result.error.code == R_STD_NET_ERROR_RESOURCE_EXHAUSTED &&
                     result.error.native_code == 0);
    }
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    return 0;
}

static RStdNetDeadline deadline_after_milliseconds(uint32_t milliseconds) {
    const RStdTimeInstantResult now = r_std_time_monotonic_now();
    const RStdTimeDurationResult duration =
        r_std_time_duration_from_parts(0, milliseconds * UINT32_C(1000000));
    RStdTimeInstantResult deadline;

    if (!now.is_ok || !duration.is_ok) {
        abort();
    }
    deadline = r_std_time_instant_add(now.value, duration.value);
    if (!deadline.is_ok) {
        abort();
    }
    return (RStdNetDeadline){1, deadline.value};
}

static int test_cancellation_and_deadline(void) {
    RStdNetTaskStartResult cancelled = r_std_net_resolve(
        string_view("pending.test"), 80U, R_STD_NET_FAMILY_ANY, (RStdNetDeadline){0});
    RStdNetResolveResult result = {0};

    R_TEST_CHECK(cancelled.is_ok && cancelled.task != NULL);
    r_runtime_task_cancel(&cancelled.task);
    R_TEST_CHECK(cancelled.task == NULL);

    R_TEST_CHECK(await_result(r_std_net_resolve(string_view("pending.test"),
                                                80U,
                                                R_STD_NET_FAMILY_ANY,
                                                deadline_after_milliseconds(UINT32_C(5))),
                              &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(1) && result.error.code == R_STD_NET_ERROR_TIMED_OUT &&
                 result.error.native_code == 0);
    return 0;
}

int main(void) {
    RRuntimeAllocator allocator;
    RStdNetTaskStartResult stopped;

    atomic_init(&fake_native_starts, 0U);
    atomic_init(&fake_native_deallocations, 0U);
    r_runtime_allocator_initialize(&fake_dns_allocator);
    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    R_TEST_CHECK(test_numeric_and_family() == 0);
    R_TEST_CHECK(test_validation_precedence() == 0);
    R_TEST_CHECK(test_allocation_failures(&allocator) == 0);
    R_TEST_CHECK(test_dns_results() == 0);
    R_TEST_CHECK(test_cancellation_and_deadline() == 0);
    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
    R_TEST_CHECK(atomic_load_explicit(&fake_native_starts, memory_order_relaxed) ==
                 atomic_load_explicit(&fake_native_deallocations, memory_order_relaxed));

    stopped = r_std_net_resolve(
        string_view("127.0.0.1"), 80U, R_STD_NET_FAMILY_ANY, (RStdNetDeadline){0});
    R_TEST_CHECK(!stopped.is_ok && stopped.task == NULL &&
                 stopped.error == R_STD_ASYNC_START_RUNTIME_STOPPING);
    (void)puts("library_net_resolve_tests: ok");
    return EXIT_SUCCESS;
}
