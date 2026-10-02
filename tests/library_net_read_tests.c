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

static RRuntimeDarwinIoHandle *retain_stream_data_io(RStdNetTcpStreamStorage *storage) {
    RRuntimeDarwinIoHandle *data_io;
    _Bool retained = 0;

    if (storage == NULL || pthread_mutex_lock(&storage->handle.mutex) != 0) {
        abort();
    }
    data_io = storage->data_io;
    if (data_io != NULL) {
        retained = r_runtime_darwin_io_handle_retain_view(data_io);
    }
    if (pthread_mutex_unlock(&storage->handle.mutex) != 0) {
        abort();
    }
    return retained ? data_io : NULL;
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

static int test_partial_read_and_eof(RRuntimeAllocator *allocator) {
    static const uint8_t input[] = {UINT8_C(0x61), UINT8_C(0x62), UINT8_C(0x63)};
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray buffer = byte_array(allocator, 8U, UINT8_C(0xdd));
    RRuntimeArray end_buffer;
    RStdNetTcpReadResult result = {0};
    RStdNetTcpReadResult end = {0};
    void *owner = buffer.data;

    R_TEST_CHECK(pair.stream.storage != NULL && pair.peer >= 0);
    R_TEST_CHECK(!stream_has_data_io(pair.stream.storage));
    R_TEST_CHECK(send_exact(pair.peer, input, sizeof(input)));
    R_TEST_CHECK(
        await_read(r_std_net_tcp_read(&pair.stream, &buffer, (RStdNetDeadline){0}), &result));
    R_TEST_CHECK(result.kind == R_STD_NET_TCP_READ_RESULT_READ && result.count == sizeof(input) &&
                 result.buffer.data == owner);
    R_TEST_CHECK(memcmp(result.buffer.data, input, sizeof(input)) == 0);
    R_TEST_CHECK(((const uint8_t *)result.buffer.data)[3] == UINT8_C(0xdd) &&
                 ((const uint8_t *)result.buffer.data)[7] == UINT8_C(0xdd));
    R_TEST_CHECK(stream_has_data_io(pair.stream.storage));
    r_runtime_array_destroy(&result.buffer);

    R_TEST_CHECK(shutdown(pair.peer, SHUT_WR) == 0);
    end_buffer = byte_array(allocator, 4U, UINT8_C(0xee));
    owner = end_buffer.data;
    R_TEST_CHECK(
        await_read(r_std_net_tcp_read(&pair.stream, &end_buffer, (RStdNetDeadline){0}), &end));
    R_TEST_CHECK(end.kind == R_STD_NET_TCP_READ_RESULT_END && end.count == 0U &&
                 end.buffer.data == owner);
    R_TEST_CHECK(((const uint8_t *)end.buffer.data)[0] == UINT8_C(0xee) &&
                 ((const uint8_t *)end.buffer.data)[3] == UINT8_C(0xee));
    r_runtime_array_destroy(&end.buffer);
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_empty_shutdown_and_closed_outcomes(RRuntimeAllocator *allocator) {
    const RStdNetDeadline expired = {1, {0, 0U}};
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray empty = byte_array(allocator, 0U, UINT8_C(0));
    RRuntimeArray shutdown_buffer;
    RRuntimeArray closed_buffer;
    RStdNetTcpReadResult result = {0};

    R_TEST_CHECK(pair.stream.storage != NULL);
    if (pthread_mutex_lock(&pair.stream.storage->handle.mutex) != 0) {
        abort();
    }
    pair.stream.storage->handle.read_shutdown = 1;
    if (pthread_mutex_unlock(&pair.stream.storage->handle.mutex) != 0) {
        abort();
    }
    R_TEST_CHECK(await_read(r_std_net_tcp_read(&pair.stream, &empty, expired), &result));
    R_TEST_CHECK(result.kind == R_STD_NET_TCP_READ_RESULT_READ && result.count == 0U);
    r_runtime_array_destroy(&result.buffer);

    shutdown_buffer = byte_array(allocator, 2U, UINT8_C(0x91));
    R_TEST_CHECK(await_read(
        r_std_net_tcp_read(&pair.stream, &shutdown_buffer, (RStdNetDeadline){0}), &result));
    R_TEST_CHECK(result.kind == R_STD_NET_TCP_READ_RESULT_END && result.count == 0U &&
                 ((const uint8_t *)result.buffer.data)[0] == UINT8_C(0x91));
    r_runtime_array_destroy(&result.buffer);

    if (pthread_mutex_lock(&pair.stream.storage->handle.mutex) != 0) {
        abort();
    }
    pair.stream.storage->handle.read_shutdown = 0;
    pair.stream.storage->handle.close_reserved = 1;
    if (pthread_mutex_unlock(&pair.stream.storage->handle.mutex) != 0) {
        abort();
    }
    closed_buffer = byte_array(allocator, 2U, UINT8_C(0x92));
    R_TEST_CHECK(await_read(r_std_net_tcp_read(&pair.stream, &closed_buffer, (RStdNetDeadline){0}),
                            &result));
    R_TEST_CHECK(result.kind == R_STD_NET_TCP_READ_RESULT_FAILED && result.count == 0U &&
                 result.error.code == R_STD_NET_ERROR_CLOSED &&
                 ((const uint8_t *)result.buffer.data)[1] == UINT8_C(0x92));
    r_runtime_array_destroy(&result.buffer);
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

static int test_read_fifo_and_cancel_advancement(RRuntimeAllocator *allocator) {
    static const uint8_t input[] = {UINT8_C(0x41), UINT8_C(0x42)};
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray first_buffer = byte_array(allocator, 1U, UINT8_C(0));
    RRuntimeArray second_buffer = byte_array(allocator, 1U, UINT8_C(0));
    RStdNetTaskStartResult first;
    RStdNetTaskStartResult second;
    RStdNetTcpReadResult first_result = {0};
    RStdNetTcpReadResult second_result = {0};

    R_TEST_CHECK(pair.stream.storage != NULL);
    first = r_std_net_tcp_read(&pair.stream, &first_buffer, (RStdNetDeadline){0});
    second = r_std_net_tcp_read(&pair.stream, &second_buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(first.is_ok && second.is_ok && first_buffer.data == NULL &&
                 second_buffer.data == NULL);
    R_TEST_CHECK(send_exact(pair.peer, input, sizeof(input)));
    R_TEST_CHECK(await_read(second, &second_result));
    R_TEST_CHECK(await_read(first, &first_result));
    R_TEST_CHECK(first_result.kind == R_STD_NET_TCP_READ_RESULT_READ && first_result.count == 1U &&
                 ((const uint8_t *)first_result.buffer.data)[0] == input[0]);
    R_TEST_CHECK(second_result.kind == R_STD_NET_TCP_READ_RESULT_READ &&
                 second_result.count == 1U &&
                 ((const uint8_t *)second_result.buffer.data)[0] == input[1]);
    r_runtime_array_destroy(&first_result.buffer);
    r_runtime_array_destroy(&second_result.buffer);

    first_buffer = byte_array(allocator, 1U, UINT8_C(0x55));
    second_buffer = byte_array(allocator, 1U, UINT8_C(0x56));
    first = r_std_net_tcp_read(&pair.stream, &first_buffer, (RStdNetDeadline){0});
    second = r_std_net_tcp_read(&pair.stream, &second_buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(first.is_ok && second.is_ok);
    r_library_internal_net_tcp_read_testing_arm_cancel_acknowledgement();
    r_runtime_task_cancel(&first.task);
    R_TEST_CHECK(first.task == NULL);
    r_library_internal_net_tcp_read_testing_wait_cancel_acknowledgement();
    R_TEST_CHECK(send_exact(pair.peer, input + 1U, 1U));
    R_TEST_CHECK(await_read(second, &second_result));
    R_TEST_CHECK(second_result.kind == R_STD_NET_TCP_READ_RESULT_READ &&
                 second_result.count == 1U &&
                 ((const uint8_t *)second_result.buffer.data)[0] == input[1]);
    r_runtime_array_destroy(&second_result.buffer);
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_read_and_output_directions_are_independent(RRuntimeAllocator *allocator) {
    const uint8_t outbound = UINT8_C(0x73);
    const uint8_t inbound = UINT8_C(0x72);
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray read_buffer = byte_array(allocator, 1U, UINT8_C(0));
    RStdNetTaskStartResult read_started;
    RStdNetTcpReadResult read_result = {0};
    RRuntimeDarwinIoBufferResult allocated;
    RRuntimeDarwinIoBuffer returned;
    RRuntimeDarwinIoSubmitResult write_started;
    RRuntimeDarwinIoResult write_result;
    RRuntimeDarwinIoHandle *data_io;
    uint8_t observed = 0U;

    R_TEST_CHECK(pair.stream.storage != NULL);
    read_started = r_std_net_tcp_read(&pair.stream, &read_buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(read_started.is_ok && read_started.task != NULL);
    data_io = retain_stream_data_io(pair.stream.storage);
    R_TEST_CHECK(data_io != NULL);
    allocated = r_runtime_darwin_io_buffer_allocate(allocator, 1U);
    R_TEST_CHECK(allocated.status == R_RUNTIME_DARWIN_IO_START_OK && allocated.buffer.data != NULL);
    allocated.buffer.data[0] = outbound;
    allocated.buffer.size = 1U;
    write_started =
        r_runtime_darwin_io_submit_write_some(data_io, 0, &allocated.buffer, UINT64_C(0));
    R_TEST_CHECK(write_started.status == R_RUNTIME_DARWIN_IO_START_OK &&
                 write_started.request != NULL);
    write_result = r_runtime_darwin_io_request_wait(write_started.request);
    R_TEST_CHECK(write_result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE &&
                 write_result.native_error == 0 && write_result.bytes_transferred == 1U);
    returned = r_runtime_darwin_io_request_take_buffer(write_started.request);
    r_runtime_darwin_io_request_release(write_started.request);
    r_runtime_darwin_io_buffer_release(&returned);
    R_TEST_CHECK(receive_exact(pair.peer, &observed, 1U) && observed == outbound);
    R_TEST_CHECK(r_runtime_task_state(read_started.task) == R_RUNTIME_TASK_RUNNING);
    R_TEST_CHECK(send_exact(pair.peer, &inbound, 1U));
    R_TEST_CHECK(await_read(read_started, &read_result));
    R_TEST_CHECK(read_result.kind == R_STD_NET_TCP_READ_RESULT_READ &&
                 ((const uint8_t *)read_result.buffer.data)[0] == inbound);
    r_runtime_array_destroy(&read_result.buffer);
    r_runtime_darwin_io_handle_release(data_io);
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_partial_cancel_and_deadline(RRuntimeAllocator *allocator) {
    static const uint8_t input[] = {UINT8_C(0x70), UINT8_C(0x71), UINT8_C(0x72)};
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray buffer = byte_array(allocator, 8U, UINT8_C(0xcc));
    RStdNetTaskStartResult started;
    RStdNetTcpReadResult result = {0};
    void *owner = buffer.data;

    R_TEST_CHECK(pair.stream.storage != NULL);
    r_runtime_darwin_io_testing_pause_next_read_after_progress();
    started = r_std_net_tcp_read(&pair.stream, &buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(started.is_ok && started.task != NULL);
    R_TEST_CHECK(send_exact(pair.peer, input, sizeof(input)));
    r_runtime_darwin_io_testing_wait_for_read_after_progress();
    R_TEST_CHECK(r_runtime_darwin_io_testing_cancel_read_after_progress());
    r_runtime_darwin_io_testing_release_read_after_progress();
    R_TEST_CHECK(await_read(started, &result));
    R_TEST_CHECK(result.kind == R_STD_NET_TCP_READ_RESULT_FAILED &&
                 result.error.code == R_STD_NET_ERROR_CANCELLED && result.count == sizeof(input) &&
                 result.buffer.data == owner);
    R_TEST_CHECK(memcmp(result.buffer.data, input, sizeof(input)) == 0 &&
                 ((const uint8_t *)result.buffer.data)[3] == UINT8_C(0xcc) &&
                 ((const uint8_t *)result.buffer.data)[7] == UINT8_C(0xcc));
    r_runtime_array_destroy(&result.buffer);
    destroy_tcp_pair(&pair);

    pair = create_tcp_pair(allocator);
    buffer = byte_array(allocator, 8U, UINT8_C(0xcd));
    owner = buffer.data;
    r_runtime_darwin_io_testing_pause_next_read_after_progress();
    started =
        r_std_net_tcp_read(&pair.stream, &buffer, deadline_after_milliseconds(UINT32_C(1000)));
    R_TEST_CHECK(started.is_ok && started.task != NULL);
    R_TEST_CHECK(send_exact(pair.peer, input, sizeof(input)));
    r_runtime_darwin_io_testing_wait_for_read_after_progress();
    R_TEST_CHECK(r_runtime_darwin_io_testing_expire_read_after_progress());
    r_runtime_darwin_io_testing_release_read_after_progress();
    R_TEST_CHECK(await_read(started, &result));
    R_TEST_CHECK(result.kind == R_STD_NET_TCP_READ_RESULT_FAILED &&
                 result.error.code == R_STD_NET_ERROR_TIMED_OUT && result.count == sizeof(input) &&
                 result.buffer.data == owner);
    R_TEST_CHECK(memcmp(result.buffer.data, input, sizeof(input)) == 0 &&
                 ((const uint8_t *)result.buffer.data)[3] == UINT8_C(0xcd) &&
                 ((const uint8_t *)result.buffer.data)[7] == UINT8_C(0xcd));
    r_runtime_array_destroy(&result.buffer);
    destroy_tcp_pair(&pair);
    return 0;
}

static int test_deadline_and_task_cancellation_order(RRuntimeAllocator *allocator) {
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray buffer = byte_array(allocator, 4U, UINT8_C(0xa1));
    RStdNetTaskStartResult started;
    RStdNetTcpReadResult result = {0};
    RTestStopContext stop_context;
    pthread_t stop_thread;

    R_TEST_CHECK(pair.stream.storage != NULL);
    r_runtime_darwin_io_testing_pause_next_native_completion();
    started = r_std_net_tcp_read(&pair.stream, &buffer, deadline_after_milliseconds(UINT32_C(10)));
    R_TEST_CHECK(started.is_ok && started.task != NULL);
    r_runtime_darwin_io_testing_wait_for_native_completion();
    R_TEST_CHECK(begin_executor_stop(&stop_thread, &stop_context));
    r_runtime_darwin_io_testing_release_native_completion();
    R_TEST_CHECK(finish_executor_stop(stop_thread, &stop_context));
    R_TEST_CHECK(await_read(started, &result));
    R_TEST_CHECK(result.kind == R_STD_NET_TCP_READ_RESULT_FAILED && result.count == 0U &&
                 result.error.code == R_STD_NET_ERROR_TIMED_OUT &&
                 ((const uint8_t *)result.buffer.data)[0] == UINT8_C(0xa1));
    r_runtime_array_destroy(&result.buffer);
    destroy_tcp_pair(&pair);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(allocator) == R_RUNTIME_EXECUTOR_START_OK);

    pair = create_tcp_pair(allocator);
    buffer = byte_array(allocator, 4U, UINT8_C(0xa2));
    started =
        r_std_net_tcp_read(&pair.stream, &buffer, deadline_after_milliseconds(UINT32_C(1000)));
    R_TEST_CHECK(started.is_ok && started.task != NULL);
    R_TEST_CHECK(begin_executor_stop(&stop_thread, &stop_context));
    R_TEST_CHECK(finish_executor_stop(stop_thread, &stop_context));
    R_TEST_CHECK(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_CANCELLED &&
                 started.task == NULL);
    destroy_tcp_pair(&pair);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(allocator) == R_RUNTIME_EXECUTOR_START_OK);
    return 0;
}

static int test_native_result_precedes_task_cancel(RRuntimeAllocator *allocator) {
    const uint8_t byte = UINT8_C(0xb1);
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray buffer = byte_array(allocator, 1U, UINT8_C(0));
    RStdNetTaskStartResult started;
    RStdNetTcpReadResult result = {0};
    RTestStopContext stop_context;
    pthread_t stop_thread;

    R_TEST_CHECK(pair.stream.storage != NULL);
    r_runtime_darwin_io_testing_pause_next_native_completion();
    started = r_std_net_tcp_read(&pair.stream, &buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(started.is_ok && started.task != NULL);
    R_TEST_CHECK(send_exact(pair.peer, &byte, 1U));
    r_runtime_darwin_io_testing_wait_for_native_completion();
    R_TEST_CHECK(begin_executor_stop(&stop_thread, &stop_context));
    r_runtime_darwin_io_testing_release_native_completion();
    R_TEST_CHECK(finish_executor_stop(stop_thread, &stop_context));
    R_TEST_CHECK(await_read(started, &result));
    R_TEST_CHECK(result.kind == R_STD_NET_TCP_READ_RESULT_READ && result.count == 1U &&
                 ((const uint8_t *)result.buffer.data)[0] == byte);
    r_runtime_array_destroy(&result.buffer);
    destroy_tcp_pair(&pair);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(allocator) == R_RUNTIME_EXECUTOR_START_OK);
    return 0;
}

static int test_drop_after_start_keeps_request_alive(RRuntimeAllocator *allocator) {
    const uint8_t byte = UINT8_C(0xc1);
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray buffer = byte_array(allocator, 1U, UINT8_C(0));
    RStdNetTaskStartResult started;
    RStdNetTcpReadResult result = {0};
    int original_descriptor;

    R_TEST_CHECK(pair.stream.storage != NULL);
    original_descriptor = pair.stream.storage->handle.descriptor;
    started = r_std_net_tcp_read(&pair.stream, &buffer, (RStdNetDeadline){0});
    R_TEST_CHECK(started.is_ok && started.task != NULL && buffer.data == NULL);
    r_std_net_tcp_stream_destroy(&pair.stream);
    R_TEST_CHECK(pair.stream.storage == NULL);
    errno = 0;
    R_TEST_CHECK(fcntl(original_descriptor, F_GETFD) < 0 && errno == EBADF);
    R_TEST_CHECK(send_exact(pair.peer, &byte, 1U));
    R_TEST_CHECK(await_read(started, &result));
    R_TEST_CHECK(result.kind == R_STD_NET_TCP_READ_RESULT_READ && result.count == 1U &&
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
    };
    const uint8_t byte = UINT8_C(0xd1);
    size_t index;

    for (index = 0U; index != sizeof(handle_stages) / sizeof(handle_stages[0]); ++index) {
        RTestTcpPair pair = create_tcp_pair(allocator);
        RRuntimeArray buffer = byte_array(allocator, 1U, UINT8_C(0xd2));
        RRuntimeArray original = buffer;
        RStdNetTaskStartResult started;
        RStdNetTcpReadResult result = {0};

        R_TEST_CHECK(pair.stream.storage != NULL && !stream_has_data_io(pair.stream.storage));
        r_runtime_darwin_io_testing_fail_handle_create_stage(handle_stages[index]);
        started = r_std_net_tcp_read(&pair.stream, &buffer, (RStdNetDeadline){0});
        R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                     started.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
        R_TEST_CHECK(memcmp(&buffer, &original, sizeof(buffer)) == 0 &&
                     ((const uint8_t *)buffer.data)[0] == UINT8_C(0xd2));
        R_TEST_CHECK(send_exact(pair.peer, &byte, 1U));
        R_TEST_CHECK(
            await_read(r_std_net_tcp_read(&pair.stream, &buffer, (RStdNetDeadline){0}), &result));
        R_TEST_CHECK(result.kind == R_STD_NET_TCP_READ_RESULT_READ &&
                     ((const uint8_t *)result.buffer.data)[0] == byte);
        r_runtime_array_destroy(&result.buffer);
        destroy_tcp_pair(&pair);
    }

    {
        RTestTcpPair pair = create_tcp_pair(allocator);
        RRuntimeArray buffer = byte_array(allocator, 1U, UINT8_C(0xd3));
        RRuntimeArray original = buffer;
        RStdNetTaskStartResult started;

        r_runtime_darwin_io_testing_fail_next_handle_create_native(EMFILE);
        started = r_std_net_tcp_read(&pair.stream, &buffer, (RStdNetDeadline){0});
        R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                     started.error == R_STD_ASYNC_START_ALLOCATION_FAILED &&
                     memcmp(&buffer, &original, sizeof(buffer)) == 0);
        r_runtime_array_destroy(&buffer);
        destroy_tcp_pair(&pair);
    }

    for (index = 0U; index != sizeof(prepare_stages) / sizeof(prepare_stages[0]); ++index) {
        RTestTcpPair pair = create_tcp_pair(allocator);
        RRuntimeArray buffer = byte_array(allocator, 1U, UINT8_C(0xd4));
        RRuntimeArray original = buffer;
        RStdNetTaskStartResult started;
        RStdNetDeadline deadline = {0};

        if (prepare_stages[index] == R_RUNTIME_DARWIN_IO_PREPARE_FAIL_DEADLINE) {
            deadline = deadline_after_milliseconds(UINT32_C(1000));
        }
        r_runtime_darwin_io_testing_fail_prepare_stage(prepare_stages[index]);
        started = r_std_net_tcp_read(&pair.stream, &buffer, deadline);
        R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                     started.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
        R_TEST_CHECK(memcmp(&buffer, &original, sizeof(buffer)) == 0 &&
                     ((const uint8_t *)buffer.data)[0] == UINT8_C(0xd4));
        r_runtime_array_destroy(&buffer);
        destroy_tcp_pair(&pair);
    }
    return 0;
}

static int test_allocator_sweep_and_runtime_stop(RRuntimeAllocator *allocator) {
    const uint8_t byte = UINT8_C(0xe1);
    RTestTcpPair pair = create_tcp_pair(allocator);
    RRuntimeArray warmup = byte_array(allocator, 1U, UINT8_C(0));
    RStdNetTcpReadResult result = {0};
    uint64_t fail_at;
    _Bool reached_success = 0;

    R_TEST_CHECK(pair.stream.storage != NULL);
    R_TEST_CHECK(send_exact(pair.peer, &byte, 1U));
    R_TEST_CHECK(
        await_read(r_std_net_tcp_read(&pair.stream, &warmup, (RStdNetDeadline){0}), &result));
    r_runtime_array_destroy(&result.buffer);
    R_TEST_CHECK(stream_has_data_io(pair.stream.storage));

    for (fail_at = UINT64_C(1); fail_at <= UINT64_C(8); ++fail_at) {
        RRuntimeArray buffer = byte_array(allocator, 1U, UINT8_C(0xe2));
        RRuntimeArray original = buffer;
        RStdNetTaskStartResult started;

        r_runtime_allocator_set_failure(allocator, fail_at);
        started = r_std_net_tcp_read(&pair.stream, &buffer, (RStdNetDeadline){0});
        if (!started.is_ok) {
            R_TEST_CHECK(started.task == NULL &&
                         started.error == R_STD_ASYNC_START_ALLOCATION_FAILED &&
                         r_runtime_allocator_attempt_count(allocator) == fail_at);
            R_TEST_CHECK(memcmp(&buffer, &original, sizeof(buffer)) == 0 &&
                         ((const uint8_t *)buffer.data)[0] == UINT8_C(0xe2));
            r_runtime_allocator_set_failure(allocator, UINT64_C(0));
            r_runtime_array_destroy(&buffer);
            continue;
        }
        r_runtime_allocator_set_failure(allocator, UINT64_C(0));
        R_TEST_CHECK(buffer.data == NULL && send_exact(pair.peer, &byte, 1U));
        R_TEST_CHECK(await_read(started, &result));
        R_TEST_CHECK(result.kind == R_STD_NET_TCP_READ_RESULT_READ &&
                     ((const uint8_t *)result.buffer.data)[0] == byte);
        r_runtime_array_destroy(&result.buffer);
        reached_success = 1;
        break;
    }
    R_TEST_CHECK(reached_success && fail_at > UINT64_C(1));
    destroy_tcp_pair(&pair);

    pair = create_tcp_pair(allocator);
    {
        RRuntimeArray buffer = byte_array(allocator, 1U, UINT8_C(0xe3));
        RRuntimeArray original = buffer;
        RStdNetTaskStartResult started;

        R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
        started = r_std_net_tcp_read(&pair.stream, &buffer, (RStdNetDeadline){0});
        R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                     started.error == R_STD_ASYNC_START_RUNTIME_STOPPING &&
                     memcmp(&buffer, &original, sizeof(buffer)) == 0);
        R_TEST_CHECK(r_runtime_executor_lifecycle_start(allocator) == R_RUNTIME_EXECUTOR_START_OK);
        R_TEST_CHECK(send_exact(pair.peer, &byte, 1U));
        R_TEST_CHECK(
            await_read(r_std_net_tcp_read(&pair.stream, &buffer, (RStdNetDeadline){0}), &result));
        R_TEST_CHECK(result.kind == R_STD_NET_TCP_READ_RESULT_READ &&
                     ((const uint8_t *)result.buffer.data)[0] == byte);
        r_runtime_array_destroy(&result.buffer);
    }
    destroy_tcp_pair(&pair);
    return 0;
}

int main(void) {
    RRuntimeAllocator allocator;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    R_TEST_CHECK(test_partial_read_and_eof(&allocator) == 0);
    R_TEST_CHECK(test_empty_shutdown_and_closed_outcomes(&allocator) == 0);
    R_TEST_CHECK(test_read_fifo_and_cancel_advancement(&allocator) == 0);
    R_TEST_CHECK(test_read_and_output_directions_are_independent(&allocator) == 0);
    R_TEST_CHECK(test_partial_cancel_and_deadline(&allocator) == 0);
    R_TEST_CHECK(test_deadline_and_task_cancellation_order(&allocator) == 0);
    R_TEST_CHECK(test_native_result_precedes_task_cancel(&allocator) == 0);
    R_TEST_CHECK(test_drop_after_start_keeps_request_alive(&allocator) == 0);
    R_TEST_CHECK(test_native_reservation_failures_preserve_owner(&allocator) == 0);
    R_TEST_CHECK(test_allocator_sweep_and_runtime_stop(&allocator) == 0);
    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
    return 0;
}
