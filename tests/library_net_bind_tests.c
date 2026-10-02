#include "r_library_net_internal.h"
#include "r_runtime_allocator.h"
#include "r_runtime_task.h"
#include "r_std_net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <sched.h>
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

static RStdNetSocketAddress v4_loopback(uint16_t port) {
    RStdNetSocketAddress address = {0};
    static const uint8_t bytes[4] = {UINT8_C(127), UINT8_C(0), UINT8_C(0), UINT8_C(1)};

    address.address.kind = R_STD_NET_IP_ADDRESS_V4;
    (void)memcpy(address.address.bytes.v4, bytes, sizeof(bytes));
    address.port = port;
    return address;
}

static RStdNetSocketAddress v6_loopback(uint16_t port, uint32_t scope_id) {
    RStdNetSocketAddress address = {0};

    address.address.kind = R_STD_NET_IP_ADDRESS_V6;
    address.address.bytes.v6[15] = UINT8_C(1);
    address.port = port;
    address.scope_id = scope_id;
    return address;
}

static int await_listener(RStdNetTaskStartResult started, RStdNetTcpListenerResult *result) {
    if (!started.is_ok || (started.task == NULL)) {
        return 0;
    }
    return r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static int await_udp(RStdNetTaskStartResult started, RStdNetUdpSocketResult *result) {
    if (!started.is_ok || (started.task == NULL)) {
        return 0;
    }
    return r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static int test_tcp_listen_success_and_conflict(void) {
    const RStdNetDeadline no_deadline = {0};
    const RStdNetListenOptions options = {0U, 0, 0};
    RStdNetTaskStartResult started;
    RStdNetTcpListenerResult first = {0};
    RStdNetTcpListenerResult conflicting = {0};
    RStdNetSocketAddressResult local;
    struct sockaddr_in native = {0};
    int client_descriptor;
    int accepted_descriptor = -1;
    int descriptor_flags;
    int status_flags;
    size_t attempt;

    started = r_std_net_tcp_listen(v4_loopback(0U), options, no_deadline);
    R_TEST_CHECK(started.is_ok && started.task != NULL);
    R_TEST_CHECK(r_runtime_task_state(started.task) == R_RUNTIME_TASK_COMPLETED);
    R_TEST_CHECK(await_listener(started, &first));
    R_TEST_CHECK(first.r_tag == UINT32_C(0) && first.value.storage != NULL);
    local = r_std_net_tcp_listener_local_address(&first.value);
    R_TEST_CHECK(local.is_ok && local.value.port != 0U);
    R_TEST_CHECK(local.value.address.kind == R_STD_NET_IP_ADDRESS_V4);
    R_TEST_CHECK(local.value.scope_id == 0U);

    descriptor_flags = fcntl(first.value.storage->handle.descriptor, F_GETFD);
    status_flags = fcntl(first.value.storage->handle.descriptor, F_GETFL);
    R_TEST_CHECK(descriptor_flags >= 0 && (descriptor_flags & FD_CLOEXEC) != 0);
    R_TEST_CHECK(status_flags >= 0 && (status_flags & O_NONBLOCK) != 0);
    native.sin_family = AF_INET;
    native.sin_port = htons(local.value.port);
    (void)memcpy(&native.sin_addr, local.value.address.bytes.v4, sizeof(native.sin_addr));
    client_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    R_TEST_CHECK(client_descriptor >= 0);
    R_TEST_CHECK(connect(client_descriptor,
                         (const struct sockaddr *)&native,
                         (socklen_t)sizeof(native)) == 0);
    for (attempt = 0U; attempt < 100000U; ++attempt) {
        accepted_descriptor = accept(first.value.storage->handle.descriptor, NULL, NULL);
        if (accepted_descriptor >= 0) {
            break;
        }
        R_TEST_CHECK(errno == EAGAIN || errno == EWOULDBLOCK);
        (void)sched_yield();
    }
    R_TEST_CHECK(accepted_descriptor >= 0);
    R_TEST_CHECK(close(accepted_descriptor) == 0);
    R_TEST_CHECK(close(client_descriptor) == 0);

    started = r_std_net_tcp_listen(local.value, options, no_deadline);
    R_TEST_CHECK(await_listener(started, &conflicting));
    R_TEST_CHECK(conflicting.r_tag == UINT32_C(1));
    R_TEST_CHECK(conflicting.error.code == R_STD_NET_ERROR_ADDRESS_IN_USE);
    R_TEST_CHECK(conflicting.error.native_code == EADDRINUSE);

    r_std_net_tcp_listener_destroy(&first.value);
    R_TEST_CHECK(first.value.storage == NULL);
    return 0;
}

static int test_udp_bind_success(void) {
    const RStdNetDeadline no_deadline = {0};
    RStdNetTaskStartResult started = r_std_net_udp_bind(v4_loopback(0U), 0, no_deadline);
    RStdNetUdpSocketResult result = {0};
    RStdNetSocketAddressResult local;
    socklen_t option_length = (socklen_t)sizeof(int);
    int socket_type = 0;
    int descriptor_flags;
    int status_flags;

    R_TEST_CHECK(started.is_ok && started.task != NULL);
    R_TEST_CHECK(r_runtime_task_state(started.task) == R_RUNTIME_TASK_COMPLETED);
    R_TEST_CHECK(await_udp(started, &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(0) && result.value.storage != NULL);
    local = r_std_net_udp_local_address(&result.value);
    R_TEST_CHECK(local.is_ok && local.value.port != 0U);
    R_TEST_CHECK(local.value.address.kind == R_STD_NET_IP_ADDRESS_V4);

    descriptor_flags = fcntl(result.value.storage->handle.descriptor, F_GETFD);
    status_flags = fcntl(result.value.storage->handle.descriptor, F_GETFL);
    R_TEST_CHECK(descriptor_flags >= 0 && (descriptor_flags & FD_CLOEXEC) != 0);
    R_TEST_CHECK(status_flags >= 0 && (status_flags & O_NONBLOCK) != 0);
    R_TEST_CHECK(getsockopt(result.value.storage->handle.descriptor,
                            SOL_SOCKET,
                            SO_TYPE,
                            &socket_type,
                            &option_length) == 0);
    R_TEST_CHECK(socket_type == SOCK_DGRAM);

    r_std_net_udp_socket_destroy(&result.value);
    R_TEST_CHECK(result.value.storage == NULL);
    return 0;
}

static int test_validation_and_deadline(void) {
    const RStdNetListenOptions options = {1U, 0, 0};
    const RStdNetDeadline no_deadline = {0};
    const RStdNetDeadline expired = {1, {0, 0U}};
    RStdNetTaskStartResult started;
    RStdNetTcpListenerResult listener = {0};
    RStdNetUdpSocketResult udp = {0};
    RStdNetSocketAddress invalid_v4 = v4_loopback(0U);

    invalid_v4.scope_id = 1U;
    started = r_std_net_tcp_listen(invalid_v4, options, expired);
    R_TEST_CHECK(await_listener(started, &listener));
    R_TEST_CHECK(listener.r_tag == UINT32_C(1) &&
                 listener.error.code == R_STD_NET_ERROR_INVALID_ADDRESS);
    R_TEST_CHECK(listener.error.native_code == 0);

    started = r_std_net_udp_bind(v6_loopback(0U, UINT32_MAX), 0, no_deadline);
    R_TEST_CHECK(await_udp(started, &udp));
    R_TEST_CHECK(udp.r_tag == UINT32_C(1) && udp.error.code == R_STD_NET_ERROR_INVALID_ADDRESS);
    R_TEST_CHECK(udp.error.native_code == 0);

    started = r_std_net_tcp_listen(v4_loopback(0U), options, expired);
    R_TEST_CHECK(await_listener(started, &listener));
    R_TEST_CHECK(listener.r_tag == UINT32_C(1) && listener.error.code == R_STD_NET_ERROR_TIMED_OUT);
    R_TEST_CHECK(listener.error.native_code == 0);
    return 0;
}

static int test_two_phase_allocation_failures(RRuntimeAllocator *allocator) {
    const RStdNetListenOptions options = {4U, 0, 0};
    const RStdNetDeadline no_deadline = {0};
    const RStdNetSocketAddress original = v4_loopback(0U);
    RStdNetTaskStartResult started;

    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    started = r_std_net_tcp_listen(original, options, no_deadline);
    R_TEST_CHECK(!started.is_ok && started.task == NULL);
    R_TEST_CHECK(started.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(allocator) == UINT64_C(1));
    R_TEST_CHECK(original.address.kind == R_STD_NET_IP_ADDRESS_V4 && original.port == 0U &&
                 original.scope_id == 0U);

    r_runtime_allocator_set_failure(allocator, UINT64_C(2));
    started = r_std_net_udp_bind(original, 0, no_deadline);
    R_TEST_CHECK(!started.is_ok && started.task == NULL);
    R_TEST_CHECK(started.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(allocator) == UINT64_C(2));

    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    return 0;
}

int main(void) {
    RRuntimeAllocator allocator;
    RStdNetTaskStartResult stopped;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    R_TEST_CHECK(test_two_phase_allocation_failures(&allocator) == 0);
    R_TEST_CHECK(test_tcp_listen_success_and_conflict() == 0);
    R_TEST_CHECK(test_udp_bind_success() == 0);
    R_TEST_CHECK(test_validation_and_deadline() == 0);
    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());

    stopped = r_std_net_udp_bind(v4_loopback(0U), 0, (RStdNetDeadline){0});
    R_TEST_CHECK(!stopped.is_ok && stopped.task == NULL);
    R_TEST_CHECK(stopped.error == R_STD_ASYNC_START_RUNTIME_STOPPING);
    (void)puts("library_net_bind_tests: ok");
    return EXIT_SUCCESS;
}
