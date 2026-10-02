#include "r_library_net_internal.h"
#include "r_runtime_allocator.h"
#include "r_runtime_darwin_io.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"
#include "r_std_net.h"
#include "r_std_time.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
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

typedef struct RTestTcpPair {
    RStdNetTcpStream stream;
    int peer;
} RTestTcpPair;

typedef struct RTestUdpEndpoint {
    RStdNetUdpSocket socket;
    RStdNetSocketAddress address;
} RTestUdpEndpoint;

typedef struct RTestStopContext {
    _Bool stopped;
} RTestStopContext;

static int configure_descriptor(int descriptor) {
    int descriptor_flags = fcntl(descriptor, F_GETFD);
    int status_flags = fcntl(descriptor, F_GETFL);

    if (descriptor_flags < 0 || status_flags < 0 ||
        fcntl(descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) != 0 ||
        fcntl(descriptor, F_SETFL, status_flags | O_NONBLOCK) != 0) {
        return 0;
    }
#if defined(SO_NOSIGPIPE)
    {
        const int enabled = 1;

        if (setsockopt(descriptor, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) {
            return 0;
        }
    }
#endif
    return 1;
}

static RTestTcpPair create_tcp_pair(RRuntimeAllocator *allocator) {
    struct sockaddr_in address = {0};
    socklen_t address_length = (socklen_t)sizeof(address);
    RStdNetTcpStreamStorage *storage;
    RTestTcpPair pair = {{NULL}, -1};
    int listener = -1;
    int accepted = -1;
    int peer = -1;
    int enabled = 1;

    listener = socket(AF_INET, SOCK_STREAM, 0);
    if (listener < 0 ||
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled)) != 0) {
        goto cleanup;
    }
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, (const struct sockaddr *)&address, (socklen_t)sizeof(address)) != 0 ||
        getsockname(listener, (struct sockaddr *)&address, &address_length) != 0 ||
        listen(listener, 8) != 0) {
        goto cleanup;
    }
    peer = socket(AF_INET, SOCK_STREAM, 0);
    if (peer < 0 ||
        connect(peer, (const struct sockaddr *)&address, (socklen_t)sizeof(address)) != 0) {
        goto cleanup;
    }
    accepted = accept(listener, NULL, NULL);
    if (accepted < 0 || !configure_descriptor(accepted)) {
        goto cleanup;
    }
    storage = r_library_internal_net_tcp_stream_reserve(allocator);
    if (storage == NULL) {
        goto cleanup;
    }
    r_library_internal_net_tcp_stream_publish(storage, accepted);
    accepted = -1;
    pair.stream.storage = storage;
    pair.peer = peer;
    peer = -1;

cleanup:
    if (accepted >= 0) {
        (void)close(accepted);
    }
    if (peer >= 0) {
        (void)close(peer);
    }
    if (listener >= 0) {
        (void)close(listener);
    }
    return pair;
}

static void destroy_tcp_pair(RTestTcpPair *pair) {
    r_std_net_tcp_stream_destroy(&pair->stream);
    if (pair->peer >= 0) {
        (void)close(pair->peer);
        pair->peer = -1;
    }
}

static RStdNetSocketAddress native_local_address(int descriptor) {
    struct sockaddr_in native = {0};
    socklen_t native_length = (socklen_t)sizeof(native);
    RStdNetSocketAddress address = {0};

    if (getsockname(descriptor, (struct sockaddr *)&native, &native_length) != 0 ||
        native_length != (socklen_t)sizeof(native) || native.sin_family != AF_INET) {
        abort();
    }
    address.address.kind = R_STD_NET_IP_ADDRESS_V4;
    (void)memcpy(address.address.bytes.v4, &native.sin_addr, sizeof(native.sin_addr));
    address.port = ntohs(native.sin_port);
    return address;
}

static int create_udp_endpoint(RRuntimeAllocator *allocator, RTestUdpEndpoint *endpoint) {
    struct sockaddr_in local = {0};
    RStdNetUdpSocketStorage *storage;
    int descriptor = socket(AF_INET, SOCK_DGRAM, 0);

    if (descriptor < 0 || !configure_descriptor(descriptor)) {
        if (descriptor >= 0) {
            (void)close(descriptor);
        }
        return 0;
    }
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(descriptor, (const struct sockaddr *)&local, (socklen_t)sizeof(local)) != 0) {
        (void)close(descriptor);
        return 0;
    }
    storage = r_library_internal_net_udp_socket_reserve(allocator);
    if (storage == NULL) {
        (void)close(descriptor);
        return 0;
    }
    endpoint->address = native_local_address(descriptor);
    r_library_internal_net_udp_socket_publish(storage, descriptor);
    endpoint->socket.storage = storage;
    return 1;
}

static _Bool socket_address_equal(RStdNetSocketAddress left, RStdNetSocketAddress right) {
    return left.address.kind == right.address.kind && left.port == right.port &&
           left.scope_id == right.scope_id &&
           memcmp(left.address.bytes.v4, right.address.bytes.v4, sizeof(left.address.bytes.v4)) ==
               0;
}

static int send_exact(int descriptor, const uint8_t *bytes, size_t length) {
    size_t offset = 0U;

    while (offset != length) {
        ssize_t count = send(descriptor, bytes + offset, length - offset, 0);

        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return 0;
        }
        offset += (size_t)count;
    }
    return 1;
}

static int receive_exact(int descriptor, uint8_t *bytes, size_t length) {
    size_t offset = 0U;

    while (offset != length) {
        ssize_t count = recv(descriptor, bytes + offset, length - offset, 0);

        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return 0;
        }
        offset += (size_t)count;
    }
    return 1;
}

static int await_count(RStdNetTaskStartResult started, RStdNetCountResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static int await_void(RStdNetTaskStartResult started, RStdNetVoidResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static int await_datagram(RStdNetTaskStartResult started, RStdNetDatagramResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static int all_bytes_equal(const uint8_t *bytes, size_t length, uint8_t expected) {
    size_t index;

    for (index = 0U; index != length; ++index) {
        if (bytes[index] != expected) {
            return 0;
        }
    }
    return 1;
}

static void probe_task_body(RRuntimeTaskExecution *execution, void *payload, void *result) {
    (void)execution;
    (void)payload;
    (void)result;
}

static void *stop_executor(void *context_pointer) {
    RTestStopContext *context = context_pointer;

    context->stopped = r_runtime_executor_lifecycle_stop();
    return NULL;
}

static int wait_for_executor_stopping(void) {
    const RRuntimeTypeInfo empty_type = {0U, 1U, NULL, NULL};

    for (;;) {
        RRuntimeTaskPrepareResult prepared =
            r_runtime_task_start_prepare(empty_type, empty_type, probe_task_body);

        if (prepared.status == R_RUNTIME_TASK_START_RUNTIME_STOPPING) {
            return 1;
        }
        if (prepared.status != R_RUNTIME_TASK_START_OK || prepared.transaction == NULL) {
            return 0;
        }
        r_runtime_task_start_abort(&prepared.transaction);
        (void)sched_yield();
    }
}

static int begin_executor_stop(pthread_t *thread, RTestStopContext *context) {
    context->stopped = 0;
    return pthread_create(thread, NULL, stop_executor, context) == 0 &&
           wait_for_executor_stopping();
}

static int finish_executor_stop(pthread_t thread, RTestStopContext *context) {
    return pthread_join(thread, NULL) == 0 && context->stopped;
}

static int test_tcp_round_trip(RRuntimeAllocator *allocator) {
    static const uint8_t outbound[] = {UINT8_C(0x10), UINT8_C(0x11), UINT8_C(0x12), UINT8_C(0x13)};
    static const uint8_t inbound[] = {UINT8_C(0x61), UINT8_C(0x62), UINT8_C(0x63)};
    const RStdNetDeadline expired = {1, {0, 0U}};
    RTestTcpPair pair = create_tcp_pair(allocator);
    uint8_t observed[sizeof(outbound)] = {0};
    uint8_t target[8];
    RStdNetVoidResult void_result = {0};
    RStdNetCountResult count_result = {0};

    R_TEST_CHECK(pair.stream.storage != NULL && pair.peer >= 0);

    /* write_all_from keeps borrowing the immutable source until the void result is published. */
    R_TEST_CHECK(await_void(
        r_std_net_tcp_write_all_from(
            &pair.stream, (RStdNetConstBytes){outbound, sizeof(outbound)}, (RStdNetDeadline){0}),
        &void_result));
    R_TEST_CHECK(void_result.r_tag == UINT32_C(0));
    R_TEST_CHECK(receive_exact(pair.peer, observed, sizeof(observed)) &&
                 memcmp(observed, outbound, sizeof(outbound)) == 0);

    /* write_from reports one positive prefix. */
    R_TEST_CHECK(await_count(r_std_net_tcp_write_from(&pair.stream,
                                                      (RStdNetConstBytes){outbound, 2U},
                                                      (RStdNetDeadline){0}),
                             &count_result));
    R_TEST_CHECK(count_result.r_tag == UINT32_C(0) && count_result.r_payload.r_value == 2U);
    R_TEST_CHECK(receive_exact(pair.peer, observed, 2U) && memcmp(observed, outbound, 2U) == 0);

    /* read_into places the first nonempty chunk and leaves the tail untouched. */
    (void)memset(target, 0xcc, sizeof(target));
    R_TEST_CHECK(send_exact(pair.peer, inbound, sizeof(inbound)));
    R_TEST_CHECK(await_count(r_std_net_tcp_read_into(&pair.stream,
                                                     (RStdNetMutableBytes){target, sizeof(target)},
                                                     (RStdNetDeadline){0}),
                             &count_result));
    R_TEST_CHECK(count_result.r_tag == UINT32_C(0) &&
                 count_result.r_payload.r_value == sizeof(inbound));
    R_TEST_CHECK(
        memcmp(target, inbound, sizeof(inbound)) == 0 &&
        all_bytes_equal(target + sizeof(inbound), sizeof(target) - sizeof(inbound), UINT8_C(0xcc)));

    /* A zero-length target is a specified no-op success without native submission. */
    R_TEST_CHECK(await_count(
        r_std_net_tcp_read_into(&pair.stream, (RStdNetMutableBytes){target, 0U}, expired),
        &count_result));
    R_TEST_CHECK(count_result.r_tag == UINT32_C(0) && count_result.r_payload.r_value == 0U);
    R_TEST_CHECK(await_count(
        r_std_net_tcp_write_from(&pair.stream, (RStdNetConstBytes){outbound, 0U}, expired),
        &count_result));
    R_TEST_CHECK(count_result.r_tag == UINT32_C(0) && count_result.r_payload.r_value == 0U);
    R_TEST_CHECK(await_void(
        r_std_net_tcp_write_all_from(&pair.stream, (RStdNetConstBytes){outbound, 0U}, expired),
        &void_result));
    R_TEST_CHECK(void_result.r_tag == UINT32_C(0));

    /* An expired deadline fails before native submission and never touches target. */
    (void)memset(target, 0xdd, sizeof(target));
    R_TEST_CHECK(await_count(r_std_net_tcp_read_into(&pair.stream,
                                                     (RStdNetMutableBytes){target, sizeof(target)},
                                                     expired),
                             &count_result));
    R_TEST_CHECK(count_result.r_tag == UINT32_C(1) &&
                 count_result.r_payload.r_error_00000001.code == R_STD_NET_ERROR_TIMED_OUT);
    R_TEST_CHECK(all_bytes_equal(target, sizeof(target), UINT8_C(0xdd)));

    /* End of stream completes with count 0 and leaves target untouched. */
    R_TEST_CHECK(shutdown(pair.peer, SHUT_WR) == 0);
    R_TEST_CHECK(await_count(r_std_net_tcp_read_into(&pair.stream,
                                                     (RStdNetMutableBytes){target, sizeof(target)},
                                                     (RStdNetDeadline){0}),
                             &count_result));
    R_TEST_CHECK(count_result.r_tag == UINT32_C(0) && count_result.r_payload.r_value == 0U);
    R_TEST_CHECK(all_bytes_equal(target, sizeof(target), UINT8_C(0xdd)));
    destroy_tcp_pair(&pair);
    return 0;
}

/*
 * Cancellation acknowledgement ordering: while the native terminal callback is held back, the
 * task is still running (no result observable) and the borrowed target is untouched; only after
 * the native backend releases the request is the cancellation acknowledged.
 */
static int test_tcp_read_into_cancel_acknowledgement(RRuntimeAllocator *allocator) {
    RTestTcpPair pair = create_tcp_pair(allocator);
    uint8_t target[4];
    RStdNetTaskStartResult started;
    RStdNetCountResult result = {0};
    RTestStopContext stop_context;
    pthread_t stop_thread;

    R_TEST_CHECK(pair.stream.storage != NULL);
    (void)memset(target, 0xa5, sizeof(target));
    r_runtime_darwin_io_testing_pause_next_native_completion();
    started = r_std_net_tcp_read_into(
        &pair.stream, (RStdNetMutableBytes){target, sizeof(target)}, (RStdNetDeadline){0});
    R_TEST_CHECK(started.is_ok && started.task != NULL);
    R_TEST_CHECK(r_runtime_task_state(started.task) == R_RUNTIME_TASK_RUNNING);
    r_library_internal_net_tcp_read_testing_arm_cancel_acknowledgement();
    R_TEST_CHECK(begin_executor_stop(&stop_thread, &stop_context));
    r_runtime_darwin_io_testing_wait_for_native_completion();
    R_TEST_CHECK(r_runtime_task_state(started.task) == R_RUNTIME_TASK_RUNNING);
    R_TEST_CHECK(all_bytes_equal(target, sizeof(target), UINT8_C(0xa5)));
    r_runtime_darwin_io_testing_release_native_completion();
    r_library_internal_net_tcp_read_testing_wait_cancel_acknowledgement();
    R_TEST_CHECK(finish_executor_stop(stop_thread, &stop_context));
    R_TEST_CHECK(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_CANCELLED &&
                 started.task == NULL);
    R_TEST_CHECK(all_bytes_equal(target, sizeof(target), UINT8_C(0xa5)));
    destroy_tcp_pair(&pair);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(allocator) == R_RUNTIME_EXECUTOR_START_OK);
    return 0;
}

static int test_udp_round_trip(RRuntimeAllocator *allocator) {
    static const uint8_t payload_bytes[] = {UINT8_C(0x21), UINT8_C(0x22), UINT8_C(0x23)};
    RTestUdpEndpoint first = {{NULL}, {0}};
    RTestUdpEndpoint second = {{NULL}, {0}};
    uint8_t target[8];
    uint8_t short_target[2];
    RStdNetTaskStartResult receive_started;
    RStdNetVoidResult send_result = {0};
    RStdNetDatagramResult receive_result = {0};

    R_TEST_CHECK(create_udp_endpoint(allocator, &first) && create_udp_endpoint(allocator, &second));
    (void)memset(target, 0xcc, sizeof(target));
    receive_started = r_std_net_udp_receive_into(
        &second.socket, (RStdNetMutableBytes){target, sizeof(target)}, (RStdNetDeadline){0});
    R_TEST_CHECK(receive_started.is_ok && receive_started.task != NULL);
    R_TEST_CHECK(await_void(
        r_std_net_udp_send_from(&first.socket,
                                second.address,
                                (RStdNetConstBytes){payload_bytes, sizeof(payload_bytes)},
                                (RStdNetDeadline){0}),
        &send_result));
    R_TEST_CHECK(send_result.r_tag == UINT32_C(0));
    R_TEST_CHECK(await_datagram(receive_started, &receive_result));
    R_TEST_CHECK(receive_result.r_tag == UINT32_C(0) &&
                 receive_result.r_payload.r_value.count == sizeof(payload_bytes) &&
                 !receive_result.r_payload.r_value.truncated &&
                 socket_address_equal(receive_result.r_payload.r_value.peer, first.address));
    R_TEST_CHECK(memcmp(target, payload_bytes, sizeof(payload_bytes)) == 0 &&
                 all_bytes_equal(target + sizeof(payload_bytes),
                                 sizeof(target) - sizeof(payload_bytes),
                                 UINT8_C(0xcc)));

    /* Truncation copies only the prefix that fits and reports it. */
    (void)memset(short_target, 0xdd, sizeof(short_target));
    receive_started =
        r_std_net_udp_receive_into(&second.socket,
                                   (RStdNetMutableBytes){short_target, sizeof(short_target)},
                                   (RStdNetDeadline){0});
    R_TEST_CHECK(await_void(
        r_std_net_udp_send_from(&first.socket,
                                second.address,
                                (RStdNetConstBytes){payload_bytes, sizeof(payload_bytes)},
                                (RStdNetDeadline){0}),
        &send_result));
    R_TEST_CHECK(send_result.r_tag == UINT32_C(0));
    R_TEST_CHECK(await_datagram(receive_started, &receive_result));
    R_TEST_CHECK(receive_result.r_tag == UINT32_C(0) &&
                 receive_result.r_payload.r_value.count == sizeof(short_target) &&
                 receive_result.r_payload.r_value.truncated &&
                 memcmp(short_target, payload_bytes, sizeof(short_target)) == 0);

    /* A zero-length send is still one real datagram. */
    (void)memset(target, 0xee, sizeof(target));
    receive_started = r_std_net_udp_receive_into(
        &second.socket, (RStdNetMutableBytes){target, sizeof(target)}, (RStdNetDeadline){0});
    R_TEST_CHECK(await_void(
        r_std_net_udp_send_from(
            &first.socket, second.address, (RStdNetConstBytes){NULL, 0U}, (RStdNetDeadline){0}),
        &send_result));
    R_TEST_CHECK(send_result.r_tag == UINT32_C(0));
    R_TEST_CHECK(await_datagram(receive_started, &receive_result));
    R_TEST_CHECK(receive_result.r_tag == UINT32_C(0) &&
                 receive_result.r_payload.r_value.count == 0U &&
                 !receive_result.r_payload.r_value.truncated &&
                 all_bytes_equal(target, sizeof(target), UINT8_C(0xee)));

    /* An expired deadline fails before native submission and never touches target. */
    R_TEST_CHECK(
        await_datagram(r_std_net_udp_receive_into(&second.socket,
                                                  (RStdNetMutableBytes){target, sizeof(target)},
                                                  (RStdNetDeadline){1, {0, 0U}}),
                       &receive_result));
    R_TEST_CHECK(receive_result.r_tag == UINT32_C(1) &&
                 receive_result.r_payload.r_error_00000001.code == R_STD_NET_ERROR_TIMED_OUT &&
                 all_bytes_equal(target, sizeof(target), UINT8_C(0xee)));
    r_std_net_udp_socket_destroy(&first.socket);
    r_std_net_udp_socket_destroy(&second.socket);
    return 0;
}

int main(void) {
    RRuntimeAllocator allocator;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    R_TEST_CHECK(test_tcp_round_trip(&allocator) == 0);
    R_TEST_CHECK(test_tcp_read_into_cancel_acknowledgement(&allocator) == 0);
    R_TEST_CHECK(test_udp_round_trip(&allocator) == 0);
    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
    return 0;
}
