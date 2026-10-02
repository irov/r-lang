#include "r_std_io.h"

#include "r_runtime_allocator.h"
#include "r_runtime_darwin_io.h"
#include "r_runtime_task.h"
#include "r_std_time.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void fail(const char *message) {
    (void)fprintf(stderr, "library io scoped test failed: %s\n", message);
    exit(EXIT_FAILURE);
}

static void require(_Bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
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

static RStdIoCountResult await_count(RStdIoTaskStartResult started) {
    RStdIoCountResult result = {0};

    require(started.is_ok && started.task != NULL, "count task start");
    require(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
            "await count task");
    return result;
}

static RStdIoVoidResult await_void(RStdIoTaskStartResult started) {
    RStdIoVoidResult result = {0};

    require(started.is_ok && started.task != NULL, "void task start");
    require(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
            "await void task");
    return result;
}

static _Bool all_bytes_equal(const uint8_t *bytes, size_t length, uint8_t expected) {
    size_t index;

    for (index = 0U; index != length; ++index) {
        if (bytes[index] != expected) {
            return 0;
        }
    }
    return 1;
}

static void test_write_from_pipe(RRuntimeAllocator *allocator) {
    static const uint8_t payload[] = {0x73U, 0x63U, 0x6fU, 0x70U, 0x65U, 0x64U};
    const RStdIoDeadline expired = {1, {0, 0U}};
    RRuntimeDarwinIoHandle *handle;
    RStdIoOutput output;
    RStdIoVoidResult void_result;
    RStdIoCountResult count_result;
    unsigned char observed[sizeof(payload)];
    int descriptors[2];

    require(pipe(descriptors) == 0, "create write pipe");
    handle = stream_handle(allocator, descriptors[1]);
    require(close(descriptors[1]) == 0, "close borrowed write descriptor");
    output.handle = handle;

    void_result = await_void(r_std_io_write_all_from(
        &output, (RStdIoConstBytes){payload, sizeof(payload)}, (RStdIoDeadline){0}));
    require(void_result.r_tag == UINT32_C(0), "write_all_from succeeded");
    read_exact(descriptors[0], observed, sizeof(observed));
    require(memcmp(observed, payload, sizeof(payload)) == 0, "write_all_from pipe payload");

    count_result = await_count(
        r_std_io_write_from(&output, (RStdIoConstBytes){payload, 2U}, (RStdIoDeadline){0}));
    require(count_result.r_tag == UINT32_C(0) && count_result.r_payload.r_value == 2U,
            "write_from reported positive prefix");
    read_exact(descriptors[0], observed, 2U);
    require(memcmp(observed, payload, 2U) == 0, "write_from pipe payload");

    void_result =
        await_void(r_std_io_write_all_from(&output, (RStdIoConstBytes){payload, 0U}, expired));
    require(void_result.r_tag == UINT32_C(0), "empty write_all_from precedes expired deadline");
    count_result =
        await_count(r_std_io_write_from(&output, (RStdIoConstBytes){payload, 0U}, expired));
    require(count_result.r_tag == UINT32_C(0) && count_result.r_payload.r_value == 0U,
            "empty write_from precedes expired deadline");
    void_result = await_void(
        r_std_io_write_all_from(&output, (RStdIoConstBytes){payload, sizeof(payload)}, expired));
    require(void_result.r_tag == UINT32_C(1) &&
                void_result.r_payload.r_err.code == R_STD_IO_ERROR_TIMED_OUT,
            "expired deadline fails write_all_from before submission");
    require(close(descriptors[0]) == 0, "close write pipe reader");
    close_handle(handle);
}

static void test_read_into_pipe(RRuntimeAllocator *allocator) {
    static const uint8_t payload[] = {0x72U, 0x65U, 0x61U, 0x64U};
    const RStdIoDeadline expired = {1, {0, 0U}};
    RRuntimeDarwinIoHandle *handle;
    RStdIoInput input;
    RStdIoCountResult result;
    uint8_t target[16];
    int descriptors[2];

    require(pipe(descriptors) == 0, "create read pipe");
    handle = stream_handle(allocator, descriptors[0]);
    require(close(descriptors[0]) == 0, "close borrowed read descriptor");
    input.handle = handle;
    require(write(descriptors[1], payload, sizeof(payload)) == (ssize_t)sizeof(payload),
            "seed read pipe");
    (void)memset(target, 0xa5, sizeof(target));
    result = await_count(r_std_io_read_into(
        &input, (RStdIoMutableBytes){target, sizeof(target)}, (RStdIoDeadline){0}));
    require(result.r_tag == UINT32_C(0) && result.r_payload.r_value == sizeof(payload),
            "read_into returned positive prefix");
    require(memcmp(target, payload, sizeof(payload)) == 0, "read_into placed exact prefix");
    require(all_bytes_equal(target + sizeof(payload), sizeof(target) - sizeof(payload), 0xa5U),
            "read_into preserved suffix");

    result = await_count(r_std_io_read_into(&input, (RStdIoMutableBytes){target, 0U}, expired));
    require(result.r_tag == UINT32_C(0) && result.r_payload.r_value == 0U,
            "empty read_into precedes expired deadline");
    (void)memset(target, 0x5cU, sizeof(target));
    result = await_count(
        r_std_io_read_into(&input, (RStdIoMutableBytes){target, sizeof(target)}, expired));
    require(result.r_tag == UINT32_C(1) &&
                result.r_payload.r_error_00000001.code == R_STD_IO_ERROR_TIMED_OUT &&
                all_bytes_equal(target, sizeof(target), 0x5cU),
            "expired deadline fails read_into without touching target");

    require(close(descriptors[1]) == 0, "close read pipe writer");
    result = await_count(r_std_io_read_into(
        &input, (RStdIoMutableBytes){target, sizeof(target)}, (RStdIoDeadline){0}));
    require(result.r_tag == UINT32_C(0) && result.r_payload.r_value == 0U,
            "read_into reports end of stream as zero");
    require(all_bytes_equal(target, sizeof(target), 0x5cU), "end preserved every byte");
    close_handle(handle);
}

int main(void) {
    RRuntimeAllocator allocator;

    r_runtime_allocator_initialize(&allocator);
    require(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK,
            "executor start");
    test_write_from_pipe(&allocator);
    test_read_into_pipe(&allocator);
    require(r_runtime_executor_lifecycle_stop(), "executor stop");
    (void)fprintf(stdout, "library_io_scoped_tests: ok\n");
    return EXIT_SUCCESS;
}
