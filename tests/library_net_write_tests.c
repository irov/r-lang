#include "r_library_net_internal.h"
#include "r_runtime_allocator.h"
#include "r_runtime_array.h"
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

typedef struct RTestStopContext {
    _Bool stopped;
} RTestStopContext;

typedef struct RTestCancelContext {
    RRuntimeTask **task;
} RTestCancelContext;

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

static int configure_stream_descriptor(int descriptor) {
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
    if (accepted < 0 || !configure_stream_descriptor(accepted)) {
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
    if (pair == NULL) {
        return;
    }
    r_std_net_tcp_stream_destroy(&pair->stream);
    if (pair->peer >= 0) {
        (void)close(pair->peer);
        pair->peer = -1;
    }
}

static _Bool stream_has_data_io(RStdNetTcpStreamStorage *storage) {
    _Bool has_data_io;

    if (storage == NULL || pthread_mutex_lock(&storage->handle.mutex) != 0) {
        abort();
    }
    has_data_io = storage->data_io != NULL;
    if (pthread_mutex_unlock(&storage->handle.mutex) != 0) {
        abort();
    }
    return has_data_io;
}

static int constrain_socket_buffers(RTestTcpPair *pair) {
    const int capacity = 4096;

    return pair->stream.storage != NULL && pair->peer >= 0 &&
           setsockopt(pair->stream.storage->handle.descriptor,
                      SOL_SOCKET,
                      SO_SNDBUF,
                      &capacity,
                      (socklen_t)sizeof(capacity)) == 0 &&
           setsockopt(pair->peer, SOL_SOCKET, SO_RCVBUF, &capacity, (socklen_t)sizeof(capacity)) ==
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

static int receive_fill_exact(int descriptor, size_t length, uint8_t fill) {
    uint8_t bytes[4096];
    size_t offset = 0U;

    while (offset != length) {
        const size_t remaining = length - offset;
        const size_t requested = remaining < sizeof(bytes) ? remaining : sizeof(bytes);
        ssize_t count = recv(descriptor, bytes, requested, 0);
        size_t index;

        if (count < 0 && errno == EINTR) {
            continue;
        }
        if (count <= 0) {
            return 0;
        }
        for (index = 0U; index != (size_t)count; ++index) {
            if (bytes[index] != fill) {
                return 0;
            }
        }
        offset += (size_t)count;
    }
    return 1;
}

static int peer_has_no_bytes(int descriptor) {
    uint8_t byte = 0U;
    int flags = fcntl(descriptor, F_GETFL);
    ssize_t count;
    int saved_error;

    if (flags < 0 || fcntl(descriptor, F_SETFL, flags | O_NONBLOCK) != 0) {
        return 0;
    }
    errno = 0;
    count = recv(descriptor, &byte, 1U, 0);
    saved_error = errno;
    if (fcntl(descriptor, F_SETFL, flags) != 0) {
        return 0;
    }
    return count < 0 && (saved_error == EAGAIN || saved_error == EWOULDBLOCK);
}

static int await_write(RStdNetTaskStartResult started, RStdNetTcpWriteResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static int await_write_all(RStdNetTaskStartResult started, RStdNetTcpWriteAllResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static int await_read(RStdNetTaskStartResult started, RStdNetTcpReadResult *result) {
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

static void *cancel_task(void *context_pointer) {
    RTestCancelContext *context = context_pointer;

    r_runtime_task_cancel(context->task);
    return NULL;
}

static int test_write_some_and_write_all(RRuntimeAllocator *allocator) {
    static const uint8_t some_bytes[] = {
        UINT8_C(0x11),
        UINT8_C(0x12),
        UINT8_C(0x13),
        UINT8_C(0x14),
        UINT8_C(0x15),
    };
    static const uint8_t all_bytes[] = {
        UINT8_C(0x21),
        UINT8_C(0x22),
        UINT8_C(0x23),
        UINT8_C(0x24),
        UINT8_C(0x25),
        UINT8_C(0x26),
        UINT8_C(0x27),
    };
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray buffer = byte_array(allocator, sizeof(some_bytes), UINT8_C(0));
    RStdNetTaskStartResult started;
    RStdNetTcpWriteResult some_result = {0};
    RStdNetTcpWriteAllResult all_result = {0};
    uint8_t observed[sizeof(all_bytes)];
    void *owner;

    R_TEST_CHECK(pair.stream.storage != NULL && pair.peer >= 0);
    (void)memcpy(buffer.data, some_bytes, sizeof(some_bytes));
    owner = buffer.data;
    started = r_std_net_tcp_write(&pair.stream, &buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(started.is_ok && started.task != NULL && buffer.data == NULL);
    R_TEST_CHECK(await_write(started, &some_result));
    R_TEST_CHECK(some_result.kind == R_STD_NET_TCP_WRITE_RESULT_WRITTEN &&
                 some_result.written > 0U && some_result.written <= sizeof(some_bytes) &&
                 some_result.buffer.data == owner &&
                 memcmp(some_result.buffer.data, some_bytes, sizeof(some_bytes)) == 0);
    R_TEST_CHECK(receive_exact(pair.peer, observed, some_result.written) &&
                 memcmp(observed, some_bytes, some_result.written) == 0);
    r_runtime_array_destroy(&some_result.buffer);

    buffer = byte_array(allocator, sizeof(all_bytes), UINT8_C(0));
    (void)memcpy(buffer.data, all_bytes, sizeof(all_bytes));
    owner = buffer.data;
    started = r_std_net_tcp_write_all(&pair.stream, &buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(started.is_ok && started.task != NULL && buffer.data == NULL);
    R_TEST_CHECK(receive_exact(pair.peer, observed, sizeof(all_bytes)));
    R_TEST_CHECK(await_write_all(started, &all_result));
    R_TEST_CHECK(all_result.kind == R_STD_NET_TCP_WRITE_ALL_RESULT_WRITTEN &&
                 all_result.written == sizeof(all_bytes) && all_result.buffer.data == owner &&
                 memcmp(all_result.buffer.data, all_bytes, sizeof(all_bytes)) == 0 &&
                 memcmp(observed, all_bytes, sizeof(all_bytes)) == 0);
    r_runtime_array_destroy(&all_result.buffer);
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_some_stops_and_all_retries(RRuntimeAllocator *allocator) {
    const size_t buffer_size = 2U * 1024U * 1024U;
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray buffer;
    RStdNetTaskStartResult started;
    RStdNetTcpWriteResult some_result = {0};
    RStdNetTcpWriteAllResult all_result = {0};
    void *owner;

    R_TEST_CHECK(pair.stream.storage != NULL && constrain_socket_buffers(&pair));
    buffer = byte_array(allocator, buffer_size, UINT8_C(0x31));
    owner = buffer.data;
    r_runtime_darwin_io_testing_pause_next_write_after_progress();
    started = r_std_net_tcp_write(&pair.stream, &buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(started.is_ok && buffer.data == NULL);
    r_runtime_darwin_io_testing_wait_for_write_after_progress();
    R_TEST_CHECK(r_runtime_task_state(started.task) == R_RUNTIME_TASK_RUNNING);
    r_runtime_darwin_io_testing_release_write_after_progress();
    R_TEST_CHECK(await_write(started, &some_result));
    R_TEST_CHECK(some_result.kind == R_STD_NET_TCP_WRITE_RESULT_WRITTEN &&
                 some_result.written > 0U && some_result.written < buffer_size &&
                 some_result.buffer.data == owner &&
                 ((const uint8_t *)some_result.buffer.data)[0] == UINT8_C(0x31) &&
                 ((const uint8_t *)some_result.buffer.data)[buffer_size - 1U] == UINT8_C(0x31));
    R_TEST_CHECK(receive_fill_exact(pair.peer, some_result.written, UINT8_C(0x31)));
    r_runtime_array_destroy(&some_result.buffer);
    destroy_tcp_pair(&pair);

    pair = create_tcp_pair(allocator);
    R_TEST_CHECK(pair.stream.storage != NULL && constrain_socket_buffers(&pair));
    buffer = byte_array(allocator, buffer_size, UINT8_C(0x32));
    owner = buffer.data;
    r_runtime_darwin_io_testing_pause_next_write_after_progress();
    started = r_std_net_tcp_write_all(&pair.stream, &buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(started.is_ok && buffer.data == NULL);
    r_runtime_darwin_io_testing_wait_for_write_after_progress();
    R_TEST_CHECK(r_runtime_task_state(started.task) == R_RUNTIME_TASK_RUNNING);
    r_runtime_darwin_io_testing_release_write_after_progress();
    R_TEST_CHECK(receive_fill_exact(pair.peer, buffer_size, UINT8_C(0x32)));
    R_TEST_CHECK(await_write_all(started, &all_result));
    R_TEST_CHECK(all_result.kind == R_STD_NET_TCP_WRITE_ALL_RESULT_WRITTEN &&
                 all_result.written == buffer_size && all_result.buffer.data == owner &&
                 ((const uint8_t *)all_result.buffer.data)[0] == UINT8_C(0x32) &&
                 ((const uint8_t *)all_result.buffer.data)[buffer_size - 1U] == UINT8_C(0x32));
    r_runtime_array_destroy(&all_result.buffer);
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_empty_shutdown_and_closed_outcomes(RRuntimeAllocator *allocator) {
    const RStdNetDeadline expired = {1, {0, 0U}};
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray empty = byte_array(allocator, 0U, UINT8_C(0));
    RRuntimeArray shutdown_buffer;
    RRuntimeArray closed_buffer;
    RStdNetTcpWriteResult write_result = {0};
    RStdNetTcpWriteAllResult all_result = {0};

    R_TEST_CHECK(pair.stream.storage != NULL && !stream_has_data_io(pair.stream.storage));
    if (pthread_mutex_lock(&pair.stream.storage->handle.mutex) != 0) {
        abort();
    }
    pair.stream.storage->handle.write_shutdown = 1;
    if (pthread_mutex_unlock(&pair.stream.storage->handle.mutex) != 0) {
        abort();
    }
    R_TEST_CHECK(await_write(r_std_net_tcp_write(&pair.stream, &empty, expired), &write_result));
    R_TEST_CHECK(write_result.kind == R_STD_NET_TCP_WRITE_RESULT_WRITTEN &&
                 write_result.written == 0U && !stream_has_data_io(pair.stream.storage) &&
                 peer_has_no_bytes(pair.peer));
    r_runtime_array_destroy(&write_result.buffer);

    empty = byte_array(allocator, 0U, UINT8_C(0));
    R_TEST_CHECK(
        await_write_all(r_std_net_tcp_write_all(&pair.stream, &empty, expired), &all_result));
    R_TEST_CHECK(all_result.kind == R_STD_NET_TCP_WRITE_ALL_RESULT_WRITTEN &&
                 all_result.written == 0U && !stream_has_data_io(pair.stream.storage) &&
                 peer_has_no_bytes(pair.peer));
    r_runtime_array_destroy(&all_result.buffer);

    shutdown_buffer = byte_array(allocator, 2U, UINT8_C(0x41));
    R_TEST_CHECK(await_write_all(
        r_std_net_tcp_write_all(&pair.stream, &shutdown_buffer, (RStdNetDeadline){0}),
        &all_result));
    R_TEST_CHECK(all_result.kind == R_STD_NET_TCP_WRITE_ALL_RESULT_FAILED &&
                 all_result.error.code == R_STD_NET_ERROR_NOT_CONNECTED &&
                 all_result.error.native_code == 0 && all_result.written == 0U &&
                 ((const uint8_t *)all_result.buffer.data)[1] == UINT8_C(0x41));
    r_runtime_array_destroy(&all_result.buffer);

    if (pthread_mutex_lock(&pair.stream.storage->handle.mutex) != 0) {
        abort();
    }
    pair.stream.storage->handle.write_shutdown = 0;
    pair.stream.storage->handle.close_reserved = 1;
    if (pthread_mutex_unlock(&pair.stream.storage->handle.mutex) != 0) {
        abort();
    }
    closed_buffer = byte_array(allocator, 2U, UINT8_C(0x42));
    R_TEST_CHECK(await_write(
        r_std_net_tcp_write(&pair.stream, &closed_buffer, (RStdNetDeadline){0}), &write_result));
    R_TEST_CHECK(write_result.kind == R_STD_NET_TCP_WRITE_RESULT_FAILED &&
                 write_result.error.code == R_STD_NET_ERROR_CLOSED &&
                 write_result.error.native_code == 0 && write_result.written == 0U &&
                 ((const uint8_t *)write_result.buffer.data)[0] == UINT8_C(0x42));
    r_runtime_array_destroy(&write_result.buffer);
    if (pthread_mutex_lock(&pair.stream.storage->handle.mutex) != 0) {
        abort();
    }
    pair.stream.storage->handle.close_reserved = 0;
    if (pthread_mutex_unlock(&pair.stream.storage->handle.mutex) != 0) {
        abort();
    }
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_empty_write_bypasses_fifo_and_deadline(RRuntimeAllocator *allocator) {
    const size_t buffer_size = 8U * 1024U * 1024U;
    const RStdNetDeadline expired = {1, {0, 0U}};
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray head_buffer;
    RRuntimeArray empty;
    RStdNetTaskStartResult head_started;
    RStdNetTcpWriteAllResult head_result = {0};
    RStdNetTcpWriteResult empty_result = {0};

    R_TEST_CHECK(pair.stream.storage != NULL && constrain_socket_buffers(&pair));
    head_buffer = byte_array(allocator, buffer_size, UINT8_C(0x43));
    r_runtime_darwin_io_testing_pause_next_write_after_progress();
    head_started = r_std_net_tcp_write_all(&pair.stream, &head_buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(head_started.is_ok && head_started.task != NULL && head_buffer.data == NULL);
    r_runtime_darwin_io_testing_wait_for_write_after_progress();

    empty = byte_array(allocator, 0U, UINT8_C(0));
    R_TEST_CHECK(await_write(r_std_net_tcp_write(&pair.stream, &empty, expired), &empty_result));
    R_TEST_CHECK(empty_result.kind == R_STD_NET_TCP_WRITE_RESULT_WRITTEN &&
                 empty_result.written == 0U &&
                 r_runtime_task_state(head_started.task) == R_RUNTIME_TASK_RUNNING);
    r_runtime_array_destroy(&empty_result.buffer);

    R_TEST_CHECK(r_runtime_darwin_io_testing_cancel_write_after_progress());
    r_runtime_darwin_io_testing_release_write_after_progress();
    R_TEST_CHECK(await_write_all(head_started, &head_result));
    R_TEST_CHECK(head_result.kind == R_STD_NET_TCP_WRITE_ALL_RESULT_FAILED &&
                 head_result.error.code == R_STD_NET_ERROR_CANCELLED && head_result.written > 0U &&
                 head_result.written < buffer_size);
    r_runtime_array_destroy(&head_result.buffer);
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_fifo_and_read_independence(RRuntimeAllocator *allocator) {
    static const uint8_t first_bytes[] = {UINT8_C(0x51), UINT8_C(0x52), UINT8_C(0x53)};
    static const uint8_t second_bytes[] = {UINT8_C(0x61), UINT8_C(0x62), UINT8_C(0x63)};
    static const uint8_t expected[] = {
        UINT8_C(0x51),
        UINT8_C(0x52),
        UINT8_C(0x53),
        UINT8_C(0x61),
        UINT8_C(0x62),
        UINT8_C(0x63),
    };
    const uint8_t inbound = UINT8_C(0x71);
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray first = byte_array(allocator, sizeof(first_bytes), UINT8_C(0));
    RRuntimeArray second = byte_array(allocator, sizeof(second_bytes), UINT8_C(0));
    RStdNetTaskStartResult first_started;
    RStdNetTaskStartResult second_started;
    RStdNetTcpWriteAllResult first_result = {0};
    RStdNetTcpWriteAllResult second_result = {0};
    uint8_t observed[sizeof(expected)];

    R_TEST_CHECK(pair.stream.storage != NULL);
    (void)memcpy(first.data, first_bytes, sizeof(first_bytes));
    (void)memcpy(second.data, second_bytes, sizeof(second_bytes));
    first_started = r_std_net_tcp_write_all(&pair.stream, &first, (RStdNetDeadline){0});
    second_started = r_std_net_tcp_write_all(&pair.stream, &second, (RStdNetDeadline){0});
    R_TEST_CHECK(first_started.is_ok && second_started.is_ok && first.data == NULL &&
                 second.data == NULL);
    R_TEST_CHECK(receive_exact(pair.peer, observed, sizeof(observed)) &&
                 memcmp(observed, expected, sizeof(expected)) == 0);
    R_TEST_CHECK(await_write_all(second_started, &second_result));
    R_TEST_CHECK(await_write_all(first_started, &first_result));
    R_TEST_CHECK(first_result.kind == R_STD_NET_TCP_WRITE_ALL_RESULT_WRITTEN &&
                 second_result.kind == R_STD_NET_TCP_WRITE_ALL_RESULT_WRITTEN);
    r_runtime_array_destroy(&first_result.buffer);
    r_runtime_array_destroy(&second_result.buffer);

    {
        const uint8_t outbound = UINT8_C(0x72);
        RRuntimeArray read_buffer = byte_array(allocator, 1U, UINT8_C(0));
        RRuntimeArray write_buffer = byte_array(allocator, 1U, outbound);
        RStdNetTaskStartResult read_started =
            r_std_net_tcp_read(&pair.stream, &read_buffer, (RStdNetDeadline){0});
        RStdNetTaskStartResult write_started =
            r_std_net_tcp_write_all(&pair.stream, &write_buffer, (RStdNetDeadline){0});
        RStdNetTcpReadResult read_result = {0};
        RStdNetTcpWriteAllResult write_result = {0};

        R_TEST_CHECK(read_started.is_ok && write_started.is_ok);
        R_TEST_CHECK(receive_exact(pair.peer, observed, 1U) && observed[0] == outbound);
        R_TEST_CHECK(await_write_all(write_started, &write_result));
        R_TEST_CHECK(write_result.kind == R_STD_NET_TCP_WRITE_ALL_RESULT_WRITTEN &&
                     r_runtime_task_state(read_started.task) == R_RUNTIME_TASK_RUNNING);
        R_TEST_CHECK(send_exact(pair.peer, &inbound, 1U));
        R_TEST_CHECK(await_read(read_started, &read_result));
        R_TEST_CHECK(read_result.kind == R_STD_NET_TCP_READ_RESULT_READ &&
                     read_result.count == 1U &&
                     ((const uint8_t *)read_result.buffer.data)[0] == inbound);
        r_runtime_array_destroy(&write_result.buffer);
        r_runtime_array_destroy(&read_result.buffer);
    }
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_partial_cancel_and_deadline(RRuntimeAllocator *allocator) {
    const size_t buffer_size = 8U * 1024U * 1024U;
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray buffer;
    RStdNetTaskStartResult started;
    RStdNetTcpWriteResult partial_result = {0};
    RStdNetTcpWriteAllResult result = {0};
    void *owner;

    R_TEST_CHECK(pair.stream.storage != NULL && constrain_socket_buffers(&pair));
    buffer = byte_array(allocator, buffer_size, UINT8_C(0x81));
    owner = buffer.data;
    r_runtime_darwin_io_testing_pause_next_write_after_progress();
    started = r_std_net_tcp_write(&pair.stream, &buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(started.is_ok && started.task != NULL && buffer.data == NULL);
    r_runtime_darwin_io_testing_wait_for_write_after_progress();
    R_TEST_CHECK(r_runtime_darwin_io_testing_cancel_write_after_progress());
    r_runtime_darwin_io_testing_release_write_after_progress();
    R_TEST_CHECK(await_write(started, &partial_result));
    R_TEST_CHECK(partial_result.kind == R_STD_NET_TCP_WRITE_RESULT_FAILED &&
                 partial_result.error.code == R_STD_NET_ERROR_CANCELLED &&
                 partial_result.written > 0U && partial_result.written < buffer_size &&
                 partial_result.buffer.data == owner &&
                 ((const uint8_t *)partial_result.buffer.data)[0] == UINT8_C(0x81) &&
                 ((const uint8_t *)partial_result.buffer.data)[buffer_size - 1U] == UINT8_C(0x81));
    r_runtime_array_destroy(&partial_result.buffer);
    destroy_tcp_pair(&pair);

    pair = create_tcp_pair(allocator);
    R_TEST_CHECK(pair.stream.storage != NULL && constrain_socket_buffers(&pair));
    buffer = byte_array(allocator, buffer_size, UINT8_C(0x82));
    owner = buffer.data;
    r_runtime_darwin_io_testing_pause_next_write_after_progress();
    started =
        r_std_net_tcp_write_all(&pair.stream, &buffer, deadline_after_milliseconds(UINT32_C(1000)));
    R_TEST_CHECK(started.is_ok && started.task != NULL && buffer.data == NULL);
    r_runtime_darwin_io_testing_wait_for_write_after_progress();
    R_TEST_CHECK(r_runtime_darwin_io_testing_expire_write_after_progress());
    r_runtime_darwin_io_testing_release_write_after_progress();
    R_TEST_CHECK(await_write_all(started, &result));
    R_TEST_CHECK(result.kind == R_STD_NET_TCP_WRITE_ALL_RESULT_FAILED &&
                 result.error.code == R_STD_NET_ERROR_TIMED_OUT && result.written > 0U &&
                 result.written < buffer_size && result.buffer.data == owner &&
                 ((const uint8_t *)result.buffer.data)[0] == UINT8_C(0x82) &&
                 ((const uint8_t *)result.buffer.data)[buffer_size - 1U] == UINT8_C(0x82));
    r_runtime_array_destroy(&result.buffer);
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_task_cancel_waits_for_native_ack(RRuntimeAllocator *allocator) {
    const size_t buffer_size = 8U * 1024U * 1024U;
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray buffer;
    RStdNetTaskStartResult started;
    RTestCancelContext context;
    pthread_t thread;

    R_TEST_CHECK(pair.stream.storage != NULL && constrain_socket_buffers(&pair));
    buffer = byte_array(allocator, buffer_size, UINT8_C(0x91));
    r_runtime_darwin_io_testing_pause_next_write_after_progress();
    started = r_std_net_tcp_write_all(&pair.stream, &buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(started.is_ok && started.task != NULL && buffer.data == NULL);
    r_runtime_darwin_io_testing_wait_for_write_after_progress();
    r_library_internal_net_tcp_write_testing_arm_cancel_acknowledgement();
    context.task = &started.task;
    R_TEST_CHECK(pthread_create(&thread, NULL, cancel_task, &context) == 0);
    r_library_internal_net_tcp_write_testing_wait_cancel_reported();
    r_runtime_darwin_io_testing_release_write_after_progress();
    R_TEST_CHECK(pthread_join(thread, NULL) == 0 && started.task == NULL);
    r_library_internal_net_tcp_write_testing_wait_cancel_acknowledgement();
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_native_completion_precedes_task_cancel(RRuntimeAllocator *allocator) {
    const uint8_t byte = UINT8_C(0xa1);
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray buffer = byte_array(allocator, 1U, byte);
    RStdNetTaskStartResult started;
    RStdNetTcpWriteAllResult result = {0};
    RTestStopContext stop_context;
    pthread_t stop_thread;
    uint8_t observed = 0U;

    R_TEST_CHECK(pair.stream.storage != NULL);
    r_runtime_darwin_io_testing_pause_next_native_completion();
    started = r_std_net_tcp_write_all(&pair.stream, &buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(started.is_ok && started.task != NULL);
    r_runtime_darwin_io_testing_wait_for_native_completion();
    R_TEST_CHECK(begin_executor_stop(&stop_thread, &stop_context));
    r_runtime_darwin_io_testing_release_native_completion();
    R_TEST_CHECK(finish_executor_stop(stop_thread, &stop_context));
    R_TEST_CHECK(await_write_all(started, &result));
    R_TEST_CHECK(result.kind == R_STD_NET_TCP_WRITE_ALL_RESULT_WRITTEN && result.written == 1U &&
                 ((const uint8_t *)result.buffer.data)[0] == byte &&
                 receive_exact(pair.peer, &observed, 1U) && observed == byte);
    r_runtime_array_destroy(&result.buffer);
    destroy_tcp_pair(&pair);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(allocator) == R_RUNTIME_EXECUTOR_START_OK);
    return 0;
}

static int test_drop_after_start_keeps_request_alive(RRuntimeAllocator *allocator) {
    const uint8_t byte = UINT8_C(0xb1);
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray buffer = byte_array(allocator, 1U, byte);
    RStdNetTaskStartResult started;
    RStdNetTcpWriteAllResult result = {0};
    int original_descriptor;
    uint8_t observed = 0U;

    R_TEST_CHECK(pair.stream.storage != NULL);
    original_descriptor = pair.stream.storage->handle.descriptor;
    started = r_std_net_tcp_write_all(&pair.stream, &buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(started.is_ok && started.task != NULL && buffer.data == NULL);
    r_std_net_tcp_stream_destroy(&pair.stream);
    R_TEST_CHECK(pair.stream.storage == NULL);
    errno = 0;
    R_TEST_CHECK(fcntl(original_descriptor, F_GETFD) < 0 && errno == EBADF);
    R_TEST_CHECK(receive_exact(pair.peer, &observed, 1U) && observed == byte);
    R_TEST_CHECK(await_write_all(started, &result));
    R_TEST_CHECK(result.kind == R_STD_NET_TCP_WRITE_ALL_RESULT_WRITTEN && result.written == 1U &&
                 ((const uint8_t *)result.buffer.data)[0] == byte);
    r_runtime_array_destroy(&result.buffer);
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_native_reservation_failures_preserve_owner(RRuntimeAllocator *allocator) {
    static const RRuntimeDarwinIoHandleCreateFailureStage handle_stages[] = {
        R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_ALLOCATION,
        R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_AFTER_DUPLICATE,
        R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_AFTER_QUEUE,
    };
    static const RRuntimeDarwinIoPrepareFailureStage prepare_stages[] = {
        R_RUNTIME_DARWIN_IO_PREPARE_FAIL_REQUEST,
        R_RUNTIME_DARWIN_IO_PREPARE_FAIL_DEADLINE,
        R_RUNTIME_DARWIN_IO_PREPARE_FAIL_CHANNEL,
        R_RUNTIME_DARWIN_IO_PREPARE_FAIL_WRITE_DATA,
    };
    const uint8_t byte = UINT8_C(0xc1);
    size_t index;

    for (index = 0U; index != sizeof(handle_stages) / sizeof(handle_stages[0]); ++index) {
        RTestTcpPair pair = create_tcp_pair(allocator);
        RRuntimeArray buffer = byte_array(allocator, 1U, byte);
        RRuntimeArray original = buffer;
        RStdNetTaskStartResult started;
        RStdNetTcpWriteAllResult result = {0};
        uint8_t observed = 0U;

        R_TEST_CHECK(pair.stream.storage != NULL && !stream_has_data_io(pair.stream.storage));
        r_runtime_darwin_io_testing_fail_handle_create_stage(handle_stages[index]);
        started = r_std_net_tcp_write_all(&pair.stream, &buffer, (RStdNetDeadline){0});
        R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                     started.error == R_STD_ASYNC_START_ALLOCATION_FAILED &&
                     memcmp(&buffer, &original, sizeof(buffer)) == 0 &&
                     peer_has_no_bytes(pair.peer));
        R_TEST_CHECK(await_write_all(
            r_std_net_tcp_write_all(&pair.stream, &buffer, (RStdNetDeadline){0}), &result));
        R_TEST_CHECK(receive_exact(pair.peer, &observed, 1U) && observed == byte &&
                     result.kind == R_STD_NET_TCP_WRITE_ALL_RESULT_WRITTEN);
        r_runtime_array_destroy(&result.buffer);
        destroy_tcp_pair(&pair);
    }

    {
        RTestTcpPair pair = create_tcp_pair(allocator);
        RRuntimeArray buffer = byte_array(allocator, 1U, byte);
        RRuntimeArray original = buffer;
        RStdNetTaskStartResult started;

        r_runtime_darwin_io_testing_fail_next_handle_create_native(EMFILE);
        started = r_std_net_tcp_write_all(&pair.stream, &buffer, (RStdNetDeadline){0});
        R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                     started.error == R_STD_ASYNC_START_ALLOCATION_FAILED &&
                     memcmp(&buffer, &original, sizeof(buffer)) == 0 &&
                     peer_has_no_bytes(pair.peer));
        r_runtime_array_destroy(&buffer);
        destroy_tcp_pair(&pair);
    }

    for (index = 0U; index != sizeof(prepare_stages) / sizeof(prepare_stages[0]); ++index) {
        RTestTcpPair pair = create_tcp_pair(allocator);
        RRuntimeArray buffer = byte_array(allocator, 1U, byte);
        RRuntimeArray original = buffer;
        RStdNetTaskStartResult started;
        RStdNetDeadline deadline = {0};
        RStdNetTcpWriteAllResult result = {0};
        uint8_t observed = 0U;

        if (prepare_stages[index] == R_RUNTIME_DARWIN_IO_PREPARE_FAIL_DEADLINE) {
            deadline = deadline_after_milliseconds(UINT32_C(1000));
        }
        r_runtime_darwin_io_testing_fail_prepare_stage(prepare_stages[index]);
        started = r_std_net_tcp_write_all(&pair.stream, &buffer, deadline);
        R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                     started.error == R_STD_ASYNC_START_ALLOCATION_FAILED &&
                     memcmp(&buffer, &original, sizeof(buffer)) == 0 &&
                     peer_has_no_bytes(pair.peer));
        R_TEST_CHECK(await_write_all(
            r_std_net_tcp_write_all(&pair.stream, &buffer, (RStdNetDeadline){0}), &result));
        R_TEST_CHECK(receive_exact(pair.peer, &observed, 1U) && observed == byte &&
                     result.kind == R_STD_NET_TCP_WRITE_ALL_RESULT_WRITTEN);
        r_runtime_array_destroy(&result.buffer);
        destroy_tcp_pair(&pair);
    }
    return 0;
}

static int test_allocator_sweep_and_runtime_stop(RRuntimeAllocator *allocator) {
    const uint8_t byte = UINT8_C(0xd1);
    RTestTcpPair pair = create_tcp_pair(allocator);
    uint64_t fail_at;
    _Bool reached_success = 0;

    R_TEST_CHECK(pair.stream.storage != NULL);
    for (fail_at = UINT64_C(1); fail_at <= UINT64_C(16); ++fail_at) {
        RRuntimeArray buffer = byte_array(allocator, 1U, byte);
        RRuntimeArray original = buffer;
        RStdNetTaskStartResult started;
        RStdNetTcpWriteAllResult result = {0};
        uint8_t observed = 0U;

        r_runtime_allocator_set_failure(allocator, fail_at);
        started = r_std_net_tcp_write_all(&pair.stream, &buffer, (RStdNetDeadline){0});
        if (!started.is_ok) {
            R_TEST_CHECK(
                started.task == NULL && started.error == R_STD_ASYNC_START_ALLOCATION_FAILED &&
                r_runtime_allocator_attempt_count(allocator) == fail_at &&
                memcmp(&buffer, &original, sizeof(buffer)) == 0 && peer_has_no_bytes(pair.peer));
            r_runtime_allocator_set_failure(allocator, UINT64_C(0));
            r_runtime_array_destroy(&buffer);
            continue;
        }
        r_runtime_allocator_set_failure(allocator, UINT64_C(0));
        R_TEST_CHECK(buffer.data == NULL && receive_exact(pair.peer, &observed, 1U) &&
                     observed == byte && await_write_all(started, &result));
        R_TEST_CHECK(result.kind == R_STD_NET_TCP_WRITE_ALL_RESULT_WRITTEN);
        r_runtime_array_destroy(&result.buffer);
        reached_success = 1;
        break;
    }
    R_TEST_CHECK(reached_success && fail_at > UINT64_C(1));
    destroy_tcp_pair(&pair);

    pair = create_tcp_pair(allocator);
    {
        RRuntimeArray buffer = byte_array(allocator, 1U, byte);
        RRuntimeArray original = buffer;
        RStdNetTaskStartResult started;
        RStdNetTcpWriteAllResult result = {0};
        uint8_t observed = 0U;

        R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
        started = r_std_net_tcp_write_all(&pair.stream, &buffer, (RStdNetDeadline){0});
        R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                     started.error == R_STD_ASYNC_START_RUNTIME_STOPPING &&
                     memcmp(&buffer, &original, sizeof(buffer)) == 0 &&
                     peer_has_no_bytes(pair.peer));
        R_TEST_CHECK(r_runtime_executor_lifecycle_start(allocator) == R_RUNTIME_EXECUTOR_START_OK);
        R_TEST_CHECK(await_write_all(
            r_std_net_tcp_write_all(&pair.stream, &buffer, (RStdNetDeadline){0}), &result));
        R_TEST_CHECK(receive_exact(pair.peer, &observed, 1U) && observed == byte &&
                     result.kind == R_STD_NET_TCP_WRITE_ALL_RESULT_WRITTEN);
        r_runtime_array_destroy(&result.buffer);
    }
    destroy_tcp_pair(&pair);
    return 0;
}

int main(void) {
    RRuntimeAllocator allocator;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    R_TEST_CHECK(test_write_some_and_write_all(&allocator) == 0);
    R_TEST_CHECK(test_some_stops_and_all_retries(&allocator) == 0);
    R_TEST_CHECK(test_empty_shutdown_and_closed_outcomes(&allocator) == 0);
    R_TEST_CHECK(test_empty_write_bypasses_fifo_and_deadline(&allocator) == 0);
    R_TEST_CHECK(test_fifo_and_read_independence(&allocator) == 0);
    R_TEST_CHECK(test_partial_cancel_and_deadline(&allocator) == 0);
    R_TEST_CHECK(test_task_cancel_waits_for_native_ack(&allocator) == 0);
    R_TEST_CHECK(test_native_completion_precedes_task_cancel(&allocator) == 0);
    R_TEST_CHECK(test_drop_after_start_keeps_request_alive(&allocator) == 0);
    R_TEST_CHECK(test_native_reservation_failures_preserve_owner(&allocator) == 0);
    R_TEST_CHECK(test_allocator_sweep_and_runtime_stop(&allocator) == 0);
    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
    return 0;
}
