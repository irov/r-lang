#include "r_library_net_internal.h"
#include "r_runtime_allocator.h"
#include "r_runtime_task.h"
#include "r_std_net.h"
#include "r_std_time.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sched.h>
#include <stdatomic.h>
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

static _Bool is_v4_loopback(RStdNetSocketAddress address) {
    static const uint8_t bytes[4] = {UINT8_C(127), UINT8_C(0), UINT8_C(0), UINT8_C(1)};

    return address.address.kind == R_STD_NET_IP_ADDRESS_V4 && address.scope_id == 0U &&
           memcmp(address.address.bytes.v4, bytes, sizeof(bytes)) == 0;
}

static _Bool socket_address_equal(RStdNetSocketAddress left, RStdNetSocketAddress right) {
    return left.address.kind == right.address.kind && left.port == right.port &&
           left.scope_id == right.scope_id &&
           memcmp(left.address.bytes.v4, right.address.bytes.v4, sizeof(left.address.bytes.v4)) ==
               0;
}

static int await_listener(RStdNetTaskStartResult started, RStdNetTcpListenerResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static int await_connection(RStdNetTaskStartResult started, RStdNetTcpConnectionResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static int create_listener(RStdNetTcpListenerResult *listener,
                           RStdNetSocketAddress *local_address) {
    const RStdNetListenOptions options = {8U, 0, 0};
    RStdNetTaskStartResult started =
        r_std_net_tcp_listen(v4_loopback(0U), options, (RStdNetDeadline){0});
    RStdNetSocketAddressResult local;

    if (!await_listener(started, listener) || listener->r_tag != UINT32_C(0) ||
        listener->value.storage == NULL) {
        return 0;
    }
    local = r_std_net_tcp_listener_local_address(&listener->value);
    if (!local.is_ok || !is_v4_loopback(local.value) || local.value.port == 0U) {
        r_std_net_tcp_listener_destroy(&listener->value);
        return 0;
    }
    *local_address = local.value;
    return 1;
}

static int connect_client(RStdNetSocketAddress remote, RStdNetSocketAddress *local_address) {
    struct sockaddr_in native_remote = {0};
    struct sockaddr_in native_local = {0};
    socklen_t native_length = (socklen_t)sizeof(native_local);
    int descriptor = socket(AF_INET, SOCK_STREAM, 0);

    if (descriptor < 0) {
        return -1;
    }
    native_remote.sin_family = AF_INET;
    native_remote.sin_port = htons(remote.port);
    (void)memcpy(&native_remote.sin_addr, remote.address.bytes.v4, sizeof(native_remote.sin_addr));
    if (connect(descriptor,
                (const struct sockaddr *)&native_remote,
                (socklen_t)sizeof(native_remote)) != 0 ||
        getsockname(descriptor, (struct sockaddr *)&native_local, &native_length) != 0) {
        (void)close(descriptor);
        return -1;
    }
    *local_address = v4_loopback(ntohs(native_local.sin_port));
    (void)memcpy(
        local_address->address.bytes.v4, &native_local.sin_addr, sizeof(native_local.sin_addr));
    return descriptor;
}

static int wait_for_listener_reference_count(RStdNetTcpListenerStorage *storage, size_t expected) {
    size_t attempt;

    for (attempt = 0U; attempt < 100000U; ++attempt) {
        if (atomic_load_explicit(&storage->handle.references, memory_order_acquire) == expected) {
            return 1;
        }
        (void)sched_yield();
    }
    return 0;
}

static int test_success_retains_listener(void) {
    RStdNetTcpListenerResult listener = {0};
    RStdNetTcpConnectionResult connection = {0};
    RStdNetSocketAddress listener_address = {0};
    RStdNetSocketAddress client_address = {0};
    RStdNetSocketAddressResult observed;
    RStdNetTaskStartResult started;
    RStdNetTcpListenerStorage *listener_storage;
    int client_descriptor;
    int descriptor_flags;
    int status_flags;
#if defined(SO_NOSIGPIPE)
    socklen_t no_sigpipe_length = (socklen_t)sizeof(int);
    int no_sigpipe = 0;
#endif

    R_TEST_CHECK(create_listener(&listener, &listener_address));
    listener_storage = listener.value.storage;
    started = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
    R_TEST_CHECK(started.is_ok && started.task != NULL);
    R_TEST_CHECK(r_runtime_task_state(started.task) == R_RUNTIME_TASK_RUNNING);
    R_TEST_CHECK(atomic_load_explicit(&listener_storage->handle.references, memory_order_acquire) ==
                 2U);
    r_std_net_tcp_listener_destroy(&listener.value);
    R_TEST_CHECK(listener.value.storage == NULL);
    R_TEST_CHECK(fcntl(listener_storage->handle.descriptor, F_GETFD) >= 0);

    client_descriptor = connect_client(listener_address, &client_address);
    R_TEST_CHECK(client_descriptor >= 0);
    R_TEST_CHECK(await_connection(started, &connection));
    R_TEST_CHECK(connection.r_tag == UINT32_C(0) && connection.value.stream.storage != NULL);
    R_TEST_CHECK(is_v4_loopback(connection.value.peer));
    R_TEST_CHECK(connection.value.peer.port == client_address.port);
    R_TEST_CHECK(memcmp(connection.value.peer.address.bytes.v4,
                        client_address.address.bytes.v4,
                        sizeof(client_address.address.bytes.v4)) == 0);
    observed = r_std_net_tcp_peer_address(&connection.value.stream);
    R_TEST_CHECK(observed.is_ok);
    R_TEST_CHECK(observed.value.port == connection.value.peer.port);
    R_TEST_CHECK(memcmp(observed.value.address.bytes.v4,
                        connection.value.peer.address.bytes.v4,
                        sizeof(observed.value.address.bytes.v4)) == 0);
    observed = r_std_net_tcp_local_address(&connection.value.stream);
    R_TEST_CHECK(observed.is_ok && observed.value.port == listener_address.port);
    descriptor_flags = fcntl(connection.value.stream.storage->handle.descriptor, F_GETFD);
    status_flags = fcntl(connection.value.stream.storage->handle.descriptor, F_GETFL);
    R_TEST_CHECK(descriptor_flags >= 0 && (descriptor_flags & FD_CLOEXEC) != 0);
    R_TEST_CHECK(status_flags >= 0 && (status_flags & O_NONBLOCK) != 0);
#if defined(SO_NOSIGPIPE)
    R_TEST_CHECK(getsockopt(connection.value.stream.storage->handle.descriptor,
                            SOL_SOCKET,
                            SO_NOSIGPIPE,
                            &no_sigpipe,
                            &no_sigpipe_length) == 0);
    R_TEST_CHECK(no_sigpipe == 1);
#endif

    R_TEST_CHECK(close(client_descriptor) == 0);
    r_std_net_tcp_stream_destroy(&connection.value.stream);
    R_TEST_CHECK(connection.value.stream.storage == NULL);
    return 0;
}

static int test_cancellation_acknowledges_retain(void) {
    RStdNetTcpListenerResult listener = {0};
    RStdNetTcpConnectionResult connection = {0};
    RStdNetSocketAddress listener_address = {0};
    RStdNetSocketAddress client_address = {0};
    RStdNetTaskStartResult cancelled;
    RStdNetTaskStartResult accepted;
    RStdNetTcpListenerStorage *storage;
    int client_descriptor;

    R_TEST_CHECK(create_listener(&listener, &listener_address));
    storage = listener.value.storage;
    cancelled = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
    R_TEST_CHECK(cancelled.is_ok && cancelled.task != NULL);
    accepted = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
    R_TEST_CHECK(accepted.is_ok && accepted.task != NULL);
    R_TEST_CHECK(atomic_load_explicit(&storage->handle.references, memory_order_acquire) == 3U);
    R_TEST_CHECK(storage->accept_head != NULL && storage->accept_tail != NULL &&
                 storage->accept_head != storage->accept_tail);
    r_runtime_task_cancel(&cancelled.task);
    R_TEST_CHECK(cancelled.task == NULL);
    R_TEST_CHECK(wait_for_listener_reference_count(storage, 2U));
    R_TEST_CHECK(storage->accept_head != NULL && storage->accept_head == storage->accept_tail);

    client_descriptor = connect_client(listener_address, &client_address);
    R_TEST_CHECK(client_descriptor >= 0);
    R_TEST_CHECK(await_connection(accepted, &connection));
    R_TEST_CHECK(connection.r_tag == UINT32_C(0) &&
                 connection.value.peer.port == client_address.port);
    R_TEST_CHECK(close(client_descriptor) == 0);
    r_std_net_tcp_stream_destroy(&connection.value.stream);
    R_TEST_CHECK(wait_for_listener_reference_count(storage, 1U));
    R_TEST_CHECK(storage->accept_head == NULL && storage->accept_tail == NULL);
    r_std_net_tcp_listener_destroy(&listener.value);
    return 0;
}

static int test_fifo_submission_order(void) {
    RStdNetTcpListenerResult listener = {0};
    RStdNetTcpConnectionResult first_connection = {0};
    RStdNetTcpConnectionResult second_connection = {0};
    RStdNetSocketAddress listener_address = {0};
    RStdNetSocketAddress first_client_address = {0};
    RStdNetSocketAddress second_client_address = {0};
    RStdNetTaskStartResult first;
    RStdNetTaskStartResult second;
    RStdNetTcpListenerStorage *storage;
    int first_client;
    int second_client;

    R_TEST_CHECK(create_listener(&listener, &listener_address));
    storage = listener.value.storage;
    first = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
    second = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
    R_TEST_CHECK(first.is_ok && first.task != NULL && second.is_ok && second.task != NULL);
    R_TEST_CHECK(storage->accept_head != NULL && storage->accept_tail != NULL &&
                 storage->accept_head != storage->accept_tail);

    first_client = connect_client(listener_address, &first_client_address);
    R_TEST_CHECK(first_client >= 0);
    R_TEST_CHECK(await_connection(first, &first_connection));
    R_TEST_CHECK(first_connection.r_tag == UINT32_C(0));
    R_TEST_CHECK(socket_address_equal(first_connection.value.peer, first_client_address));
    R_TEST_CHECK(r_runtime_task_state(second.task) == R_RUNTIME_TASK_RUNNING);
    R_TEST_CHECK(storage->accept_head != NULL && storage->accept_head == storage->accept_tail);

    second_client = connect_client(listener_address, &second_client_address);
    R_TEST_CHECK(second_client >= 0);
    R_TEST_CHECK(first_client_address.port != second_client_address.port);
    R_TEST_CHECK(await_connection(second, &second_connection));
    R_TEST_CHECK(second_connection.r_tag == UINT32_C(0));
    R_TEST_CHECK(socket_address_equal(second_connection.value.peer, second_client_address));
    R_TEST_CHECK(wait_for_listener_reference_count(storage, 1U));
    R_TEST_CHECK(storage->accept_head == NULL && storage->accept_tail == NULL);

    R_TEST_CHECK(close(first_client) == 0);
    R_TEST_CHECK(close(second_client) == 0);
    r_std_net_tcp_stream_destroy(&first_connection.value.stream);
    r_std_net_tcp_stream_destroy(&second_connection.value.stream);
    r_std_net_tcp_listener_destroy(&listener.value);
    return 0;
}

static int test_cancel_readiness_races(void) {
    size_t iteration;

    for (iteration = 0U; iteration < 32U; ++iteration) {
        RStdNetTcpListenerResult listener = {0};
        RStdNetTcpConnectionResult second_connection = {0};
        RStdNetSocketAddress listener_address = {0};
        RStdNetSocketAddress first_client_address = {0};
        RStdNetSocketAddress second_client_address = {0};
        RStdNetTaskStartResult first;
        RStdNetTaskStartResult second;
        RStdNetTcpListenerStorage *storage;
        int first_client;
        int second_client;

        R_TEST_CHECK(create_listener(&listener, &listener_address));
        storage = listener.value.storage;
        first = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
        second = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
        R_TEST_CHECK(first.is_ok && first.task != NULL && second.is_ok && second.task != NULL);
        R_TEST_CHECK(storage->accept_head != storage->accept_tail);
        first_client = connect_client(listener_address, &first_client_address);
        R_TEST_CHECK(first_client >= 0);
        r_runtime_task_cancel(&first.task);
        R_TEST_CHECK(first.task == NULL);
        second_client = connect_client(listener_address, &second_client_address);
        R_TEST_CHECK(second_client >= 0);
        R_TEST_CHECK(await_connection(second, &second_connection));
        R_TEST_CHECK(second_connection.r_tag == UINT32_C(0));
        R_TEST_CHECK(socket_address_equal(second_connection.value.peer, first_client_address) ||
                     socket_address_equal(second_connection.value.peer, second_client_address));
        R_TEST_CHECK(wait_for_listener_reference_count(storage, 1U));
        R_TEST_CHECK(storage->accept_head == NULL && storage->accept_tail == NULL);
        R_TEST_CHECK(close(first_client) == 0);
        R_TEST_CHECK(close(second_client) == 0);
        r_std_net_tcp_stream_destroy(&second_connection.value.stream);
        r_std_net_tcp_listener_destroy(&listener.value);
    }
    return 0;
}

static int test_queued_deadline_does_not_activate_read(void) {
    RStdNetTcpListenerResult listener = {0};
    RStdNetTcpConnectionResult timed_out = {0};
    RStdNetSocketAddress listener_address = {0};
    RStdTimeInstantResult now;
    RStdTimeInstantResult deadline;
    RStdNetTaskStartResult first;
    RStdNetTaskStartResult second;
    RStdNetTcpListenerStorage *storage;

    R_TEST_CHECK(create_listener(&listener, &listener_address));
    storage = listener.value.storage;
    now = r_std_time_monotonic_now();
    R_TEST_CHECK(now.is_ok);
    deadline = r_std_time_instant_add(now.value, (RStdTimeDuration){0, UINT32_C(10000000)});
    R_TEST_CHECK(deadline.is_ok);
    first = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
    second = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){1, deadline.value});
    R_TEST_CHECK(first.is_ok && first.task != NULL && second.is_ok && second.task != NULL);
    R_TEST_CHECK(storage->accept_head != storage->accept_tail);
    R_TEST_CHECK(await_connection(second, &timed_out));
    R_TEST_CHECK(timed_out.r_tag == UINT32_C(1) &&
                 timed_out.error.code == R_STD_NET_ERROR_TIMED_OUT);
    R_TEST_CHECK(timed_out.error.native_code == 0);
    R_TEST_CHECK(r_runtime_task_state(first.task) == R_RUNTIME_TASK_RUNNING);
    R_TEST_CHECK(wait_for_listener_reference_count(storage, 2U));
    R_TEST_CHECK(storage->accept_head != NULL && storage->accept_head == storage->accept_tail);
    r_runtime_task_cancel(&first.task);
    R_TEST_CHECK(first.task == NULL);
    R_TEST_CHECK(wait_for_listener_reference_count(storage, 1U));
    R_TEST_CHECK(storage->accept_head == NULL && storage->accept_tail == NULL);
    r_std_net_tcp_listener_destroy(&listener.value);
    return 0;
}

static int test_deadline(void) {
    RStdNetTcpListenerResult listener = {0};
    RStdNetTcpConnectionResult connection = {0};
    RStdNetSocketAddress listener_address = {0};
    RStdTimeInstantResult now;
    RStdTimeInstantResult deadline;
    RStdNetTaskStartResult started;

    R_TEST_CHECK(create_listener(&listener, &listener_address));
    now = r_std_time_monotonic_now();
    R_TEST_CHECK(now.is_ok);
    deadline = r_std_time_instant_add(now.value, (RStdTimeDuration){0, UINT32_C(10000000)});
    R_TEST_CHECK(deadline.is_ok);
    started = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){1, deadline.value});
    R_TEST_CHECK(await_connection(started, &connection));
    R_TEST_CHECK(connection.r_tag == UINT32_C(1));
    R_TEST_CHECK(connection.error.code == R_STD_NET_ERROR_TIMED_OUT);
    R_TEST_CHECK(connection.error.native_code == 0);
    R_TEST_CHECK(wait_for_listener_reference_count(listener.value.storage, 1U));
    r_std_net_tcp_listener_destroy(&listener.value);
    return 0;
}

static int test_two_phase_allocation_failures(RRuntimeAllocator *allocator) {
    RStdNetTcpListenerResult listener = {0};
    RStdNetSocketAddress listener_address = {0};
    RStdNetTcpListenerStorage *storage;
    uint64_t fail_at;

    R_TEST_CHECK(create_listener(&listener, &listener_address));
    storage = listener.value.storage;
    for (fail_at = UINT64_C(1); fail_at <= UINT64_C(5); ++fail_at) {
        RStdNetTaskStartResult started;
        RStdNetSocketAddressResult observed;

        r_runtime_allocator_set_failure(allocator, fail_at);
        started = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
        R_TEST_CHECK(!started.is_ok && started.task == NULL);
        R_TEST_CHECK(started.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
        R_TEST_CHECK(r_runtime_allocator_attempt_count(allocator) == fail_at);
        R_TEST_CHECK(atomic_load_explicit(&storage->handle.references, memory_order_acquire) == 1U);
        R_TEST_CHECK(storage->accept_head == NULL && storage->accept_tail == NULL);
        observed = r_std_net_tcp_listener_local_address(&listener.value);
        R_TEST_CHECK(observed.is_ok && observed.value.port == listener_address.port);
    }
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    r_std_net_tcp_listener_destroy(&listener.value);
    return 0;
}

int main(void) {
    RRuntimeAllocator allocator;
    RStdNetTcpListenerResult listener = {0};
    RStdNetSocketAddress listener_address = {0};
    RStdNetTaskStartResult stopped;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    R_TEST_CHECK(test_two_phase_allocation_failures(&allocator) == 0);
    R_TEST_CHECK(test_success_retains_listener() == 0);
    R_TEST_CHECK(test_cancellation_acknowledges_retain() == 0);
    R_TEST_CHECK(test_fifo_submission_order() == 0);
    R_TEST_CHECK(test_cancel_readiness_races() == 0);
    R_TEST_CHECK(test_queued_deadline_does_not_activate_read() == 0);
    R_TEST_CHECK(test_deadline() == 0);
    R_TEST_CHECK(create_listener(&listener, &listener_address));
    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());

    stopped = r_std_net_tcp_accept(&listener.value, (RStdNetDeadline){0});
    R_TEST_CHECK(!stopped.is_ok && stopped.task == NULL);
    R_TEST_CHECK(stopped.error == R_STD_ASYNC_START_RUNTIME_STOPPING);
    r_std_net_tcp_listener_destroy(&listener.value);
    (void)puts("library_net_accept_tests: ok");
    return EXIT_SUCCESS;
}
