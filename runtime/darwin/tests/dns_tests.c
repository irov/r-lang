#include "r_runtime_darwin_dns.h"

#include "r_runtime_allocator.h"
#include "r_runtime_darwin_event.h"

#include <arpa/inet.h>
#include <dispatch/dispatch.h>
#include <dns_sd.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#define R_DNS_TEST_REQUIRE(condition, message)                                                     \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "dns adapter test failure: %s\n", message);                      \
            abort();                                                                               \
        }                                                                                          \
    } while (0)

typedef enum RTestDnsScenario {
    R_TEST_DNS_ORDERED = 0,
    R_TEST_DNS_MISSING,
    R_TEST_DNS_PENDING,
    R_TEST_DNS_START_ERROR,
    R_TEST_DNS_QUEUE_ERROR
} RTestDnsScenario;

struct _DNSServiceRef_t {
    DNSServiceGetAddrInfoReply callback;
    void *context;
    dispatch_queue_t queue;
    DNSServiceProtocol protocol;
    RTestDnsScenario scenario;
    _Bool in_callback;
    _Bool deallocated;
};

static RRuntimeAllocator fake_dns_allocator;

typedef struct RTestDnsCompletion {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    RRuntimeDarwinDnsResult result;
    struct sockaddr_storage addresses[4];
    socklen_t address_lengths[4];
    size_t address_count;
    _Bool completed;
} RTestDnsCompletion;

static RTestDnsScenario fake_scenario(const char *hostname) {
    if (strcmp(hostname, "ordered.test") == 0) {
        return R_TEST_DNS_ORDERED;
    }
    if (strcmp(hostname, "missing.test") == 0) {
        return R_TEST_DNS_MISSING;
    }
    if (strcmp(hostname, "pending.test") == 0) {
        return R_TEST_DNS_PENDING;
    }
    if (strcmp(hostname, "start-error.test") == 0) {
        return R_TEST_DNS_START_ERROR;
    }
    if (strcmp(hostname, "queue-error.test") == 0) {
        return R_TEST_DNS_QUEUE_ERROR;
    }
    return R_TEST_DNS_MISSING;
}

DNSServiceErrorType DNSSD_API DNSServiceGetAddrInfo(DNSServiceRef *service,
                                                    DNSServiceFlags flags,
                                                    uint32_t interface_index,
                                                    DNSServiceProtocol protocol,
                                                    const char *hostname,
                                                    DNSServiceGetAddrInfoReply callback,
                                                    void *context) {
    DNSServiceRef result;
    RTestDnsScenario scenario;

    (void)flags;
    (void)interface_index;
    if (service == NULL || hostname == NULL || callback == NULL) {
        return kDNSServiceErr_BadParam;
    }
    scenario = fake_scenario(hostname);
    if (scenario == R_TEST_DNS_START_ERROR) {
        *service = NULL;
        return kDNSServiceErr_ServiceNotRunning;
    }
    R_DNS_TEST_REQUIRE(r_runtime_allocator_allocate(&fake_dns_allocator,
                                                    sizeof(*result),
                                                    _Alignof(struct _DNSServiceRef_t),
                                                    (void **)&result) == R_RUNTIME_ALLOCATION_OK,
                       "reserve fake DNS service");
    (void)memset(result, 0, sizeof(*result));
    result->callback = callback;
    result->context = context;
    result->protocol = protocol;
    result->scenario = scenario;
    *service = result;
    return kDNSServiceErr_NoError;
}

static void fake_invoke(DNSServiceRef service,
                        DNSServiceFlags flags,
                        uint32_t interface_index,
                        DNSServiceErrorType error,
                        const struct sockaddr *address) {
    service->in_callback = 1;
    service->callback(service,
                      flags,
                      interface_index,
                      error,
                      "ordered.test.",
                      address,
                      UINT32_C(60),
                      service->context);
    service->in_callback = 0;
}

static void fake_dns_callbacks(void *context) {
    DNSServiceRef service = context;
    struct sockaddr_in first = {0};
    struct sockaddr_in duplicate = {0};
    struct sockaddr_in6 second = {0};

    if (service->scenario == R_TEST_DNS_MISSING) {
        fake_invoke(service, 0U, 0U, kDNSServiceErr_NoSuchName, NULL);
    } else if (service->scenario == R_TEST_DNS_ORDERED) {
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
                        UINT32_C(4),
                        kDNSServiceErr_NoError,
                        (const struct sockaddr *)&first);
        }
        if (!service->deallocated && (service->protocol & kDNSServiceProtocol_IPv4) != 0U) {
            fake_invoke(service,
                        kDNSServiceFlagsAdd | kDNSServiceFlagsMoreComing,
                        UINT32_C(4),
                        kDNSServiceErr_NoError,
                        (const struct sockaddr *)&duplicate);
        }
        if (!service->deallocated && (service->protocol & kDNSServiceProtocol_IPv6) != 0U) {
            fake_invoke(service,
                        kDNSServiceFlagsAdd,
                        UINT32_C(6),
                        kDNSServiceErr_NoError,
                        (const struct sockaddr *)&second);
        } else if (!service->deallocated) {
            fake_invoke(service, 0U, 0U, kDNSServiceErr_NoError, NULL);
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
    if (service->scenario == R_TEST_DNS_QUEUE_ERROR) {
        return kDNSServiceErr_NoMemory;
    }
    service->queue = queue;
    if (service->scenario != R_TEST_DNS_PENDING) {
        dispatch_async_f(queue, service, fake_dns_callbacks);
    }
    return kDNSServiceErr_NoError;
}

void DNSSD_API DNSServiceRefDeallocate(DNSServiceRef service) {
    R_DNS_TEST_REQUIRE(service != NULL, "deallocate non-null fake DNS service");
    R_DNS_TEST_REQUIRE(!service->deallocated, "deallocate fake DNS service exactly once");
    service->deallocated = 1;
    if (!service->in_callback && service->scenario == R_TEST_DNS_PENDING) {
        r_runtime_allocator_deallocate(service, _Alignof(struct _DNSServiceRef_t));
    } else if (!service->in_callback && service->scenario == R_TEST_DNS_QUEUE_ERROR) {
        r_runtime_allocator_deallocate(service, _Alignof(struct _DNSServiceRef_t));
    }
}

static void completion_initialize(RTestDnsCompletion *completion) {
    (void)memset(completion, 0, sizeof(*completion));
    R_DNS_TEST_REQUIRE(pthread_mutex_init(&completion->mutex, NULL) == 0,
                       "initialize completion mutex");
    R_DNS_TEST_REQUIRE(pthread_cond_init(&completion->condition, NULL) == 0,
                       "initialize completion condition");
}

static void completion_destroy(RTestDnsCompletion *completion) {
    R_DNS_TEST_REQUIRE(pthread_cond_destroy(&completion->condition) == 0,
                       "destroy completion condition");
    R_DNS_TEST_REQUIRE(pthread_mutex_destroy(&completion->mutex) == 0, "destroy completion mutex");
}

static void dns_completed(RRuntimeDarwinDnsRequest *request, void *context) {
    RTestDnsCompletion *completion = context;
    size_t index;

    R_DNS_TEST_REQUIRE(pthread_mutex_lock(&completion->mutex) == 0, "lock completion");
    R_DNS_TEST_REQUIRE(!completion->completed, "deliver completion exactly once");
    completion->result = r_runtime_darwin_dns_result(request);
    completion->address_count = completion->result.address_count;
    R_DNS_TEST_REQUIRE(completion->address_count <= 4U, "bounded callback address count");
    for (index = 0U; index < completion->address_count; ++index) {
        R_DNS_TEST_REQUIRE(
            r_runtime_darwin_dns_copy_address(
                request, index, &completion->addresses[index], &completion->address_lengths[index]),
            "copy callback address");
    }
    completion->completed = 1;
    R_DNS_TEST_REQUIRE(pthread_cond_broadcast(&completion->condition) == 0, "broadcast completion");
    R_DNS_TEST_REQUIRE(pthread_mutex_unlock(&completion->mutex) == 0, "unlock completion");
    r_runtime_darwin_dns_release(request);
}

static void completion_wait(RTestDnsCompletion *completion) {
    R_DNS_TEST_REQUIRE(pthread_mutex_lock(&completion->mutex) == 0, "lock completion wait");
    while (!completion->completed) {
        R_DNS_TEST_REQUIRE(pthread_cond_wait(&completion->condition, &completion->mutex) == 0,
                           "wait for completion");
    }
    R_DNS_TEST_REQUIRE(pthread_mutex_unlock(&completion->mutex) == 0, "unlock completion wait");
}

static RRuntimeDarwinDnsRequest *prepare_bound(RRuntimeAllocator *allocator,
                                               const char *hostname,
                                               RRuntimeDarwinDnsProtocol protocol,
                                               _Bool deadline,
                                               RTestDnsCompletion *completion) {
    RRuntimeDarwinDnsPrepareResult prepared =
        r_runtime_darwin_dns_prepare(allocator, hostname, strlen(hostname), protocol, deadline);

    R_DNS_TEST_REQUIRE(prepared.status == R_RUNTIME_DARWIN_DNS_PREPARE_OK, "prepare DNS request");
    R_DNS_TEST_REQUIRE(r_runtime_darwin_dns_bind(prepared.request, dns_completed, completion),
                       "bind DNS request");
    return prepared.request;
}

static void test_order_copy_and_deduplication(RRuntimeAllocator *allocator) {
    char hostname[] = "ordered.test";
    RTestDnsCompletion completion;
    RRuntimeDarwinDnsRequest *request;
    const struct sockaddr_in *first;
    const struct sockaddr_in6 *second;

    completion_initialize(&completion);
    request = prepare_bound(allocator, hostname, R_RUNTIME_DARWIN_DNS_PROTOCOL_ANY, 0, &completion);
    hostname[0] = 'x';
    R_DNS_TEST_REQUIRE(r_runtime_darwin_dns_activate(request), "activate ordered DNS request");
    completion_wait(&completion);
    R_DNS_TEST_REQUIRE(completion.result.terminal_event == R_RUNTIME_DARWIN_DNS_TERMINAL_NATIVE,
                       "ordered lookup native terminal");
    R_DNS_TEST_REQUIRE(completion.result.status == R_RUNTIME_DARWIN_DNS_RESULT_ADDRESSES,
                       "ordered lookup address result");
    R_DNS_TEST_REQUIRE(completion.address_count == 2U, "deduplicate ordered callbacks");
    R_DNS_TEST_REQUIRE(completion.address_lengths[0] == (socklen_t)sizeof(*first),
                       "first address size");
    R_DNS_TEST_REQUIRE(completion.address_lengths[1] == (socklen_t)sizeof(*second),
                       "second address size");
    first = (const struct sockaddr_in *)&completion.addresses[0];
    second = (const struct sockaddr_in6 *)&completion.addresses[1];
    R_DNS_TEST_REQUIRE(first->sin_family == AF_INET &&
                           ntohl(first->sin_addr.s_addr) == UINT32_C(0xc0000201),
                       "preserve first v4 callback");
    R_DNS_TEST_REQUIRE(second->sin6_family == AF_INET6 && second->sin6_addr.s6_addr[15] == 1U,
                       "preserve second v6 callback");
    completion_destroy(&completion);
}

static void test_native_and_allocation_failures(RRuntimeAllocator *allocator) {
    RTestDnsCompletion missing;
    RTestDnsCompletion start_error;
    RTestDnsCompletion queue_error;
    RTestDnsCompletion storage_error;
    RRuntimeDarwinDnsRequest *request;

    completion_initialize(&missing);
    request =
        prepare_bound(allocator, "missing.test", R_RUNTIME_DARWIN_DNS_PROTOCOL_V4, 0, &missing);
    R_DNS_TEST_REQUIRE(r_runtime_darwin_dns_activate(request), "activate missing lookup");
    completion_wait(&missing);
    R_DNS_TEST_REQUIRE(missing.result.status == R_RUNTIME_DARWIN_DNS_RESULT_NATIVE_ERROR &&
                           missing.result.native_error == kDNSServiceErr_NoSuchName,
                       "preserve missing native error");
    completion_destroy(&missing);

    completion_initialize(&start_error);
    request = prepare_bound(
        allocator, "start-error.test", R_RUNTIME_DARWIN_DNS_PROTOCOL_ANY, 0, &start_error);
    R_DNS_TEST_REQUIRE(r_runtime_darwin_dns_activate(request), "activate start-error lookup");
    completion_wait(&start_error);
    R_DNS_TEST_REQUIRE(start_error.result.status == R_RUNTIME_DARWIN_DNS_RESULT_NATIVE_ERROR &&
                           start_error.result.native_error == kDNSServiceErr_ServiceNotRunning,
                       "preserve start native error");
    completion_destroy(&start_error);

    completion_initialize(&queue_error);
    request = prepare_bound(
        allocator, "queue-error.test", R_RUNTIME_DARWIN_DNS_PROTOCOL_ANY, 0, &queue_error);
    R_DNS_TEST_REQUIRE(r_runtime_darwin_dns_activate(request), "activate queue-error lookup");
    completion_wait(&queue_error);
    R_DNS_TEST_REQUIRE(queue_error.result.status == R_RUNTIME_DARWIN_DNS_RESULT_NATIVE_ERROR &&
                           queue_error.result.native_error == kDNSServiceErr_NoMemory,
                       "preserve queue native error");
    completion_destroy(&queue_error);

    completion_initialize(&storage_error);
    request = prepare_bound(
        allocator, "ordered.test", R_RUNTIME_DARWIN_DNS_PROTOCOL_ANY, 0, &storage_error);
    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    R_DNS_TEST_REQUIRE(r_runtime_darwin_dns_activate(request), "activate storage-error lookup");
    completion_wait(&storage_error);
    R_DNS_TEST_REQUIRE(storage_error.result.status ==
                               R_RUNTIME_DARWIN_DNS_RESULT_ALLOCATION_FAILED &&
                           storage_error.result.address_count == 0U,
                       "normalize callback storage allocation failure");
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    completion_destroy(&storage_error);
}

static void test_cancellation_and_deadline(RRuntimeAllocator *allocator) {
    RTestDnsCompletion cancelled;
    RTestDnsCompletion timed_out;
    RRuntimeDarwinDnsRequest *request;
    uint64_t sequence;

    completion_initialize(&cancelled);
    request =
        prepare_bound(allocator, "pending.test", R_RUNTIME_DARWIN_DNS_PROTOCOL_ANY, 0, &cancelled);
    R_DNS_TEST_REQUIRE(r_runtime_darwin_dns_activate(request), "activate cancelled lookup");
    sequence = r_runtime_darwin_event_sequence_next();
    R_DNS_TEST_REQUIRE(r_runtime_darwin_dns_cancel(request, sequence), "cancel pending lookup");
    completion_wait(&cancelled);
    R_DNS_TEST_REQUIRE(cancelled.result.terminal_event == R_RUNTIME_DARWIN_DNS_TERMINAL_CANCELLED &&
                           cancelled.result.terminal_event_sequence == sequence,
                       "publish cancellation after deallocation");
    completion_destroy(&cancelled);

    completion_initialize(&timed_out);
    request =
        prepare_bound(allocator, "pending.test", R_RUNTIME_DARWIN_DNS_PROTOCOL_ANY, 1, &timed_out);
    R_DNS_TEST_REQUIRE(r_runtime_darwin_dns_set_timeout(request, UINT64_C(1000000)),
                       "set pending lookup deadline");
    R_DNS_TEST_REQUIRE(r_runtime_darwin_dns_activate(request), "activate timed lookup");
    completion_wait(&timed_out);
    R_DNS_TEST_REQUIRE(timed_out.result.terminal_event == R_RUNTIME_DARWIN_DNS_TERMINAL_TIMED_OUT &&
                           timed_out.result.native_error == 0,
                       "publish timeout after deallocation and timer acknowledgement");
    completion_destroy(&timed_out);
}

static void test_prepare_and_abort(RRuntimeAllocator *allocator) {
    RRuntimeDarwinDnsPrepareResult prepared;
    const char *hostname = NULL;
    size_t hostname_length = 0U;

    prepared = r_runtime_darwin_dns_prepare(
        allocator, "copy.test", 9U, R_RUNTIME_DARWIN_DNS_PROTOCOL_ANY, 1);
    R_DNS_TEST_REQUIRE(prepared.status == R_RUNTIME_DARWIN_DNS_PREPARE_OK,
                       "prepare abortable request");
    R_DNS_TEST_REQUIRE(
        r_runtime_darwin_dns_hostname(prepared.request, &hostname, &hostname_length) &&
            hostname_length == 9U && memcmp(hostname, "copy.test", 9U) == 0,
        "expose immutable hostname copy");
    r_runtime_darwin_dns_abort(&prepared.request);
    R_DNS_TEST_REQUIRE(prepared.request == NULL, "consume aborted request");

    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    prepared = r_runtime_darwin_dns_prepare(
        allocator, "copy.test", 9U, R_RUNTIME_DARWIN_DNS_PROTOCOL_ANY, 0);
    R_DNS_TEST_REQUIRE(prepared.status == R_RUNTIME_DARWIN_DNS_PREPARE_ALLOCATION_FAILED,
                       "inject request allocation failure");
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
}

int main(void) {
    RRuntimeAllocator allocator;

    r_runtime_allocator_initialize(&fake_dns_allocator);
    r_runtime_allocator_initialize(&allocator);
    test_prepare_and_abort(&allocator);
    test_order_copy_and_deduplication(&allocator);
    test_native_and_allocation_failures(&allocator);
    test_cancellation_and_deadline(&allocator);
    return 0;
}
