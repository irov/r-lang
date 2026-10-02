#include "r_std_io.h"

#include "r_runtime_allocator.h"
#include "r_runtime_array.h"
#include "r_runtime_darwin_io.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"
#include "r_std_time.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void fail(const char *message) {
    (void)fprintf(stderr, "library io test failed: %s\n", message);
    exit(EXIT_FAILURE);
}

static void require(_Bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
}

static RRuntimeArray byte_array(RRuntimeAllocator *allocator, size_t length, unsigned char fill) {
    const RRuntimeTypeInfo byte_type = {
        sizeof(uint8_t),
        _Alignof(uint8_t),
        NULL,
        NULL,
    };
    RRuntimeArray array;

    require(r_runtime_array_with_capacity(&array, allocator, byte_type, length) ==
                R_RUNTIME_ARRAY_OK,
            "allocate byte array");
    array.length = length;
    if (length != 0U) {
        (void)memset(array.data, (int)fill, length);
    }
    return array;
}

static void move_byte_array(void *destination, void *source) {
    *(RRuntimeArray *)destination = *(RRuntimeArray *)source;
    (void)memset(source, 0, sizeof(RRuntimeArray));
}

static void drop_byte_array(void *value) {
    r_runtime_array_destroy(value);
}

static RRuntimeArc
shared_byte_array(RRuntimeAllocator *allocator, const unsigned char *bytes, size_t length) {
    const RRuntimeTypeInfo array_type = {
        sizeof(RRuntimeArray),
        _Alignof(RRuntimeArray),
        move_byte_array,
        drop_byte_array,
    };
    RRuntimeArray array = byte_array(allocator, length, 0U);
    RRuntimeArc owner = {0};

    if (length != 0U) {
        (void)memcpy(array.data, bytes, length);
    }
    require(r_runtime_arc_create(allocator, array_type, &array, &owner) == R_RUNTIME_ARC_OK,
            "create shared byte array");
    require(array.data == NULL, "shared byte array consumed staged array");
    return owner;
}

static RRuntimeDarwinIoHandle *stream_handle(RRuntimeAllocator *allocator, int descriptor) {
    RRuntimeDarwinIoHandleCreateResult created =
        r_runtime_darwin_io_handle_create(allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);

    require(created.status == R_RUNTIME_DARWIN_IO_START_OK && created.handle != NULL,
            "create stream handle");
    return created.handle;
}

static void close_handle(RRuntimeDarwinIoHandle *handle) {
    RRuntimeDarwinIoSubmitResult submission = r_runtime_darwin_io_submit_close(handle);
    RRuntimeDarwinIoResult result;

    require(submission.status == R_RUNTIME_DARWIN_IO_START_OK && submission.request != NULL,
            "submit stream close");
    result = r_runtime_darwin_io_request_wait(submission.request);
    require(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE &&
                result.native_error == 0 && result.cleanup_acknowledged,
            "complete stream close");
    r_runtime_darwin_io_request_release(submission.request);
    r_runtime_darwin_io_handle_release(handle);
}

static void read_exact(int descriptor, unsigned char *bytes, size_t length) {
    size_t offset = 0U;

    while (offset != length) {
        ssize_t count = read(descriptor, bytes + offset, length - offset);

        require(count > 0, "read expected pipe bytes");
        offset += (size_t)count;
    }
}

static RStdIoReadResult await_read(RStdIoTaskStartResult started) {
    RStdIoReadResult result = {0};

    require(started.is_ok && started.task != NULL, "read task start");
    require(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
            "await read task");
    return result;
}

static RStdIoWriteResult await_write(RStdIoTaskStartResult started) {
    RStdIoWriteResult result = {0};

    require(started.is_ok && started.task != NULL, "write task start");
    require(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
            "await write task");
    return result;
}

static RStdIoWriteAllResult await_write_all(RStdIoTaskStartResult started) {
    RStdIoWriteAllResult result = {0};

    require(started.is_ok && started.task != NULL, "write_all task start");
    require(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
            "await write_all task");
    return result;
}

static RStdIoVoidResult await_flush(RStdIoTaskStartResult started) {
    RStdIoVoidResult result = {0};

    require(started.is_ok && started.task != NULL, "flush task start");
    require(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
            "await flush task");
    return result;
}

static RStdIoSharedWriteResult await_shared_write(RStdIoTaskStartResult started) {
    RStdIoSharedWriteResult result = {0};

    require(started.is_ok && started.task != NULL, "shared write task start");
    require(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
            "await shared write task");
    return result;
}

static RStdIoVoidResult await_close(RStdIoTaskStartResult started) {
    RStdIoVoidResult result = {0};

    require(started.is_ok && started.task != NULL, "close task start");
    require(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
            "await close task");
    return result;
}

static void test_as_error(void) {
    static const RStdIoError cases[] = {
        {R_STD_IO_ERROR_CLOSED, INT64_C(-1)},
        {R_STD_IO_ERROR_TIMED_OUT, INT64_MIN},
        {R_STD_IO_ERROR_OTHER, INT64_MAX},
    };
    size_t index;

    for (index = 0U; index < (sizeof(cases) / sizeof(cases[0])); ++index) {
        const RStdIoError source = cases[index];
        const RStdError converted = r_std_io_as_error(source);

        require(converted.domain == R_STD_ERROR_DOMAIN_IO, "as_error IO domain");
        require(converted.code == (uint32_t)source.code, "as_error portable code");
        require(converted.native_code == source.native_code, "as_error native code");
        require(cases[index].code == source.code && cases[index].native_code == source.native_code,
                "as_error preserves Copy source");
    }
}

typedef struct ExecutorStopContext {
    _Bool stopped;
} ExecutorStopContext;

static void probe_task_body(RRuntimeTaskExecution *execution, void *payload, void *result) {
    (void)execution;
    (void)payload;
    (void)result;
}

static void *stop_executor(void *context_pointer) {
    ExecutorStopContext *context = context_pointer;

    context->stopped = r_runtime_executor_lifecycle_stop();
    return NULL;
}

static void wait_for_executor_stop_selection(void) {
    const RRuntimeTypeInfo empty_type = {
        0U,
        1U,
        NULL,
        NULL,
    };

    for (;;) {
        RRuntimeTaskPrepareResult preparation =
            r_runtime_task_start_prepare(empty_type, empty_type, probe_task_body);

        if (preparation.status == R_RUNTIME_TASK_START_RUNTIME_STOPPING) {
            return;
        }
        require(preparation.status == R_RUNTIME_TASK_START_OK && preparation.transaction != NULL,
                "probe executor stopping state");
        r_runtime_task_start_abort(&preparation.transaction);
        (void)sched_yield();
    }
}

static pthread_t begin_executor_stop(ExecutorStopContext *context) {
    pthread_t thread;

    context->stopped = 0;
    require(pthread_create(&thread, NULL, stop_executor, context) == 0,
            "start executor drain thread");
    wait_for_executor_stop_selection();
    return thread;
}

static void finish_executor_stop(pthread_t thread, ExecutorStopContext *context) {
    require(pthread_join(thread, NULL) == 0, "join executor drain thread");
    require(context->stopped, "executor drain selected task cancellation");
}

static void test_native_stream_result_precedes_task_cancellation(RRuntimeAllocator *allocator) {
    static const unsigned char bytes[] = {0x72U, 0x61U, 0x63U, 0x65U};
    RRuntimeDarwinIoHandle *handle;
    RStdIoOutput output;
    RRuntimeArray buffer;
    RStdIoTaskStartResult started;
    RStdIoWriteAllResult result;
    ExecutorStopContext stop_context;
    pthread_t stop_thread;
    int descriptor;

    require(r_runtime_executor_lifecycle_start(allocator) == R_RUNTIME_EXECUTOR_START_OK,
            "start native-first stream executor");
    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    require(descriptor >= 0, "open native-first read-only descriptor");
    handle = stream_handle(allocator, descriptor);
    require(close(descriptor) == 0, "close borrowed native-first descriptor");
    output.handle = handle;
    buffer = byte_array(allocator, sizeof(bytes), 0U);
    (void)memcpy(buffer.data, bytes, sizeof(bytes));

    r_runtime_darwin_io_testing_pause_next_native_completion();
    started = r_std_io_write_all(&output, &buffer, (RStdIoDeadline){0});
    require(started.is_ok && started.task != NULL && buffer.data == NULL,
            "start native-first stream write");
    r_runtime_darwin_io_testing_wait_for_native_completion();
    stop_thread = begin_executor_stop(&stop_context);
    r_runtime_darwin_io_testing_release_native_completion();
    finish_executor_stop(stop_thread, &stop_context);

    result = await_write_all(started);
    require(result.kind == R_STD_IO_WRITE_ALL_RESULT_FAILED && result.written == 0U &&
                result.error.code == R_STD_IO_ERROR_CLOSED && result.error.native_code == EBADF,
            "earlier native stream error replaced delayed task cancellation");
    require(memcmp(result.buffer.data, bytes, sizeof(bytes)) == 0,
            "native-first stream error returned unchanged source owner");
    r_runtime_array_destroy(&result.buffer);
    close_handle(handle);
}

static void test_native_close_result_precedes_task_cancellation(RRuntimeAllocator *allocator) {
    RRuntimeDarwinIoHandle *handle;
    RStdIoOutput output;
    RStdIoTaskStartResult started;
    RStdIoVoidResult result;
    ExecutorStopContext stop_context;
    pthread_t stop_thread;
    int descriptor;

    require(r_runtime_executor_lifecycle_start(allocator) == R_RUNTIME_EXECUTOR_START_OK,
            "start native-first close executor");
    descriptor = open("/dev/null", O_WRONLY | O_CLOEXEC);
    require(descriptor >= 0, "open native-first close descriptor");
    handle = stream_handle(allocator, descriptor);
    require(close(descriptor) == 0, "close borrowed native-first close descriptor");
    output.handle = handle;

    r_runtime_darwin_io_testing_force_root_cleanup_error(EIO);
    r_runtime_darwin_io_testing_pause_next_native_completion();
    started = r_std_io_close_output(&output, (RStdIoDeadline){0});
    require(started.is_ok && started.task != NULL && output.handle == NULL,
            "start native-first close");
    r_runtime_darwin_io_testing_wait_for_native_completion();
    stop_thread = begin_executor_stop(&stop_context);
    r_runtime_darwin_io_testing_release_native_completion();
    finish_executor_stop(stop_thread, &stop_context);

    result = await_close(started);
    require(result.r_tag == UINT32_C(1) && result.r_payload.r_err.code == R_STD_IO_ERROR_OTHER &&
                result.r_payload.r_err.native_code == EIO,
            "earlier native close error replaced delayed task cancellation");
}

static void test_console_constructor_requires_lifecycle(void) {
    pid_t child = fork();
    int status;

    require(child >= 0, "fork console lifecycle contract probe");
    if (child == 0) {
        (void)r_std_io_stdout();
        _exit(EXIT_SUCCESS);
    }
    require(waitpid(child, &status, 0) == child, "wait for console lifecycle contract probe");
    require(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT,
            "console constructor outside hosted lifecycle aborted");
}

static void test_console_views_and_close_barrier(RRuntimeAllocator *allocator, int input_writer) {
    RStdIoInput first_input;
    RStdIoInput second_input;
    RStdIoOutput output;
    RStdIoOutput error;
    RStdIoTaskStartResult started_read;
    RStdIoTaskStartResult started_close;
    RStdIoTaskStartResult failed_close;
    RRuntimeArray buffer;
    RStdIoReadResult read_result;
    RStdIoVoidResult close_result;
    RStdTimeInstant expired = {0, 0U};
    RRuntimeDarwinIoHandle *unchanged_handle;
    uint64_t allocation_count;
    size_t index;
    unsigned char byte = 0x5aU;

    allocation_count = r_runtime_allocator_attempt_count(allocator);
    first_input = r_std_io_stdin();
    second_input = r_std_io_stdin();
    for (index = 0U; index != 32U; ++index) {
        output = r_std_io_stdout();
        error = r_std_io_stderr();
        r_std_io_output_destroy(&output);
        r_std_io_output_destroy(&error);
    }
    require(r_runtime_allocator_attempt_count(allocator) == allocation_count,
            "console constructors and view destruction allocated nothing");
    require(fcntl(STDOUT_FILENO, F_GETFD) >= 0 && fcntl(STDERR_FILENO, F_GETFD) >= 0,
            "repeated console view destruction preserved process descriptors");

    output = r_std_io_stdout();
    unchanged_handle = output.handle;
    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    failed_close = r_std_io_close_output(&output, (RStdIoDeadline){0});
    require(!failed_close.is_ok && failed_close.task == NULL &&
                failed_close.error == R_STD_ASYNC_START_ALLOCATION_FAILED &&
                output.handle == unchanged_handle,
            "native close reservation failure preserved named output view");
    r_runtime_allocator_set_failure(allocator, UINT64_C(2));
    failed_close = r_std_io_close_output(&output, (RStdIoDeadline){0});
    require(!failed_close.is_ok && failed_close.task == NULL &&
                failed_close.error == R_STD_ASYNC_START_ALLOCATION_FAILED &&
                output.handle == unchanged_handle,
            "task frame failure preserved named output view");
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    r_std_io_output_destroy(&output);

    buffer = byte_array(allocator, 8U, 0x2dU);
    started_read = r_std_io_read(&first_input, &buffer, (RStdIoDeadline){0});
    require(started_read.is_ok && buffer.data == NULL, "start console read before close barrier");
    close_result = await_close(r_std_io_close_input(&first_input, (RStdIoDeadline){0}));
    require(close_result.r_tag == UINT32_C(0) && first_input.handle == NULL,
            "console close consumed exactly its first view");
    read_result = await_read(started_read);
    require(read_result.kind == R_STD_IO_READ_RESULT_FAILED && read_result.count == 0U &&
                read_result.error.code == R_STD_IO_ERROR_CANCELLED,
            "console close cancelled and acknowledged earlier read");
    r_runtime_array_destroy(&read_result.buffer);

    require(write(input_writer, &byte, 1U) == 1, "seed later console read");
    buffer = byte_array(allocator, 1U, 0U);
    read_result = await_read(r_std_io_read(&second_input, &buffer, (RStdIoDeadline){0}));
    require(read_result.kind == R_STD_IO_READ_RESULT_READ && read_result.count == 1U &&
                ((unsigned char *)read_result.buffer.data)[0] == byte,
            "second console view completed a later read after close barrier");
    r_runtime_array_destroy(&read_result.buffer);
    r_std_io_input_destroy(&second_input);

    first_input = r_std_io_stdin();
    buffer = byte_array(allocator, 4U, 0x4cU);
    started_read = r_std_io_read(&first_input, &buffer, (RStdIoDeadline){0});
    require(started_read.is_ok && buffer.data == NULL,
            "start read before cancelled close observation");
    started_close = r_std_io_close_input(&first_input, (RStdIoDeadline){0});
    require(started_close.is_ok && first_input.handle == NULL,
            "cancelled close task consumed input at successful start");
    r_runtime_task_cancel(&started_close.task);
    require(started_close.task == NULL, "close task cancellation consumed observation");
    read_result = await_read(started_read);
    require(read_result.kind == R_STD_IO_READ_RESULT_FAILED &&
                read_result.error.code == R_STD_IO_ERROR_CANCELLED,
            "cancelled close task still completed native cleanup");
    r_runtime_array_destroy(&read_result.buffer);
    second_input = r_std_io_stdin();
    r_std_io_input_destroy(&second_input);

    error = r_std_io_stderr();
    close_result = await_close(r_std_io_close_output(&error, (RStdIoDeadline){1, expired}));
    require(close_result.r_tag == UINT32_C(1) &&
                close_result.r_payload.r_err.code == R_STD_IO_ERROR_TIMED_OUT &&
                close_result.r_payload.r_err.native_code == 0 && error.handle == NULL,
            "expired close deadline still released console view after cleanup");
    require(fcntl(STDERR_FILENO, F_GETFD) >= 0,
            "expired console close preserved runtime-owned process descriptor");
}

static void test_shared_write_range_and_owner(RRuntimeAllocator *allocator) {
    static const unsigned char payload[] = {
        0x78U, 0x78U, 0x73U, 0x68U, 0x61U, 0x72U, 0x65U, 0x64U, 0x79U, 0x79U};
    static const unsigned char expected[] = {0x73U, 0x68U, 0x61U, 0x72U, 0x65U, 0x64U};
    RRuntimeDarwinIoHandle *handle;
    RStdIoOutput output;
    RRuntimeArc owner;
    RRuntimeArcControl *identity;
    RStdIoSharedWriteResult result;
    const RRuntimeArray *returned_array;
    unsigned char observed[sizeof(expected)];
    int descriptors[2];

    require(pipe(descriptors) == 0, "create shared-write pipe");
    handle = stream_handle(allocator, descriptors[1]);
    require(close(descriptors[1]) == 0, "close borrowed shared-write descriptor");
    output.handle = handle;
    owner = shared_byte_array(allocator, payload, sizeof(payload));
    identity = owner.control;
    result = await_shared_write(
        r_std_io_write_shared(&output, &owner, 2U, sizeof(expected), (RStdIoDeadline){0}));
    require(owner.control == NULL, "shared write consumed named owner");
    require(result.kind == R_STD_IO_SHARED_WRITE_RESULT_WRITTEN &&
                result.written == sizeof(expected) && result.buffer.control == identity,
            "shared write returned identical owner");
    returned_array = r_runtime_arc_get(&result.buffer);
    require(returned_array != NULL && returned_array->length == sizeof(payload) &&
                memcmp(returned_array->data, payload, sizeof(payload)) == 0,
            "shared write preserved immutable array bytes");
    read_exact(descriptors[0], observed, sizeof(observed));
    require(memcmp(observed, expected, sizeof(expected)) == 0,
            "shared write emitted selected range");
    r_runtime_arc_release(&result.buffer);
    require(close(descriptors[0]) == 0, "close shared-write pipe reader");
    close_handle(handle);
}

static void test_preexisting_close_failure_precedes_expired_deadline(RRuntimeAllocator *allocator) {
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoSubmitResult native_close;
    RRuntimeDarwinIoResult native_result;
    RStdIoOutput output;
    RStdIoVoidResult result;
    int descriptor;

    descriptor = open("/dev/null", O_WRONLY | O_CLOEXEC);
    require(descriptor >= 0, "open close-failure fixture");
    handle = stream_handle(allocator, descriptor);
    require(close(descriptor) == 0, "close borrowed close-failure descriptor");
    output.handle = handle;
    r_runtime_darwin_io_testing_force_root_cleanup_error(EIO);
    native_close = r_runtime_darwin_io_submit_close(handle);
    require(native_close.status == R_RUNTIME_DARWIN_IO_START_OK,
            "submit injected native close failure");
    native_result = r_runtime_darwin_io_request_wait(native_close.request);
    require(native_result.native_error == EIO && native_result.cleanup_acknowledged,
            "cache injected terminal close failure");
    r_runtime_darwin_io_request_release(native_close.request);

    result =
        await_close(r_std_io_close_output(&output, (RStdIoDeadline){1, (RStdTimeInstant){0, 0U}}));
    require(result.r_tag == UINT32_C(1) && result.r_payload.r_err.code == R_STD_IO_ERROR_OTHER &&
                result.r_payload.r_err.native_code == EIO && output.handle == NULL,
            "preexisting close failure preceded already-expired deadline");
}

static void test_shared_write_range_failure(RRuntimeAllocator *allocator) {
    static const unsigned char payload[] = {0x72U, 0x61U, 0x6eU, 0x67U, 0x65U};
    RRuntimeDarwinIoHandle *handle;
    RStdIoOutput output;
    RRuntimeArc owner;
    RRuntimeArcControl *identity;
    RStdIoSharedWriteResult result;
    unsigned char observed;
    ssize_t count;
    int descriptors[2];
    int flags;

    require(pipe(descriptors) == 0, "create shared-range pipe");
    flags = fcntl(descriptors[0], F_GETFL);
    require(flags >= 0 && fcntl(descriptors[0], F_SETFL, flags | O_NONBLOCK) == 0,
            "make shared-range reader nonblocking");
    handle = stream_handle(allocator, descriptors[1]);
    require(close(descriptors[1]) == 0, "close borrowed shared-range descriptor");
    output.handle = handle;
    owner = shared_byte_array(allocator, payload, sizeof(payload));
    identity = owner.control;
    result =
        await_shared_write(r_std_io_write_shared(&output, &owner, 4U, 2U, (RStdIoDeadline){0}));
    require(owner.control == NULL, "invalid shared range consumed owner after task start");
    require(result.kind == R_STD_IO_SHARED_WRITE_RESULT_FAILED && result.written == 0U &&
                result.error.code == R_STD_IO_ERROR_INVALID_OPERATION &&
                result.error.native_code == 0 && result.buffer.control == identity,
            "invalid shared range returned exact recoverable outcome");
    count = read(descriptors[0], &observed, 1U);
    require(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK),
            "invalid shared range performed no native write");
    r_runtime_arc_release(&result.buffer);
    require(close(descriptors[0]) == 0, "close shared-range pipe reader");
    close_handle(handle);
}

static void test_write_all_and_flush(RRuntimeAllocator *allocator) {
    static const unsigned char payload[] = {
        0x70U, 0x72U, 0x65U, 0x63U, 0x6fU, 0x6dU, 0x6dU, 0x69U, 0x74U};
    RRuntimeDarwinIoHandle *handle;
    RStdIoOutput output;
    RRuntimeArray buffer;
    RStdIoWriteAllResult write_result;
    RStdIoWriteAllResult moved_write_result = {0};
    RStdIoVoidResult flush_result;
    unsigned char *identity;
    unsigned char observed[sizeof(payload)];
    int descriptors[2];

    require(pipe(descriptors) == 0, "create write pipe");
    handle = stream_handle(allocator, descriptors[1]);
    require(close(descriptors[1]) == 0, "close borrowed write descriptor");
    output.handle = handle;
    buffer = byte_array(allocator, sizeof(payload), 0U);
    (void)memcpy(buffer.data, payload, sizeof(payload));
    identity = buffer.data;
    write_result = await_write_all(r_std_io_write_all(&output, &buffer, (RStdIoDeadline){0}));
    require(buffer.data == NULL, "write_all start consumed named buffer");
    require(write_result.kind == R_STD_IO_WRITE_ALL_RESULT_WRITTEN &&
                write_result.written == sizeof(payload) && write_result.buffer.data == identity,
            "write_all returned identical owner");
    require(memcmp(write_result.buffer.data, payload, sizeof(payload)) == 0,
            "write_all kept source bytes immutable");
    r_std_io_write_all_result_move_initialize(&moved_write_result, &write_result);
    require(write_result.buffer.data == NULL && moved_write_result.buffer.data == identity &&
                moved_write_result.written == sizeof(payload),
            "write_all result move transferred the returned owner");
    flush_result = await_flush(r_std_io_flush(&output, (RStdIoDeadline){0}));
    require(flush_result.r_tag == UINT32_C(0), "flush result");
    read_exact(descriptors[0], observed, sizeof(observed));
    require(memcmp(observed, payload, sizeof(payload)) == 0, "write_all pipe payload");
    r_std_io_write_all_result_destroy(&moved_write_result);
    require(close(descriptors[0]) == 0, "close write pipe reader");
    close_handle(handle);
}

static void test_read_prefix_and_end(RRuntimeAllocator *allocator) {
    static const unsigned char payload[] = {0x72U, 0x65U, 0x61U, 0x64U};
    RRuntimeDarwinIoHandle *handle;
    RStdIoInput input;
    RRuntimeArray buffer;
    RStdIoReadResult result;
    RStdIoReadResult moved_result;
    unsigned char *identity;
    int descriptors[2];

    require(pipe(descriptors) == 0, "create read pipe");
    handle = stream_handle(allocator, descriptors[0]);
    require(close(descriptors[0]) == 0, "close borrowed read descriptor");
    input.handle = handle;
    require(write(descriptors[1], payload, sizeof(payload)) == (ssize_t)sizeof(payload),
            "seed read pipe");
    buffer = byte_array(allocator, 16U, 0xa5U);
    identity = buffer.data;
    result = await_read(r_std_io_read(&input, &buffer, (RStdIoDeadline){0}));
    require(result.kind == R_STD_IO_READ_RESULT_READ && result.count == sizeof(payload) &&
                result.buffer.data == identity,
            "read returned positive prefix and identical owner");
    r_std_io_read_result_move_initialize(&moved_result, &result);
    require(result.buffer.allocator == NULL && result.buffer.data == NULL &&
                result.buffer.length == 0U && result.buffer.capacity == 0U &&
                moved_result.kind == R_STD_IO_READ_RESULT_READ &&
                moved_result.count == sizeof(payload) && moved_result.buffer.data == identity,
            "read result move transferred the returned owner");
    require(memcmp(moved_result.buffer.data, payload, sizeof(payload)) == 0,
            "read replaced exact prefix");
    require(((unsigned char *)moved_result.buffer.data)[sizeof(payload)] == 0xa5U &&
                ((unsigned char *)moved_result.buffer.data)[15] == 0xa5U,
            "read preserved suffix");
    r_std_io_read_result_destroy(&result);
    r_std_io_read_result_destroy(&moved_result);
    require(close(descriptors[1]) == 0, "close read pipe writer");

    buffer = byte_array(allocator, 8U, 0x5cU);
    identity = buffer.data;
    result = await_read(r_std_io_read(&input, &buffer, (RStdIoDeadline){0}));
    require(result.kind == R_STD_IO_READ_RESULT_END && result.count == 0U &&
                result.buffer.data == identity,
            "read distinguished terminal end");
    require(((unsigned char *)result.buffer.data)[0] == 0x5cU &&
                ((unsigned char *)result.buffer.data)[7] == 0x5cU,
            "end preserved every byte");
    r_std_io_read_result_destroy(&result);
    close_handle(handle);
}

static void test_zero_length_precedes_expired_deadline(RRuntimeAllocator *allocator) {
    static const unsigned char shared_payload[] = {0x7aU, 0x65U, 0x72U, 0x6fU};
    RRuntimeDarwinIoHandle *handle;
    RStdIoOutput output;
    RRuntimeArray buffer;
    RRuntimeArc shared_buffer;
    RStdIoWriteResult result;
    RStdIoWriteResult moved_result;
    RStdIoSharedWriteResult shared_result;
    RStdIoSharedWriteResult moved_shared_result = {0};
    RRuntimeDarwinIoPrepareResult flush_preparation;
    int descriptors[2];

    require(pipe(descriptors) == 0, "create zero write pipe");
    handle = stream_handle(allocator, descriptors[1]);
    require(close(descriptors[1]) == 0, "close borrowed zero write descriptor");
    require(close(descriptors[0]) == 0, "close zero write peer");
    output.handle = handle;
    flush_preparation = r_runtime_darwin_io_prepare_flush(handle, UINT64_C(0));
    require(flush_preparation.status == R_RUNTIME_DARWIN_IO_START_OK &&
                flush_preparation.prepared != NULL,
            "reserve zero-write ordering barrier");
    buffer = byte_array(allocator, 0U, 0U);
    result = await_write(
        r_std_io_write(&output, &buffer, (RStdIoDeadline){1, (RStdTimeInstant){0, 0U}}));
    require(result.kind == R_STD_IO_WRITE_RESULT_WRITTEN && result.count == 0U,
            "zero write won over ordering barrier and expired deadline");
    r_std_io_write_result_move_initialize(&moved_result, &result);
    require(result.buffer.allocator == NULL && result.buffer.data == NULL &&
                result.buffer.length == 0U && result.buffer.capacity == 0U &&
                moved_result.kind == R_STD_IO_WRITE_RESULT_WRITTEN && moved_result.count == 0U,
            "write result move transferred the returned owner");
    r_std_io_write_result_destroy(&result);
    r_std_io_write_result_destroy(&moved_result);
    shared_buffer = shared_byte_array(allocator, shared_payload, sizeof(shared_payload));
    shared_result = await_shared_write(r_std_io_write_shared(
        &output, &shared_buffer, 2U, 0U, (RStdIoDeadline){1, (RStdTimeInstant){0, 0U}}));
    require(shared_result.kind == R_STD_IO_SHARED_WRITE_RESULT_WRITTEN &&
                shared_result.written == 0U && shared_buffer.control == NULL,
            "zero shared write preceded expired deadline and native ordering");
    r_std_io_shared_write_result_move_initialize(&moved_shared_result, &shared_result);
    require(shared_result.buffer.control == NULL && moved_shared_result.buffer.control != NULL,
            "shared write result move transferred the returned strong owner");
    r_std_io_shared_write_result_destroy(&shared_result);
    r_std_io_shared_write_result_destroy(&moved_shared_result);
    r_runtime_darwin_io_prepared_abort(&flush_preparation.prepared);
    close_handle(handle);
}

static void test_deadline_uses_monotonic_instant(RRuntimeAllocator *allocator) {
    RRuntimeDarwinIoHandle *handle;
    RStdIoInput input;
    RRuntimeArray buffer;
    RStdTimeInstantResult now;
    RStdTimeInstantResult deadline;
    RStdIoReadResult result;
    int descriptors[2];

    require(pipe(descriptors) == 0, "create deadline pipe");
    handle = stream_handle(allocator, descriptors[0]);
    require(close(descriptors[0]) == 0, "close borrowed deadline descriptor");
    input.handle = handle;
    buffer = byte_array(allocator, 32U, 0x39U);
    now = r_std_time_monotonic_now();
    require(now.is_ok, "read continuous instant");
    deadline = r_std_time_instant_add(now.value, (RStdTimeDuration){0, UINT32_C(5000000)});
    require(deadline.is_ok, "build continuous deadline");
    result = await_read(r_std_io_read(&input, &buffer, (RStdIoDeadline){1, deadline.value}));
    require(result.kind == R_STD_IO_READ_RESULT_FAILED && result.count == 0U &&
                result.error.code == R_STD_IO_ERROR_TIMED_OUT && result.error.native_code == 0,
            "continuous deadline selected timed_out");
    require(((unsigned char *)result.buffer.data)[0] == 0x39U &&
                ((unsigned char *)result.buffer.data)[31] == 0x39U,
            "deadline returned unchanged bytes");
    r_runtime_array_destroy(&result.buffer);
    require(close(descriptors[1]) == 0, "close deadline pipe writer");
    close_handle(handle);
}

static void test_deadline_returns_partial_write(RRuntimeAllocator *allocator) {
    const size_t buffer_size = 4U * 1024U * 1024U;
    RRuntimeDarwinIoHandle *handle;
    RStdIoOutput output;
    RRuntimeArray buffer;
    RStdTimeInstantResult now;
    RStdTimeInstantResult deadline;
    RStdIoWriteAllResult result;
    unsigned char *identity;
    int descriptors[2];

    require(pipe(descriptors) == 0, "create partial deadline pipe");
    handle = stream_handle(allocator, descriptors[1]);
    require(close(descriptors[1]) == 0, "close borrowed partial deadline descriptor");
    output.handle = handle;
    buffer = byte_array(allocator, buffer_size, 0x6dU);
    identity = buffer.data;
    now = r_std_time_monotonic_now();
    require(now.is_ok, "read partial-write start instant");
    deadline = r_std_time_instant_add(now.value, (RStdTimeDuration){0, UINT32_C(100000000)});
    require(deadline.is_ok, "build partial-write deadline");
    result =
        await_write_all(r_std_io_write_all(&output, &buffer, (RStdIoDeadline){1, deadline.value}));
    require(result.kind == R_STD_IO_WRITE_ALL_RESULT_FAILED &&
                result.error.code == R_STD_IO_ERROR_TIMED_OUT && result.written > 0U &&
                result.written < buffer_size && result.buffer.data == identity,
            "deadline preserved positive partial write progress and owner");
    require(((unsigned char *)result.buffer.data)[0] == 0x6dU &&
                ((unsigned char *)result.buffer.data)[buffer_size - 1U] == 0x6dU,
            "partial write kept source bytes immutable");
    r_runtime_array_destroy(&result.buffer);
    require(close(descriptors[0]) == 0, "close partial deadline reader");
    close_handle(handle);
}

static void test_task_frame_failure_aborts_native_reservation(RRuntimeAllocator *allocator) {
    static const unsigned char payload[] = {0x66U, 0x61U, 0x69U, 0x6cU};
    RRuntimeDarwinIoHandle *handle;
    RStdIoOutput output;
    RRuntimeArray buffer;
    RRuntimeArray original;
    RRuntimeArc shared_buffer;
    RRuntimeArcControl *shared_identity;
    RStdIoTaskStartResult started;
    RStdIoWriteAllResult write_result;
    unsigned char original_bytes[sizeof(payload)];
    unsigned char observed[sizeof(payload)];
    ssize_t count;
    int descriptors[2];
    int flags;

    require(pipe(descriptors) == 0, "create frame-failure pipe");
    flags = fcntl(descriptors[0], F_GETFL);
    require(flags >= 0 && fcntl(descriptors[0], F_SETFL, flags | O_NONBLOCK) == 0,
            "make frame-failure reader nonblocking");
    handle = stream_handle(allocator, descriptors[1]);
    require(close(descriptors[1]) == 0, "close borrowed frame-failure descriptor");
    output.handle = handle;
    buffer = byte_array(allocator, sizeof(payload), 0U);
    (void)memcpy(buffer.data, payload, sizeof(payload));
    original = buffer;
    (void)memcpy(original_bytes, buffer.data, sizeof(original_bytes));

    r_runtime_allocator_set_failure(allocator, UINT64_C(2));
    started = r_std_io_write_all(&output, &buffer, (RStdIoDeadline){0});
    require(!started.is_ok && started.task == NULL &&
                started.error == R_STD_ASYNC_START_ALLOCATION_FAILED,
            "task-frame failure classification");
    require(memcmp(&buffer, &original, sizeof(buffer)) == 0 &&
                memcmp(buffer.data, original_bytes, sizeof(original_bytes)) == 0,
            "task-frame failure preserved named buffer byte-for-byte");
    count = read(descriptors[0], observed, sizeof(observed));
    require(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK),
            "aborted reservation caused no write side effect");

    shared_buffer = shared_byte_array(allocator, payload, sizeof(payload));
    shared_identity = shared_buffer.control;
    r_runtime_allocator_set_failure(allocator, UINT64_C(2));
    started =
        r_std_io_write_shared(&output, &shared_buffer, 0U, sizeof(payload), (RStdIoDeadline){0});
    require(!started.is_ok && started.task == NULL &&
                started.error == R_STD_ASYNC_START_ALLOCATION_FAILED,
            "shared-write task-frame failure classification");
    require(shared_buffer.control == shared_identity &&
                memcmp(((const RRuntimeArray *)r_runtime_arc_get(&shared_buffer))->data,
                       payload,
                       sizeof(payload)) == 0,
            "shared-write task-frame failure preserved owner and bytes");
    count = read(descriptors[0], observed, sizeof(observed));
    require(count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK),
            "aborted shared reservation caused no write side effect");
    r_runtime_arc_release(&shared_buffer);

    r_runtime_allocator_set_failure(allocator, UINT64_C(2));
    started = r_std_io_flush(&output, (RStdIoDeadline){0});
    require(!started.is_ok && started.task == NULL &&
                started.error == R_STD_ASYNC_START_ALLOCATION_FAILED,
            "flush task-frame failure classification");
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    write_result = await_write_all(r_std_io_write_all(&output, &buffer, (RStdIoDeadline){0}));
    require(write_result.kind == R_STD_IO_WRITE_ALL_RESULT_WRITTEN &&
                write_result.written == sizeof(payload) &&
                write_result.buffer.data == original.data,
            "aborted flush reservation left no ordering artifact");
    read_exact(descriptors[0], observed, sizeof(observed));
    require(memcmp(observed, payload, sizeof(payload)) == 0,
            "write progressed after aborted flush reservation");
    r_runtime_array_destroy(&write_result.buffer);
    require(close(descriptors[0]) == 0, "close frame-failure reader");
    close_handle(handle);
}

static void test_handle_drop_and_task_cancel_drain(RRuntimeAllocator *allocator) {
    static const unsigned char payload[] = {0x72U, 0x65U, 0x74U, 0x61U, 0x69U, 0x6eU};
    RRuntimeDarwinIoHandle *retained_handle;
    RRuntimeDarwinIoHandle *cancelled_handle;
    RStdIoInput input;
    RRuntimeArray buffer;
    RStdIoTaskStartResult started;
    RStdIoReadResult result;
    int retained_pipe[2];
    int cancelled_pipe[2];

    require(pipe(retained_pipe) == 0, "create retained-handle pipe");
    retained_handle = stream_handle(allocator, retained_pipe[0]);
    require(close(retained_pipe[0]) == 0, "close borrowed retained descriptor");
    input.handle = retained_handle;
    buffer = byte_array(allocator, 32U, 0U);
    started = r_std_io_read(&input, &buffer, (RStdIoDeadline){0});
    require(started.is_ok, "start retained-handle read");
    r_runtime_darwin_io_handle_release(retained_handle);
    require(write(retained_pipe[1], payload, sizeof(payload)) == (ssize_t)sizeof(payload),
            "write after source handle drop");
    require(close(retained_pipe[1]) == 0, "close retained-handle peer");
    result = await_read(started);
    require(result.kind == R_STD_IO_READ_RESULT_READ && result.count == sizeof(payload),
            "native request retain survived source handle drop");
    r_runtime_array_destroy(&result.buffer);

    require(pipe(cancelled_pipe) == 0, "create cancellation pipe");
    cancelled_handle = stream_handle(allocator, cancelled_pipe[0]);
    require(close(cancelled_pipe[0]) == 0, "close borrowed cancellation descriptor");
    input.handle = cancelled_handle;
    buffer = byte_array(allocator, 64U, 0x17U);
    started = r_std_io_read(&input, &buffer, (RStdIoDeadline){0});
    require(started.is_ok && buffer.data == NULL, "start cancellable read");
    r_runtime_task_cancel(&started.task);
    require(started.task == NULL, "cancel consumed task observation");
    r_runtime_darwin_io_handle_release(cancelled_handle);
    require(close(cancelled_pipe[1]) == 0, "close cancellation peer");
}

int main(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinIoConsoleStartResult console_start;
    int input_descriptors[2];
    int saved_stdin;

    r_runtime_allocator_initialize(&allocator);
    test_as_error();
    test_console_constructor_requires_lifecycle();
    test_native_stream_result_precedes_task_cancellation(&allocator);
    test_native_close_result_precedes_task_cancellation(&allocator);
    require(pipe(input_descriptors) == 0, "create controlled process input");
    saved_stdin = dup(STDIN_FILENO);
    require(saved_stdin >= 0 && dup2(input_descriptors[0], STDIN_FILENO) == STDIN_FILENO,
            "install controlled process input");
    require(close(input_descriptors[0]) == 0, "close copied process input descriptor");
    require(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK,
            "executor start");
    console_start = r_runtime_darwin_io_process_console_start(&allocator);
    require(console_start.status == R_RUNTIME_DARWIN_IO_START_OK, "process console root start");
    test_console_views_and_close_barrier(&allocator, input_descriptors[1]);
    test_preexisting_close_failure_precedes_expired_deadline(&allocator);
    test_write_all_and_flush(&allocator);
    test_shared_write_range_and_owner(&allocator);
    test_shared_write_range_failure(&allocator);
    test_read_prefix_and_end(&allocator);
    test_zero_length_precedes_expired_deadline(&allocator);
    test_deadline_uses_monotonic_instant(&allocator);
    test_deadline_returns_partial_write(&allocator);
    test_task_frame_failure_aborts_native_reservation(&allocator);
    test_handle_drop_and_task_cancel_drain(&allocator);
    require(r_runtime_executor_lifecycle_stop(), "executor acknowledged IO cancellation drain");
    r_runtime_darwin_io_process_console_stop();
    require(close(input_descriptors[1]) == 0, "close controlled process input writer");
    require(dup2(saved_stdin, STDIN_FILENO) == STDIN_FILENO && close(saved_stdin) == 0,
            "restore process input");
    (void)fprintf(stdout, "library_io_tests: ok\n");
    return EXIT_SUCCESS;
}
