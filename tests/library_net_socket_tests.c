#include "r_library_net_internal.h"
#include "r_std_net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
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

typedef struct RNetQueryThreadContext {
    const RStdNetTcpStream *stream;
    uint16_t local_port;
    uint16_t peer_port;
    _Atomic int failed;
} RNetQueryThreadContext;

static int make_runtime_descriptor(int domain, int type) {
    int descriptor = socket(domain, type, 0);
    int descriptor_flags;
    int status_flags;

    if (descriptor < 0) {
        return -1;
    }
    descriptor_flags = fcntl(descriptor, F_GETFD);
    status_flags = fcntl(descriptor, F_GETFL);
    if ((descriptor_flags < 0) || (status_flags < 0) ||
        (fcntl(descriptor, F_SETFD, descriptor_flags | FD_CLOEXEC) != 0) ||
        (fcntl(descriptor, F_SETFL, status_flags | O_NONBLOCK) != 0)) {
        (void)close(descriptor);
        return -1;
    }
    return descriptor;
}

static _Bool is_v4_loopback(RStdNetSocketAddress address) {
    static const uint8_t expected[4] = {UINT8_C(127), UINT8_C(0), UINT8_C(0), UINT8_C(1)};

    return (address.address.kind == R_STD_NET_IP_ADDRESS_V4) && (address.scope_id == 0U) &&
           (memcmp(address.address.bytes.v4, expected, sizeof(expected)) == 0);
}

static void *query_thread(void *context_pointer) {
    RNetQueryThreadContext *context = context_pointer;
    size_t iteration;

    for (iteration = 0U; iteration < 2000U; ++iteration) {
        const RStdNetSocketAddressResult local = r_std_net_tcp_local_address(context->stream);
        const RStdNetSocketAddressResult peer = r_std_net_tcp_peer_address(context->stream);

        if (!local.is_ok || !peer.is_ok || !is_v4_loopback(local.value) ||
            !is_v4_loopback(peer.value) || (local.value.port != context->local_port) ||
            (peer.value.port != context->peer_port)) {
            atomic_store_explicit(&context->failed, 1, memory_order_relaxed);
            break;
        }
    }
    return NULL;
}

static int test_tcp_observation_and_lifetime(void) {
    RRuntimeAllocator allocator;
    struct sockaddr_in listener_address = {0};
    struct sockaddr_in client_address = {0};
    socklen_t address_length;
    int listener_descriptor = -1;
    int client_descriptor = -1;
    int accepted_descriptor = -1;
    RStdNetTcpListenerStorage *listener_storage;
    RStdNetTcpStreamStorage *stream_storage;
    RStdNetTcpListener listener = {0};
    RStdNetTcpListener moved_listener = {0};
    RStdNetTcpStream stream = {0};
    RStdNetSocketAddressResult observed;
    RNetQueryThreadContext thread_context = {0};
    pthread_t threads[4];
    size_t thread_count = 0U;

    r_runtime_allocator_initialize(&allocator);
    listener_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    R_TEST_CHECK(listener_descriptor >= 0);
    listener_address.sin_family = AF_INET;
    listener_address.sin_port = htons(UINT16_C(0));
    listener_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    R_TEST_CHECK(bind(listener_descriptor,
                      (const struct sockaddr *)&listener_address,
                      (socklen_t)sizeof(listener_address)) == 0);
    R_TEST_CHECK(listen(listener_descriptor, 4) == 0);
    address_length = (socklen_t)sizeof(listener_address);
    R_TEST_CHECK(getsockname(listener_descriptor,
                             (struct sockaddr *)&listener_address,
                             &address_length) == 0);

    client_descriptor = socket(AF_INET, SOCK_STREAM, 0);
    R_TEST_CHECK(client_descriptor >= 0);
    R_TEST_CHECK(connect(client_descriptor,
                         (const struct sockaddr *)&listener_address,
                         (socklen_t)sizeof(listener_address)) == 0);
    accepted_descriptor = accept(listener_descriptor, NULL, NULL);
    R_TEST_CHECK(accepted_descriptor >= 0);
    address_length = (socklen_t)sizeof(client_address);
    R_TEST_CHECK(
        getsockname(client_descriptor, (struct sockaddr *)&client_address, &address_length) == 0);

    {
        const int listener_descriptor_flags = fcntl(listener_descriptor, F_GETFD);
        const int listener_status_flags = fcntl(listener_descriptor, F_GETFL);
        const int accepted_descriptor_flags = fcntl(accepted_descriptor, F_GETFD);
        const int accepted_status_flags = fcntl(accepted_descriptor, F_GETFL);

        R_TEST_CHECK((listener_descriptor_flags >= 0) && (listener_status_flags >= 0));
        R_TEST_CHECK((accepted_descriptor_flags >= 0) && (accepted_status_flags >= 0));
        R_TEST_CHECK(fcntl(listener_descriptor, F_SETFD, listener_descriptor_flags | FD_CLOEXEC) ==
                     0);
        R_TEST_CHECK(fcntl(listener_descriptor, F_SETFL, listener_status_flags | O_NONBLOCK) == 0);
        R_TEST_CHECK(fcntl(accepted_descriptor, F_SETFD, accepted_descriptor_flags | FD_CLOEXEC) ==
                     0);
        R_TEST_CHECK(fcntl(accepted_descriptor, F_SETFL, accepted_status_flags | O_NONBLOCK) == 0);
    }

    listener_storage = r_library_internal_net_tcp_listener_reserve(&allocator);
    stream_storage = r_library_internal_net_tcp_stream_reserve(&allocator);
    R_TEST_CHECK(listener_storage != NULL);
    R_TEST_CHECK(stream_storage != NULL);
    r_library_internal_net_tcp_listener_publish(listener_storage, listener_descriptor);
    r_library_internal_net_tcp_stream_publish(stream_storage, accepted_descriptor);
    listener.storage = listener_storage;
    stream.storage = stream_storage;

    observed = r_std_net_tcp_listener_local_address(&listener);
    R_TEST_CHECK(observed.is_ok);
    R_TEST_CHECK(is_v4_loopback(observed.value));
    R_TEST_CHECK(observed.value.port == ntohs(listener_address.sin_port));

    observed = r_std_net_tcp_local_address(&stream);
    R_TEST_CHECK(observed.is_ok);
    R_TEST_CHECK(is_v4_loopback(observed.value));
    R_TEST_CHECK(observed.value.port == ntohs(listener_address.sin_port));
    thread_context.local_port = observed.value.port;

    observed = r_std_net_tcp_peer_address(&stream);
    R_TEST_CHECK(observed.is_ok);
    R_TEST_CHECK(is_v4_loopback(observed.value));
    R_TEST_CHECK(observed.value.port == ntohs(client_address.sin_port));
    thread_context.peer_port = observed.value.port;
    thread_context.stream = &stream;
    atomic_init(&thread_context.failed, 0);
    while (thread_count < (sizeof(threads) / sizeof(threads[0]))) {
        R_TEST_CHECK(pthread_create(&threads[thread_count], NULL, query_thread, &thread_context) ==
                     0);
        thread_count += 1U;
    }
    while (thread_count != 0U) {
        thread_count -= 1U;
        R_TEST_CHECK(pthread_join(threads[thread_count], NULL) == 0);
    }
    R_TEST_CHECK(atomic_load_explicit(&thread_context.failed, memory_order_relaxed) == 0);

    r_std_net_tcp_listener_move_initialize(&moved_listener, &listener);
    R_TEST_CHECK(listener.storage == NULL);
    R_TEST_CHECK(moved_listener.storage == listener_storage);
    r_std_net_tcp_listener_destroy(&moved_listener);
    R_TEST_CHECK(moved_listener.storage == NULL);
    errno = 0;
    R_TEST_CHECK(fcntl(listener_descriptor, F_GETFD) < 0);
    R_TEST_CHECK(errno == EBADF);

    r_std_net_tcp_stream_destroy(&stream);
    r_std_net_tcp_stream_destroy(&stream);
    errno = 0;
    R_TEST_CHECK(fcntl(accepted_descriptor, F_GETFD) < 0);
    R_TEST_CHECK(errno == EBADF);
    R_TEST_CHECK(close(client_descriptor) == 0);
    return 0;
}

static int test_udp_observation_and_operation_retain(void) {
    RRuntimeAllocator allocator;
    struct sockaddr_in address = {0};
    RStdNetUdpSocketStorage *storage;
    RStdNetUdpSocket socket = {0};
    RStdNetSocketAddressResult observed;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = make_runtime_descriptor(AF_INET, SOCK_DGRAM);
    R_TEST_CHECK(descriptor >= 0);
    address.sin_family = AF_INET;
    address.sin_port = htons(UINT16_C(0));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    R_TEST_CHECK(bind(descriptor, (const struct sockaddr *)&address, (socklen_t)sizeof(address)) ==
                 0);
    storage = r_library_internal_net_udp_socket_reserve(&allocator);
    R_TEST_CHECK(storage != NULL);
    r_library_internal_net_udp_socket_publish(storage, descriptor);
    socket.storage = storage;

    observed = r_std_net_udp_local_address(&socket);
    R_TEST_CHECK(observed.is_ok);
    R_TEST_CHECK(is_v4_loopback(observed.value));
    R_TEST_CHECK(observed.value.port != 0U);
    R_TEST_CHECK(r_library_internal_net_handle_retain(&storage->handle));
    r_std_net_udp_socket_destroy(&socket);
    R_TEST_CHECK(socket.storage == NULL);
    R_TEST_CHECK(fcntl(descriptor, F_GETFD) >= 0);
    r_library_internal_net_handle_release(&storage->handle);
    errno = 0;
    R_TEST_CHECK(fcntl(descriptor, F_GETFD) < 0);
    R_TEST_CHECK(errno == EBADF);
    return 0;
}

static int test_closed_not_connected_and_allocation_failure(void) {
    RRuntimeAllocator allocator;
    RStdNetTcpStream stream = {0};
    RStdNetTcpStreamStorage *stream_storage;
    RStdNetSocketAddressResult observed;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    R_TEST_CHECK(r_library_internal_net_tcp_stream_reserve(&allocator) == NULL);
    R_TEST_CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));

    r_runtime_allocator_initialize(&allocator);
    descriptor = make_runtime_descriptor(AF_INET, SOCK_STREAM);
    R_TEST_CHECK(descriptor >= 0);
    stream_storage = r_library_internal_net_tcp_stream_reserve(&allocator);
    R_TEST_CHECK(stream_storage != NULL);
    r_library_internal_net_tcp_stream_publish(stream_storage, descriptor);
    stream.storage = stream_storage;
    observed = r_std_net_tcp_peer_address(&stream);
    R_TEST_CHECK(!observed.is_ok);
    R_TEST_CHECK(observed.error.code == R_STD_NET_ERROR_NOT_CONNECTED);
    R_TEST_CHECK(observed.error.native_code == ENOTCONN);
    r_std_net_tcp_stream_destroy(&stream);
    return 0;
}

int main(void) {
    if (test_tcp_observation_and_lifetime() != 0) {
        return 1;
    }
    if (test_udp_observation_and_operation_retain() != 0) {
        return 1;
    }
    if (test_closed_not_connected_and_allocation_failure() != 0) {
        return 1;
    }
    return 0;
}
