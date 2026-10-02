#include "r_runtime_allocator.h"
#include "r_runtime_task.h"
#include "r_std_net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct RTestServer {
    int descriptor;
    uint16_t port;
} RTestServer;

static RStdNetSocketAddress v4_loopback(uint16_t port) {
    RStdNetSocketAddress address = {0};
    static const uint8_t bytes[4] = {UINT8_C(127), UINT8_C(0), UINT8_C(0), UINT8_C(1)};

    address.address.kind = R_STD_NET_IP_ADDRESS_V4;
    (void)memcpy(address.address.bytes.v4, bytes, sizeof(bytes));
    address.port = port;
    return address;
}

static int server_create(RTestServer *server) {
    struct sockaddr_in address = {0};
    socklen_t address_length = (socklen_t)sizeof(address);

    server->descriptor = socket(AF_INET, SOCK_STREAM, 0);
    if (server->descriptor < 0) {
        return 0;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (bind(server->descriptor, (const struct sockaddr *)&address, address_length) != 0 ||
        listen(server->descriptor, 16) != 0 ||
        getsockname(server->descriptor, (struct sockaddr *)&address, &address_length) != 0) {
        (void)close(server->descriptor);
        server->descriptor = -1;
        return 0;
    }
    server->port = ntohs(address.sin_port);
    return 1;
}

static int await_stream(RStdNetTaskStartResult started, RStdNetTcpStreamResult *result) {
    if (!started.is_ok || started.task == NULL) {
        return 0;
    }
    return r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static int test_connect_success(void) {
    RTestServer server = {-1, 0U};
    RStdNetTaskStartResult started;
    RStdNetTcpStreamResult result = {0};
    RStdNetSocketAddressResult local;
    RStdNetSocketAddressResult peer;
    int accepted;

    R_TEST_CHECK(server_create(&server));
    started = r_std_net_tcp_connect(v4_loopback(server.port), (RStdNetDeadline){0});
    R_TEST_CHECK(await_stream(started, &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(0) && result.value.storage != NULL);
    peer = r_std_net_tcp_peer_address(&result.value);
    local = r_std_net_tcp_local_address(&result.value);
    R_TEST_CHECK(peer.is_ok && peer.value.port == server.port);
    R_TEST_CHECK(peer.value.address.kind == R_STD_NET_IP_ADDRESS_V4);
    R_TEST_CHECK(local.is_ok && local.value.port != 0U);
    accepted = accept(server.descriptor, NULL, NULL);
    R_TEST_CHECK(accepted >= 0);
    R_TEST_CHECK(close(accepted) == 0);
    r_std_net_tcp_stream_destroy(&result.value);
    R_TEST_CHECK(result.value.storage == NULL);
    R_TEST_CHECK(close(server.descriptor) == 0);
    return 0;
}

static int test_refused_connect(void) {
    RTestServer server = {-1, 0U};
    RStdNetTcpStreamResult result = {0};
    RStdNetTaskStartResult started;
    uint16_t port;

    R_TEST_CHECK(server_create(&server));
    port = server.port;
    R_TEST_CHECK(close(server.descriptor) == 0);
    started = r_std_net_tcp_connect(v4_loopback(port), (RStdNetDeadline){0});
    R_TEST_CHECK(await_stream(started, &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(1));
    R_TEST_CHECK(result.error.code == R_STD_NET_ERROR_CONNECTION_REFUSED);
    R_TEST_CHECK(result.error.native_code == ECONNREFUSED);
    return 0;
}

static int test_validation_and_deadline(void) {
    RStdNetSocketAddress invalid = v4_loopback(1U);
    RStdNetTcpStreamResult result = {0};
    RStdNetTaskStartResult started;
    const RStdNetDeadline expired = {1, {0, 0U}};

    invalid.scope_id = 1U;
    started = r_std_net_tcp_connect(invalid, expired);
    R_TEST_CHECK(await_stream(started, &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(1) &&
                 result.error.code == R_STD_NET_ERROR_INVALID_ADDRESS);
    R_TEST_CHECK(result.error.native_code == 0);

    started = r_std_net_tcp_connect(v4_loopback(1U), expired);
    R_TEST_CHECK(await_stream(started, &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(1) && result.error.code == R_STD_NET_ERROR_TIMED_OUT);
    R_TEST_CHECK(result.error.native_code == 0);
    return 0;
}

static int test_two_phase_allocation_failures(RRuntimeAllocator *allocator) {
    const RStdNetSocketAddress remote = v4_loopback(1U);
    uint64_t fail_at;

    for (fail_at = UINT64_C(1); fail_at <= UINT64_C(4); ++fail_at) {
        RStdNetTaskStartResult started;

        r_runtime_allocator_set_failure(allocator, fail_at);
        started = r_std_net_tcp_connect(remote, (RStdNetDeadline){0});
        R_TEST_CHECK(!started.is_ok && started.task == NULL);
        R_TEST_CHECK(started.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
        R_TEST_CHECK(r_runtime_allocator_attempt_count(allocator) == fail_at);
        R_TEST_CHECK(remote.port == 1U && remote.scope_id == 0U);
    }
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    return 0;
}

static int test_cancel_completion_races(void) {
    size_t iteration;

    for (iteration = 0U; iteration < 32U; ++iteration) {
        RTestServer server = {-1, 0U};
        RStdNetTaskStartResult started;

        R_TEST_CHECK(server_create(&server));
        started = r_std_net_tcp_connect(v4_loopback(server.port), (RStdNetDeadline){0});
        R_TEST_CHECK(started.is_ok && started.task != NULL);
        r_runtime_task_cancel(&started.task);
        R_TEST_CHECK(started.task == NULL);
        R_TEST_CHECK(close(server.descriptor) == 0);
    }
    return 0;
}

int main(void) {
    RRuntimeAllocator allocator;
    RStdNetTaskStartResult stopped;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    R_TEST_CHECK(test_two_phase_allocation_failures(&allocator) == 0);
    R_TEST_CHECK(test_connect_success() == 0);
    R_TEST_CHECK(test_refused_connect() == 0);
    R_TEST_CHECK(test_validation_and_deadline() == 0);
    R_TEST_CHECK(test_cancel_completion_races() == 0);
    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());

    stopped = r_std_net_tcp_connect(v4_loopback(1U), (RStdNetDeadline){0});
    R_TEST_CHECK(!stopped.is_ok && stopped.task == NULL);
    R_TEST_CHECK(stopped.error == R_STD_ASYNC_START_RUNTIME_STOPPING);
    (void)puts("library_net_connect_tests: ok");
    return EXIT_SUCCESS;
}
