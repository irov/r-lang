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

typedef struct RTestCancelContext {
    RRuntimeTask **task;
} RTestCancelContext;

typedef struct RTestStopContext {
    _Bool stopped;
} RTestStopContext;

static int await_void(RStdNetTaskStartResult started, RStdNetVoidResult *result);

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

static void drop_tcp_pair(RTestTcpPair *pair) {
    if (pair == NULL) {
        return;
    }
    r_std_net_tcp_stream_destroy(&pair->stream);
    if (pair->peer >= 0) {
        (void)close(pair->peer);
        pair->peer = -1;
    }
}

static void destroy_tcp_pair(RTestTcpPair *pair) {
    RStdNetVoidResult close_result = {0};

    if (pair != NULL && pair->stream.storage != NULL &&
        !await_void(r_std_net_tcp_close(&pair->stream, (RStdNetDeadline){0}), &close_result)) {
        abort();
    }
    drop_tcp_pair(pair);
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

static int await_void(RStdNetTaskStartResult started, RStdNetVoidResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static int await_read(RStdNetTaskStartResult started, RStdNetTcpReadResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static int await_write_all(RStdNetTaskStartResult started, RStdNetTcpWriteAllResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
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

static int receive_eof(int descriptor) {
    uint8_t byte;
    ssize_t count;

    do {
        count = recv(descriptor, &byte, sizeof(byte), 0);
    } while (count < 0 && errno == EINTR);
    return count == 0;
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

static void *cancel_task(void *context_pointer) {
    RTestCancelContext *context = context_pointer;

    r_runtime_task_cancel(context->task);
    return NULL;
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

static int test_write_shutdown_and_idempotence(RRuntimeAllocator *allocator) {
    const uint8_t inbound = UINT8_C(0x31);
    RTestTcpPair pair = create_tcp_pair(allocator);
    RTestTcpPair failure_pair;
    RRuntimeArray write_buffer;
    RRuntimeArray read_buffer;
    RStdNetVoidResult result = {0};
    RStdNetTcpWriteAllResult write_result = {0};
    RStdNetTcpReadResult read_result = {0};

    R_TEST_CHECK(pair.stream.storage != NULL && pair.peer >= 0);
    R_TEST_CHECK(await_void(r_std_net_tcp_shutdown(
                                &pair.stream, R_STD_NET_SHUTDOWN_WRITE, (RStdNetDeadline){0}),
                            &result) &&
                 result.r_tag == UINT32_C(0) && receive_eof(pair.peer));

    write_buffer = byte_array(allocator, 1U, UINT8_C(0x41));
    R_TEST_CHECK(await_write_all(
        r_std_net_tcp_write_all(&pair.stream, &write_buffer, (RStdNetDeadline){0}), &write_result));
    R_TEST_CHECK(write_result.kind == R_STD_NET_TCP_WRITE_ALL_RESULT_FAILED &&
                 write_result.error.code == R_STD_NET_ERROR_NOT_CONNECTED &&
                 write_result.written == 0U &&
                 ((const uint8_t *)write_result.buffer.data)[0] == UINT8_C(0x41));
    r_runtime_array_destroy(&write_result.buffer);

    R_TEST_CHECK(send_exact(pair.peer, &inbound, sizeof(inbound)));
    read_buffer = byte_array(allocator, 1U, UINT8_C(0));
    R_TEST_CHECK(await_read(r_std_net_tcp_read(&pair.stream, &read_buffer, (RStdNetDeadline){0}),
                            &read_result));
    R_TEST_CHECK(read_result.kind == R_STD_NET_TCP_READ_RESULT_READ && read_result.count == 1U &&
                 ((const uint8_t *)read_result.buffer.data)[0] == inbound);
    r_runtime_array_destroy(&read_result.buffer);

    r_runtime_darwin_io_testing_fail_next_shutdown_native(EIO);
    R_TEST_CHECK(await_void(r_std_net_tcp_shutdown(
                                &pair.stream, R_STD_NET_SHUTDOWN_WRITE, (RStdNetDeadline){0}),
                            &result) &&
                 result.r_tag == UINT32_C(0));
    destroy_tcp_pair(&pair);

    failure_pair = create_tcp_pair(allocator);
    R_TEST_CHECK(failure_pair.stream.storage != NULL);
    R_TEST_CHECK(await_void(r_std_net_tcp_shutdown(&failure_pair.stream,
                                                   R_STD_NET_SHUTDOWN_WRITE,
                                                   (RStdNetDeadline){0}),
                            &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(1) && result.error.code == R_STD_NET_ERROR_OTHER &&
                 result.error.native_code == EIO);
    destroy_tcp_pair(&failure_pair);
    return 0;
}

static int test_read_shutdown(RRuntimeAllocator *allocator) {
    const uint8_t outbound = UINT8_C(0x52);
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray read_buffer;
    RRuntimeArray write_buffer;
    RStdNetVoidResult result = {0};
    RStdNetTcpReadResult read_result = {0};
    RStdNetTcpWriteAllResult write_result = {0};
    uint8_t observed = 0U;

    R_TEST_CHECK(pair.stream.storage != NULL);
    R_TEST_CHECK(await_void(r_std_net_tcp_shutdown(
                                &pair.stream, R_STD_NET_SHUTDOWN_READ, (RStdNetDeadline){0}),
                            &result) &&
                 result.r_tag == UINT32_C(0));
    read_buffer = byte_array(allocator, 1U, UINT8_C(0x7a));
    R_TEST_CHECK(await_read(r_std_net_tcp_read(&pair.stream, &read_buffer, (RStdNetDeadline){0}),
                            &read_result));
    R_TEST_CHECK(read_result.kind == R_STD_NET_TCP_READ_RESULT_END && read_result.count == 0U &&
                 ((const uint8_t *)read_result.buffer.data)[0] == UINT8_C(0x7a));
    r_runtime_array_destroy(&read_result.buffer);

    write_buffer = byte_array(allocator, 1U, outbound);
    R_TEST_CHECK(await_write_all(
        r_std_net_tcp_write_all(&pair.stream, &write_buffer, (RStdNetDeadline){0}), &write_result));
    R_TEST_CHECK(write_result.kind == R_STD_NET_TCP_WRITE_ALL_RESULT_WRITTEN &&
                 write_result.written == 1U && receive_exact(pair.peer, &observed, 1U) &&
                 observed == outbound);
    r_runtime_array_destroy(&write_result.buffer);
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_direction_ordering_and_independence(RRuntimeAllocator *allocator) {
    static const uint8_t bytes[] = {UINT8_C(0x61), UINT8_C(0x62), UINT8_C(0x63)};
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray write_buffer = byte_array(allocator, sizeof(bytes), UINT8_C(0));
    RStdNetTaskStartResult write_started;
    RStdNetTaskStartResult shutdown_started;
    RStdNetTcpWriteAllResult write_result = {0};
    RStdNetVoidResult shutdown_result = {0};
    uint8_t observed[sizeof(bytes)] = {0};

    R_TEST_CHECK(pair.stream.storage != NULL);
    (void)memcpy(write_buffer.data, bytes, sizeof(bytes));
    r_runtime_darwin_io_testing_pause_next_native_completion();
    write_started = r_std_net_tcp_write_all(&pair.stream, &write_buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(write_started.is_ok && write_buffer.data == NULL);
    r_runtime_darwin_io_testing_wait_for_native_completion();
    shutdown_started =
        r_std_net_tcp_shutdown(&pair.stream, R_STD_NET_SHUTDOWN_WRITE, (RStdNetDeadline){0});
    R_TEST_CHECK(shutdown_started.is_ok && shutdown_started.task != NULL &&
                 r_runtime_task_state(shutdown_started.task) == R_RUNTIME_TASK_RUNNING);
    r_runtime_darwin_io_testing_release_native_completion();
    R_TEST_CHECK(await_write_all(write_started, &write_result));
    R_TEST_CHECK(write_result.kind == R_STD_NET_TCP_WRITE_ALL_RESULT_WRITTEN &&
                 write_result.written == sizeof(bytes));
    r_runtime_array_destroy(&write_result.buffer);
    R_TEST_CHECK(await_void(shutdown_started, &shutdown_result) &&
                 shutdown_result.r_tag == UINT32_C(0));
    R_TEST_CHECK(receive_exact(pair.peer, observed, sizeof(observed)) &&
                 memcmp(observed, bytes, sizeof(bytes)) == 0 && receive_eof(pair.peer));
    destroy_tcp_pair(&pair);

    pair = create_tcp_pair(allocator);
    R_TEST_CHECK(pair.stream.storage != NULL);
    {
        RRuntimeArray read_buffer = byte_array(allocator, 1U, UINT8_C(0));
        RStdNetTaskStartResult read_started =
            r_std_net_tcp_read(&pair.stream, &read_buffer, (RStdNetDeadline){0});

        R_TEST_CHECK(read_started.is_ok && read_started.task != NULL && read_buffer.data == NULL);
        R_TEST_CHECK(await_void(r_std_net_tcp_shutdown(
                                    &pair.stream, R_STD_NET_SHUTDOWN_WRITE, (RStdNetDeadline){0}),
                                &shutdown_result) &&
                     shutdown_result.r_tag == UINT32_C(0) &&
                     r_runtime_task_state(read_started.task) == R_RUNTIME_TASK_RUNNING &&
                     receive_eof(pair.peer));
        r_runtime_task_cancel(&read_started.task);
        R_TEST_CHECK(read_started.task == NULL);
    }
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_read_shutdown_fifo(RRuntimeAllocator *allocator) {
    const uint8_t byte = UINT8_C(0x71);
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray first_buffer = byte_array(allocator, 1U, UINT8_C(0));
    RRuntimeArray later_buffer;
    RStdNetTaskStartResult first_started;
    RStdNetTaskStartResult shutdown_started;
    RStdNetTcpReadResult first_result = {0};
    RStdNetTcpReadResult later_result = {0};
    RStdNetVoidResult shutdown_result = {0};

    R_TEST_CHECK(pair.stream.storage != NULL);
    first_started = r_std_net_tcp_read(&pair.stream, &first_buffer, (RStdNetDeadline){0});
    shutdown_started =
        r_std_net_tcp_shutdown(&pair.stream, R_STD_NET_SHUTDOWN_READ, (RStdNetDeadline){0});
    R_TEST_CHECK(first_started.is_ok && shutdown_started.is_ok &&
                 r_runtime_task_state(shutdown_started.task) == R_RUNTIME_TASK_RUNNING);
    R_TEST_CHECK(send_exact(pair.peer, &byte, sizeof(byte)));
    R_TEST_CHECK(await_read(first_started, &first_result));
    R_TEST_CHECK(first_result.kind == R_STD_NET_TCP_READ_RESULT_READ && first_result.count == 1U &&
                 ((const uint8_t *)first_result.buffer.data)[0] == byte);
    r_runtime_array_destroy(&first_result.buffer);
    R_TEST_CHECK(await_void(shutdown_started, &shutdown_result) &&
                 shutdown_result.r_tag == UINT32_C(0));

    later_buffer = byte_array(allocator, 1U, UINT8_C(0x72));
    R_TEST_CHECK(await_read(r_std_net_tcp_read(&pair.stream, &later_buffer, (RStdNetDeadline){0}),
                            &later_result));
    R_TEST_CHECK(later_result.kind == R_STD_NET_TCP_READ_RESULT_END && later_result.count == 0U &&
                 ((const uint8_t *)later_result.buffer.data)[0] == UINT8_C(0x72));
    r_runtime_array_destroy(&later_result.buffer);
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_cancelled_and_timed_out_shutdown_do_not_commit(RRuntimeAllocator *allocator) {
    const uint8_t first = UINT8_C(0x81);
    const uint8_t second = UINT8_C(0x82);
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray first_buffer = byte_array(allocator, 1U, UINT8_C(0));
    RRuntimeArray second_buffer;
    RStdNetTaskStartResult first_started;
    RStdNetTaskStartResult shutdown_started;
    RStdNetTcpReadResult first_result = {0};
    RStdNetTcpReadResult second_result = {0};
    RStdNetVoidResult shutdown_result = {0};

    R_TEST_CHECK(pair.stream.storage != NULL);
    first_started = r_std_net_tcp_read(&pair.stream, &first_buffer, (RStdNetDeadline){0});
    shutdown_started =
        r_std_net_tcp_shutdown(&pair.stream, R_STD_NET_SHUTDOWN_READ, (RStdNetDeadline){0});
    R_TEST_CHECK(first_started.is_ok && shutdown_started.is_ok);
    r_runtime_task_cancel(&shutdown_started.task);
    R_TEST_CHECK(shutdown_started.task == NULL && send_exact(pair.peer, &first, sizeof(first)));
    R_TEST_CHECK(await_read(first_started, &first_result));
    R_TEST_CHECK(first_result.kind == R_STD_NET_TCP_READ_RESULT_READ &&
                 ((const uint8_t *)first_result.buffer.data)[0] == first);
    r_runtime_array_destroy(&first_result.buffer);
    second_buffer = byte_array(allocator, 1U, UINT8_C(0));
    first_started = r_std_net_tcp_read(&pair.stream, &second_buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(send_exact(pair.peer, &second, sizeof(second)));
    R_TEST_CHECK(await_read(first_started, &second_result));
    R_TEST_CHECK(second_result.kind == R_STD_NET_TCP_READ_RESULT_READ &&
                 ((const uint8_t *)second_result.buffer.data)[0] == second);
    r_runtime_array_destroy(&second_result.buffer);
    destroy_tcp_pair(&pair);

    pair = create_tcp_pair(allocator);
    R_TEST_CHECK(pair.stream.storage != NULL);
    first_buffer = byte_array(allocator, 1U, UINT8_C(0));
    first_started = r_std_net_tcp_read(&pair.stream, &first_buffer, (RStdNetDeadline){0});
    shutdown_started = r_std_net_tcp_shutdown(
        &pair.stream, R_STD_NET_SHUTDOWN_READ, deadline_after_milliseconds(UINT32_C(20)));
    R_TEST_CHECK(await_void(shutdown_started, &shutdown_result));
    R_TEST_CHECK(shutdown_result.r_tag == UINT32_C(1) &&
                 shutdown_result.error.code == R_STD_NET_ERROR_TIMED_OUT &&
                 send_exact(pair.peer, &first, sizeof(first)));
    R_TEST_CHECK(await_read(first_started, &first_result));
    R_TEST_CHECK(first_result.kind == R_STD_NET_TCP_READ_RESULT_READ &&
                 ((const uint8_t *)first_result.buffer.data)[0] == first);
    r_runtime_array_destroy(&first_result.buffer);
    destroy_tcp_pair(&pair);
    return 0;
}

/* Task cancellation reaches a queued request only through the executor, so the shutdown worker
   can arrive at the native half-close first; a cancellation selected before that entry must still
   keep the read side open. */
static int test_cancel_before_shutdown_entry_does_not_commit(RRuntimeAllocator *allocator) {
    const uint8_t first = UINT8_C(0x91);
    const uint8_t second = UINT8_C(0x92);
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray first_buffer = byte_array(allocator, 1U, UINT8_C(0));
    RRuntimeArray second_buffer;
    RStdNetTaskStartResult read_started;
    RStdNetTaskStartResult shutdown_started;
    RStdNetTcpReadResult first_result = {0};
    RStdNetTcpReadResult second_result = {0};

    R_TEST_CHECK(pair.stream.storage != NULL);
    read_started = r_std_net_tcp_read(&pair.stream, &first_buffer, (RStdNetDeadline){0});
    r_runtime_darwin_io_testing_pause_next_shutdown_entry();
    shutdown_started =
        r_std_net_tcp_shutdown(&pair.stream, R_STD_NET_SHUTDOWN_READ, (RStdNetDeadline){0});
    R_TEST_CHECK(read_started.is_ok && shutdown_started.is_ok);
    R_TEST_CHECK(send_exact(pair.peer, &first, sizeof(first)));
    R_TEST_CHECK(await_read(read_started, &first_result));
    R_TEST_CHECK(first_result.kind == R_STD_NET_TCP_READ_RESULT_READ &&
                 ((const uint8_t *)first_result.buffer.data)[0] == first);
    r_runtime_array_destroy(&first_result.buffer);
    r_runtime_darwin_io_testing_wait_for_shutdown_entry();
    r_runtime_task_cancel(&shutdown_started.task);
    R_TEST_CHECK(shutdown_started.task == NULL);
    r_runtime_darwin_io_testing_release_shutdown_entry();
    second_buffer = byte_array(allocator, 1U, UINT8_C(0));
    read_started = r_std_net_tcp_read(&pair.stream, &second_buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(send_exact(pair.peer, &second, sizeof(second)));
    R_TEST_CHECK(await_read(read_started, &second_result));
    R_TEST_CHECK(second_result.kind == R_STD_NET_TCP_READ_RESULT_READ &&
                 ((const uint8_t *)second_result.buffer.data)[0] == second);
    r_runtime_array_destroy(&second_result.buffer);
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_shutdown_waits_for_native_ack(RRuntimeAllocator *allocator) {
    RTestTcpPair pair = create_tcp_pair(allocator);
    RStdNetTcpStreamStorage *storage;
    RStdNetTaskStartResult started;
    RStdNetVoidResult result = {0};
    _Bool write_shutdown;

    R_TEST_CHECK(pair.stream.storage != NULL);
    storage = pair.stream.storage;
    r_runtime_darwin_io_testing_pause_next_native_completion();
    started = r_std_net_tcp_shutdown(&pair.stream, R_STD_NET_SHUTDOWN_WRITE, (RStdNetDeadline){0});
    R_TEST_CHECK(started.is_ok && started.task != NULL);
    r_runtime_darwin_io_testing_wait_for_native_completion();
    R_TEST_CHECK(r_runtime_task_state(started.task) == R_RUNTIME_TASK_RUNNING);
    if (pthread_mutex_lock(&storage->handle.mutex) != 0) {
        abort();
    }
    write_shutdown = storage->handle.write_shutdown;
    if (pthread_mutex_unlock(&storage->handle.mutex) != 0) {
        abort();
    }
    R_TEST_CHECK(write_shutdown);
    r_runtime_darwin_io_testing_release_native_completion();
    R_TEST_CHECK(await_void(started, &result) && result.r_tag == UINT32_C(0) &&
                 receive_eof(pair.peer));
    destroy_tcp_pair(&pair);
    return 0;
}

static int storage_is_terminal(RStdNetTcpStreamStorage *storage) {
    _Bool terminal;

    if (pthread_mutex_lock(&storage->handle.mutex) != 0) {
        abort();
    }
    terminal =
        storage->handle.terminal && storage->handle.descriptor < 0 && storage->data_io == NULL;
    if (pthread_mutex_unlock(&storage->handle.mutex) != 0) {
        abort();
    }
    return terminal;
}

static int test_close_drains_and_consumes(RRuntimeAllocator *allocator) {
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray read_buffer = byte_array(allocator, 1U, UINT8_C(0x91));
    RStdNetTaskStartResult read_started;
    RStdNetTaskStartResult close_started;
    RStdNetTcpReadResult read_result = {0};
    RStdNetVoidResult close_result = {0};
    RStdNetTcpStreamStorage *storage;
    int descriptor;

    R_TEST_CHECK(pair.stream.storage != NULL);
    storage = pair.stream.storage;
    descriptor = storage->handle.descriptor;
    R_TEST_CHECK(r_library_internal_net_handle_retain(&storage->handle));
    read_started = r_std_net_tcp_read(&pair.stream, &read_buffer, (RStdNetDeadline){0});
    close_started = r_std_net_tcp_close(&pair.stream, (RStdNetDeadline){0});
    R_TEST_CHECK(close_started.is_ok && close_started.task != NULL && pair.stream.storage == NULL);
    R_TEST_CHECK(await_void(close_started, &close_result) && close_result.r_tag == UINT32_C(0));
    R_TEST_CHECK(await_read(read_started, &read_result));
    R_TEST_CHECK(read_result.kind == R_STD_NET_TCP_READ_RESULT_FAILED &&
                 read_result.error.code == R_STD_NET_ERROR_CANCELLED &&
                 ((const uint8_t *)read_result.buffer.data)[0] == UINT8_C(0x91));
    r_runtime_array_destroy(&read_result.buffer);
    R_TEST_CHECK(storage_is_terminal(storage) && receive_eof(pair.peer));
    errno = 0;
    R_TEST_CHECK(fcntl(descriptor, F_GETFD) < 0 && errno == EBADF);
    r_library_internal_net_handle_release(&storage->handle);
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_close_deadline_and_preexisting_failure(RRuntimeAllocator *allocator) {
    const RStdNetDeadline expired = {1, {0, 0U}};
    RTestTcpPair pair = create_tcp_pair(allocator);
    RStdNetTcpStreamStorage *storage;
    RStdNetVoidResult result = {0};
    int descriptor;

    R_TEST_CHECK(pair.stream.storage != NULL);
    storage = pair.stream.storage;
    descriptor = storage->handle.descriptor;
    R_TEST_CHECK(r_library_internal_net_handle_retain(&storage->handle));
    R_TEST_CHECK(await_void(r_std_net_tcp_close(&pair.stream, expired), &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(1) && result.error.code == R_STD_NET_ERROR_TIMED_OUT &&
                 result.error.native_code == 0 && pair.stream.storage == NULL &&
                 storage_is_terminal(storage));
    errno = 0;
    R_TEST_CHECK(fcntl(descriptor, F_GETFD) < 0 && errno == EBADF);
    r_library_internal_net_handle_release(&storage->handle);
    destroy_tcp_pair(&pair);

    pair = create_tcp_pair(allocator);
    R_TEST_CHECK(pair.stream.storage != NULL);
    storage = pair.stream.storage;
    descriptor = storage->handle.descriptor;
    R_TEST_CHECK(r_library_internal_net_handle_retain(&storage->handle) && close(descriptor) == 0);
    R_TEST_CHECK(await_void(r_std_net_tcp_close(&pair.stream, expired), &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(1) && result.error.code == R_STD_NET_ERROR_CLOSED &&
                 result.error.native_code == EBADF && storage_is_terminal(storage));
    r_library_internal_net_handle_release(&storage->handle);
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_close_cancel_waits_for_ack(RRuntimeAllocator *allocator) {
    RTestTcpPair pair = create_tcp_pair(allocator);
    RStdNetTcpStreamStorage *storage;
    RStdNetTaskStartResult started;
    RTestCancelContext cancel_context;
    pthread_t cancel_thread;

    R_TEST_CHECK(pair.stream.storage != NULL);
    storage = pair.stream.storage;
    R_TEST_CHECK(r_library_internal_net_handle_retain(&storage->handle));
    r_runtime_darwin_io_testing_pause_next_root_cleanup_before_report();
    started = r_std_net_tcp_close(&pair.stream, (RStdNetDeadline){0});
    R_TEST_CHECK(started.is_ok && started.task != NULL && pair.stream.storage == NULL);
    r_runtime_darwin_io_testing_wait_for_root_cleanup_before_report();
    R_TEST_CHECK(r_runtime_task_state(started.task) == R_RUNTIME_TASK_RUNNING &&
                 !storage_is_terminal(storage));
    r_library_internal_net_tcp_close_testing_arm_cancel_acknowledgement();
    cancel_context.task = &started.task;
    R_TEST_CHECK(pthread_create(&cancel_thread, NULL, cancel_task, &cancel_context) == 0);
    r_library_internal_net_tcp_close_testing_wait_cancel_reported();
    R_TEST_CHECK(!storage_is_terminal(storage));
    r_runtime_darwin_io_testing_release_root_cleanup_before_report();
    R_TEST_CHECK(pthread_join(cancel_thread, NULL) == 0);
    R_TEST_CHECK(started.task == NULL);
    r_library_internal_net_tcp_close_testing_wait_cancel_acknowledgement();
    R_TEST_CHECK(storage_is_terminal(storage));
    R_TEST_CHECK(receive_eof(pair.peer));
    r_library_internal_net_handle_release(&storage->handle);
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_close_native_failure_still_releases(RRuntimeAllocator *allocator) {
    RTestTcpPair pair = create_tcp_pair(allocator);
    RStdNetTcpStreamStorage *storage;
    RStdNetVoidResult result = {0};

    R_TEST_CHECK(pair.stream.storage != NULL);
    storage = pair.stream.storage;
    R_TEST_CHECK(r_library_internal_net_handle_retain(&storage->handle));
    r_runtime_darwin_io_testing_force_root_cleanup_error(EIO);
    R_TEST_CHECK(await_void(r_std_net_tcp_close(&pair.stream, (RStdNetDeadline){0}), &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(1) && result.error.code == R_STD_NET_ERROR_OTHER &&
                 result.error.native_code == EIO && pair.stream.storage == NULL &&
                 storage_is_terminal(storage));
    r_library_internal_net_handle_release(&storage->handle);
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_start_failures_preserve_stream(RRuntimeAllocator *allocator) {
    const RRuntimeDarwinIoPrepareFailureStage stages[] = {
        R_RUNTIME_DARWIN_IO_PREPARE_FAIL_REQUEST,
        R_RUNTIME_DARWIN_IO_PREPARE_FAIL_DEADLINE,
    };
    size_t index;

    for (index = 0U; index != sizeof(stages) / sizeof(stages[0]); ++index) {
        RTestTcpPair pair = create_tcp_pair(allocator);
        RStdNetTcpStreamStorage *unchanged;
        RStdNetTaskStartResult started;
        RStdNetDeadline deadline =
            index == 0U ? (RStdNetDeadline){0} : deadline_after_milliseconds(UINT32_C(1000));

        R_TEST_CHECK(pair.stream.storage != NULL);
        unchanged = pair.stream.storage;
        r_runtime_darwin_io_testing_fail_prepare_stage(stages[index]);
        started = r_std_net_tcp_shutdown(&pair.stream, R_STD_NET_SHUTDOWN_WRITE, deadline);
        R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                     started.error == R_STD_ASYNC_START_ALLOCATION_FAILED &&
                     pair.stream.storage == unchanged);
        destroy_tcp_pair(&pair);
    }
    for (index = 0U; index != sizeof(stages) / sizeof(stages[0]); ++index) {
        RTestTcpPair pair = create_tcp_pair(allocator);
        RStdNetTcpStreamStorage *unchanged;
        RStdNetTaskStartResult started;
        RStdNetDeadline deadline =
            index == 0U ? (RStdNetDeadline){0} : deadline_after_milliseconds(UINT32_C(1000));

        R_TEST_CHECK(pair.stream.storage != NULL);
        unchanged = pair.stream.storage;
        r_runtime_darwin_io_testing_fail_prepare_stage(stages[index]);
        started = r_std_net_tcp_close(&pair.stream, deadline);
        R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                     started.error == R_STD_ASYNC_START_ALLOCATION_FAILED &&
                     pair.stream.storage == unchanged);
        destroy_tcp_pair(&pair);
    }
    return 0;
}

static int test_runtime_stop_preserves_close_operand(RRuntimeAllocator *allocator) {
    RTestTcpPair shutdown_pair = create_tcp_pair(allocator);
    RTestTcpPair close_pair = create_tcp_pair(allocator);
    RStdNetTcpStreamStorage *unchanged;
    RStdNetTaskStartResult started;
    RTestStopContext stop_context = {0};
    pthread_t stop_thread;

    R_TEST_CHECK(shutdown_pair.stream.storage != NULL && close_pair.stream.storage != NULL);
    R_TEST_CHECK(pthread_create(&stop_thread, NULL, stop_executor, &stop_context) == 0 &&
                 wait_for_executor_stopping());
    started = r_std_net_tcp_shutdown(
        &shutdown_pair.stream, R_STD_NET_SHUTDOWN_WRITE, (RStdNetDeadline){0});
    R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                 started.error == R_STD_ASYNC_START_RUNTIME_STOPPING);
    unchanged = close_pair.stream.storage;
    started = r_std_net_tcp_close(&close_pair.stream, (RStdNetDeadline){0});
    R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                 started.error == R_STD_ASYNC_START_RUNTIME_STOPPING &&
                 close_pair.stream.storage == unchanged);
    drop_tcp_pair(&shutdown_pair);
    drop_tcp_pair(&close_pair);
    R_TEST_CHECK(pthread_join(stop_thread, NULL) == 0 && stop_context.stopped);
    return 0;
}

int main(void) {
    RRuntimeAllocator allocator;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    R_TEST_CHECK(test_write_shutdown_and_idempotence(&allocator) == 0);
    R_TEST_CHECK(test_read_shutdown(&allocator) == 0);
    R_TEST_CHECK(test_direction_ordering_and_independence(&allocator) == 0);
    R_TEST_CHECK(test_read_shutdown_fifo(&allocator) == 0);
    R_TEST_CHECK(test_cancelled_and_timed_out_shutdown_do_not_commit(&allocator) == 0);
    R_TEST_CHECK(test_cancel_before_shutdown_entry_does_not_commit(&allocator) == 0);
    R_TEST_CHECK(test_shutdown_waits_for_native_ack(&allocator) == 0);
    R_TEST_CHECK(test_close_drains_and_consumes(&allocator) == 0);
    R_TEST_CHECK(test_close_deadline_and_preexisting_failure(&allocator) == 0);
    R_TEST_CHECK(test_close_cancel_waits_for_ack(&allocator) == 0);
    R_TEST_CHECK(test_close_native_failure_still_releases(&allocator) == 0);
    R_TEST_CHECK(test_start_failures_preserve_stream(&allocator) == 0);
    R_TEST_CHECK(test_runtime_stop_preserves_close_operand(&allocator) == 0);
    (void)puts("library_net_terminal_tests: ok");
    return EXIT_SUCCESS;
}
