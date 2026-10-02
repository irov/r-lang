#include "r_runtime_0_1.h"

#if defined(__APPLE__)
#include "r_runtime_allocator.h"
#include "r_runtime_darwin_io.h"
#endif

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if !defined(R_RUNTIME_TESTING)
#error R_RUNTIME_TESTING is required
#endif

static int r_runtime_test_failure(const char *message) {
    (void)fprintf(stderr, "runtime test failure: %s\n", message);
    return EXIT_FAILURE;
}

static int r_runtime_test_valid(void) {
    static char program[] = "r-test";
    static char utf8_argument[] = "caf\xc3\xa9";
    char *arguments[] = {program, utf8_argument};
    RRuntimeArgumentSnapshotView snapshot = {0};
    RRuntimeStartResult result;

    if (r_runtime_hosted_allocator() != NULL) {
        return r_runtime_test_failure("hosted allocator was visible before startup");
    }
    result = r_runtime_hosted_start(2, arguments);

    if (!result.started || (result.process_status != 0)) {
        return r_runtime_test_failure("valid UTF-8 startup failed");
    }
    if ((r_runtime_hosted_allocator() == NULL) || !r_runtime_hosted_argument_snapshot(&snapshot) ||
        (snapshot.count != 2U) ||
        (snapshot.byte_length != (sizeof(program) + sizeof(utf8_argument))) ||
        (snapshot.arguments == NULL) || (snapshot.arguments[0].data != snapshot.data) ||
        (snapshot.arguments[0].length != (sizeof(program) - 1U)) ||
        (snapshot.arguments[1].data != (snapshot.data + sizeof(program))) ||
        (snapshot.arguments[1].length != (sizeof(utf8_argument) - 1U)) ||
        (memcmp(snapshot.data, program, sizeof(program)) != 0) ||
        (memcmp(snapshot.data + sizeof(program), utf8_argument, sizeof(utf8_argument)) != 0)) {
        return r_runtime_test_failure("hosted argument snapshot changed");
    }
    if (r_runtime_hosted_finish(INT32_C(37)) != 37) {
        return r_runtime_test_failure("finish did not preserve status");
    }
    if (r_runtime_hosted_argument_snapshot(&snapshot) || (r_runtime_hosted_allocator() != NULL)) {
        return r_runtime_test_failure("finished argument snapshot remained observable");
    }
    result = r_runtime_hosted_start(0, NULL);
    if (!result.started || (result.process_status != 0)) {
        return r_runtime_test_failure("synthesized argv[0] startup failed");
    }
    if ((r_runtime_hosted_allocator() == NULL) || !r_runtime_hosted_argument_snapshot(&snapshot) ||
        (snapshot.count != 1U) || (snapshot.byte_length != sizeof("<program>")) ||
        (snapshot.arguments == NULL) || (snapshot.arguments[0].data != snapshot.data) ||
        (snapshot.arguments[0].length != (sizeof("<program>") - 1U)) ||
        (memcmp(snapshot.data, "<program>", sizeof("<program>")) != 0)) {
        return r_runtime_test_failure("synthesized argument snapshot changed");
    }
    if (r_runtime_hosted_finish(-INT32_C(7)) != -7) {
        return r_runtime_test_failure("second lifecycle did not reset runtime");
    }
    if (r_runtime_hosted_allocator() != NULL) {
        return r_runtime_test_failure("hosted allocator remained visible after finish");
    }
    (void)puts("runtime_valid_lifecycle");
    return EXIT_SUCCESS;
}

static int r_runtime_test_invalid_utf8(void) {
    static char program[] = "r-test";
    static unsigned char invalid[] = {UINT8_C(0xc3), UINT8_C(0x28), UINT8_C(0)};
    char *arguments[] = {program, (char *)invalid};
    RRuntimeArgumentSnapshotView snapshot = {0};
    const RRuntimeStartResult result = r_runtime_hosted_start(2, arguments);

    if (result.started || (result.process_status != R_RUNTIME_ARGUMENT_ENCODING_STATUS)) {
        return r_runtime_test_failure("invalid UTF-8 classification changed");
    }
    if (r_runtime_hosted_argument_snapshot(&snapshot)) {
        return r_runtime_test_failure("invalid UTF-8 published a startup snapshot");
    }
    (void)puts("runtime_invalid_utf8");
    return EXIT_SUCCESS;
}

static int r_runtime_test_snapshot_oom(void) {
    static char program[] = "r-test";
    char *arguments[] = {program};
    RRuntimeArgumentSnapshotView snapshot = {0};
    RRuntimeStartResult result;

    r_runtime_testing_set_snapshot_allocation_failure(1);
    result = r_runtime_hosted_start(1, arguments);
    r_runtime_testing_set_snapshot_allocation_failure(0);
    if (result.started || (result.process_status != R_RUNTIME_ALLOCATION_FAILURE_STATUS)) {
        return r_runtime_test_failure("snapshot OOM classification changed");
    }
    if (r_runtime_hosted_argument_snapshot(&snapshot)) {
        return r_runtime_test_failure("snapshot OOM published a startup snapshot");
    }
    (void)puts("runtime_snapshot_oom");
    return EXIT_SUCCESS;
}

static int r_runtime_test_executor_oom(void) {
    static char program[] = "r-test";
    char *arguments[] = {program};
    RRuntimeArgumentSnapshotView snapshot = {0};
    RRuntimeStartResult result;

    r_runtime_testing_set_executor_allocation_failure(1);
    result = r_runtime_hosted_start(1, arguments);
    r_runtime_testing_set_executor_allocation_failure(0);
    if (result.started || (result.process_status != R_RUNTIME_ASYNC_ROOT_START_FAILURE_STATUS)) {
        return r_runtime_test_failure("executor OOM classification changed");
    }
    if (r_runtime_hosted_argument_snapshot(&snapshot)) {
        return r_runtime_test_failure("failed executor start published arguments");
    }
    result = r_runtime_hosted_start(1, arguments);
    if (!result.started || (result.process_status != 0)) {
        return r_runtime_test_failure("executor OOM rollback prevented restart");
    }
    if (r_runtime_hosted_finish(0) != 0) {
        return r_runtime_test_failure("executor OOM rollback restart did not finish");
    }
    (void)puts("runtime_executor_oom");
    return EXIT_SUCCESS;
}

static int r_runtime_test_async_root_start_failure(void) {
    static char program[] = "r-test";
    char *arguments[] = {program};
    RRuntimeArgumentSnapshotView snapshot = {0};
    RRuntimeStartResult result = r_runtime_hosted_start(1, arguments);

    if (!result.started || (result.process_status != 0)) {
        return r_runtime_test_failure("async root failure setup did not start");
    }
    if (r_runtime_hosted_async_root_start_failure() != R_RUNTIME_ASYNC_ROOT_START_FAILURE_STATUS) {
        return r_runtime_test_failure("async root failure status changed");
    }
    if (r_runtime_hosted_argument_snapshot(&snapshot)) {
        return r_runtime_test_failure("async root failure retained hosted arguments");
    }
    result = r_runtime_hosted_start(1, arguments);
    if (!result.started || (result.process_status != 0)) {
        return r_runtime_test_failure("async root failure teardown prevented restart");
    }
    if (r_runtime_hosted_finish(INT32_C(0)) != 0) {
        return r_runtime_test_failure("async root failure restart did not finish");
    }
    (void)puts("runtime_async_root_start_failure");
    return EXIT_SUCCESS;
}

static int r_runtime_test_invalid_precedes_oom(void) {
    static char program[] = "r-test";
    static unsigned char invalid[] = {
        UINT8_C(0xf4), UINT8_C(0x90), UINT8_C(0x80), UINT8_C(0x80), UINT8_C(0)};
    char *arguments[] = {program, (char *)invalid};
    RRuntimeStartResult result;

    r_runtime_testing_set_snapshot_allocation_failure(1);
    result = r_runtime_hosted_start(2, arguments);
    r_runtime_testing_set_snapshot_allocation_failure(0);
    if (result.started || (result.process_status != R_RUNTIME_ARGUMENT_ENCODING_STATUS)) {
        return r_runtime_test_failure("invalid UTF-8 did not take priority over snapshot OOM");
    }
    (void)puts("runtime_invalid_precedes_oom");
    return EXIT_SUCCESS;
}

#if defined(__APPLE__)
static int r_runtime_test_console_drain(void) {
    static char program[] = "r-test";
    static const unsigned char payload[] = {0x64U, 0x72U, 0x61U, 0x69U, 0x6eU};
    char *arguments[] = {program};
    RRuntimeStartResult start;
    RRuntimeAllocator buffer_allocator;
    RRuntimeDarwinIoHandle *output;
    RRuntimeDarwinIoBufferResult allocation;
    RRuntimeDarwinIoBuffer returned;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoSubmitResult flush_submission;
    RRuntimeDarwinIoResult result;
    RRuntimeDarwinIoResult flush_result;
    unsigned char observed[sizeof(payload)];
    size_t offset = 0U;
    int descriptors[2];
    int saved_stdout;

    if (pipe(descriptors) != 0) {
        return r_runtime_test_failure("console drain pipe creation failed");
    }
    saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout < 0 || dup2(descriptors[1], STDOUT_FILENO) != STDOUT_FILENO ||
        close(descriptors[1]) != 0) {
        return r_runtime_test_failure("console drain stdout redirection failed");
    }
    start = r_runtime_hosted_start(1, arguments);
    if (!start.started) {
        return r_runtime_test_failure("console drain hosted start failed");
    }
    output = r_runtime_darwin_io_process_stdout_retain();
    if (output == NULL) {
        return r_runtime_test_failure("console drain output view failed");
    }
    r_runtime_allocator_initialize(&buffer_allocator);
    allocation = r_runtime_darwin_io_buffer_allocate(&buffer_allocator, sizeof(payload));
    if (allocation.status != R_RUNTIME_DARWIN_IO_START_OK) {
        return r_runtime_test_failure("console drain buffer allocation failed");
    }
    (void)memcpy(allocation.buffer.data, payload, sizeof(payload));
    allocation.buffer.size = sizeof(payload);
    submission = r_runtime_darwin_io_submit_write(output, 0, &allocation.buffer, UINT64_C(0));
    if (submission.status != R_RUNTIME_DARWIN_IO_START_OK || submission.request == NULL) {
        return r_runtime_test_failure("console drain write submission failed");
    }
    flush_submission = r_runtime_darwin_io_submit_flush(output, UINT64_C(0));
    if (flush_submission.status != R_RUNTIME_DARWIN_IO_START_OK ||
        flush_submission.request == NULL) {
        return r_runtime_test_failure("console drain flush submission failed");
    }
    if (r_runtime_hosted_finish(0) != 0) {
        return r_runtime_test_failure("console drain hosted finish failed");
    }
    result = r_runtime_darwin_io_request_wait(submission.request);
    if (result.terminal_event != R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE || result.native_error != 0 ||
        result.bytes_transferred != sizeof(payload) || !result.cleanup_acknowledged) {
        return r_runtime_test_failure("hosted finish did not drain console write");
    }
    flush_result = r_runtime_darwin_io_request_wait(flush_submission.request);
    if (flush_result.terminal_event != R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE ||
        flush_result.native_error != 0 || !flush_result.cleanup_acknowledged) {
        return r_runtime_test_failure("hosted finish did not acknowledge console flush");
    }
    returned = r_runtime_darwin_io_request_take_buffer(submission.request);
    r_runtime_darwin_io_request_release(submission.request);
    r_runtime_darwin_io_request_release(flush_submission.request);
    r_runtime_darwin_io_buffer_release(&returned);
    r_runtime_darwin_io_handle_release(output);
    if (dup2(saved_stdout, STDOUT_FILENO) != STDOUT_FILENO || close(saved_stdout) != 0) {
        return r_runtime_test_failure("console drain stdout restoration failed");
    }
    while (offset != sizeof(observed)) {
        ssize_t count = read(descriptors[0], observed + offset, sizeof(observed) - offset);

        if (count <= 0) {
            return r_runtime_test_failure("console drain payload read failed");
        }
        offset += (size_t)count;
    }
    if (close(descriptors[0]) != 0 || memcmp(observed, payload, sizeof(payload)) != 0) {
        return r_runtime_test_failure("console drain payload changed");
    }
    (void)puts("runtime_console_drain");
    return EXIT_SUCCESS;
}
#endif

int main(int argc, char *argv[]) {
    if ((argc != 2) || (argv == NULL) || (argv[1] == NULL)) {
        return r_runtime_test_failure(
            "expected valid|invalid|oom|executor-oom|root-start-failure|invalid-and-oom|"
            "console-drain");
    }
    if (strcmp(argv[1], "valid") == 0) {
        return r_runtime_test_valid();
    }
    if (strcmp(argv[1], "invalid") == 0) {
        return r_runtime_test_invalid_utf8();
    }
    if (strcmp(argv[1], "oom") == 0) {
        return r_runtime_test_snapshot_oom();
    }
    if (strcmp(argv[1], "executor-oom") == 0) {
        return r_runtime_test_executor_oom();
    }
    if (strcmp(argv[1], "root-start-failure") == 0) {
        return r_runtime_test_async_root_start_failure();
    }
    if (strcmp(argv[1], "invalid-and-oom") == 0) {
        return r_runtime_test_invalid_precedes_oom();
    }
#if defined(__APPLE__)
    if (strcmp(argv[1], "console-drain") == 0) {
        return r_runtime_test_console_drain();
    }
#endif
    return r_runtime_test_failure("unknown test mode");
}
