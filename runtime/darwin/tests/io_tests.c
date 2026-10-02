#include "r_runtime_darwin_io.h"

#include <dispatch/dispatch.h>
#include <errno.h>
#include <fcntl.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <util.h>

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

typedef struct HandleCleanupObservation {
    dispatch_semaphore_t semaphore;
    _Atomic unsigned int call_count;
    _Atomic int native_error;
} HandleCleanupObservation;

typedef struct BorrowedPipelineObservation {
    dispatch_semaphore_t completion_semaphore;
    dispatch_semaphore_t cleanup_semaphore;
    _Atomic unsigned int completion_count;
    _Atomic unsigned int cleanup_count;
    _Atomic int cleanup_error;
    _Atomic _Bool completion_finished;
    _Atomic _Bool ordering_failed;
    RRuntimeDarwinIoResult result;
} BorrowedPipelineObservation;

static void observe_completion(RRuntimeDarwinIoRequest *request, void *context) {
    CompletionObservation *observation = context;

    observation->result = r_runtime_darwin_io_request_wait(request);
    (void)atomic_fetch_add_explicit(&observation->call_count, 1U, memory_order_relaxed);
    (void)dispatch_semaphore_signal(observation->semaphore);
}

static void observe_handle_cleanup(void *context, int native_error) {
    HandleCleanupObservation *observation = context;

    atomic_store_explicit(&observation->native_error, native_error, memory_order_relaxed);
    (void)atomic_fetch_add_explicit(&observation->call_count, 1U, memory_order_release);
    (void)dispatch_semaphore_signal(observation->semaphore);
}

static void observe_borrowed_pipeline_completion(RRuntimeDarwinIoRequest *request, void *context) {
    BorrowedPipelineObservation *observation = context;

    observation->result = r_runtime_darwin_io_request_wait(request);
    r_runtime_darwin_io_request_release(request);
    (void)atomic_fetch_add_explicit(&observation->completion_count, 1U, memory_order_relaxed);
    atomic_store_explicit(&observation->completion_finished, 1, memory_order_release);
    (void)dispatch_semaphore_signal(observation->completion_semaphore);
}

static void observe_borrowed_pipeline_cleanup(void *context, int native_error) {
    BorrowedPipelineObservation *observation = context;

    if (!atomic_load_explicit(&observation->completion_finished, memory_order_acquire)) {
        atomic_store_explicit(&observation->ordering_failed, 1, memory_order_relaxed);
    }
    atomic_store_explicit(&observation->cleanup_error, native_error, memory_order_relaxed);
    (void)atomic_fetch_add_explicit(&observation->cleanup_count, 1U, memory_order_release);
    (void)dispatch_semaphore_signal(observation->cleanup_semaphore);
}

static RRuntimeDarwinIoHandle *
create_handle(RRuntimeAllocator *allocator, int descriptor, RRuntimeDarwinIoType type) {
    RRuntimeDarwinIoHandleCreateResult result =
        r_runtime_darwin_io_handle_create(allocator, descriptor, type);

    if (result.status != R_RUNTIME_DARWIN_IO_START_OK || result.handle == NULL) {
        (void)fprintf(stderr,
                      "handle creation failed: status=%d native_error=%d\n",
                      (int)result.status,
                      result.native_error);
        return NULL;
    }
    return result.handle;
}

static RRuntimeDarwinIoBuffer allocate_buffer(RRuntimeAllocator *allocator, size_t capacity) {
    RRuntimeDarwinIoBufferResult result = r_runtime_darwin_io_buffer_allocate(allocator, capacity);

    if (result.status != R_RUNTIME_DARWIN_IO_START_OK) {
        (void)fprintf(stderr, "buffer allocation failed: status=%d\n", (int)result.status);
        (void)memset(&result.buffer, 0, sizeof(result.buffer));
    }
    return result.buffer;
}

static int close_handle(RRuntimeDarwinIoHandle *handle) {
    RRuntimeDarwinIoSubmitResult submission = r_runtime_darwin_io_submit_close(handle);
    RRuntimeDarwinIoResult result;

    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(submission.request != NULL);
    CHECK(!r_runtime_darwin_io_request_cancel(submission.request));
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_IO_CLOSE);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.cleanup_acknowledged);
    r_runtime_darwin_io_request_release(submission.request);
    r_runtime_darwin_io_handle_release(handle);
    return 0;
}

static int read_exact(int descriptor, unsigned char *bytes, size_t size) {
    size_t offset = 0U;

    while (offset < size) {
        ssize_t count = read(descriptor, bytes + offset, size - offset);

        if (count <= 0) {
            return -1;
        }
        offset += (size_t)count;
    }
    return 0;
}

static int test_handle_create_failure_closes_duplicate_before_return(void) {
    static const RRuntimeDarwinIoHandleCreateFailureStage stages[] = {
        R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_AFTER_DUPLICATE,
        R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_AFTER_QUEUE,
    };
    size_t index;

    for (index = 0U; index < sizeof(stages) / sizeof(stages[0]); ++index) {
        RRuntimeAllocator allocator;
        RRuntimeDarwinIoHandleCreateResult created;
        unsigned char byte;
        int descriptors[2];
        int flags;

        r_runtime_allocator_initialize(&allocator);
        CHECK(pipe(descriptors) == 0);
        flags = fcntl(descriptors[0], F_GETFL);
        CHECK(flags >= 0);
        CHECK(fcntl(descriptors[0], F_SETFL, flags | O_NONBLOCK) == 0);
        r_runtime_darwin_io_testing_fail_handle_create_stage(stages[index]);
        created = r_runtime_darwin_io_handle_create(
            &allocator, descriptors[1], R_RUNTIME_DARWIN_IO_STREAM);
        CHECK(created.handle == NULL);
        CHECK(created.status == R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED);
        CHECK(created.native_error == ENOMEM);
        CHECK(close(descriptors[1]) == 0);
        errno = 0;
        CHECK(read(descriptors[0], &byte, sizeof(byte)) == 0);
        CHECK(close(descriptors[0]) == 0);
    }
    return 0;
}

static int test_handle_release_cleanup_observer(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    HandleCleanupObservation observation;
    unsigned char byte;
    int descriptors[2];

    r_runtime_allocator_initialize(&allocator);
    CHECK(pipe(descriptors) == 0);
    handle = create_handle(&allocator, descriptors[1], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(handle != NULL);
    CHECK(close(descriptors[1]) == 0);
    observation.semaphore = dispatch_semaphore_create(0);
    CHECK(observation.semaphore != NULL);
    atomic_init(&observation.call_count, 0U);
    atomic_init(&observation.native_error, 0);
    CHECK(r_runtime_darwin_io_handle_release_with_cleanup(
        handle, observe_handle_cleanup, &observation));
    CHECK(dispatch_semaphore_wait(observation.semaphore, DISPATCH_TIME_FOREVER) == 0);
    CHECK(atomic_load_explicit(&observation.call_count, memory_order_acquire) == 1U);
    CHECK(atomic_load_explicit(&observation.native_error, memory_order_relaxed) == 0);
    CHECK(read(descriptors[0], &byte, sizeof(byte)) == 0);
    CHECK(close(descriptors[0]) == 0);
    dispatch_release(observation.semaphore);
    return 0;
}

static int test_handle_release_cleanup_observer_after_close(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    HandleCleanupObservation observation;
    int descriptors[2];

    r_runtime_allocator_initialize(&allocator);
    CHECK(pipe(descriptors) == 0);
    handle = create_handle(&allocator, descriptors[1], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(handle != NULL);
    CHECK(close(descriptors[1]) == 0);
    submission = r_runtime_darwin_io_submit_close(handle);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0 && result.cleanup_acknowledged);
    r_runtime_darwin_io_request_release(submission.request);

    observation.semaphore = dispatch_semaphore_create(0);
    CHECK(observation.semaphore != NULL);
    atomic_init(&observation.call_count, 0U);
    atomic_init(&observation.native_error, 0);
    CHECK(r_runtime_darwin_io_handle_release_with_cleanup(
        handle, observe_handle_cleanup, &observation));
    CHECK(atomic_load_explicit(&observation.call_count, memory_order_acquire) == 1U);
    CHECK(atomic_load_explicit(&observation.native_error, memory_order_relaxed) == 0);
    CHECK(dispatch_semaphore_wait(observation.semaphore, DISPATCH_TIME_NOW) == 0);
    CHECK(close(descriptors[0]) == 0);
    dispatch_release(observation.semaphore);
    return 0;
}

static int test_random_file_read_write_flush_and_close(void) {
    static const unsigned char payload[] = {
        0x52U, 0x20U, 0x44U, 0x69U, 0x73U, 0x70U, 0x61U, 0x74U, 0x63U, 0x68U};
    char path[] = "/tmp/r-dispatch-io-XXXXXX";
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoBuffer write_buffer;
    RRuntimeDarwinIoBuffer read_buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoSubmitResult write_submission;
    RRuntimeDarwinIoSubmitResult flush_submission;
    RRuntimeDarwinIoSubmitResult read_submission;
    RRuntimeDarwinIoResult result;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = mkstemp(path);
    CHECK(descriptor >= 0);
    handle = create_handle(&allocator, descriptor, R_RUNTIME_DARWIN_IO_RANDOM);
    CHECK(handle != NULL);
    CHECK(close(descriptor) == 0);

    write_buffer = allocate_buffer(&allocator, sizeof(payload));
    CHECK(write_buffer.data != NULL);
    (void)memcpy(write_buffer.data, payload, sizeof(payload));
    write_buffer.size = sizeof(payload);
    write_submission = r_runtime_darwin_io_submit_write(handle, 0, &write_buffer, UINT64_C(0));
    CHECK(write_submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(write_submission.request != NULL);
    CHECK(write_buffer.data == NULL);
    CHECK(write_buffer.capacity == 0U);

    flush_submission = r_runtime_darwin_io_submit_flush(handle, UINT64_C(0));
    CHECK(flush_submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(flush_submission.request != NULL);
    result = r_runtime_darwin_io_request_wait(flush_submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_IO_FLUSH);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.cleanup_acknowledged);
    result = r_runtime_darwin_io_request_wait(write_submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_IO_WRITE);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred == sizeof(payload));
    CHECK(result.bytes_remaining == 0U);
    CHECK(!result.partial);
    CHECK(result.cleanup_acknowledged);
    CHECK(!r_runtime_darwin_io_request_cancel(write_submission.request));
    returned_buffer = r_runtime_darwin_io_request_take_buffer(write_submission.request);
    CHECK(returned_buffer.data != NULL);
    CHECK(returned_buffer.size == sizeof(payload));
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    r_runtime_darwin_io_request_release(write_submission.request);
    r_runtime_darwin_io_request_release(flush_submission.request);

    read_buffer = allocate_buffer(&allocator, sizeof(payload) + 8U);
    CHECK(read_buffer.data != NULL);
    read_submission = r_runtime_darwin_io_submit_read(handle, 0, &read_buffer, UINT64_C(0));
    CHECK(read_submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(read_buffer.data == NULL);
    result = r_runtime_darwin_io_request_wait(read_submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_IO_READ);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred == sizeof(payload));
    CHECK(result.bytes_remaining == 8U);
    CHECK(result.eof);
    CHECK(result.partial);
    CHECK(result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(read_submission.request);
    CHECK(returned_buffer.size == sizeof(payload));
    CHECK(memcmp(returned_buffer.data, payload, sizeof(payload)) == 0);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    r_runtime_darwin_io_request_release(read_submission.request);

    CHECK(close_handle(handle) == 0);
    CHECK(unlink(path) == 0);
    return 0;
}

static int test_stream_eof_preserves_partial_buffer(void) {
    static const unsigned char payload[] = {0x65U, 0x6fU, 0x66U};
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    int descriptors[2];

    r_runtime_allocator_initialize(&allocator);
    CHECK(pipe(descriptors) == 0);
    handle = create_handle(&allocator, descriptors[0], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(handle != NULL);
    CHECK(close(descriptors[0]) == 0);
    CHECK(write(descriptors[1], payload, sizeof(payload)) == (ssize_t)sizeof(payload));
    CHECK(close(descriptors[1]) == 0);

    buffer = allocate_buffer(&allocator, 32U);
    CHECK(buffer.data != NULL);
    submission = r_runtime_darwin_io_submit_read(handle, 0, &buffer, UINT64_C(0));
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred == sizeof(payload));
    CHECK(result.bytes_remaining == 32U - sizeof(payload));
    CHECK(result.eof);
    CHECK(result.partial);
    CHECK(result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned_buffer.size == sizeof(payload));
    CHECK(memcmp(returned_buffer.data, payload, sizeof(payload)) == 0);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    r_runtime_darwin_io_request_release(submission.request);
    CHECK(close_handle(handle) == 0);
    return 0;
}

static int test_pseudo_terminal_console_read_write(void) {
    static const unsigned char output[] = {0x63U, 0x6fU, 0x6eU, 0x73U, 0x6fU, 0x6cU, 0x65U};
    static const unsigned char input[] = {0x69U, 0x6eU, 0x70U, 0x75U, 0x74U, 0x0aU};
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    unsigned char observed[sizeof(output)];
    int master_descriptor;
    int slave_descriptor;

    r_runtime_allocator_initialize(&allocator);
    CHECK(openpty(&master_descriptor, &slave_descriptor, NULL, NULL, NULL) == 0);
    handle = create_handle(&allocator, slave_descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(handle != NULL);
    CHECK(close(slave_descriptor) == 0);

    buffer = allocate_buffer(&allocator, sizeof(output));
    CHECK(buffer.data != NULL);
    (void)memcpy(buffer.data, output, sizeof(output));
    buffer.size = sizeof(output);
    submission = r_runtime_darwin_io_submit_write(handle, 0, &buffer, UINT64_C(0));
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred == sizeof(output));
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    r_runtime_darwin_io_request_release(submission.request);
    CHECK(read_exact(master_descriptor, observed, sizeof(observed)) == 0);
    CHECK(memcmp(observed, output, sizeof(output)) == 0);

    CHECK(write(master_descriptor, input, sizeof(input)) == (ssize_t)sizeof(input));
    buffer = allocate_buffer(&allocator, sizeof(input));
    CHECK(buffer.data != NULL);
    submission = r_runtime_darwin_io_submit_read(handle, 0, &buffer, UINT64_C(0));
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred == sizeof(input));
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned_buffer.size == sizeof(input));
    CHECK(memcmp(returned_buffer.data, input, sizeof(input)) == 0);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    r_runtime_darwin_io_request_release(submission.request);

    CHECK(close_handle(handle) == 0);
    CHECK(close(master_descriptor) == 0);
    return 0;
}

static int test_cancel_and_deadline_acknowledgement(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *cancel_handle;
    RRuntimeDarwinIoHandle *deadline_handle;
    RRuntimeDarwinIoBuffer cancel_buffer;
    RRuntimeDarwinIoBuffer deadline_buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoSubmitResult cancelled;
    RRuntimeDarwinIoSubmitResult timed_out;
    RRuntimeDarwinIoResult result;
    int cancel_pipe[2];
    int deadline_pipe[2];

    r_runtime_allocator_initialize(&allocator);
    CHECK(pipe(cancel_pipe) == 0);
    cancel_handle = create_handle(&allocator, cancel_pipe[0], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(cancel_handle != NULL);
    CHECK(close(cancel_pipe[0]) == 0);
    cancel_buffer = allocate_buffer(&allocator, 64U);
    cancelled = r_runtime_darwin_io_submit_read(cancel_handle, 0, &cancel_buffer, UINT64_C(0));
    CHECK(cancelled.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_request_cancel(cancelled.request));
    CHECK(!r_runtime_darwin_io_request_deadline_expired(cancelled.request));
    result = r_runtime_darwin_io_request_wait(cancelled.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred == 0U);
    CHECK(result.bytes_remaining == 64U);
    CHECK(result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(cancelled.request);
    CHECK(returned_buffer.data != NULL);
    CHECK(returned_buffer.size == 0U);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    r_runtime_darwin_io_request_release(cancelled.request);
    CHECK(close(cancel_pipe[1]) == 0);
    CHECK(close_handle(cancel_handle) == 0);

    CHECK(pipe(deadline_pipe) == 0);
    deadline_handle = create_handle(&allocator, deadline_pipe[0], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(deadline_handle != NULL);
    CHECK(close(deadline_pipe[0]) == 0);
    deadline_buffer = allocate_buffer(&allocator, 64U);
    timed_out =
        r_runtime_darwin_io_submit_read(deadline_handle, 0, &deadline_buffer, UINT64_C(5000000));
    CHECK(timed_out.status == R_RUNTIME_DARWIN_IO_START_OK);
    result = r_runtime_darwin_io_request_wait(timed_out.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_TIMED_OUT);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred == 0U);
    CHECK(result.bytes_remaining == 64U);
    CHECK(result.cleanup_acknowledged);
    CHECK(!r_runtime_darwin_io_request_cancel(timed_out.request));
    returned_buffer = r_runtime_darwin_io_request_take_buffer(timed_out.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    r_runtime_darwin_io_request_release(timed_out.request);
    CHECK(close(deadline_pipe[1]) == 0);
    CHECK(close_handle(deadline_handle) == 0);
    return 0;
}

static int test_partial_write_cancel_retains_source(void) {
    const size_t buffer_size = 8U * 1024U * 1024U;
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    int descriptors[2];

    r_runtime_allocator_initialize(&allocator);
    CHECK(pipe(descriptors) == 0);
    handle = create_handle(&allocator, descriptors[1], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(handle != NULL);
    CHECK(close(descriptors[1]) == 0);
    buffer = allocate_buffer(&allocator, buffer_size);
    CHECK(buffer.data != NULL);
    (void)memset(buffer.data, 0x5a, buffer.capacity);
    buffer.size = buffer.capacity;
    submission = r_runtime_darwin_io_submit_write(handle, 0, &buffer, UINT64_C(0));
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    r_runtime_darwin_io_request_testing_wait_for_progress(submission.request, 1U);
    CHECK(r_runtime_darwin_io_request_progress(submission.request) > 0U);
    CHECK(r_runtime_darwin_io_request_cancel(submission.request));
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred > 0U);
    CHECK(result.bytes_transferred < buffer_size);
    CHECK(result.bytes_remaining == buffer_size - result.bytes_transferred);
    CHECK(result.partial);
    CHECK(result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned_buffer.data != NULL);
    CHECK(returned_buffer.size == buffer_size);
    CHECK(returned_buffer.data[0] == 0x5aU);
    CHECK(returned_buffer.data[buffer_size - 1U] == 0x5aU);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    r_runtime_darwin_io_request_release(submission.request);
    CHECK(close(descriptors[0]) == 0);
    CHECK(close_handle(handle) == 0);
    return 0;
}

static int test_flush_deadline_before_barrier_commit(void) {
    const size_t buffer_size = 4U * 1024U * 1024U;
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer queued_buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoSubmitResult write_submission;
    RRuntimeDarwinIoSubmitResult flush_submission;
    RRuntimeDarwinIoSubmitResult queued_submission;
    RRuntimeDarwinIoSubmitResult close_submission;
    RRuntimeDarwinIoResult result;
    int descriptors[2];

    r_runtime_allocator_initialize(&allocator);
    CHECK(pipe(descriptors) == 0);
    handle = create_handle(&allocator, descriptors[1], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(handle != NULL);
    CHECK(close(descriptors[1]) == 0);
    buffer = allocate_buffer(&allocator, buffer_size);
    CHECK(buffer.data != NULL);
    (void)memset(buffer.data, 0x3c, buffer.capacity);
    buffer.size = buffer.capacity;
    write_submission = r_runtime_darwin_io_submit_write(handle, 0, &buffer, UINT64_C(0));
    CHECK(write_submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    r_runtime_darwin_io_request_testing_wait_for_progress(write_submission.request, 1U);

    flush_submission = r_runtime_darwin_io_submit_flush(handle, UINT64_C(0));
    CHECK(flush_submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    queued_buffer = allocate_buffer(&allocator, buffer_size);
    CHECK(queued_buffer.data != NULL);
    (void)memset(queued_buffer.data, 0x4d, queued_buffer.capacity);
    queued_buffer.size = queued_buffer.capacity;
    queued_submission = r_runtime_darwin_io_submit_write(handle, 0, &queued_buffer, UINT64_C(0));
    CHECK(queued_submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(queued_submission.request != NULL);
    CHECK(queued_buffer.data == NULL);
    close_submission = r_runtime_darwin_io_submit_close(handle);
    CHECK(close_submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(close_submission.request != NULL);
    result = r_runtime_darwin_io_request_wait(flush_submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_IO_FLUSH);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED);
    CHECK(result.native_error == 0);
    CHECK(result.cleanup_acknowledged);
    r_runtime_darwin_io_request_release(flush_submission.request);

    result = r_runtime_darwin_io_request_wait(write_submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED);
    CHECK(result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(write_submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    r_runtime_darwin_io_request_release(write_submission.request);

    result = r_runtime_darwin_io_request_wait(queued_submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED);
    CHECK(result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(queued_submission.request);
    CHECK(returned_buffer.data != NULL);
    CHECK(returned_buffer.size == buffer_size);
    CHECK(returned_buffer.data[0] == 0x4dU);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    r_runtime_darwin_io_request_release(queued_submission.request);

    result = r_runtime_darwin_io_request_wait(close_submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_IO_CLOSE);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.cleanup_acknowledged);
    r_runtime_darwin_io_request_release(close_submission.request);

    CHECK(close(descriptors[0]) == 0);
    r_runtime_darwin_io_handle_release(handle);
    return 0;
}

static int test_close_rejects_new_start_without_consumption(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoSubmitResult close_submission;
    RRuntimeDarwinIoSubmitResult read_submission;
    RRuntimeDarwinIoResult close_result;
    unsigned char *original_data;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    CHECK(descriptor >= 0);
    handle = create_handle(&allocator, descriptor, R_RUNTIME_DARWIN_IO_RANDOM);
    CHECK(handle != NULL);
    CHECK(close(descriptor) == 0);
    buffer = allocate_buffer(&allocator, 16U);
    original_data = buffer.data;

    close_submission = r_runtime_darwin_io_submit_close(handle);
    CHECK(close_submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    read_submission = r_runtime_darwin_io_submit_read(handle, 0, &buffer, UINT64_C(0));
    CHECK(read_submission.status == R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED);
    CHECK(read_submission.request == NULL);
    CHECK(buffer.data == original_data);
    CHECK(buffer.capacity == 16U);
    close_result = r_runtime_darwin_io_request_wait(close_submission.request);
    CHECK(close_result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(close_result.cleanup_acknowledged);
    r_runtime_darwin_io_request_release(close_submission.request);
    r_runtime_darwin_io_buffer_release(&buffer);
    r_runtime_darwin_io_handle_release(handle);
    return 0;
}

static int test_handle_drop_preserves_started_operation(void) {
    static const unsigned char payload[] = {0x72U, 0x65U, 0x74U, 0x61U, 0x69U, 0x6eU};
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    int descriptors[2];

    r_runtime_allocator_initialize(&allocator);
    CHECK(pipe(descriptors) == 0);
    handle = create_handle(&allocator, descriptors[0], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(handle != NULL);
    CHECK(close(descriptors[0]) == 0);
    buffer = allocate_buffer(&allocator, 64U);
    CHECK(buffer.data != NULL);
    submission = r_runtime_darwin_io_submit_read(handle, 0, &buffer, UINT64_C(0));
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);

    r_runtime_darwin_io_handle_release(handle);
    CHECK(write(descriptors[1], payload, sizeof(payload)) == (ssize_t)sizeof(payload));
    CHECK(close(descriptors[1]) == 0);
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_IO_READ);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred == sizeof(payload));
    CHECK(result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned_buffer.data != NULL);
    CHECK(returned_buffer.capacity == 64U);
    CHECK(memcmp(returned_buffer.data, payload, sizeof(payload)) == 0);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    r_runtime_darwin_io_request_release(submission.request);
    return 0;
}

static int test_prepared_write_reservation_and_failure_stages(void) {
    static const unsigned char payload[] = {0x70U, 0x72U, 0x65U, 0x70U, 0x61U, 0x72U, 0x65U};
    static const RRuntimeDarwinIoPrepareFailureStage stages[] = {
        R_RUNTIME_DARWIN_IO_PREPARE_FAIL_REQUEST,
        R_RUNTIME_DARWIN_IO_PREPARE_FAIL_DEADLINE,
        R_RUNTIME_DARWIN_IO_PREPARE_FAIL_CHANNEL,
        R_RUNTIME_DARWIN_IO_PREPARE_FAIL_WRITE_DATA,
    };
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer original;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    unsigned char original_bytes[sizeof(payload)];
    unsigned char observed[sizeof(payload)];
    size_t index;
    ssize_t count;
    int descriptors[2];
    int flags;

    r_runtime_allocator_initialize(&allocator);
    CHECK(pipe(descriptors) == 0);
    flags = fcntl(descriptors[0], F_GETFL);
    CHECK(flags >= 0);
    CHECK(fcntl(descriptors[0], F_SETFL, flags | O_NONBLOCK) == 0);
    handle = create_handle(&allocator, descriptors[1], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(handle != NULL);
    CHECK(close(descriptors[1]) == 0);
    buffer = allocate_buffer(&allocator, sizeof(payload));
    CHECK(buffer.data != NULL);
    (void)memcpy(buffer.data, payload, sizeof(payload));
    buffer.size = sizeof(payload);
    original = buffer;
    (void)memcpy(original_bytes, buffer.data, sizeof(original_bytes));

    for (index = 0U; index < sizeof(stages) / sizeof(stages[0]); ++index) {
        uint64_t timeout = stages[index] == R_RUNTIME_DARWIN_IO_PREPARE_FAIL_DEADLINE
                               ? UINT64_C(1000000000)
                               : UINT64_C(0);

        r_runtime_darwin_io_testing_fail_prepare_stage(stages[index]);
        preparation = r_runtime_darwin_io_prepare_write(handle, 0, &buffer, timeout);
        CHECK(preparation.status != R_RUNTIME_DARWIN_IO_START_OK);
        CHECK(preparation.prepared == NULL);
        CHECK(memcmp(&buffer, &original, sizeof(buffer)) == 0);
        CHECK(memcmp(buffer.data, original_bytes, sizeof(original_bytes)) == 0);
        count = read(descriptors[0], observed, sizeof(observed));
        CHECK(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
    }

    preparation = r_runtime_darwin_io_prepare_write(handle, 0, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(preparation.prepared != NULL);
    CHECK(r_runtime_darwin_io_request_state(preparation.prepared) ==
          R_RUNTIME_DARWIN_IO_REQUEST_PREPARED);
    CHECK(!r_runtime_darwin_io_request_cancel(preparation.prepared));
    CHECK(memcmp(&buffer, &original, sizeof(buffer)) == 0);
    CHECK(memcmp(buffer.data, original_bytes, sizeof(original_bytes)) == 0);
    count = read(descriptors[0], observed, sizeof(observed));
    CHECK(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
    r_runtime_darwin_io_prepared_abort(&preparation.prepared);
    CHECK(preparation.prepared == NULL);
    CHECK(memcmp(&buffer, &original, sizeof(buffer)) == 0);
    CHECK(memcmp(buffer.data, original_bytes, sizeof(original_bytes)) == 0);

    preparation = r_runtime_darwin_io_prepare_write(handle, 0, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(preparation.prepared == NULL);
    CHECK(buffer.data == NULL);
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred == sizeof(payload));
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned_buffer.data == original.data);
    CHECK(memcmp(returned_buffer.data, payload, sizeof(payload)) == 0);
    r_runtime_darwin_io_request_release(submission.request);
    flags = fcntl(descriptors[0], F_GETFL);
    CHECK(flags >= 0);
    CHECK(fcntl(descriptors[0], F_SETFL, flags & ~O_NONBLOCK) == 0);
    CHECK(read_exact(descriptors[0], observed, sizeof(observed)) == 0);
    CHECK(memcmp(observed, payload, sizeof(payload)) == 0);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    CHECK(close(descriptors[0]) == 0);
    CHECK(close_handle(handle) == 0);
    return 0;
}

static int test_prepared_read_and_flush_ordering(void) {
    static const unsigned char payload[] = {0x72U, 0x65U, 0x61U, 0x64U};
    static const RRuntimeDarwinIoPrepareFailureStage read_stages[] = {
        R_RUNTIME_DARWIN_IO_PREPARE_FAIL_REQUEST,
        R_RUNTIME_DARWIN_IO_PREPARE_FAIL_DEADLINE,
        R_RUNTIME_DARWIN_IO_PREPARE_FAIL_CHANNEL,
    };
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *input_handle;
    RRuntimeDarwinIoHandle *output_handle;
    RRuntimeDarwinIoBuffer read_buffer;
    RRuntimeDarwinIoBuffer original_read;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoBuffer write_buffer;
    RRuntimeDarwinIoPrepareResult read_preparation;
    RRuntimeDarwinIoPrepareResult write_preparation;
    RRuntimeDarwinIoPrepareResult flush_preparation;
    RRuntimeDarwinIoSubmitResult read_submission;
    RRuntimeDarwinIoSubmitResult write_submission;
    RRuntimeDarwinIoResult result;
    unsigned char observed[sizeof(payload)];
    unsigned char sentinel[16];
    size_t index;
    ssize_t count;
    int input_pipe[2];
    int output_pipe[2];
    int flags;

    r_runtime_allocator_initialize(&allocator);
    CHECK(pipe(input_pipe) == 0);
    input_handle = create_handle(&allocator, input_pipe[0], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(input_handle != NULL);
    CHECK(close(input_pipe[0]) == 0);
    read_buffer = allocate_buffer(&allocator, sizeof(sentinel));
    CHECK(read_buffer.data != NULL);
    (void)memset(read_buffer.data, 0xa5, read_buffer.capacity);
    original_read = read_buffer;
    (void)memcpy(sentinel, read_buffer.data, sizeof(sentinel));
    for (index = 0U; index < sizeof(read_stages) / sizeof(read_stages[0]); ++index) {
        uint64_t timeout = read_stages[index] == R_RUNTIME_DARWIN_IO_PREPARE_FAIL_DEADLINE
                               ? UINT64_C(1000000000)
                               : UINT64_C(0);

        r_runtime_darwin_io_testing_fail_prepare_stage(read_stages[index]);
        read_preparation =
            r_runtime_darwin_io_prepare_read_some(input_handle, 0, &read_buffer, timeout);
        CHECK(read_preparation.status != R_RUNTIME_DARWIN_IO_START_OK);
        CHECK(read_preparation.prepared == NULL);
        CHECK(memcmp(&read_buffer, &original_read, sizeof(read_buffer)) == 0);
        CHECK(memcmp(read_buffer.data, sentinel, sizeof(sentinel)) == 0);
    }
    read_preparation =
        r_runtime_darwin_io_prepare_read_some(input_handle, 0, &read_buffer, UINT64_C(0));
    CHECK(read_preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(write(input_pipe[1], payload, sizeof(payload)) == (ssize_t)sizeof(payload));
    CHECK(memcmp(read_buffer.data, sentinel, sizeof(sentinel)) == 0);
    read_submission =
        r_runtime_darwin_io_prepared_activate(&read_preparation.prepared, &read_buffer);
    CHECK(read_submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    result = r_runtime_darwin_io_request_wait(read_submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.bytes_transferred == sizeof(payload));
    returned_buffer = r_runtime_darwin_io_request_take_buffer(read_submission.request);
    CHECK(returned_buffer.data == original_read.data);
    CHECK(memcmp(returned_buffer.data, payload, sizeof(payload)) == 0);
    CHECK(memcmp(returned_buffer.data + sizeof(payload),
                 sentinel + sizeof(payload),
                 sizeof(sentinel) - sizeof(payload)) == 0);
    r_runtime_darwin_io_request_release(read_submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    CHECK(close(input_pipe[1]) == 0);
    CHECK(close_handle(input_handle) == 0);

    CHECK(pipe(output_pipe) == 0);
    flags = fcntl(output_pipe[0], F_GETFL);
    CHECK(flags >= 0);
    CHECK(fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK) == 0);
    output_handle = create_handle(&allocator, output_pipe[1], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(output_handle != NULL);
    CHECK(close(output_pipe[1]) == 0);
    r_runtime_darwin_io_testing_fail_prepare_stage(R_RUNTIME_DARWIN_IO_PREPARE_FAIL_REQUEST);
    flush_preparation = r_runtime_darwin_io_prepare_flush(output_handle, UINT64_C(0));
    CHECK(flush_preparation.status == R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED);
    CHECK(flush_preparation.prepared == NULL);
    r_runtime_darwin_io_testing_fail_prepare_stage(R_RUNTIME_DARWIN_IO_PREPARE_FAIL_DEADLINE);
    flush_preparation = r_runtime_darwin_io_prepare_flush(output_handle, UINT64_C(1000000000));
    CHECK(flush_preparation.status == R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED);
    CHECK(flush_preparation.prepared == NULL);
    flush_preparation = r_runtime_darwin_io_prepare_flush(output_handle, UINT64_C(0));
    CHECK(flush_preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    write_buffer = allocate_buffer(&allocator, sizeof(payload));
    CHECK(write_buffer.data != NULL);
    (void)memcpy(write_buffer.data, payload, sizeof(payload));
    write_buffer.size = sizeof(payload);
    write_preparation =
        r_runtime_darwin_io_prepare_write(output_handle, 0, &write_buffer, UINT64_C(0));
    CHECK(write_preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    write_submission =
        r_runtime_darwin_io_prepared_activate(&write_preparation.prepared, &write_buffer);
    CHECK(write_submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(write_buffer.data == NULL);
    count = read(output_pipe[0], observed, sizeof(observed));
    CHECK(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
    r_runtime_darwin_io_prepared_abort(&flush_preparation.prepared);
    CHECK(flush_preparation.prepared == NULL);
    result = r_runtime_darwin_io_request_wait(write_submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.bytes_transferred == sizeof(payload));
    returned_buffer = r_runtime_darwin_io_request_take_buffer(write_submission.request);
    r_runtime_darwin_io_request_release(write_submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    CHECK(fcntl(output_pipe[0], F_SETFL, flags) == 0);
    CHECK(read_exact(output_pipe[0], observed, sizeof(observed)) == 0);
    CHECK(memcmp(observed, payload, sizeof(payload)) == 0);
    CHECK(close(output_pipe[0]) == 0);
    CHECK(close_handle(output_handle) == 0);
    return 0;
}

static int test_prepared_random_offset_late_binding(void) {
    static const unsigned char initial[] = {
        0x30U, 0x31U, 0x32U, 0x33U, 0x34U, 0x35U, 0x36U, 0x37U, 0x38U};
    static const unsigned char replacement[] = {0x78U, 0x79U};
    char path[] = "/tmp/r-dispatch-io-offset-XXXXXX";
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoHandle *stream_handle;
    RRuntimeDarwinIoBuffer read_buffer;
    RRuntimeDarwinIoBuffer write_buffer;
    RRuntimeDarwinIoBuffer stream_buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoPreparedRequest *prepared_alias;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    unsigned char observed[sizeof(initial)];
    int descriptor;
    int stream_descriptors[2];

    r_runtime_allocator_initialize(&allocator);
    descriptor = mkstemp(path);
    CHECK(descriptor >= 0);
    CHECK(write(descriptor, initial, sizeof(initial)) == (ssize_t)sizeof(initial));
    CHECK(lseek(descriptor, (off_t)0, SEEK_SET) == (off_t)0);
    handle = create_handle(&allocator, descriptor, R_RUNTIME_DARWIN_IO_RANDOM);
    CHECK(handle != NULL);
    CHECK(close(descriptor) == 0);

    read_buffer = allocate_buffer(&allocator, 3U);
    CHECK(read_buffer.data != NULL);
    preparation = r_runtime_darwin_io_prepare_read(handle, (off_t)0, &read_buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(preparation.prepared != NULL);
    CHECK(!r_runtime_darwin_io_prepared_set_offset(preparation.prepared, (off_t)-1));
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    CHECK(r_runtime_darwin_io_prepared_set_offset(preparation.prepared, (off_t)3));
    CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    prepared_alias = preparation.prepared;
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &read_buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(submission.request == prepared_alias);
    CHECK(preparation.prepared == NULL);
    CHECK(r_runtime_darwin_io_request_state(prepared_alias) !=
          R_RUNTIME_DARWIN_IO_REQUEST_PREPARED);
    CHECK(!r_runtime_darwin_io_prepared_set_offset(prepared_alias, (off_t)0));
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_IO_READ);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred == 3U);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned_buffer.size == 3U);
    CHECK(memcmp(returned_buffer.data, initial + 3U, 3U) == 0);
    r_runtime_darwin_io_request_release(submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);

    write_buffer = allocate_buffer(&allocator, sizeof(replacement));
    CHECK(write_buffer.data != NULL);
    (void)memcpy(write_buffer.data, replacement, sizeof(replacement));
    write_buffer.size = sizeof(replacement);
    preparation = r_runtime_darwin_io_prepare_write(handle, (off_t)0, &write_buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_prepared_set_offset(preparation.prepared, (off_t)0));
    CHECK(r_runtime_darwin_io_prepared_set_offset(preparation.prepared, (off_t)6));
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &write_buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_IO_WRITE);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred == sizeof(replacement));
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned_buffer.size == sizeof(replacement));
    CHECK(memcmp(returned_buffer.data, replacement, sizeof(replacement)) == 0);
    r_runtime_darwin_io_request_release(submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);

    preparation = r_runtime_darwin_io_prepare_flush(handle, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(!r_runtime_darwin_io_prepared_set_offset(preparation.prepared, (off_t)0));
    r_runtime_darwin_io_prepared_abort(&preparation.prepared);
    CHECK(close_handle(handle) == 0);

    descriptor = open(path, O_RDONLY | O_CLOEXEC);
    CHECK(descriptor >= 0);
    CHECK(read_exact(descriptor, observed, sizeof(observed)) == 0);
    CHECK(memcmp(observed, initial, 6U) == 0);
    CHECK(memcmp(observed + 6U, replacement, sizeof(replacement)) == 0);
    CHECK(observed[8] == initial[8]);
    CHECK(close(descriptor) == 0);
    CHECK(unlink(path) == 0);

    CHECK(pipe(stream_descriptors) == 0);
    stream_handle = create_handle(&allocator, stream_descriptors[1], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(stream_handle != NULL);
    CHECK(close(stream_descriptors[1]) == 0);
    stream_buffer = allocate_buffer(&allocator, 1U);
    CHECK(stream_buffer.data != NULL);
    stream_buffer.data[0] = 0x73U;
    stream_buffer.size = 1U;
    preparation =
        r_runtime_darwin_io_prepare_write(stream_handle, (off_t)0, &stream_buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(!r_runtime_darwin_io_prepared_set_offset(preparation.prepared, (off_t)0));
    r_runtime_darwin_io_prepared_abort(&preparation.prepared);
    r_runtime_darwin_io_buffer_release(&stream_buffer);
    CHECK(close_handle(stream_handle) == 0);
    CHECK(close(stream_descriptors[0]) == 0);
    return 0;
}

static int test_prepared_stream_position_late_binding(void) {
    static const unsigned char initial[] = {
        0x30U, 0x31U, 0x32U, 0x33U, 0x34U, 0x35U, 0x36U, 0x37U, 0x38U};
    static const unsigned char replacement[] = {0x78U, 0x79U};
    char path[] = "/tmp/r-dispatch-stream-position-XXXXXX";
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoHandle *random_handle;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoPreparedRequest *prepared_alias;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    unsigned char observed[sizeof(initial)];
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = mkstemp(path);
    CHECK(descriptor >= 0);
    CHECK(write(descriptor, initial, sizeof(initial)) == (ssize_t)sizeof(initial));
    handle = create_handle(&allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(handle != NULL);

    buffer = allocate_buffer(&allocator, 3U);
    CHECK(buffer.data != NULL);
    preparation = r_runtime_darwin_io_prepare_read(handle, (off_t)0, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(!r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, (off_t)-1));
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    CHECK(r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, (off_t)3));
    CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    prepared_alias = preparation.prepared;
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(!r_runtime_darwin_io_prepared_set_stream_position(prepared_alias, (off_t)0));
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_IO_READ);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred == 3U);
    CHECK(result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned_buffer.size == 3U);
    CHECK(memcmp(returned_buffer.data, initial + 3U, 3U) == 0);
    r_runtime_darwin_io_request_release(submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    CHECK(lseek(descriptor, (off_t)0, SEEK_CUR) == (off_t)6);

    buffer = allocate_buffer(&allocator, sizeof(replacement));
    CHECK(buffer.data != NULL);
    (void)memcpy(buffer.data, replacement, sizeof(replacement));
    buffer.size = sizeof(replacement);
    preparation = r_runtime_darwin_io_prepare_write(handle, (off_t)0, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, (off_t)1));
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_IO_WRITE);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred == sizeof(replacement));
    CHECK(result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned_buffer.size == sizeof(replacement));
    r_runtime_darwin_io_request_release(submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    CHECK(lseek(descriptor, (off_t)0, SEEK_CUR) == (off_t)3);

    preparation = r_runtime_darwin_io_prepare_flush(handle, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(!r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, (off_t)0));
    r_runtime_darwin_io_prepared_abort(&preparation.prepared);
    CHECK(close_handle(handle) == 0);
    CHECK(pread(descriptor, observed, sizeof(observed), (off_t)0) == (ssize_t)sizeof(observed));
    CHECK(observed[0] == initial[0]);
    CHECK(memcmp(observed + 1U, replacement, sizeof(replacement)) == 0);
    CHECK(memcmp(observed + 3U, initial + 3U, sizeof(initial) - 3U) == 0);

    random_handle = create_handle(&allocator, descriptor, R_RUNTIME_DARWIN_IO_RANDOM);
    CHECK(random_handle != NULL);
    buffer = allocate_buffer(&allocator, 1U);
    CHECK(buffer.data != NULL);
    preparation = r_runtime_darwin_io_prepare_read(random_handle, (off_t)0, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(!r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, (off_t)0));
    r_runtime_darwin_io_prepared_abort(&preparation.prepared);
    r_runtime_darwin_io_buffer_release(&buffer);
    CHECK(close_handle(random_handle) == 0);
    CHECK(close(descriptor) == 0);
    CHECK(unlink(path) == 0);
    return 0;
}

static int test_stream_position_native_error(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    int descriptors[2];

    r_runtime_allocator_initialize(&allocator);
    CHECK(pipe(descriptors) == 0);
    handle = create_handle(&allocator, descriptors[0], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(handle != NULL);
    CHECK(close(descriptors[0]) == 0);
    buffer = allocate_buffer(&allocator, 4U);
    CHECK(buffer.data != NULL);
    preparation = r_runtime_darwin_io_prepare_read(handle, (off_t)0, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, (off_t)0));
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_IO_READ);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == ESPIPE);
    CHECK(result.operation_native_error == ESPIPE);
    CHECK(result.native_event_sequence != UINT64_C(0));
    CHECK(result.terminal_event_sequence == result.native_event_sequence);
    CHECK(result.bytes_transferred == 0U);
    CHECK(result.bytes_remaining == 4U);
    CHECK(result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned_buffer.data != NULL);
    CHECK(returned_buffer.size == 0U && returned_buffer.capacity == 4U);
    r_runtime_darwin_io_request_release(submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    CHECK(close(descriptors[1]) == 0);
    CHECK(close_handle(handle) == 0);
    return 0;
}

static int test_stream_position_barrier_cancel_and_deadline(void) {
    static const unsigned char initial[] = {0x61U, 0x62U, 0x63U};
    char path[] = "/tmp/r-dispatch-stream-barrier-XXXXXX";
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    CompletionObservation observation;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = mkstemp(path);
    CHECK(descriptor >= 0);
    CHECK(unlink(path) == 0);
    CHECK(write(descriptor, initial, sizeof(initial)) == (ssize_t)sizeof(initial));
    handle = create_handle(&allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(handle != NULL);
    CHECK(close(descriptor) == 0);

    buffer = allocate_buffer(&allocator, sizeof(initial));
    CHECK(buffer.data != NULL);
    preparation = r_runtime_darwin_io_prepare_read(handle, (off_t)0, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, (off_t)0));
    r_runtime_darwin_io_testing_pause_next_stream_position_barrier();
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    observation.semaphore = dispatch_semaphore_create(0L);
    CHECK(observation.semaphore != NULL);
    atomic_init(&observation.call_count, 0U);
    (void)memset(&observation.result, 0, sizeof(observation.result));
    CHECK(r_runtime_darwin_io_request_set_completion(
        submission.request, observe_completion, &observation));
    r_runtime_darwin_io_testing_wait_for_stream_position_barrier();
    CHECK(r_runtime_darwin_io_request_cancel(submission.request));
    CHECK(r_runtime_darwin_io_request_state(submission.request) ==
          R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE);
    CHECK(dispatch_semaphore_wait(observation.semaphore, DISPATCH_TIME_NOW) != 0L);
    r_runtime_darwin_io_testing_release_stream_position_barrier();
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED);
    CHECK(result.native_error == 0);
    CHECK(result.operation_native_error == 0);
    CHECK(result.terminal_event_sequence != UINT64_C(0));
    CHECK(result.native_event_sequence != UINT64_C(0));
    CHECK(result.terminal_event_sequence < result.native_event_sequence);
    CHECK(result.bytes_transferred == 0U && result.cleanup_acknowledged);
    CHECK(dispatch_semaphore_wait(observation.semaphore,
                                  dispatch_time(DISPATCH_TIME_NOW, INT64_C(2000000000))) == 0L);
    CHECK(atomic_load_explicit(&observation.call_count, memory_order_relaxed) == 1U);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned_buffer.size == 0U && returned_buffer.capacity == sizeof(initial));
    r_runtime_darwin_io_request_release(submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    dispatch_release(observation.semaphore);

    buffer = allocate_buffer(&allocator, sizeof(initial));
    CHECK(buffer.data != NULL);
    preparation = r_runtime_darwin_io_prepare_read(handle, (off_t)0, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, (off_t)0));
    r_runtime_darwin_io_testing_pause_next_stream_position_barrier();
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    r_runtime_darwin_io_testing_wait_for_stream_position_barrier();
    CHECK(r_runtime_darwin_io_request_deadline_expired(submission.request));
    CHECK(r_runtime_darwin_io_request_state(submission.request) ==
          R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE);
    r_runtime_darwin_io_testing_release_stream_position_barrier();
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_TIMED_OUT);
    CHECK(result.native_error == 0);
    CHECK(result.operation_native_error == 0);
    CHECK(result.terminal_event_sequence != UINT64_C(0));
    CHECK(result.native_event_sequence != UINT64_C(0));
    CHECK(result.terminal_event_sequence < result.native_event_sequence);
    CHECK(result.bytes_transferred == 0U && result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned_buffer.size == 0U && returned_buffer.capacity == sizeof(initial));
    r_runtime_darwin_io_request_release(submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);

    CHECK(close_handle(handle) == 0);
    return 0;
}

static int run_native_completion_before_signal(_Bool deadline) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    int descriptors[2];

    r_runtime_allocator_initialize(&allocator);
    CHECK(pipe(descriptors) == 0);
    handle = create_handle(&allocator, descriptors[0], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(handle != NULL);
    CHECK(close(descriptors[0]) == 0);
    buffer = allocate_buffer(&allocator, 4U);
    CHECK(buffer.data != NULL);
    preparation = r_runtime_darwin_io_prepare_read(handle, (off_t)0, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, (off_t)0));
    r_runtime_darwin_io_testing_pause_next_native_completion();
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    r_runtime_darwin_io_testing_wait_for_native_completion();
    if (deadline) {
        CHECK(r_runtime_darwin_io_request_deadline_expired(submission.request));
    } else {
        CHECK(r_runtime_darwin_io_request_cancel(submission.request));
    }
    CHECK(r_runtime_darwin_io_request_state(submission.request) ==
          R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE);
    r_runtime_darwin_io_testing_release_native_completion();

    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_IO_READ);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == ESPIPE);
    CHECK(result.operation_native_error == ESPIPE);
    CHECK(result.native_event_sequence != UINT64_C(0));
    CHECK(result.terminal_event_sequence == result.native_event_sequence);
    CHECK(result.bytes_transferred == 0U && result.bytes_remaining == 4U);
    CHECK(result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned_buffer.data != NULL);
    CHECK(returned_buffer.size == 0U && returned_buffer.capacity == 4U);
    r_runtime_darwin_io_request_release(submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    CHECK(close(descriptors[1]) == 0);
    CHECK(close_handle(handle) == 0);
    return 0;
}

static int run_signal_before_native_completion(_Bool deadline) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    int descriptors[2];

    r_runtime_allocator_initialize(&allocator);
    CHECK(pipe(descriptors) == 0);
    handle = create_handle(&allocator, descriptors[0], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(handle != NULL);
    CHECK(close(descriptors[0]) == 0);
    buffer = allocate_buffer(&allocator, 4U);
    CHECK(buffer.data != NULL);
    preparation = r_runtime_darwin_io_prepare_read(handle, (off_t)0, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, (off_t)0));
    r_runtime_darwin_io_testing_pause_next_stream_position_barrier();
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    r_runtime_darwin_io_testing_wait_for_stream_position_barrier();
    if (deadline) {
        CHECK(r_runtime_darwin_io_request_deadline_expired(submission.request));
    } else {
        CHECK(r_runtime_darwin_io_request_cancel(submission.request));
    }
    r_runtime_darwin_io_testing_release_stream_position_barrier();

    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_IO_READ);
    CHECK(result.terminal_event == (deadline ? R_RUNTIME_DARWIN_IO_TERMINAL_TIMED_OUT
                                             : R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED));
    CHECK(result.native_error == 0);
    CHECK(result.operation_native_error == ESPIPE);
    CHECK(result.terminal_event_sequence != UINT64_C(0));
    CHECK(result.native_event_sequence != UINT64_C(0));
    CHECK(result.terminal_event_sequence < result.native_event_sequence);
    CHECK(result.bytes_transferred == 0U && result.bytes_remaining == 4U);
    CHECK(result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned_buffer.data != NULL);
    CHECK(returned_buffer.size == 0U && returned_buffer.capacity == 4U);
    r_runtime_darwin_io_request_release(submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    CHECK(close(descriptors[1]) == 0);
    CHECK(close_handle(handle) == 0);
    return 0;
}

static int test_native_completion_precedes_cancel_and_deadline(void) {
    CHECK(run_native_completion_before_signal(0) == 0);
    CHECK(run_native_completion_before_signal(1) == 0);
    return 0;
}

static int test_cancel_and_deadline_precede_native_completion(void) {
    CHECK(run_signal_before_native_completion(0) == 0);
    CHECK(run_signal_before_native_completion(1) == 0);
    return 0;
}

static int test_close_waits_for_stream_position_barrier(void) {
    static const unsigned char initial[] = {0x61U, 0x62U, 0x63U};
    char path[] = "/tmp/r-dispatch-stream-close-barrier-XXXXXX";
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult read_submission;
    RRuntimeDarwinIoSubmitResult close_submission;
    RRuntimeDarwinIoResult result;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = mkstemp(path);
    CHECK(descriptor >= 0);
    CHECK(unlink(path) == 0);
    CHECK(write(descriptor, initial, sizeof(initial)) == (ssize_t)sizeof(initial));
    handle = create_handle(&allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(handle != NULL);
    CHECK(close(descriptor) == 0);
    buffer = allocate_buffer(&allocator, sizeof(initial));
    CHECK(buffer.data != NULL);
    preparation = r_runtime_darwin_io_prepare_read(handle, (off_t)0, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, (off_t)0));
    r_runtime_darwin_io_testing_pause_next_stream_position_barrier();
    read_submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(read_submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    r_runtime_darwin_io_testing_wait_for_stream_position_barrier();
    close_submission = r_runtime_darwin_io_submit_close(handle);
    CHECK(close_submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_request_state(read_submission.request) ==
          R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE);
    CHECK(r_runtime_darwin_io_request_state(close_submission.request) ==
          R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE);
    r_runtime_darwin_io_testing_release_stream_position_barrier();
    result = r_runtime_darwin_io_request_wait(read_submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED);
    CHECK(result.native_error == 0 && result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(read_submission.request);
    CHECK(returned_buffer.size == 0U && returned_buffer.capacity == sizeof(initial));
    r_runtime_darwin_io_request_release(read_submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    result = r_runtime_darwin_io_request_wait(close_submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_IO_CLOSE);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0 && result.cleanup_acknowledged);
    r_runtime_darwin_io_request_release(close_submission.request);
    r_runtime_darwin_io_handle_release(handle);
    return 0;
}

static int test_external_deadline_selects_close_without_internal_timer(void) {
    static const unsigned char initial[] = {0x61U, 0x62U, 0x63U};
    char path[] = "/tmp/r-dispatch-external-close-deadline-XXXXXX";
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoPrepareResult read_preparation;
    RRuntimeDarwinIoPrepareResult close_preparation;
    RRuntimeDarwinIoSubmitResult read_submission;
    RRuntimeDarwinIoSubmitResult close_submission;
    RRuntimeDarwinIoResult result;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = mkstemp(path);
    CHECK(descriptor >= 0);
    CHECK(unlink(path) == 0);
    CHECK(write(descriptor, initial, sizeof(initial)) == (ssize_t)sizeof(initial));
    handle = create_handle(&allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(handle != NULL);
    CHECK(close(descriptor) == 0);
    buffer = allocate_buffer(&allocator, sizeof(initial));
    CHECK(buffer.data != NULL);
    read_preparation = r_runtime_darwin_io_prepare_read(handle, 0, &buffer, UINT64_C(0));
    CHECK(read_preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_prepared_set_stream_position(read_preparation.prepared, 0));
    r_runtime_darwin_io_testing_pause_next_stream_position_barrier();
    read_submission = r_runtime_darwin_io_prepared_activate(&read_preparation.prepared, &buffer);
    CHECK(read_submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    r_runtime_darwin_io_testing_wait_for_stream_position_barrier();

    close_preparation = r_runtime_darwin_io_prepare_close(handle, UINT64_C(0));
    CHECK(close_preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    close_submission = r_runtime_darwin_io_prepared_activate_close(&close_preparation.prepared, 0);
    CHECK(close_submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_request_deadline_expired(close_submission.request));
    CHECK(!r_runtime_darwin_io_request_deadline_expired(close_submission.request));
    CHECK(r_runtime_darwin_io_request_state(close_submission.request) ==
          R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE);
    r_runtime_darwin_io_testing_release_stream_position_barrier();

    result = r_runtime_darwin_io_request_wait(read_submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED);
    CHECK(result.native_error == 0 && result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(read_submission.request);
    r_runtime_darwin_io_request_release(read_submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    result = r_runtime_darwin_io_request_wait(close_submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_IO_CLOSE);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_TIMED_OUT);
    CHECK(result.native_error == 0 && result.cleanup_acknowledged);
    r_runtime_darwin_io_request_release(close_submission.request);
    r_runtime_darwin_io_handle_release(handle);
    return 0;
}

static int test_stream_append_and_head_reposition(void) {
    static const unsigned char initial[] = {0x30U, 0x31U};
    static const unsigned char appended[] = {0x61U, 0x62U};
    char path[] = "/tmp/r-dispatch-append-XXXXXX";
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    struct stat status;
    unsigned char observed[sizeof(initial) + sizeof(appended)];
    int descriptor;
    int flags;

    r_runtime_allocator_initialize(&allocator);
    descriptor = mkstemp(path);
    CHECK(descriptor >= 0);
    CHECK(unlink(path) == 0);
    CHECK(write(descriptor, initial, sizeof(initial)) == (ssize_t)sizeof(initial));
    flags = fcntl(descriptor, F_GETFL);
    CHECK(flags >= 0);
    CHECK(fcntl(descriptor, F_SETFL, flags | O_APPEND) == 0);
    CHECK(lseek(descriptor, (off_t)0, SEEK_SET) == (off_t)0);
    handle = create_handle(&allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(handle != NULL);

    buffer = allocate_buffer(&allocator, sizeof(appended));
    CHECK(buffer.data != NULL);
    (void)memcpy(buffer.data, appended, sizeof(appended));
    buffer.size = sizeof(appended);
    submission = r_runtime_darwin_io_submit_write(handle, 0, &buffer, UINT64_C(0));
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred == sizeof(appended));
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    r_runtime_darwin_io_request_release(submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    CHECK(lseek(descriptor, (off_t)0, SEEK_CUR) == (off_t)sizeof(observed));
    CHECK(fstat(descriptor, &status) == 0);
    CHECK(status.st_size == (off_t)sizeof(observed));
    CHECK(pread(descriptor, observed, sizeof(observed), (off_t)0) == (ssize_t)sizeof(observed));
    CHECK(memcmp(observed, initial, sizeof(initial)) == 0);
    CHECK(memcmp(observed + sizeof(initial), appended, sizeof(appended)) == 0);

    buffer = allocate_buffer(&allocator, sizeof(initial));
    CHECK(buffer.data != NULL);
    preparation = r_runtime_darwin_io_prepare_read(handle, 0, &buffer, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, (off_t)0));
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred == sizeof(initial));
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(memcmp(returned_buffer.data, initial, sizeof(initial)) == 0);
    r_runtime_darwin_io_request_release(submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);

    CHECK(close(descriptor) == 0);
    CHECK(close_handle(handle) == 0);
    return 0;
}

static int test_per_direction_submission_order(void) {
    static const unsigned char input[] = {0x61U, 0x62U, 0x63U, 0x64U};
    static const unsigned char first_output[] = {0x31U, 0x32U};
    static const unsigned char second_output[] = {0x33U, 0x34U};
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *input_handle;
    RRuntimeDarwinIoHandle *output_handle;
    RRuntimeDarwinIoBuffer first_buffer;
    RRuntimeDarwinIoBuffer second_buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoSubmitResult first_submission;
    RRuntimeDarwinIoSubmitResult second_submission;
    RRuntimeDarwinIoResult result;
    unsigned char observed[sizeof(input)];
    int input_pipe[2];
    int output_pipe[2];

    r_runtime_allocator_initialize(&allocator);
    CHECK(pipe(input_pipe) == 0);
    input_handle = create_handle(&allocator, input_pipe[0], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(input_handle != NULL);
    CHECK(close(input_pipe[0]) == 0);
    CHECK(write(input_pipe[1], input, sizeof(input)) == (ssize_t)sizeof(input));
    first_buffer = allocate_buffer(&allocator, 2U);
    second_buffer = allocate_buffer(&allocator, 2U);
    CHECK(first_buffer.data != NULL && second_buffer.data != NULL);
    first_submission = r_runtime_darwin_io_submit_read(input_handle, 0, &first_buffer, UINT64_C(0));
    second_submission =
        r_runtime_darwin_io_submit_read(input_handle, 0, &second_buffer, UINT64_C(0));
    CHECK(first_submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(second_submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    result = r_runtime_darwin_io_request_wait(second_submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.bytes_transferred == 2U);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(first_submission.request);
    CHECK(returned_buffer.size == 2U && memcmp(returned_buffer.data, input, 2U) == 0);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(second_submission.request);
    CHECK(returned_buffer.size == 2U && memcmp(returned_buffer.data, input + 2U, 2U) == 0);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    r_runtime_darwin_io_request_release(first_submission.request);
    r_runtime_darwin_io_request_release(second_submission.request);
    CHECK(close(input_pipe[1]) == 0);
    CHECK(close_handle(input_handle) == 0);

    CHECK(pipe(output_pipe) == 0);
    output_handle = create_handle(&allocator, output_pipe[1], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(output_handle != NULL);
    CHECK(close(output_pipe[1]) == 0);
    first_buffer = allocate_buffer(&allocator, sizeof(first_output));
    second_buffer = allocate_buffer(&allocator, sizeof(second_output));
    CHECK(first_buffer.data != NULL && second_buffer.data != NULL);
    (void)memcpy(first_buffer.data, first_output, sizeof(first_output));
    first_buffer.size = sizeof(first_output);
    (void)memcpy(second_buffer.data, second_output, sizeof(second_output));
    second_buffer.size = sizeof(second_output);
    first_submission =
        r_runtime_darwin_io_submit_write(output_handle, 0, &first_buffer, UINT64_C(0));
    second_submission =
        r_runtime_darwin_io_submit_write(output_handle, 0, &second_buffer, UINT64_C(0));
    CHECK(first_submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(second_submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    result = r_runtime_darwin_io_request_wait(second_submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.bytes_transferred == sizeof(second_output));
    returned_buffer = r_runtime_darwin_io_request_take_buffer(first_submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(second_submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    r_runtime_darwin_io_request_release(first_submission.request);
    r_runtime_darwin_io_request_release(second_submission.request);
    read_exact(output_pipe[0], observed, sizeof(observed));
    CHECK(memcmp(observed, first_output, sizeof(first_output)) == 0);
    CHECK(memcmp(observed + sizeof(first_output), second_output, sizeof(second_output)) == 0);
    CHECK(close(output_pipe[0]) == 0);
    CHECK(close_handle(output_handle) == 0);
    return 0;
}

static int test_queued_cancel_and_deadline_preserve_buffers(void) {
    static const unsigned char payload[] = {0x71U, 0x75U, 0x65U, 0x75U, 0x65U};
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoPrepareResult flush_preparation;
    RRuntimeDarwinIoPrepareResult write_preparation;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    unsigned char observed[sizeof(payload)];
    ssize_t count;
    int descriptors[2];
    int flags;

    r_runtime_allocator_initialize(&allocator);
    CHECK(pipe(descriptors) == 0);
    flags = fcntl(descriptors[0], F_GETFL);
    CHECK(flags >= 0);
    CHECK(fcntl(descriptors[0], F_SETFL, flags | O_NONBLOCK) == 0);
    handle = create_handle(&allocator, descriptors[1], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(handle != NULL);
    CHECK(close(descriptors[1]) == 0);
    flush_preparation = r_runtime_darwin_io_prepare_flush(handle, UINT64_C(0));
    CHECK(flush_preparation.status == R_RUNTIME_DARWIN_IO_START_OK);

    buffer = allocate_buffer(&allocator, sizeof(payload));
    CHECK(buffer.data != NULL);
    (void)memcpy(buffer.data, payload, sizeof(payload));
    buffer.size = sizeof(payload);
    write_preparation = r_runtime_darwin_io_prepare_write(handle, 0, &buffer, UINT64_C(0));
    CHECK(write_preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    submission = r_runtime_darwin_io_prepared_activate(&write_preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_request_cancel(submission.request));
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED);
    CHECK(result.bytes_transferred == 0U && result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned_buffer.size == sizeof(payload));
    CHECK(memcmp(returned_buffer.data, payload, sizeof(payload)) == 0);
    r_runtime_darwin_io_request_release(submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);

    buffer = allocate_buffer(&allocator, sizeof(payload));
    CHECK(buffer.data != NULL);
    (void)memcpy(buffer.data, payload, sizeof(payload));
    buffer.size = sizeof(payload);
    write_preparation = r_runtime_darwin_io_prepare_write(handle, 0, &buffer, UINT64_C(5000000));
    CHECK(write_preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    submission = r_runtime_darwin_io_prepared_activate(&write_preparation.prepared, &buffer);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_TIMED_OUT);
    CHECK(result.bytes_transferred == 0U && result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned_buffer.size == sizeof(payload));
    CHECK(memcmp(returned_buffer.data, payload, sizeof(payload)) == 0);
    r_runtime_darwin_io_request_release(submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);

    count = read(descriptors[0], observed, sizeof(observed));
    CHECK(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
    r_runtime_darwin_io_prepared_abort(&flush_preparation.prepared);
    CHECK(close(descriptors[0]) == 0);
    CHECK(close_handle(handle) == 0);
    return 0;
}

static int test_allocator_failure_preserves_owners(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoBufferResult failed_buffer;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoHandleCreateResult failed_handle;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoSubmitResult submission;
    unsigned char *original_data;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    failed_buffer = r_runtime_darwin_io_buffer_allocate(&allocator, 16U);
    CHECK(failed_buffer.status == R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED);
    CHECK(failed_buffer.buffer.data == NULL);
    CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));

    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    CHECK(descriptor >= 0);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    failed_handle =
        r_runtime_darwin_io_handle_create(&allocator, descriptor, R_RUNTIME_DARWIN_IO_RANDOM);
    CHECK(failed_handle.status == R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED);
    CHECK(failed_handle.handle == NULL);
    CHECK(fcntl(descriptor, F_GETFD) >= 0);

    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    handle = create_handle(&allocator, descriptor, R_RUNTIME_DARWIN_IO_RANDOM);
    CHECK(handle != NULL);
    CHECK(close(descriptor) == 0);
    buffer = allocate_buffer(&allocator, 16U);
    CHECK(buffer.data != NULL);
    original_data = buffer.data;
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    submission = r_runtime_darwin_io_submit_read(handle, 0, &buffer, UINT64_C(0));
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED);
    CHECK(submission.request == NULL);
    CHECK(buffer.data == original_data);
    CHECK(buffer.capacity == 16U);
    CHECK(buffer.size == 0U);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    r_runtime_darwin_io_buffer_release(&buffer);
    CHECK(close_handle(handle) == 0);
    return 0;
}

static int test_nonblocking_completion_registration(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    CompletionObservation observation;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    CHECK(descriptor >= 0);
    handle = create_handle(&allocator, descriptor, R_RUNTIME_DARWIN_IO_RANDOM);
    CHECK(handle != NULL);
    CHECK(close(descriptor) == 0);
    buffer = allocate_buffer(&allocator, 8U);
    CHECK(buffer.data != NULL);
    submission = r_runtime_darwin_io_submit_read(handle, 0, &buffer, UINT64_C(0));
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.eof);

    observation.semaphore = dispatch_semaphore_create(0L);
    CHECK(observation.semaphore != NULL);
    atomic_init(&observation.call_count, 0U);
    (void)memset(&observation.result, 0, sizeof(observation.result));
    CHECK(r_runtime_darwin_io_request_set_completion(
        submission.request, observe_completion, &observation));
    CHECK(!r_runtime_darwin_io_request_set_completion(
        submission.request, observe_completion, &observation));
    CHECK(dispatch_semaphore_wait(observation.semaphore,
                                  dispatch_time(DISPATCH_TIME_NOW, INT64_C(2000000000))) == 0L);
    CHECK(atomic_load_explicit(&observation.call_count, memory_order_relaxed) == 1U);
    CHECK(observation.result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    r_runtime_darwin_io_request_release(submission.request);
    dispatch_release(observation.semaphore);
    CHECK(close_handle(handle) == 0);
    return 0;
}

static int test_shared_write_borrows_without_buffer_transfer(void) {
    static const unsigned char payload[] = {0x73U, 0x68U, 0x61U, 0x72U, 0x65U, 0x64U};
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    RRuntimeDarwinIoBuffer returned_buffer;
    unsigned char observed[sizeof(payload)];
    int descriptors[2];

    r_runtime_allocator_initialize(&allocator);
    CHECK(pipe(descriptors) == 0);
    handle = create_handle(&allocator, descriptors[1], R_RUNTIME_DARWIN_IO_STREAM);
    CHECK(handle != NULL);
    CHECK(close(descriptors[1]) == 0);
    preparation =
        r_runtime_darwin_io_prepare_shared_write(handle, 0, payload, sizeof(payload), UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(preparation.prepared != NULL);
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, NULL);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(submission.request != NULL);
    CHECK(preparation.prepared == NULL);
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_IO_WRITE);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred == sizeof(payload));
    CHECK(result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned_buffer.data == NULL);
    CHECK(returned_buffer.allocator == NULL);
    r_runtime_darwin_io_request_release(submission.request);
    CHECK(read_exact(descriptors[0], observed, sizeof(observed)) == 0);
    CHECK(memcmp(observed, payload, sizeof(payload)) == 0);
    CHECK(close(descriptors[0]) == 0);
    CHECK(close_handle(handle) == 0);
    return 0;
}

static int test_borrowed_random_shared_write_prepared_abort(void) {
    static const unsigned char payload[] = {0x70U, 0x72U, 0x65U, 0x70U};
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoPrepareResult preparation;

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    preparation = r_runtime_darwin_io_prepare_borrowed_random_shared_write(
        &allocator, (off_t)0, payload, sizeof(payload), UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED);
    CHECK(preparation.prepared == NULL);
    CHECK(payload[0] == 0x70U && payload[sizeof(payload) - 1U] == 0x70U);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    preparation = r_runtime_darwin_io_prepare_borrowed_random_shared_write(
        &allocator, (off_t)0, payload, sizeof(payload), UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(preparation.prepared != NULL);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    r_runtime_darwin_io_prepared_abort(&preparation.prepared);
    CHECK(preparation.prepared == NULL);
    CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    return 0;
}

static int test_borrowed_random_shared_write_bound_failure_cleanup(void) {
    static const unsigned char payload[] = {0x66U, 0x61U, 0x69U, 0x6cU};
    char path[] = "/tmp/r-dispatch-borrowed-failure-XXXXXX";
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult submission;
    HandleCleanupObservation cleanup;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = mkstemp(path);
    CHECK(descriptor >= 0);
    preparation = r_runtime_darwin_io_prepare_borrowed_random_shared_write(
        &allocator, (off_t)0, payload, sizeof(payload), UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    cleanup.semaphore = dispatch_semaphore_create(0L);
    CHECK(cleanup.semaphore != NULL);
    atomic_init(&cleanup.call_count, 0U);
    atomic_init(&cleanup.native_error, 0);
    r_runtime_darwin_io_testing_fail_prepare_stage(R_RUNTIME_DARWIN_IO_PREPARE_FAIL_CHANNEL);
    submission = r_runtime_darwin_io_prepared_bind_activate_borrowed_random_shared_write(
        &preparation.prepared, descriptor, observe_handle_cleanup, &cleanup);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED);
    CHECK(submission.request == NULL);
    CHECK(preparation.prepared == NULL);
    CHECK(dispatch_semaphore_wait(cleanup.semaphore,
                                  dispatch_time(DISPATCH_TIME_NOW, INT64_C(2000000000))) == 0L);
    CHECK(atomic_load_explicit(&cleanup.call_count, memory_order_acquire) == 1U);
    CHECK(atomic_load_explicit(&cleanup.native_error, memory_order_relaxed) == 0);
    CHECK(fcntl(descriptor, F_GETFD) >= 0);
    CHECK(close(descriptor) == 0);
    CHECK(unlink(path) == 0);
    dispatch_release(cleanup.semaphore);
    return 0;
}

static int test_borrowed_random_shared_write_success_and_fd_ownership(void) {
    static const unsigned char payload[] = {0x62U, 0x6fU, 0x72U, 0x72U, 0x6fU, 0x77U, 0x65U, 0x64U};
    char path[] = "/tmp/r-dispatch-borrowed-write-XXXXXX";
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoPreparedRequest *prepared_identity;
    RRuntimeDarwinIoSubmitResult submission;
    BorrowedPipelineObservation observation;
    unsigned char observed[sizeof(payload)];
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    descriptor = mkstemp(path);
    CHECK(descriptor >= 0);
    preparation = r_runtime_darwin_io_prepare_borrowed_random_shared_write(
        &allocator, (off_t)0, payload, sizeof(payload), UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(preparation.prepared != NULL);
    prepared_identity = preparation.prepared;
    observation.completion_semaphore = dispatch_semaphore_create(0L);
    observation.cleanup_semaphore = dispatch_semaphore_create(0L);
    CHECK(observation.completion_semaphore != NULL && observation.cleanup_semaphore != NULL);
    atomic_init(&observation.completion_count, 0U);
    atomic_init(&observation.cleanup_count, 0U);
    atomic_init(&observation.cleanup_error, 0);
    atomic_init(&observation.completion_finished, 0);
    atomic_init(&observation.ordering_failed, 0);
    (void)memset(&observation.result, 0, sizeof(observation.result));

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    submission = r_runtime_darwin_io_prepared_bind_activate_borrowed_random_shared_write(
        &preparation.prepared, descriptor, observe_borrowed_pipeline_cleanup, &observation);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(submission.request == prepared_identity);
    CHECK(preparation.prepared == NULL);
    CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(0));
    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));

    CHECK(r_runtime_darwin_io_request_set_completion(
        submission.request, observe_borrowed_pipeline_completion, &observation));
    CHECK(dispatch_semaphore_wait(observation.completion_semaphore,
                                  dispatch_time(DISPATCH_TIME_NOW, INT64_C(2000000000))) == 0L);
    CHECK(atomic_load_explicit(&observation.completion_count, memory_order_relaxed) == 1U);
    CHECK(observation.result.operation == R_RUNTIME_DARWIN_IO_WRITE);
    CHECK(observation.result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE);
    CHECK(observation.result.native_error == 0);
    CHECK(observation.result.bytes_transferred == sizeof(payload));
    CHECK(observation.result.bytes_remaining == 0U);
    CHECK(observation.result.cleanup_acknowledged);
    CHECK(dispatch_semaphore_wait(observation.cleanup_semaphore,
                                  dispatch_time(DISPATCH_TIME_NOW, INT64_C(2000000000))) == 0L);
    CHECK(atomic_load_explicit(&observation.cleanup_count, memory_order_acquire) == 1U);
    CHECK(atomic_load_explicit(&observation.cleanup_error, memory_order_relaxed) == 0);
    CHECK(!atomic_load_explicit(&observation.ordering_failed, memory_order_relaxed));
    CHECK(fcntl(descriptor, F_GETFD) >= 0);
    CHECK(pread(descriptor, observed, sizeof(observed), (off_t)0) == (ssize_t)sizeof(observed));
    CHECK(memcmp(observed, payload, sizeof(payload)) == 0);
    CHECK(close(descriptor) == 0);
    CHECK(unlink(path) == 0);
    dispatch_release(observation.completion_semaphore);
    dispatch_release(observation.cleanup_semaphore);
    return 0;
}

static int test_borrowed_random_shared_write_cancel(void) {
    const size_t payload_size = 32U * 1024U * 1024U;
    char path[] = "/tmp/r-dispatch-borrowed-cancel-XXXXXX";
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    HandleCleanupObservation cleanup;
    unsigned char *payload;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    CHECK(r_runtime_allocator_allocate(
              &allocator, payload_size, _Alignof(unsigned char), (void **)&payload) ==
          R_RUNTIME_ALLOCATION_OK);
    (void)memset(payload, 0x5b, payload_size);
    descriptor = mkstemp(path);
    CHECK(descriptor >= 0);
    preparation = r_runtime_darwin_io_prepare_borrowed_random_shared_write(
        &allocator, (off_t)0, payload, payload_size, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    cleanup.semaphore = dispatch_semaphore_create(0L);
    CHECK(cleanup.semaphore != NULL);
    atomic_init(&cleanup.call_count, 0U);
    atomic_init(&cleanup.native_error, 0);
    submission = r_runtime_darwin_io_prepared_bind_activate_borrowed_random_shared_write(
        &preparation.prepared, descriptor, observe_handle_cleanup, &cleanup);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_request_cancel(submission.request));
    CHECK(!r_runtime_darwin_io_request_deadline_expired(submission.request));
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred < payload_size);
    CHECK(result.bytes_remaining == payload_size - result.bytes_transferred);
    CHECK(result.cleanup_acknowledged);
    r_runtime_darwin_io_request_release(submission.request);
    CHECK(dispatch_semaphore_wait(cleanup.semaphore,
                                  dispatch_time(DISPATCH_TIME_NOW, INT64_C(2000000000))) == 0L);
    CHECK(atomic_load_explicit(&cleanup.call_count, memory_order_acquire) == 1U);
    CHECK(atomic_load_explicit(&cleanup.native_error, memory_order_relaxed) == 0);
    CHECK(fcntl(descriptor, F_GETFD) >= 0);
    CHECK(close(descriptor) == 0);
    CHECK(unlink(path) == 0);
    dispatch_release(cleanup.semaphore);
    r_runtime_allocator_deallocate(payload, _Alignof(unsigned char));
    return 0;
}

static int test_borrowed_random_shared_write_deadline(void) {
    const size_t payload_size = 32U * 1024U * 1024U;
    char path[] = "/tmp/r-dispatch-borrowed-deadline-XXXXXX";
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    HandleCleanupObservation cleanup;
    unsigned char *payload;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    CHECK(r_runtime_allocator_allocate(
              &allocator, payload_size, _Alignof(unsigned char), (void **)&payload) ==
          R_RUNTIME_ALLOCATION_OK);
    (void)memset(payload, 0x6c, payload_size);
    descriptor = mkstemp(path);
    CHECK(descriptor >= 0);
    preparation = r_runtime_darwin_io_prepare_borrowed_random_shared_write(
        &allocator, (off_t)0, payload, payload_size, UINT64_C(5000000000));
    CHECK(preparation.status == R_RUNTIME_DARWIN_IO_START_OK);
    cleanup.semaphore = dispatch_semaphore_create(0L);
    CHECK(cleanup.semaphore != NULL);
    atomic_init(&cleanup.call_count, 0U);
    atomic_init(&cleanup.native_error, 0);
    submission = r_runtime_darwin_io_prepared_bind_activate_borrowed_random_shared_write(
        &preparation.prepared, descriptor, observe_handle_cleanup, &cleanup);
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    CHECK(r_runtime_darwin_io_request_deadline_expired(submission.request));
    CHECK(!r_runtime_darwin_io_request_cancel(submission.request));
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_TIMED_OUT);
    CHECK(result.native_error == 0);
    CHECK(result.bytes_transferred < payload_size);
    CHECK(result.bytes_remaining == payload_size - result.bytes_transferred);
    CHECK(result.cleanup_acknowledged);
    r_runtime_darwin_io_request_release(submission.request);
    CHECK(dispatch_semaphore_wait(cleanup.semaphore,
                                  dispatch_time(DISPATCH_TIME_NOW, INT64_C(2000000000))) == 0L);
    CHECK(atomic_load_explicit(&cleanup.call_count, memory_order_acquire) == 1U);
    CHECK(atomic_load_explicit(&cleanup.native_error, memory_order_relaxed) == 0);
    CHECK(fcntl(descriptor, F_GETFD) >= 0);
    CHECK(close(descriptor) == 0);
    CHECK(unlink(path) == 0);
    dispatch_release(cleanup.semaphore);
    r_runtime_allocator_deallocate(payload, _Alignof(unsigned char));
    return 0;
}

static int test_console_stop_acknowledges_pending_input(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoConsoleStartResult console_start;
    RRuntimeDarwinIoHandle *input;
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoBuffer returned_buffer;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    int descriptors[2];
    int saved_stdin;

    r_runtime_allocator_initialize(&allocator);
    CHECK(pipe(descriptors) == 0);
    saved_stdin = dup(STDIN_FILENO);
    CHECK(saved_stdin >= 0);
    CHECK(dup2(descriptors[0], STDIN_FILENO) == STDIN_FILENO);
    CHECK(close(descriptors[0]) == 0);
    console_start = r_runtime_darwin_io_process_console_start(&allocator);
    CHECK(console_start.status == R_RUNTIME_DARWIN_IO_START_OK);
    input = r_runtime_darwin_io_process_stdin_retain();
    CHECK(input != NULL);
    buffer = allocate_buffer(&allocator, 32U);
    CHECK(buffer.data != NULL);
    submission = r_runtime_darwin_io_submit_read(input, 0, &buffer, UINT64_C(0));
    CHECK(submission.status == R_RUNTIME_DARWIN_IO_START_OK);
    r_runtime_darwin_io_process_console_stop();
    result = r_runtime_darwin_io_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED);
    CHECK(result.native_error == 0);
    CHECK(result.cleanup_acknowledged);
    returned_buffer = r_runtime_darwin_io_request_take_buffer(submission.request);
    CHECK(returned_buffer.data != NULL && returned_buffer.capacity == 32U);
    r_runtime_darwin_io_request_release(submission.request);
    r_runtime_darwin_io_buffer_release(&returned_buffer);
    r_runtime_darwin_io_handle_release(input);
    CHECK(close(descriptors[1]) == 0);
    CHECK(dup2(saved_stdin, STDIN_FILENO) == STDIN_FILENO);
    CHECK(close(saved_stdin) == 0);
    return 0;
}

int main(void) {
    CHECK(test_handle_create_failure_closes_duplicate_before_return() == 0);
    CHECK(test_handle_release_cleanup_observer() == 0);
    CHECK(test_handle_release_cleanup_observer_after_close() == 0);
    CHECK(test_random_file_read_write_flush_and_close() == 0);
    CHECK(test_stream_eof_preserves_partial_buffer() == 0);
    CHECK(test_pseudo_terminal_console_read_write() == 0);
    CHECK(test_cancel_and_deadline_acknowledgement() == 0);
    CHECK(test_partial_write_cancel_retains_source() == 0);
    CHECK(test_flush_deadline_before_barrier_commit() == 0);
    CHECK(test_close_rejects_new_start_without_consumption() == 0);
    CHECK(test_handle_drop_preserves_started_operation() == 0);
    CHECK(test_prepared_write_reservation_and_failure_stages() == 0);
    CHECK(test_prepared_read_and_flush_ordering() == 0);
    CHECK(test_prepared_random_offset_late_binding() == 0);
    CHECK(test_prepared_stream_position_late_binding() == 0);
    CHECK(test_stream_position_native_error() == 0);
    CHECK(test_stream_position_barrier_cancel_and_deadline() == 0);
    CHECK(test_native_completion_precedes_cancel_and_deadline() == 0);
    CHECK(test_cancel_and_deadline_precede_native_completion() == 0);
    CHECK(test_close_waits_for_stream_position_barrier() == 0);
    CHECK(test_external_deadline_selects_close_without_internal_timer() == 0);
    CHECK(test_stream_append_and_head_reposition() == 0);
    CHECK(test_per_direction_submission_order() == 0);
    CHECK(test_queued_cancel_and_deadline_preserve_buffers() == 0);
    CHECK(test_allocator_failure_preserves_owners() == 0);
    CHECK(test_nonblocking_completion_registration() == 0);
    CHECK(test_shared_write_borrows_without_buffer_transfer() == 0);
    CHECK(test_borrowed_random_shared_write_prepared_abort() == 0);
    CHECK(test_borrowed_random_shared_write_bound_failure_cleanup() == 0);
    CHECK(test_borrowed_random_shared_write_success_and_fd_ownership() == 0);
    CHECK(test_borrowed_random_shared_write_cancel() == 0);
    CHECK(test_borrowed_random_shared_write_deadline() == 0);
    CHECK(test_console_stop_acknowledges_pending_input() == 0);
    (void)puts("Darwin Dispatch I/O adapter tests passed");
    return 0;
}
