#include "r_runtime_darwin_io.h"

#include <dispatch/dispatch.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

/* The direct engines of runtime/darwin/source/io_direct.c: FILE handles (pread/pwrite on a private
   Dispatch queue) and SOCKET handles (immediate nonblocking attempt, readiness sources). */

static int test_failure(const char *file, int line, const char *expression) {
    (void)fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
    return 1;
}

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            return test_failure(__FILE__, __LINE__, #condition);                                   \
        }                                                                                          \
    } while (0)

typedef struct CompletionObservation {
    dispatch_semaphore_t semaphore;
    _Atomic unsigned int call_count;
    RRuntimeDarwinIoResult result;
} CompletionObservation;

typedef struct CleanupObservation {
    dispatch_semaphore_t semaphore;
    _Atomic unsigned int call_count;
    _Atomic int native_error;
} CleanupObservation;

static void observe_completion(RRuntimeDarwinIoRequest *request, void *context) {
    CompletionObservation *observation = context;

    observation->result = r_runtime_darwin_io_request_wait(request);
    (void)atomic_fetch_add_explicit(&observation->call_count, 1U, memory_order_release);
    (void)dispatch_semaphore_signal(observation->semaphore);
}

static void observe_cleanup(void *context, int native_error) {
    CleanupObservation *observation = context;

    atomic_store_explicit(&observation->native_error, native_error, memory_order_relaxed);
    (void)atomic_fetch_add_explicit(&observation->call_count, 1U, memory_order_release);
    (void)dispatch_semaphore_signal(observation->semaphore);
}

static void observation_init(CompletionObservation *observation) {
    observation->semaphore = dispatch_semaphore_create(0L);
    atomic_init(&observation->call_count, 0U);
    (void)memset(&observation->result, 0, sizeof(observation->result));
}

static _Bool wait_semaphore(dispatch_semaphore_t semaphore) {
    return dispatch_semaphore_wait(semaphore,
                                   dispatch_time(DISPATCH_TIME_NOW, INT64_C(5000000000))) == 0L;
}

static RRuntimeDarwinIoBuffer allocate_buffer(RRuntimeAllocator *allocator,
                                              size_t capacity,
                                              unsigned char fill) {
    RRuntimeDarwinIoBufferResult result = r_runtime_darwin_io_buffer_allocate(allocator, capacity);

    if (result.status != R_RUNTIME_DARWIN_IO_START_OK) {
        (void)memset(&result.buffer, 0, sizeof(result.buffer));
        return result.buffer;
    }
    (void)memset(result.buffer.data, fill, capacity);
    return result.buffer;
}

static int temporary_file(const unsigned char *bytes, size_t size, int flags) {
    char path[] = "/tmp/r-io-direct-XXXXXX";
    int descriptor = mkstemp(path);
    int reopened;

    if (descriptor < 0) {
        return -1;
    }
    if (write(descriptor, bytes, size) != (ssize_t)size) {
        (void)close(descriptor);
        (void)unlink(path);
        return -1;
    }
    reopened = open(path, flags);
    (void)close(descriptor);
    (void)unlink(path);
    return reopened;
}

static int close_handle(RRuntimeDarwinIoHandle *handle) {
    RRuntimeDarwinIoSubmitResult submission = r_runtime_darwin_io_submit_close(handle);
    RRuntimeDarwinIoResult result;

    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK && submission.request != NULL);
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_IO_CLOSE);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE && result.native_error == 0);
    r_runtime_darwin_io_request_release(submission.request);
    r_runtime_darwin_io_handle_release(handle);
    return 0;
}

/* Prepares a positioned read or write of a FILE handle, activates it and waits for its result. */
static int file_transfer(RRuntimeDarwinIoHandle *handle,
                         RRuntimeAllocator *allocator,
                         _Bool write_operation,
                         _Bool positioned,
                         off_t position,
                         const unsigned char *bytes,
                         size_t size,
                         RRuntimeDarwinIoResult *result,
                         RRuntimeDarwinIoBuffer *returned) {
    RRuntimeDarwinIoBuffer buffer = allocate_buffer(allocator, size, 0xeeU);
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult submission;
    CompletionObservation observation;

    CHECK(buffer.data != NULL);
    if (write_operation) {
        (void)memcpy(buffer.data, bytes, size);
        buffer.size = size;
        preparation = r_runtime_darwin_io_prepare_write(handle, (off_t)0, &buffer, UINT64_C(0));
    } else {
        preparation = r_runtime_darwin_io_prepare_read_some(handle, (off_t)0, &buffer, UINT64_C(0));
    }
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    if (positioned) {
        CHECK(r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, position));
    }
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    observation_init(&observation);
    CHECK(r_runtime_darwin_io_request_set_completion_inline(
        submission.request, observe_completion, &observation));
    CHECK(wait_semaphore(observation.semaphore));
    CHECK(atomic_load_explicit(&observation.call_count, memory_order_acquire) == 1U);
    *result = observation.result;
    *returned = r_runtime_darwin_io_request_take_buffer(submission.request);
    r_runtime_darwin_io_request_release(submission.request);
    dispatch_release(observation.semaphore);
    return 0;
}

static int test_file_positioned_transfers_leave_the_offset(void) {
    static const unsigned char initial[] = "0123456789";
    static const unsigned char replacement[] = {'A', 'B', 'C'};
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandleCreateResult created;
    RRuntimeDarwinIoResult result;
    RRuntimeDarwinIoBuffer returned;
    unsigned char observed[10];
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = temporary_file(initial, 10U, O_RDWR);
    CHECK(descriptor >= 0);
    created = r_runtime_darwin_io_handle_create_file(
        &allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(created.status == R_RUNTIME_DARWIN_IO_START_OK && created.handle != NULL);

    CHECK(file_transfer(created.handle, &allocator, 0, 1, (off_t)4, NULL, 4U, &result, &returned) ==
          0);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE && result.native_error == 0);
    CHECK(result.bytes_transferred == 4U && !result.eof && result.cleanup_acknowledged);
    CHECK(returned.size == 4U && memcmp(returned.data, "4567", 4U) == 0);
    r_runtime_darwin_io_buffer_release(&returned);
    CHECK(lseek(descriptor, 0, SEEK_CUR) == 0);

    CHECK(file_transfer(created.handle, &allocator, 1, 1, (off_t)2, replacement, 3U, &result,
                        &returned) == 0);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE && result.native_error == 0);
    CHECK(result.bytes_transferred == 3U);
    r_runtime_darwin_io_buffer_release(&returned);
    CHECK(pread(descriptor, observed, sizeof(observed), 0) == (ssize_t)sizeof(observed));
    CHECK(memcmp(observed, "01ABC56789", sizeof(observed)) == 0);
    CHECK(lseek(descriptor, 0, SEEK_CUR) == 0);

    CHECK(file_transfer(created.handle, &allocator, 0, 1, (off_t)10, NULL, 4U, &result,
                        &returned) == 0);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE && result.native_error == 0);
    CHECK(result.bytes_transferred == 0U && result.eof);
    CHECK(returned.size == 0U && returned.data[0] == 0xeeU);
    r_runtime_darwin_io_buffer_release(&returned);

    CHECK(close_handle(created.handle) == 0);
    CHECK(close(descriptor) == 0);
    return 0;
}

static int test_file_append_and_native_error(void) {
    static const unsigned char initial[] = {'x', 'y'};
    static const unsigned char tail[] = {'z', 'w'};
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandleCreateResult created;
    RRuntimeDarwinIoResult result;
    RRuntimeDarwinIoBuffer returned;
    unsigned char observed[4];
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = temporary_file(initial, sizeof(initial), O_RDWR | O_APPEND);
    CHECK(descriptor >= 0);
    created = r_runtime_darwin_io_handle_create_file(
        &allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(created.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(file_transfer(created.handle, &allocator, 1, 0, (off_t)0, tail, sizeof(tail), &result,
                        &returned) == 0);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE && result.native_error == 0);
    CHECK(result.bytes_transferred == sizeof(tail));
    r_runtime_darwin_io_buffer_release(&returned);
    CHECK(pread(descriptor, observed, sizeof(observed), 0) == (ssize_t)sizeof(observed));
    CHECK(memcmp(observed, "xyzw", sizeof(observed)) == 0);
    CHECK(close_handle(created.handle) == 0);
    CHECK(close(descriptor) == 0);

    descriptor = temporary_file(initial, sizeof(initial), O_RDONLY);
    CHECK(descriptor >= 0);
    created = r_runtime_darwin_io_handle_create_file(
        &allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(created.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(file_transfer(created.handle, &allocator, 1, 1, (off_t)0, tail, sizeof(tail), &result,
                        &returned) == 0);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == EBADF && result.bytes_transferred == 0U);
    r_runtime_darwin_io_buffer_release(&returned);
    CHECK(close_handle(created.handle) == 0);
    CHECK(close(descriptor) == 0);
    return 0;
}

/* A cancellation recorded while the worker waits at its entry finishes the request without the
   system call, and a close submitted meanwhile closes the descriptor only after the worker. */
static int test_file_cancel_before_entry_and_close_waits(void) {
    static const unsigned char initial[] = {'a', 'b', 'c', 'd'};
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandleCreateResult created;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoSubmitResult closing;
    RRuntimeDarwinIoResult result;
    CleanupObservation cleanup;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = temporary_file(initial, sizeof(initial), O_RDWR);
    CHECK(descriptor >= 0);
    created = r_runtime_darwin_io_handle_create_file(
        &allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(created.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_handle_retain_view(created.handle));

    buffer = allocate_buffer(&allocator, sizeof(initial), 0x77U);
    preparation =
        r_runtime_darwin_io_prepare_read_some(created.handle, (off_t)0, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, (off_t)0));
    r_runtime_darwin_io_testing_pause_next_stream_position_barrier();
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    r_runtime_darwin_io_testing_wait_for_stream_position_barrier();
    CHECK(r_runtime_darwin_io_request_cancel(submission.request));
    closing = r_runtime_darwin_io_submit_close(created.handle);
    CHECK(closing.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_request_state(closing.request) == R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE);
    CHECK(r_runtime_darwin_io_request_state(submission.request) ==
          R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE);
    r_runtime_darwin_io_testing_release_stream_position_barrier();
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED);
    CHECK(result.bytes_transferred == 0U &&
          result.terminal_event_sequence < result.native_event_sequence);
    returned = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned.size == 0U && returned.data[0] == 0x77U);
    r_runtime_darwin_io_buffer_release(&returned);
    r_runtime_darwin_io_request_release(submission.request);
    result = r_runtime_darwin_io_request_wait(closing.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE && result.native_error == 0);
    r_runtime_darwin_io_request_release(closing.request);

    cleanup.semaphore = dispatch_semaphore_create(0L);
    atomic_init(&cleanup.call_count, 0U);
    atomic_init(&cleanup.native_error, -1);
    CHECK(
        r_runtime_darwin_io_handle_release_with_cleanup(created.handle, observe_cleanup, &cleanup));
    CHECK(wait_semaphore(cleanup.semaphore));
    CHECK(atomic_load_explicit(&cleanup.native_error, memory_order_relaxed) == 0);
    r_runtime_darwin_io_handle_release(created.handle);
    dispatch_release(cleanup.semaphore);
    CHECK(close(descriptor) == 0);
    return 0;
}

typedef struct InlineCompletion {
    _Atomic unsigned int call_count;
    RRuntimeDarwinIoResult result;
} InlineCompletion;

static void record_inline_completion(RRuntimeDarwinIoRequest *request, void *context) {
    InlineCompletion *completion = context;

    completion->result = r_runtime_darwin_io_request_wait(request);
    (void)atomic_fetch_add_explicit(&completion->call_count, 1U, memory_order_release);
}

static int socket_pair(int descriptors[2]) {
    const int enabled = 1;

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, descriptors) != 0) {
        return -1;
    }
    if (fcntl(descriptors[0], F_SETFL, fcntl(descriptors[0], F_GETFL) | O_NONBLOCK) != 0 ||
        setsockopt(descriptors[0], SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0 ||
        setsockopt(descriptors[1], SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled)) != 0) {
        return -1;
    }
    return 0;
}

/* Data that is already there completes the read during activation: set_completion_inline runs the
   callback before it returns, on the calling thread. */
static int test_socket_immediate_read_and_write_publish_inline(void) {
    static const unsigned char input[] = {'p', 'i', 'n', 'g'};
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandleCreateResult created;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult submission;
    InlineCompletion completion;
    unsigned char observed[4];
    int descriptors[2];

    r_runtime_allocator_initialize(&allocator);
    CHECK(socket_pair(descriptors) == 0);
    created = r_runtime_darwin_io_handle_create_socket(&allocator, descriptors[0]);
    CHECK(created.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(write(descriptors[1], input, sizeof(input)) == (ssize_t)sizeof(input));

    buffer = allocate_buffer(&allocator, 16U, 0x11U);
    preparation =
        r_runtime_darwin_io_prepare_read_some(created.handle, (off_t)0, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_request_state(submission.request) ==
          R_RUNTIME_DARWIN_IO_REQUEST_TERMINAL);
    atomic_init(&completion.call_count, 0U);
    CHECK(r_runtime_darwin_io_request_set_completion_inline(
        submission.request, record_inline_completion, &completion));
    CHECK(atomic_load_explicit(&completion.call_count, memory_order_acquire) == 1U);
    CHECK(completion.result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(completion.result.bytes_transferred == sizeof(input) && !completion.result.eof);
    returned = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned.size == sizeof(input) && memcmp(returned.data, input, sizeof(input)) == 0 &&
          returned.data[4] == 0x11U);
    r_runtime_darwin_io_buffer_release(&returned);
    r_runtime_darwin_io_request_release(submission.request);

    buffer = allocate_buffer(&allocator, sizeof(input), 0U);
    (void)memcpy(buffer.data, input, sizeof(input));
    buffer.size = sizeof(input);
    preparation = r_runtime_darwin_io_prepare_write(created.handle, (off_t)0, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    atomic_init(&completion.call_count, 0U);
    CHECK(r_runtime_darwin_io_request_set_completion_inline(
        submission.request, record_inline_completion, &completion));
    CHECK(atomic_load_explicit(&completion.call_count, memory_order_acquire) == 1U);
    CHECK(completion.result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(completion.result.bytes_transferred == sizeof(input));
    returned = r_runtime_darwin_io_request_take_buffer(submission.request);
    r_runtime_darwin_io_buffer_release(&returned);
    r_runtime_darwin_io_request_release(submission.request);
    CHECK(read(descriptors[1], observed, sizeof(observed)) == (ssize_t)sizeof(observed));
    CHECK(memcmp(observed, input, sizeof(input)) == 0);

    CHECK(close_handle(created.handle) == 0);
    CHECK(close(descriptors[0]) == 0 && close(descriptors[1]) == 0);
    return 0;
}

static int submit_socket_read(RRuntimeDarwinIoHandle *handle,
                              RRuntimeAllocator *allocator,
                              uint64_t timeout_nanoseconds,
                              CompletionObservation *observation,
                              RRuntimeDarwinIoSubmitResult *submission) {
    RRuntimeDarwinIoBuffer buffer = allocate_buffer(allocator, 8U, 0x22U);
    RRuntimeDarwinIoPrepareResult preparation =
        r_runtime_darwin_io_prepare_read_some(handle, (off_t)0, &buffer, timeout_nanoseconds);

    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    *submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(submission->status == R_RUNTIME_DARWIN_IO_START_OK);
    observation_init(observation);
    CHECK(r_runtime_darwin_io_request_set_completion_inline(
        submission->request, observe_completion, observation));
    return 0;
}

/* A read without data waits for readiness and completes when the peer writes; a waiting read is
   cancelled or times out without consuming peer bytes. */
static int test_socket_wait_cancel_and_deadline(void) {
    static const unsigned char input[] = {'q'};
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandleCreateResult created;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoBuffer returned;
    CompletionObservation observation;
    int descriptors[2];

    r_runtime_allocator_initialize(&allocator);
    CHECK(socket_pair(descriptors) == 0);
    created = r_runtime_darwin_io_handle_create_socket(&allocator, descriptors[0]);
    CHECK(created.status == R_RUNTIME_DARWIN_IO_START_OK);

    CHECK(submit_socket_read(created.handle, &allocator, UINT64_C(0), &observation, &submission) ==
          0);
    CHECK(dispatch_semaphore_wait(observation.semaphore,
                                  dispatch_time(DISPATCH_TIME_NOW, INT64_C(50000000))) != 0L);
    CHECK(write(descriptors[1], input, sizeof(input)) == (ssize_t)sizeof(input));
    CHECK(wait_semaphore(observation.semaphore));
    CHECK(observation.result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(observation.result.bytes_transferred == 1U);
    returned = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned.data[0] == 'q' && returned.data[1] == 0x22U);
    r_runtime_darwin_io_buffer_release(&returned);
    r_runtime_darwin_io_request_release(submission.request);
    dispatch_release(observation.semaphore);

    CHECK(submit_socket_read(created.handle, &allocator, UINT64_C(0), &observation, &submission) ==
          0);
    CHECK(r_runtime_darwin_io_request_cancel(submission.request));
    CHECK(wait_semaphore(observation.semaphore));
    CHECK(observation.result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED);
    CHECK(observation.result.bytes_transferred == 0U && observation.result.cleanup_acknowledged);
    returned = r_runtime_darwin_io_request_take_buffer(submission.request);
    r_runtime_darwin_io_buffer_release(&returned);
    r_runtime_darwin_io_request_release(submission.request);
    dispatch_release(observation.semaphore);

    CHECK(submit_socket_read(created.handle, &allocator, UINT64_C(5000000), &observation,
                             &submission) == 0);
    CHECK(wait_semaphore(observation.semaphore));
    CHECK(observation.result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_TIMED_OUT);
    CHECK(observation.result.bytes_transferred == 0U);
    returned = r_runtime_darwin_io_request_take_buffer(submission.request);
    r_runtime_darwin_io_buffer_release(&returned);
    r_runtime_darwin_io_request_release(submission.request);
    dispatch_release(observation.semaphore);

    /* The bytes written after the cancelled and timed-out reads are still there. */
    CHECK(write(descriptors[1], input, sizeof(input)) == (ssize_t)sizeof(input));
    CHECK(submit_socket_read(created.handle, &allocator, UINT64_C(0), &observation, &submission) ==
          0);
    CHECK(wait_semaphore(observation.semaphore));
    CHECK(observation.result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE &&
          observation.result.bytes_transferred == 1U);
    returned = r_runtime_darwin_io_request_take_buffer(submission.request);
    r_runtime_darwin_io_buffer_release(&returned);
    r_runtime_darwin_io_request_release(submission.request);
    dispatch_release(observation.semaphore);

    CHECK(close_handle(created.handle) == 0);
    CHECK(close(descriptors[0]) == 0 && close(descriptors[1]) == 0);
    return 0;
}

typedef struct Drain {
    int descriptor;
    size_t expected;
    size_t received;
    _Bool intact;
} Drain;

static void *drain_peer(void *context) {
    Drain *drain = context;
    unsigned char chunk[65536];

    drain->intact = 1;
    while (drain->received < drain->expected) {
        ssize_t count = read(drain->descriptor, chunk, sizeof(chunk));
        size_t index;

        if (count <= 0) {
            break;
        }
        for (index = 0U; index != (size_t)count; ++index) {
            if (chunk[index] != (unsigned char)((drain->received + index) % 251U)) {
                drain->intact = 0;
            }
        }
        drain->received += (size_t)count;
    }
    return NULL;
}

/* A write larger than the socket buffers progresses, waits for writability and completes in
   order while the peer drains. */
static int test_socket_large_write_waits_for_writability(void) {
    const size_t size = 8U * 1024U * 1024U;
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandleCreateResult created;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult submission;
    CompletionObservation observation;
    Drain drain;
    pthread_t thread;
    size_t index;
    int descriptors[2];

    r_runtime_allocator_initialize(&allocator);
    CHECK(socket_pair(descriptors) == 0);
    created = r_runtime_darwin_io_handle_create_socket(&allocator, descriptors[0]);
    CHECK(created.status == R_RUNTIME_DARWIN_IO_START_OK);
    buffer = allocate_buffer(&allocator, size, 0U);
    CHECK(buffer.data != NULL);
    for (index = 0U; index != size; ++index) {
        buffer.data[index] = (unsigned char)(index % 251U);
    }
    buffer.size = size;
    preparation = r_runtime_darwin_io_prepare_write(created.handle, (off_t)0, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    observation_init(&observation);
    CHECK(r_runtime_darwin_io_request_set_completion_inline(
        submission.request, observe_completion, &observation));
    CHECK(r_runtime_darwin_io_request_progress(submission.request) < size);
    drain.descriptor = descriptors[1];
    drain.expected = size;
    drain.received = 0U;
    CHECK(pthread_create(&thread, NULL, drain_peer, &drain) == 0);
    CHECK(wait_semaphore(observation.semaphore));
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(observation.result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(observation.result.bytes_transferred == size && observation.result.native_error == 0);
    CHECK(drain.received == size && drain.intact);
    returned = r_runtime_darwin_io_request_take_buffer(submission.request);
    r_runtime_darwin_io_buffer_release(&returned);
    r_runtime_darwin_io_request_release(submission.request);
    dispatch_release(observation.semaphore);
    CHECK(close_handle(created.handle) == 0);
    CHECK(close(descriptors[0]) == 0 && close(descriptors[1]) == 0);
    return 0;
}

/* Closing a handle cancels a waiting read and closes the descriptor after it; dropping the last
   view of an idle handle runs the cleanup as well. */
static int test_socket_close_and_drop(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandleCreateResult created;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoSubmitResult closing;
    RRuntimeDarwinIoBuffer returned;
    RRuntimeDarwinIoResult result;
    CompletionObservation observation;
    CleanupObservation cleanup;
    int descriptors[2];

    r_runtime_allocator_initialize(&allocator);
    CHECK(socket_pair(descriptors) == 0);
    created = r_runtime_darwin_io_handle_create_socket(&allocator, descriptors[0]);
    CHECK(created.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(submit_socket_read(created.handle, &allocator, UINT64_C(0), &observation, &submission) ==
          0);
    closing = r_runtime_darwin_io_submit_close(created.handle);
    CHECK(closing.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(wait_semaphore(observation.semaphore));
    CHECK(observation.result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED);
    returned = r_runtime_darwin_io_request_take_buffer(submission.request);
    r_runtime_darwin_io_buffer_release(&returned);
    r_runtime_darwin_io_request_release(submission.request);
    dispatch_release(observation.semaphore);
    result = r_runtime_darwin_io_request_wait(closing.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE && result.native_error == 0);
    r_runtime_darwin_io_request_release(closing.request);
    r_runtime_darwin_io_handle_release(created.handle);

    created = r_runtime_darwin_io_handle_create_socket(&allocator, descriptors[0]);
    CHECK(created.status == R_RUNTIME_DARWIN_IO_START_OK);
    cleanup.semaphore = dispatch_semaphore_create(0L);
    atomic_init(&cleanup.call_count, 0U);
    atomic_init(&cleanup.native_error, -1);
    CHECK(
        r_runtime_darwin_io_handle_release_with_cleanup(created.handle, observe_cleanup, &cleanup));
    CHECK(wait_semaphore(cleanup.semaphore));
    CHECK(atomic_load_explicit(&cleanup.call_count, memory_order_acquire) == 1U);
    CHECK(atomic_load_explicit(&cleanup.native_error, memory_order_relaxed) == 0);
    dispatch_release(cleanup.semaphore);
    CHECK(close(descriptors[0]) == 0 && close(descriptors[1]) == 0);
    return 0;
}

typedef struct ReleasingCompletion {
    dispatch_semaphore_t semaphore;
    _Atomic unsigned int call_count;
    RRuntimeDarwinIoResult result;
    RRuntimeDarwinIoBuffer buffer;
} ReleasingCompletion;

/* Like the library adapters, the callback consumes the caller's last request reference. */
static void complete_and_release(RRuntimeDarwinIoRequest *request, void *context) {
    ReleasingCompletion *completion = context;

    completion->result = r_runtime_darwin_io_request_wait(request);
    completion->buffer = r_runtime_darwin_io_request_take_buffer(request);
    r_runtime_darwin_io_request_release(request);
    (void)atomic_fetch_add_explicit(&completion->call_count, 1U, memory_order_release);
    (void)dispatch_semaphore_signal(completion->semaphore);
}

/* Registration racing a file worker's completion whose callback releases the request: the
   completion runs exactly once and the registration's own delivery attempt never touches a freed
   request (it holds a reference of its own). */
static int test_inline_registration_races_worker_completion(void) {
    static const unsigned char initial[] = {'r', 'a', 'c', 'e'};
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandleCreateResult created;
    ReleasingCompletion completion;
    size_t round;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = temporary_file(initial, sizeof(initial), O_RDONLY);
    CHECK(descriptor >= 0);
    created = r_runtime_darwin_io_handle_create_file(
        &allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(created.status == R_RUNTIME_DARWIN_IO_START_OK);
    completion.semaphore = dispatch_semaphore_create(0L);
    for (round = 0U; round != 4000U; ++round) {
        RRuntimeDarwinIoBuffer buffer = allocate_buffer(&allocator, sizeof(initial), 0U);
        RRuntimeDarwinIoPrepareResult preparation;
        RRuntimeDarwinIoSubmitResult submission;

        CHECK(buffer.data != NULL);
        preparation =
            r_runtime_darwin_io_prepare_read_some(created.handle, (off_t)0, &buffer, UINT64_C(0));
        CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
        CHECK(r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, (off_t)0));
        submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
        CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
        atomic_init(&completion.call_count, 0U);
        CHECK(r_runtime_darwin_io_request_set_completion_inline(
            submission.request, complete_and_release, &completion));
        CHECK(wait_semaphore(completion.semaphore));
        CHECK(atomic_load_explicit(&completion.call_count, memory_order_acquire) == 1U);
        CHECK(completion.result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
        CHECK(completion.result.bytes_transferred == sizeof(initial));
        CHECK(memcmp(completion.buffer.data, initial, sizeof(initial)) == 0);
        r_runtime_darwin_io_buffer_release(&completion.buffer);
    }
    dispatch_release(completion.semaphore);
    CHECK(close_handle(created.handle) == 0);
    CHECK(close(descriptor) == 0);
    return 0;
}

typedef struct InlineRegistration {
    RRuntimeDarwinIoRequest *request;
    ReleasingCompletion *completion;
    _Bool accepted;
} InlineRegistration;

static void *register_inline(void *context) {
    InlineRegistration *registration = context;

    registration->accepted = r_runtime_darwin_io_request_set_completion_inline(
        registration->request, complete_and_release, registration->completion);
    return NULL;
}

/* The same race made deterministic: the registration stops after registering, the file worker
   then runs the completion, which drops the caller's reference, and drops its own; the
   registration's delivery attempt afterwards must find a live request. */
static int test_inline_registration_outlives_worker_completion(void) {
    static const unsigned char initial[] = {'h', 'o', 'l', 'd'};
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandleCreateResult created;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult submission;
    ReleasingCompletion completion;
    InlineRegistration registration;
    pthread_t thread;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = temporary_file(initial, sizeof(initial), O_RDONLY);
    CHECK(descriptor >= 0);
    created = r_runtime_darwin_io_handle_create_file(
        &allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(created.status == R_RUNTIME_DARWIN_IO_START_OK);
    buffer = allocate_buffer(&allocator, sizeof(initial), 0U);
    CHECK(buffer.data != NULL);
    preparation =
        r_runtime_darwin_io_prepare_read_some(created.handle, (off_t)0, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, (off_t)0));
    r_runtime_darwin_io_testing_pause_next_stream_position_barrier();
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    r_runtime_darwin_io_testing_wait_for_stream_position_barrier();

    completion.semaphore = dispatch_semaphore_create(0L);
    atomic_init(&completion.call_count, 0U);
    registration.request = submission.request;
    registration.completion = &completion;
    registration.accepted = 0;
    r_runtime_darwin_io_testing_pause_next_inline_registration();
    CHECK(pthread_create(&thread, NULL, register_inline, &registration) == 0);
    r_runtime_darwin_io_testing_wait_for_inline_registration();
    r_runtime_darwin_io_testing_pause_next_direct_worker_exit();
    r_runtime_darwin_io_testing_release_stream_position_barrier();
    r_runtime_darwin_io_testing_wait_for_direct_worker_exit();
    CHECK(atomic_load_explicit(&completion.call_count, memory_order_acquire) == 1U);
    r_runtime_darwin_io_testing_release_inline_registration();
    CHECK(pthread_join(thread, NULL) == 0);
    r_runtime_darwin_io_testing_release_direct_worker_exit();
    CHECK(registration.accepted);
    CHECK(atomic_load_explicit(&completion.call_count, memory_order_acquire) == 1U);
    CHECK(completion.result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(completion.result.bytes_transferred == sizeof(initial));
    CHECK(memcmp(completion.buffer.data, initial, sizeof(initial)) == 0);
    r_runtime_darwin_io_buffer_release(&completion.buffer);
    dispatch_release(completion.semaphore);
    CHECK(close_handle(created.handle) == 0);
    CHECK(close(descriptor) == 0);
    return 0;
}

/* A RANDOM file handle reads at offsets relative to the descriptor position when the handle took
   the descriptor, like a Dispatch I/O RANDOM channel, and never moves the descriptor offset. */
static int test_file_random_offsets_follow_the_creation_position(void) {
    static const unsigned char initial[] = "abcdefghij";
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandleCreateResult created;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult submission;
    CompletionObservation observation;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = temporary_file(initial, 10U, O_RDONLY);
    CHECK(descriptor >= 0);
    CHECK(lseek(descriptor, 3, SEEK_SET) == 3);
    created = r_runtime_darwin_io_handle_create_file(&allocator, descriptor,
                                                     R_RUNTIME_DARWIN_IO_RANDOM);
    CHECK(created.status == R_RUNTIME_DARWIN_IO_START_OK);
    buffer = allocate_buffer(&allocator, 4U, 0x55U);
    preparation =
        r_runtime_darwin_io_prepare_read_some(created.handle, (off_t)2, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    observation_init(&observation);
    CHECK(r_runtime_darwin_io_request_set_completion_inline(
        submission.request, observe_completion, &observation));
    CHECK(wait_semaphore(observation.semaphore));
    CHECK(observation.result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(observation.result.bytes_transferred == 4U);
    returned = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(memcmp(returned.data, "fghi", 4U) == 0);
    r_runtime_darwin_io_buffer_release(&returned);
    r_runtime_darwin_io_request_release(submission.request);
    dispatch_release(observation.semaphore);
    CHECK(lseek(descriptor, 0, SEEK_CUR) == 3);
    CHECK(close_handle(created.handle) == 0);
    CHECK(close(descriptor) == 0);
    return 0;
}

#define ADMISSION_HANDLES 6U

static _Bool admission_drained(void) {
    unsigned int attempt;

    for (attempt = 0U; attempt != 5000U; ++attempt) {
        if (r_runtime_darwin_io_testing_file_transfers_admitted() == 0U &&
            r_runtime_darwin_io_testing_file_transfers_waiting() == 0U) {
            return 1;
        }
        (void)usleep(1000U);
    }
    return 0;
}

/* R-SLIB-ASYNC-0019: with every slot held, the adapter admits exactly four transfers; the others
   wait in activation order, a cancelled waiting transfer finishes at once without a system call,
   and a cancelled admitted transfer finishes without its call once it would enter it. */
static int test_file_admission_bound_and_cancellation(void) {
    static const unsigned char initial[] = "0123456789";
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handles[ADMISSION_HANDLES];
    RRuntimeDarwinIoSubmitResult submissions[ADMISSION_HANDLES];
    CompletionObservation observations[ADMISSION_HANDLES];
    RRuntimeDarwinIoBuffer returned;
    size_t index;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = temporary_file(initial, 10U, O_RDONLY);
    CHECK(descriptor >= 0);
    CHECK(admission_drained());
    r_runtime_darwin_io_testing_hold_file_transfers();
    for (index = 0U; index != ADMISSION_HANDLES; ++index) {
        RRuntimeDarwinIoHandleCreateResult created = r_runtime_darwin_io_handle_create_file(
            &allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
        RRuntimeDarwinIoBuffer buffer;
        RRuntimeDarwinIoPrepareResult preparation;

        CHECK(created.status == R_RUNTIME_DARWIN_IO_START_OK);
        handles[index] = created.handle;
        buffer = allocate_buffer(&allocator, 2U, 0x66U);
        preparation =
            r_runtime_darwin_io_prepare_read_some(handles[index], (off_t)0, &buffer, UINT64_C(0));
        CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
        CHECK(r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared,
                                                               (off_t)index));
        submissions[index] = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
        CHECK(submissions[index].status == R_RUNTIME_DARWIN_IO_START_OK);
        observation_init(&observations[index]);
        CHECK(r_runtime_darwin_io_request_set_completion_inline(
            submissions[index].request, observe_completion, &observations[index]));
    }
    r_runtime_darwin_io_testing_wait_for_held_file_transfers(4U);
    CHECK(r_runtime_darwin_io_testing_file_transfers_admitted() == 4U);
    CHECK(r_runtime_darwin_io_testing_file_transfers_waiting() == 2U);

    /* The last activated transfer waits for admission; its cancellation finishes it now. */
    CHECK(r_runtime_darwin_io_request_cancel(submissions[5].request));
    CHECK(wait_semaphore(observations[5].semaphore));
    CHECK(observations[5].result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED);
    CHECK(observations[5].result.bytes_transferred == 0U);
    CHECK(r_runtime_darwin_io_testing_file_transfers_waiting() == 1U);
    CHECK(r_runtime_darwin_io_testing_file_transfers_admitted() == 4U);

    /* The first is admitted and held before its call: it finishes only after the release. */
    CHECK(r_runtime_darwin_io_request_cancel(submissions[0].request));
    CHECK(dispatch_semaphore_wait(observations[0].semaphore,
                                  dispatch_time(DISPATCH_TIME_NOW, INT64_C(50000000))) != 0L);
    r_runtime_darwin_io_testing_release_file_transfers();
    for (index = 0U; index != 5U; ++index) {
        CHECK(wait_semaphore(observations[index].semaphore));
    }
    CHECK(observations[0].result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED);
    CHECK(observations[0].result.bytes_transferred == 0U);
    for (index = 1U; index != 5U; ++index) {
        CHECK(observations[index].result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
        CHECK(observations[index].result.bytes_transferred == 2U);
    }
    for (index = 0U; index != ADMISSION_HANDLES; ++index) {
        returned = r_runtime_darwin_io_request_take_buffer(submissions[index].request);
        if (index != 0U && index != 5U) {
            CHECK(returned.data[0] == initial[index] && returned.data[1] == initial[index + 1U]);
        } else {
            CHECK(returned.size == 0U && returned.data[0] == 0x66U);
        }
        r_runtime_darwin_io_buffer_release(&returned);
        r_runtime_darwin_io_request_release(submissions[index].request);
        dispatch_release(observations[index].semaphore);
        CHECK(close_handle(handles[index]) == 0);
    }
    CHECK(admission_drained());
    CHECK(close(descriptor) == 0);
    return 0;
}

/* Core R-CONF-G012: a cancellation recorded after the system call returned does not replace the
   native outcome; the transfer is acknowledged after that return with its exact progress. */
static int test_file_cancel_after_entry_keeps_native_outcome(void) {
    static const unsigned char initial[] = {'n', 'a', 't', 'i', 'v', 'e'};
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandleCreateResult created;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = temporary_file(initial, sizeof(initial), O_RDONLY);
    CHECK(descriptor >= 0);
    created = r_runtime_darwin_io_handle_create_file(
        &allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(created.status == R_RUNTIME_DARWIN_IO_START_OK);
    buffer = allocate_buffer(&allocator, sizeof(initial), 0U);
    preparation =
        r_runtime_darwin_io_prepare_read_some(created.handle, (off_t)0, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, (off_t)0));
    r_runtime_darwin_io_testing_pause_next_native_completion();
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    r_runtime_darwin_io_testing_wait_for_native_completion();
    CHECK(r_runtime_darwin_io_request_state(submission.request) ==
          R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE);
    CHECK(r_runtime_darwin_io_request_cancel(submission.request));
    CHECK(r_runtime_darwin_io_request_state(submission.request) ==
          R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE);
    r_runtime_darwin_io_testing_release_native_completion();
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0 && result.bytes_transferred == sizeof(initial));
    returned = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(memcmp(returned.data, initial, sizeof(initial)) == 0);
    r_runtime_darwin_io_buffer_release(&returned);
    r_runtime_darwin_io_request_release(submission.request);
    CHECK(close_handle(created.handle) == 0);
    CHECK(close(descriptor) == 0);
    return 0;
}

/* Core R-CONF-G012: with every admission slot held, socket transfers still complete. */
static int test_saturated_file_adapter_leaves_sockets_running(void) {
    static const unsigned char initial[] = "0123456789";
    static const unsigned char ping[] = {'p'};
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *files[4];
    RRuntimeDarwinIoSubmitResult reads[4];
    CompletionObservation observations[4];
    RRuntimeDarwinIoHandleCreateResult socket_created;
    RRuntimeDarwinIoSubmitResult socket_read;
    CompletionObservation socket_observation;
    RRuntimeDarwinIoBuffer returned;
    size_t index;
    int descriptors[2];
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = temporary_file(initial, 10U, O_RDONLY);
    CHECK(descriptor >= 0);
    CHECK(admission_drained());
    r_runtime_darwin_io_testing_hold_file_transfers();
    for (index = 0U; index != 4U; ++index) {
        RRuntimeDarwinIoHandleCreateResult created = r_runtime_darwin_io_handle_create_file(
            &allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
        RRuntimeDarwinIoBuffer buffer = allocate_buffer(&allocator, 2U, 0U);
        RRuntimeDarwinIoPrepareResult preparation;

        CHECK(created.status == R_RUNTIME_DARWIN_IO_START_OK);
        files[index] = created.handle;
        preparation =
            r_runtime_darwin_io_prepare_read_some(files[index], (off_t)0, &buffer, UINT64_C(0));
        CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
        CHECK(r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, (off_t)0));
        reads[index] = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
        CHECK(reads[index].status == R_RUNTIME_DARWIN_IO_START_OK);
        observation_init(&observations[index]);
        CHECK(r_runtime_darwin_io_request_set_completion_inline(
            reads[index].request, observe_completion, &observations[index]));
    }
    r_runtime_darwin_io_testing_wait_for_held_file_transfers(4U);
    CHECK(r_runtime_darwin_io_testing_file_transfers_admitted() == 4U);

    CHECK(socket_pair(descriptors) == 0);
    socket_created = r_runtime_darwin_io_handle_create_socket(&allocator, descriptors[0]);
    CHECK(socket_created.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(submit_socket_read(socket_created.handle, &allocator, UINT64_C(0), &socket_observation,
                             &socket_read) == 0);
    CHECK(write(descriptors[1], ping, sizeof(ping)) == (ssize_t)sizeof(ping));
    CHECK(wait_semaphore(socket_observation.semaphore));
    CHECK(socket_observation.result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(socket_observation.result.bytes_transferred == 1U);
    CHECK(r_runtime_darwin_io_testing_file_transfers_admitted() == 4U);
    returned = r_runtime_darwin_io_request_take_buffer(socket_read.request);
    r_runtime_darwin_io_buffer_release(&returned);
    r_runtime_darwin_io_request_release(socket_read.request);
    dispatch_release(socket_observation.semaphore);

    r_runtime_darwin_io_testing_release_file_transfers();
    for (index = 0U; index != 4U; ++index) {
        CHECK(wait_semaphore(observations[index].semaphore));
        CHECK(observations[index].result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
        returned = r_runtime_darwin_io_request_take_buffer(reads[index].request);
        r_runtime_darwin_io_buffer_release(&returned);
        r_runtime_darwin_io_request_release(reads[index].request);
        dispatch_release(observations[index].semaphore);
        CHECK(close_handle(files[index]) == 0);
    }
    CHECK(admission_drained());
    CHECK(close_handle(socket_created.handle) == 0);
    CHECK(close(descriptors[0]) == 0 && close(descriptors[1]) == 0);
    CHECK(close(descriptor) == 0);
    return 0;
}

int main(void) {
    CHECK(test_inline_registration_outlives_worker_completion() == 0);
    CHECK(test_file_random_offsets_follow_the_creation_position() == 0);
    CHECK(test_file_admission_bound_and_cancellation() == 0);
    CHECK(test_file_cancel_after_entry_keeps_native_outcome() == 0);
    CHECK(test_saturated_file_adapter_leaves_sockets_running() == 0);
    CHECK(test_file_positioned_transfers_leave_the_offset() == 0);
    CHECK(test_file_append_and_native_error() == 0);
    CHECK(test_file_cancel_before_entry_and_close_waits() == 0);
    CHECK(test_socket_immediate_read_and_write_publish_inline() == 0);
    CHECK(test_socket_wait_cancel_and_deadline() == 0);
    CHECK(test_socket_large_write_waits_for_writability() == 0);
    CHECK(test_socket_close_and_drop() == 0);
    CHECK(test_inline_registration_races_worker_completion() == 0);
    (void)puts("Darwin direct payload engine tests passed");
    return 0;
}
