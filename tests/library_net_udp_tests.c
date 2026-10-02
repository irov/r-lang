#include "r_library_net_internal.h"
#include "r_runtime_allocator.h"
#include "r_runtime_array.h"
#include "r_runtime_darwin_socket.h"
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
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct RTestUdpEndpoint {
    RStdNetUdpSocket socket;
    RStdNetSocketAddress address;
} RTestUdpEndpoint;

typedef struct RTestUdpPair {
    RTestUdpEndpoint first;
    RTestUdpEndpoint second;
} RTestUdpPair;

typedef struct RTestCancelContext {
    RRuntimeTask **task;
    _Atomic _Bool completed;
} RTestCancelContext;

static int configure_datagram_descriptor(int descriptor) {
    int descriptor_flags = fcntl(descriptor, F_GETFD);
    int status_flags = fcntl(descriptor, F_GETFL);

    return descriptor_flags >= 0 && status_flags >= 0 &&
           fcntl(descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) == 0 &&
           fcntl(descriptor, F_SETFL, status_flags | O_NONBLOCK) == 0;
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

static int create_native_endpoint(RRuntimeAllocator *allocator, RTestUdpEndpoint *endpoint) {
    struct sockaddr_in local = {0};
    RStdNetUdpSocketStorage *storage;
    int descriptor = socket(AF_INET, SOCK_DGRAM, 0);

    if (descriptor < 0 || !configure_datagram_descriptor(descriptor)) {
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

static RTestUdpPair create_udp_pair(RRuntimeAllocator *allocator) {
    RTestUdpPair pair = {0};

    if (!create_native_endpoint(allocator, &pair.first) ||
        !create_native_endpoint(allocator, &pair.second)) {
        r_std_net_udp_socket_destroy(&pair.first.socket);
        r_std_net_udp_socket_destroy(&pair.second.socket);
    }
    return pair;
}

static void destroy_udp_pair(RTestUdpPair *pair) {
    if (pair == NULL) {
        return;
    }
    r_std_net_udp_socket_destroy(&pair->first.socket);
    r_std_net_udp_socket_destroy(&pair->second.socket);
}

static RRuntimeArray byte_array(RRuntimeAllocator *allocator, size_t length, uint8_t fill) {
    const RRuntimeTypeInfo byte_type = {
        sizeof(uint8_t),
        _Alignof(uint8_t),
        NULL,
        NULL,
    };
    RRuntimeArray array;

    if (r_runtime_array_with_capacity(&array, allocator, byte_type, length) != R_RUNTIME_ARRAY_OK) {
        abort();
    }
    array.length = length;
    if (length != 0U) {
        (void)memset(array.data, (int)fill, length);
    }
    return array;
}

static int await_send(RStdNetTaskStartResult started, RStdNetUdpSendResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static int await_receive(RStdNetTaskStartResult started, RStdNetUdpReceiveResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static int await_close(RStdNetTaskStartResult started, RStdNetVoidResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static RStdNetDeadline deadline_after_milliseconds(uint32_t milliseconds) {
    RStdTimeInstantResult now = r_std_time_monotonic_now();
    RStdTimeDurationResult duration =
        r_std_time_duration_from_parts((int64_t)(milliseconds / UINT32_C(1000)),
                                       (milliseconds % UINT32_C(1000)) * UINT32_C(1000000));
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

static RStdNetDeadline expired_deadline(void) {
    RStdTimeInstantResult now = r_std_time_monotonic_now();

    if (!now.is_ok) {
        abort();
    }
    return (RStdNetDeadline){1, now.value};
}

static _Bool socket_address_equal(RStdNetSocketAddress left, RStdNetSocketAddress right) {
    return left.address.kind == right.address.kind && left.port == right.port &&
           left.scope_id == right.scope_id &&
           memcmp(left.address.bytes.v4, right.address.bytes.v4, sizeof(left.address.bytes.v4)) ==
               0;
}

static int
native_send_to(int descriptor, RStdNetSocketAddress peer, const void *bytes, size_t size) {
    struct sockaddr_in native = {0};
    ssize_t count;

    native.sin_family = AF_INET;
    native.sin_port = htons(peer.port);
    (void)memcpy(&native.sin_addr, peer.address.bytes.v4, sizeof(native.sin_addr));
    do {
        count = sendto(descriptor,
                       bytes,
                       size,
                       0,
                       (const struct sockaddr *)&native,
                       (socklen_t)sizeof(native));
    } while (count < 0 && errno == EINTR);
    return count >= 0 && (size_t)count == size;
}

static int native_receive(int descriptor, void *bytes, size_t capacity, size_t *size) {
    size_t attempt;

    for (attempt = 0U; attempt != 1000000U; ++attempt) {
        ssize_t count = recvfrom(descriptor, bytes, capacity, 0, NULL, NULL);

        if (count >= 0) {
            *size = (size_t)count;
            return 1;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            return 0;
        }
        (void)sched_yield();
    }
    return 0;
}

static int native_has_no_datagram(int descriptor) {
    uint8_t byte = 0U;
    ssize_t count;
    int native_error;

    errno = 0;
    count = recvfrom(descriptor, &byte, sizeof(byte), MSG_PEEK, NULL, NULL);
    native_error = errno;
    return count < 0 && (native_error == EAGAIN || native_error == EWOULDBLOCK);
}

static int wait_descriptor_closed(int descriptor) {
    size_t attempt;

    for (attempt = 0U; attempt != 1000000U; ++attempt) {
        if (fcntl(descriptor, F_GETFD) < 0 && errno == EBADF) {
            return 1;
        }
        (void)sched_yield();
    }
    return 0;
}

static void *cancel_task(void *context_pointer) {
    RTestCancelContext *context = context_pointer;

    r_runtime_task_cancel(context->task);
    atomic_store_explicit(&context->completed, 1, memory_order_release);
    return NULL;
}

static int test_send_receive_and_truncation(RRuntimeAllocator *allocator) {
    static const uint8_t payload_bytes[] = {UINT8_C(0x11), UINT8_C(0x22), UINT8_C(0x33)};
    RTestUdpPair pair = create_udp_pair(allocator);
    RRuntimeArray send_buffer = byte_array(allocator, sizeof(payload_bytes), UINT8_C(0));
    RRuntimeArray receive_buffer = byte_array(allocator, 8U, UINT8_C(0xcc));
    RStdNetUdpSendResult send_result = {0};
    RStdNetUdpReceiveResult receive_result = {0};
    RStdNetTaskStartResult receive_started;
    size_t index;

    R_TEST_CHECK(pair.first.socket.storage != NULL && pair.second.socket.storage != NULL);
    (void)memcpy(send_buffer.data, payload_bytes, sizeof(payload_bytes));
    receive_started =
        r_std_net_udp_receive_from(&pair.second.socket, &receive_buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(receive_started.is_ok && receive_buffer.data == NULL);
    R_TEST_CHECK(
        await_send(r_std_net_udp_send_to(
                       &pair.first.socket, pair.second.address, &send_buffer, (RStdNetDeadline){0}),
                   &send_result));
    R_TEST_CHECK(send_result.kind == R_STD_NET_UDP_SEND_RESULT_SENT &&
                 memcmp(send_result.buffer.data, payload_bytes, sizeof(payload_bytes)) == 0);
    R_TEST_CHECK(await_receive(receive_started, &receive_result));
    R_TEST_CHECK(receive_result.kind == R_STD_NET_UDP_RECEIVE_RESULT_RECEIVED &&
                 receive_result.count == sizeof(payload_bytes) && !receive_result.truncated &&
                 socket_address_equal(receive_result.peer, pair.first.address));
    R_TEST_CHECK(memcmp(receive_result.buffer.data, payload_bytes, sizeof(payload_bytes)) == 0);
    for (index = sizeof(payload_bytes); index != receive_result.buffer.length; ++index) {
        R_TEST_CHECK(((const uint8_t *)receive_result.buffer.data)[index] == UINT8_C(0xcc));
    }
    r_runtime_array_destroy(&send_result.buffer);
    r_runtime_array_destroy(&receive_result.buffer);

    send_buffer = byte_array(allocator, 4U, UINT8_C(0x7a));
    receive_buffer = byte_array(allocator, 2U, UINT8_C(0xdd));
    receive_started =
        r_std_net_udp_receive_from(&pair.second.socket, &receive_buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(
        await_send(r_std_net_udp_send_to(
                       &pair.first.socket, pair.second.address, &send_buffer, (RStdNetDeadline){0}),
                   &send_result));
    R_TEST_CHECK(await_receive(receive_started, &receive_result));
    R_TEST_CHECK(receive_result.kind == R_STD_NET_UDP_RECEIVE_RESULT_RECEIVED &&
                 receive_result.count == 2U && receive_result.truncated &&
                 ((const uint8_t *)receive_result.buffer.data)[0] == UINT8_C(0x7a) &&
                 ((const uint8_t *)receive_result.buffer.data)[1] == UINT8_C(0x7a));
    r_runtime_array_destroy(&send_result.buffer);
    r_runtime_array_destroy(&receive_result.buffer);
    destroy_udp_pair(&pair);
    return 0;
}

static int test_empty_datagrams(RRuntimeAllocator *allocator) {
    const uint8_t byte = UINT8_C(0xa7);
    RTestUdpPair pair = create_udp_pair(allocator);
    RRuntimeArray send_buffer = byte_array(allocator, 0U, UINT8_C(0));
    RRuntimeArray receive_buffer = byte_array(allocator, 1U, UINT8_C(0xee));
    RStdNetUdpSendResult send_result = {0};
    RStdNetUdpReceiveResult receive_result = {0};
    RStdNetTaskStartResult receive_started;

    R_TEST_CHECK(pair.first.socket.storage != NULL && pair.second.socket.storage != NULL);
    receive_started =
        r_std_net_udp_receive_from(&pair.second.socket, &receive_buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(
        await_send(r_std_net_udp_send_to(
                       &pair.first.socket, pair.second.address, &send_buffer, (RStdNetDeadline){0}),
                   &send_result));
    R_TEST_CHECK(send_result.kind == R_STD_NET_UDP_SEND_RESULT_SENT &&
                 send_result.buffer.length == 0U);
    R_TEST_CHECK(await_receive(receive_started, &receive_result));
    R_TEST_CHECK(receive_result.kind == R_STD_NET_UDP_RECEIVE_RESULT_RECEIVED &&
                 receive_result.count == 0U && !receive_result.truncated &&
                 ((const uint8_t *)receive_result.buffer.data)[0] == UINT8_C(0xee));
    r_runtime_array_destroy(&send_result.buffer);
    r_runtime_array_destroy(&receive_result.buffer);

    /* Both the datagram and destination may be empty: this consumes one datagram
     * without reporting truncation, and the following receive sees the next one. */
    send_buffer = byte_array(allocator, 0U, UINT8_C(0));
    receive_buffer = byte_array(allocator, 0U, UINT8_C(0));
    receive_started =
        r_std_net_udp_receive_from(&pair.second.socket, &receive_buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(
        await_send(r_std_net_udp_send_to(
                       &pair.first.socket, pair.second.address, &send_buffer, (RStdNetDeadline){0}),
                   &send_result));
    R_TEST_CHECK(await_receive(receive_started, &receive_result));
    R_TEST_CHECK(receive_result.kind == R_STD_NET_UDP_RECEIVE_RESULT_RECEIVED &&
                 receive_result.count == 0U && !receive_result.truncated &&
                 receive_result.buffer.length == 0U &&
                 receive_result.peer.port == pair.first.address.port);
    r_runtime_array_destroy(&send_result.buffer);
    r_runtime_array_destroy(&receive_result.buffer);

    send_buffer = byte_array(allocator, 1U, byte);
    receive_buffer = byte_array(allocator, 0U, UINT8_C(0));
    receive_started =
        r_std_net_udp_receive_from(&pair.second.socket, &receive_buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(
        await_send(r_std_net_udp_send_to(
                       &pair.first.socket, pair.second.address, &send_buffer, (RStdNetDeadline){0}),
                   &send_result));
    R_TEST_CHECK(await_receive(receive_started, &receive_result));
    R_TEST_CHECK(receive_result.kind == R_STD_NET_UDP_RECEIVE_RESULT_RECEIVED &&
                 receive_result.count == 0U && receive_result.truncated &&
                 receive_result.buffer.length == 0U);
    r_runtime_array_destroy(&send_result.buffer);
    r_runtime_array_destroy(&receive_result.buffer);
    destroy_udp_pair(&pair);
    return 0;
}

static int test_fifo_and_direction_independence(RRuntimeAllocator *allocator) {
    const uint8_t first_byte = UINT8_C(0x31);
    const uint8_t second_byte = UINT8_C(0x32);
    const uint8_t inbound_byte = UINT8_C(0x99);
    RTestUdpPair pair = create_udp_pair(allocator);
    RRuntimeArray first_buffer = byte_array(allocator, 1U, first_byte);
    RRuntimeArray second_buffer = byte_array(allocator, 1U, second_byte);
    RRuntimeArray receive_buffer = byte_array(allocator, 1U, UINT8_C(0));
    RStdNetTaskStartResult first_started;
    RStdNetTaskStartResult second_started;
    RStdNetTaskStartResult receive_started;
    RStdNetUdpSendResult first_result = {0};
    RStdNetUdpSendResult second_result = {0};
    RStdNetUdpReceiveResult receive_result = {0};
    uint8_t observed[2] = {0};
    size_t observed_size = 0U;

    R_TEST_CHECK(pair.first.socket.storage != NULL && pair.second.socket.storage != NULL);
    r_runtime_darwin_socket_testing_pause_next_datagram_before_native();
    first_started = r_std_net_udp_send_to(
        &pair.first.socket, pair.second.address, &first_buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(first_started.is_ok);
    r_runtime_darwin_socket_testing_wait_for_datagram_before_native();
    second_started = r_std_net_udp_send_to(
        &pair.first.socket, pair.second.address, &second_buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(second_started.is_ok &&
                 native_has_no_datagram(pair.second.socket.storage->handle.descriptor));

    receive_started =
        r_std_net_udp_receive_from(&pair.first.socket, &receive_buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(native_send_to(
        pair.second.socket.storage->handle.descriptor, pair.first.address, &inbound_byte, 1U));
    R_TEST_CHECK(await_receive(receive_started, &receive_result));
    R_TEST_CHECK(receive_result.kind == R_STD_NET_UDP_RECEIVE_RESULT_RECEIVED &&
                 receive_result.count == 1U &&
                 ((const uint8_t *)receive_result.buffer.data)[0] == inbound_byte);
    r_runtime_array_destroy(&receive_result.buffer);

    r_runtime_darwin_socket_testing_release_datagram_before_native();
    R_TEST_CHECK(await_send(first_started, &first_result) &&
                 await_send(second_started, &second_result));
    R_TEST_CHECK(native_receive(pair.second.socket.storage->handle.descriptor,
                                observed,
                                sizeof(observed),
                                &observed_size) &&
                 observed_size == 1U && observed[0] == first_byte);
    R_TEST_CHECK(native_receive(pair.second.socket.storage->handle.descriptor,
                                observed,
                                sizeof(observed),
                                &observed_size) &&
                 observed_size == 1U && observed[0] == second_byte);
    r_runtime_array_destroy(&first_result.buffer);
    r_runtime_array_destroy(&second_result.buffer);

    first_buffer = byte_array(allocator, 1U, UINT8_C(0xa1));
    second_buffer = byte_array(allocator, 1U, UINT8_C(0xa2));
    receive_started =
        r_std_net_udp_receive_from(&pair.first.socket, &first_buffer, (RStdNetDeadline){0});
    second_started =
        r_std_net_udp_receive_from(&pair.first.socket, &second_buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(
        native_send_to(
            pair.second.socket.storage->handle.descriptor, pair.first.address, &first_byte, 1U) &&
        native_send_to(
            pair.second.socket.storage->handle.descriptor, pair.first.address, &second_byte, 1U));
    R_TEST_CHECK(await_receive(receive_started, &receive_result));
    R_TEST_CHECK(receive_result.kind == R_STD_NET_UDP_RECEIVE_RESULT_RECEIVED &&
                 ((const uint8_t *)receive_result.buffer.data)[0] == first_byte);
    r_runtime_array_destroy(&receive_result.buffer);
    R_TEST_CHECK(await_receive(second_started, &receive_result));
    R_TEST_CHECK(receive_result.kind == R_STD_NET_UDP_RECEIVE_RESULT_RECEIVED &&
                 ((const uint8_t *)receive_result.buffer.data)[0] == second_byte);
    r_runtime_array_destroy(&receive_result.buffer);
    destroy_udp_pair(&pair);
    return 0;
}

static int test_deadline_and_native_failure_preserve_bytes(RRuntimeAllocator *allocator) {
    const uint8_t original_byte = UINT8_C(0x6d);
    const uint8_t pending_byte = UINT8_C(0x77);
    RTestUdpPair pair = create_udp_pair(allocator);
    RRuntimeArray buffer = byte_array(allocator, 1U, original_byte);
    RStdNetUdpReceiveResult receive_result = {0};
    RStdNetUdpSendResult send_result = {0};
    RStdNetTaskStartResult receive_started;

    R_TEST_CHECK(pair.first.socket.storage != NULL && pair.second.socket.storage != NULL);
    R_TEST_CHECK(
        await_receive(r_std_net_udp_receive_from(
                          &pair.first.socket, &buffer, deadline_after_milliseconds(UINT32_C(10))),
                      &receive_result));
    R_TEST_CHECK(receive_result.kind == R_STD_NET_UDP_RECEIVE_RESULT_FAILED &&
                 receive_result.error.code == R_STD_NET_ERROR_TIMED_OUT &&
                 ((const uint8_t *)receive_result.buffer.data)[0] == original_byte);
    r_runtime_array_destroy(&receive_result.buffer);

    buffer = byte_array(allocator, 1U, original_byte);
    receive_started = r_std_net_udp_receive_from(&pair.first.socket, &buffer, (RStdNetDeadline){0});
    r_runtime_darwin_socket_testing_fail_next_datagram_native(EIO);
    R_TEST_CHECK(native_send_to(
        pair.second.socket.storage->handle.descriptor, pair.first.address, &pending_byte, 1U));
    R_TEST_CHECK(await_receive(receive_started, &receive_result));
    R_TEST_CHECK(receive_result.kind == R_STD_NET_UDP_RECEIVE_RESULT_FAILED &&
                 receive_result.error.code == R_STD_NET_ERROR_OTHER &&
                 receive_result.error.native_code == EIO &&
                 ((const uint8_t *)receive_result.buffer.data)[0] == original_byte);
    r_runtime_array_destroy(&receive_result.buffer);
    {
        uint8_t drained = 0U;
        size_t drained_size = 0U;

        R_TEST_CHECK(native_receive(pair.first.socket.storage->handle.descriptor,
                                    &drained,
                                    sizeof(drained),
                                    &drained_size) &&
                     drained_size == 1U && drained == pending_byte);
    }

    buffer = byte_array(allocator, 1U, original_byte);
    r_runtime_darwin_socket_testing_fail_next_datagram_native(EMSGSIZE);
    R_TEST_CHECK(
        await_send(r_std_net_udp_send_to(
                       &pair.first.socket, pair.second.address, &buffer, (RStdNetDeadline){0}),
                   &send_result));
    R_TEST_CHECK(send_result.kind == R_STD_NET_UDP_SEND_RESULT_FAILED &&
                 send_result.error.code == R_STD_NET_ERROR_MESSAGE_TOO_LARGE &&
                 send_result.error.native_code == EMSGSIZE &&
                 ((const uint8_t *)send_result.buffer.data)[0] == original_byte &&
                 native_has_no_datagram(pair.second.socket.storage->handle.descriptor));
    r_runtime_array_destroy(&send_result.buffer);
    destroy_udp_pair(&pair);
    return 0;
}

static int test_queued_deadline_does_not_send(RRuntimeAllocator *allocator) {
    const uint8_t first_byte = UINT8_C(0xb1);
    const uint8_t second_byte = UINT8_C(0xb2);
    RTestUdpPair pair = create_udp_pair(allocator);
    RRuntimeArray first_buffer = byte_array(allocator, 1U, first_byte);
    RRuntimeArray second_buffer = byte_array(allocator, 1U, second_byte);
    RStdNetTaskStartResult first_started;
    RStdNetTaskStartResult second_started;
    RStdNetUdpSendResult first_result = {0};
    RStdNetUdpSendResult second_result = {0};
    uint8_t observed = 0U;
    size_t observed_size = 0U;

    R_TEST_CHECK(pair.first.socket.storage != NULL && pair.second.socket.storage != NULL);
    r_runtime_darwin_socket_testing_pause_next_datagram_before_native();
    first_started = r_std_net_udp_send_to(
        &pair.first.socket, pair.second.address, &first_buffer, (RStdNetDeadline){0});
    r_runtime_darwin_socket_testing_wait_for_datagram_before_native();
    second_started = r_std_net_udp_send_to(&pair.first.socket,
                                           pair.second.address,
                                           &second_buffer,
                                           deadline_after_milliseconds(UINT32_C(10)));
    R_TEST_CHECK(await_send(second_started, &second_result));
    R_TEST_CHECK(second_result.kind == R_STD_NET_UDP_SEND_RESULT_FAILED &&
                 second_result.error.code == R_STD_NET_ERROR_TIMED_OUT &&
                 ((const uint8_t *)second_result.buffer.data)[0] == second_byte);
    r_runtime_array_destroy(&second_result.buffer);
    r_runtime_darwin_socket_testing_release_datagram_before_native();
    R_TEST_CHECK(await_send(first_started, &first_result));
    R_TEST_CHECK(native_receive(pair.second.socket.storage->handle.descriptor,
                                &observed,
                                sizeof(observed),
                                &observed_size) &&
                 observed_size == 1U && observed == first_byte &&
                 native_has_no_datagram(pair.second.socket.storage->handle.descriptor));
    r_runtime_array_destroy(&first_result.buffer);
    destroy_udp_pair(&pair);
    return 0;
}

static int test_task_cancel_waits_for_native_ack(RRuntimeAllocator *allocator) {
    const uint8_t byte = UINT8_C(0xc4);
    RTestUdpPair pair = create_udp_pair(allocator);
    RRuntimeArray buffer = byte_array(allocator, 1U, UINT8_C(0x5a));
    RStdNetTaskStartResult started;
    RTestCancelContext cancel_context = {0};
    pthread_t cancel_thread;

    R_TEST_CHECK(pair.first.socket.storage != NULL && pair.second.socket.storage != NULL);
    r_runtime_darwin_socket_testing_pause_next_datagram_before_native();
    started = r_std_net_udp_receive_from(&pair.first.socket, &buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(native_send_to(
        pair.second.socket.storage->handle.descriptor, pair.first.address, &byte, 1U));
    r_runtime_darwin_socket_testing_wait_for_datagram_before_native();
    r_library_internal_net_udp_testing_arm_cancel_acknowledgement();
    cancel_context.task = &started.task;
    atomic_init(&cancel_context.completed, 0);
    R_TEST_CHECK(pthread_create(&cancel_thread, NULL, cancel_task, &cancel_context) == 0);
    r_library_internal_net_udp_testing_wait_cancel_reported();
    R_TEST_CHECK(atomic_load_explicit(&pair.first.socket.storage->handle.references,
                                      memory_order_acquire) == 2U);
    r_runtime_darwin_socket_testing_release_datagram_before_native();
    r_library_internal_net_udp_testing_wait_cancel_acknowledgement();
    R_TEST_CHECK(pthread_join(cancel_thread, NULL) == 0 && started.task == NULL &&
                 atomic_load_explicit(&cancel_context.completed, memory_order_acquire));
    destroy_udp_pair(&pair);
    return 0;
}

static int test_close_drains_and_deadline_cleanup(RRuntimeAllocator *allocator) {
    const uint8_t inbound = UINT8_C(0xd7);
    const uint8_t original = UINT8_C(0xe8);
    RTestUdpPair pair = create_udp_pair(allocator);
    RRuntimeArray buffer = byte_array(allocator, 1U, original);
    RStdNetTaskStartResult receive_started;
    RStdNetTaskStartResult close_started;
    RStdNetUdpReceiveResult receive_result = {0};
    RStdNetVoidResult close_result = {0};
    RStdNetUdpSocketStorage *storage;
    int descriptor;
    struct timespec pause = {0, 30000000L};

    R_TEST_CHECK(pair.first.socket.storage != NULL && pair.second.socket.storage != NULL);
    storage = pair.first.socket.storage;
    descriptor = storage->handle.descriptor;
    R_TEST_CHECK(r_library_internal_net_handle_retain(&storage->handle));
    r_runtime_darwin_socket_testing_pause_next_datagram_before_native();
    receive_started = r_std_net_udp_receive_from(&pair.first.socket, &buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(native_send_to(
        pair.second.socket.storage->handle.descriptor, pair.first.address, &inbound, 1U));
    r_runtime_darwin_socket_testing_wait_for_datagram_before_native();
    close_started =
        r_std_net_udp_close(&pair.first.socket, deadline_after_milliseconds(UINT32_C(10)));
    R_TEST_CHECK(close_started.is_ok && pair.first.socket.storage == NULL);
    (void)nanosleep(&pause, NULL);
    R_TEST_CHECK(fcntl(descriptor, F_GETFD) >= 0);
    r_runtime_darwin_socket_testing_release_datagram_before_native();
    R_TEST_CHECK(await_close(close_started, &close_result));
    R_TEST_CHECK(close_result.r_tag == UINT32_C(1) &&
                 close_result.error.code == R_STD_NET_ERROR_TIMED_OUT &&
                 wait_descriptor_closed(descriptor));
    R_TEST_CHECK(await_receive(receive_started, &receive_result));
    R_TEST_CHECK(receive_result.kind == R_STD_NET_UDP_RECEIVE_RESULT_FAILED &&
                 receive_result.error.code == R_STD_NET_ERROR_CANCELLED &&
                 ((const uint8_t *)receive_result.buffer.data)[0] == original);
    r_runtime_array_destroy(&receive_result.buffer);
    r_library_internal_net_handle_release(&storage->handle);
    destroy_udp_pair(&pair);
    return 0;
}

static int test_preexisting_close_failure_precedes_deadline(RRuntimeAllocator *allocator) {
    RTestUdpPair pair = create_udp_pair(allocator);
    RStdNetVoidResult close_result = {0};
    int descriptor;

    R_TEST_CHECK(pair.first.socket.storage != NULL && pair.second.socket.storage != NULL);
    descriptor = pair.first.socket.storage->handle.descriptor;
    R_TEST_CHECK(close(descriptor) == 0);
    R_TEST_CHECK(
        await_close(r_std_net_udp_close(&pair.first.socket, expired_deadline()), &close_result));
    R_TEST_CHECK(close_result.r_tag == UINT32_C(1) &&
                 close_result.error.code == R_STD_NET_ERROR_CLOSED &&
                 close_result.error.native_code == EBADF && pair.first.socket.storage == NULL);
    destroy_udp_pair(&pair);
    return 0;
}

static int test_cancelled_close_still_releases(RRuntimeAllocator *allocator) {
    const uint8_t inbound = UINT8_C(0xda);
    const uint8_t original = UINT8_C(0xdb);
    RTestUdpPair pair = create_udp_pair(allocator);
    RRuntimeArray buffer = byte_array(allocator, 1U, original);
    RStdNetTaskStartResult receive_started;
    RStdNetTaskStartResult close_started;
    RStdNetUdpReceiveResult receive_result = {0};
    RStdNetUdpSocketStorage *storage;
    int descriptor;

    R_TEST_CHECK(pair.first.socket.storage != NULL && pair.second.socket.storage != NULL);
    storage = pair.first.socket.storage;
    descriptor = storage->handle.descriptor;
    R_TEST_CHECK(r_library_internal_net_handle_retain(&storage->handle));
    r_runtime_darwin_socket_testing_pause_next_datagram_before_native();
    receive_started = r_std_net_udp_receive_from(&pair.first.socket, &buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(native_send_to(
        pair.second.socket.storage->handle.descriptor, pair.first.address, &inbound, 1U));
    r_runtime_darwin_socket_testing_wait_for_datagram_before_native();
    close_started = r_std_net_udp_close(&pair.first.socket, (RStdNetDeadline){0});
    R_TEST_CHECK(close_started.is_ok && close_started.task != NULL &&
                 pair.first.socket.storage == NULL && fcntl(descriptor, F_GETFD) >= 0);
    r_runtime_task_cancel(&close_started.task);
    R_TEST_CHECK(close_started.task == NULL && fcntl(descriptor, F_GETFD) >= 0);
    r_runtime_darwin_socket_testing_release_datagram_before_native();
    R_TEST_CHECK(wait_descriptor_closed(descriptor));
    R_TEST_CHECK(await_receive(receive_started, &receive_result));
    R_TEST_CHECK(receive_result.kind == R_STD_NET_UDP_RECEIVE_RESULT_FAILED &&
                 receive_result.error.code == R_STD_NET_ERROR_CANCELLED &&
                 ((const uint8_t *)receive_result.buffer.data)[0] == original);
    r_runtime_array_destroy(&receive_result.buffer);
    r_library_internal_net_handle_release(&storage->handle);
    destroy_udp_pair(&pair);
    return 0;
}

static int test_start_failure_preserves_operands(RRuntimeAllocator *allocator) {
    const uint8_t byte = UINT8_C(0xf1);
    RTestUdpPair pair = create_udp_pair(allocator);
    uint64_t fail_at;
    _Bool reached_success = 0;

    R_TEST_CHECK(pair.first.socket.storage != NULL && pair.second.socket.storage != NULL);
    for (fail_at = UINT64_C(1); fail_at != UINT64_C(20); ++fail_at) {
        RRuntimeArray buffer = byte_array(allocator, 1U, byte);
        RRuntimeArray unchanged = buffer;
        RStdNetTaskStartResult started;
        RStdNetUdpSendResult send_result = {0};
        uint8_t observed = 0U;
        size_t observed_size = 0U;

        r_runtime_allocator_set_failure(allocator, fail_at);
        started = r_std_net_udp_send_to(
            &pair.first.socket, pair.second.address, &buffer, (RStdNetDeadline){0});
        if (!started.is_ok) {
            R_TEST_CHECK(started.task == NULL &&
                         started.error == R_STD_ASYNC_START_ALLOCATION_FAILED &&
                         r_runtime_allocator_attempt_count(allocator) == fail_at &&
                         memcmp(&buffer, &unchanged, sizeof(buffer)) == 0 &&
                         native_has_no_datagram(pair.second.socket.storage->handle.descriptor));
            r_runtime_allocator_set_failure(allocator, UINT64_C(0));
            r_runtime_array_destroy(&buffer);
            continue;
        }
        r_runtime_allocator_set_failure(allocator, UINT64_C(0));
        R_TEST_CHECK(buffer.data == NULL && await_send(started, &send_result));
        R_TEST_CHECK(send_result.kind == R_STD_NET_UDP_SEND_RESULT_SENT &&
                     native_receive(pair.second.socket.storage->handle.descriptor,
                                    &observed,
                                    sizeof(observed),
                                    &observed_size) &&
                     observed_size == 1U && observed == byte);
        r_runtime_array_destroy(&send_result.buffer);
        reached_success = 1;
        break;
    }
    R_TEST_CHECK(reached_success && fail_at > UINT64_C(1));

    reached_success = 0;
    for (fail_at = UINT64_C(1); fail_at != UINT64_C(20); ++fail_at) {
        RRuntimeArray buffer = byte_array(allocator, 1U, byte);
        RRuntimeArray unchanged = buffer;
        RStdNetTaskStartResult started;
        RStdNetUdpReceiveResult receive_result = {0};

        r_runtime_allocator_set_failure(allocator, fail_at);
        started = r_std_net_udp_receive_from(
            &pair.first.socket, &buffer, deadline_after_milliseconds(UINT32_C(1)));
        if (!started.is_ok) {
            R_TEST_CHECK(started.task == NULL &&
                         started.error == R_STD_ASYNC_START_ALLOCATION_FAILED &&
                         r_runtime_allocator_attempt_count(allocator) == fail_at &&
                         memcmp(&buffer, &unchanged, sizeof(buffer)) == 0);
            r_runtime_allocator_set_failure(allocator, UINT64_C(0));
            r_runtime_array_destroy(&buffer);
            continue;
        }
        r_runtime_allocator_set_failure(allocator, UINT64_C(0));
        R_TEST_CHECK(buffer.data == NULL && await_receive(started, &receive_result));
        R_TEST_CHECK(receive_result.kind == R_STD_NET_UDP_RECEIVE_RESULT_FAILED &&
                     receive_result.error.code == R_STD_NET_ERROR_TIMED_OUT &&
                     ((const uint8_t *)receive_result.buffer.data)[0] == byte);
        r_runtime_array_destroy(&receive_result.buffer);
        reached_success = 1;
        break;
    }
    R_TEST_CHECK(reached_success && fail_at > UINT64_C(1));
    destroy_udp_pair(&pair);
    return 0;
}

static int test_close_start_failure_preserves_socket(RRuntimeAllocator *allocator) {
    uint64_t fail_at;
    _Bool reached_success = 0;

    for (fail_at = UINT64_C(1); fail_at != UINT64_C(12); ++fail_at) {
        RTestUdpPair pair = create_udp_pair(allocator);
        RStdNetUdpSocketStorage *unchanged;
        RStdNetTaskStartResult started;
        RStdNetVoidResult close_result = {0};

        R_TEST_CHECK(pair.first.socket.storage != NULL && pair.second.socket.storage != NULL);
        unchanged = pair.first.socket.storage;
        r_runtime_allocator_set_failure(allocator, fail_at);
        started =
            r_std_net_udp_close(&pair.first.socket, deadline_after_milliseconds(UINT32_C(1000)));
        if (!started.is_ok) {
            R_TEST_CHECK(started.task == NULL &&
                         started.error == R_STD_ASYNC_START_ALLOCATION_FAILED &&
                         r_runtime_allocator_attempt_count(allocator) == fail_at &&
                         pair.first.socket.storage == unchanged);
            r_runtime_allocator_set_failure(allocator, UINT64_C(0));
            destroy_udp_pair(&pair);
            continue;
        }
        r_runtime_allocator_set_failure(allocator, UINT64_C(0));
        R_TEST_CHECK(pair.first.socket.storage == NULL && await_close(started, &close_result) &&
                     close_result.r_tag == UINT32_C(0));
        destroy_udp_pair(&pair);
        reached_success = 1;
        break;
    }
    R_TEST_CHECK(reached_success && fail_at > UINT64_C(1));
    return 0;
}

static int test_runtime_stop_preserves_operands(RRuntimeAllocator *allocator) {
    const uint8_t byte = UINT8_C(0xfa);
    RTestUdpPair pair = create_udp_pair(allocator);
    RRuntimeArray buffer = byte_array(allocator, 1U, byte);
    RRuntimeArray unchanged = buffer;
    RStdNetUdpSocketStorage *unchanged_socket;
    RStdNetTaskStartResult started;

    R_TEST_CHECK(pair.first.socket.storage != NULL && pair.second.socket.storage != NULL);
    unchanged_socket = pair.first.socket.storage;
    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
    started = r_std_net_udp_send_to(
        &pair.first.socket, pair.second.address, &buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                 started.error == R_STD_ASYNC_START_RUNTIME_STOPPING &&
                 memcmp(&buffer, &unchanged, sizeof(buffer)) == 0 &&
                 pair.first.socket.storage == unchanged_socket);
    started = r_std_net_udp_close(&pair.first.socket, (RStdNetDeadline){0});
    R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                 started.error == R_STD_ASYNC_START_RUNTIME_STOPPING &&
                 pair.first.socket.storage == unchanged_socket);
    r_runtime_array_destroy(&buffer);
    destroy_udp_pair(&pair);
    return 0;
}

int main(void) {
    RRuntimeAllocator allocator;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    R_TEST_CHECK(test_send_receive_and_truncation(&allocator) == 0);
    R_TEST_CHECK(test_empty_datagrams(&allocator) == 0);
    R_TEST_CHECK(test_fifo_and_direction_independence(&allocator) == 0);
    R_TEST_CHECK(test_deadline_and_native_failure_preserve_bytes(&allocator) == 0);
    R_TEST_CHECK(test_queued_deadline_does_not_send(&allocator) == 0);
    R_TEST_CHECK(test_task_cancel_waits_for_native_ack(&allocator) == 0);
    R_TEST_CHECK(test_close_drains_and_deadline_cleanup(&allocator) == 0);
    R_TEST_CHECK(test_preexisting_close_failure_precedes_deadline(&allocator) == 0);
    R_TEST_CHECK(test_cancelled_close_still_releases(&allocator) == 0);
    R_TEST_CHECK(test_start_failure_preserves_operands(&allocator) == 0);
    R_TEST_CHECK(test_close_start_failure_preserves_socket(&allocator) == 0);
    R_TEST_CHECK(test_runtime_stop_preserves_operands(&allocator) == 0);
    (void)puts("library_net_udp_tests: ok");
    return EXIT_SUCCESS;
}
