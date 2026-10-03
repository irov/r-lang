#include "r_std_fs.h"

#include "r_library_fs_internal.h"
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
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

typedef struct RTestFsPayloadFile {
    RStdFsFile file;
    char path[64];
} RTestFsPayloadFile;

typedef struct RTestFsPayloadStartContext {
    RStdFsFile *file;
    RRuntimeArray *buffer;
    RStdFsTaskStartResult started;
} RTestFsPayloadStartContext;

typedef struct RTestFsPayloadStopContext {
    _Bool stopped;
} RTestFsPayloadStopContext;

static void fail(const char *message) {
    (void)fprintf(stderr, "library fs payload test failed: %s\n", message);
    exit(EXIT_FAILURE);
}

static void require(_Bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
}

/* cancel_delivered is published before the payload signals its native request, so the wait also
   needs one more applied position cancellation than were counted before the trigger. */
static _Bool wait_for_position_cancellation(RLibraryFsHandleStorage *storage,
                                            RLibraryFsPositionCancelReason reason,
                                            size_t applied_before) {
    size_t spin;
    _Bool delivered = 0;

    for (spin = 0U; spin < 1000000U; ++spin) {
        RLibraryFsPositionNode *position;

        if (!delivered) {
            require(pthread_mutex_lock(&storage->mutex) == 0, "lock position cancellation state");
            position = storage->position_head;
            delivered =
                position != NULL && position->cancel_delivered && position->cancel_reason == reason;
            require(pthread_mutex_unlock(&storage->mutex) == 0,
                    "unlock position cancellation state");
        }
        if (delivered &&
            r_library_internal_fs_payload_testing_position_cancel_applications() > applied_before) {
            return 1;
        }
        (void)sched_yield();
    }
    return 0;
}

static _Bool wait_for_payload_test_condition(_Bool (*condition)(void)) {
    size_t spin;

    for (spin = 0U; spin < 1000000U; ++spin) {
        if (condition()) {
            return 1;
        }
        (void)sched_yield();
    }
    return 0;
}

static void *start_immediate_read(void *context_pointer) {
    RTestFsPayloadStartContext *context = context_pointer;

    context->started = r_std_fs_read(context->file, context->buffer, (RStdFsDeadline){0});
    return NULL;
}

static void *stop_executor(void *context_pointer) {
    RTestFsPayloadStopContext *context = context_pointer;

    context->stopped = r_runtime_executor_lifecycle_stop();
    return NULL;
}

static void empty_task_body(RRuntimeTaskExecution *execution, void *payload, void *result) {
    (void)execution;
    (void)payload;
    (void)result;
}

static _Bool wait_for_executor_stopping(void) {
    const RRuntimeTypeInfo empty_type = {0U, 1U, NULL, NULL};
    size_t spin;

    for (spin = 0U; spin < 1000000U; ++spin) {
        RRuntimeTaskPrepareResult preparation =
            r_runtime_task_start_prepare(empty_type, empty_type, empty_task_body);

        if (preparation.status == R_RUNTIME_TASK_START_RUNTIME_STOPPING) {
            return 1;
        }
        if (preparation.status != R_RUNTIME_TASK_START_OK || preparation.transaction == NULL) {
            return 0;
        }
        r_runtime_task_start_abort(&preparation.transaction);
        (void)sched_yield();
    }
    return 0;
}

static RRuntimeArray byte_array(RRuntimeAllocator *allocator,
                                const unsigned char *bytes,
                                size_t length,
                                unsigned char fill) {
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
        if (bytes == NULL) {
            (void)memset(array.data, (int)fill, length);
        } else {
            (void)memcpy(array.data, bytes, length);
        }
    }
    return array;
}

static void publish_file(RRuntimeAllocator *allocator,
                         RStdFsFileStorage *storage,
                         int descriptor,
                         RStdFsOpenFileOptions options) {
    RRuntimeDarwinIoHandleCreateResult io_created =
        r_runtime_darwin_io_handle_create(allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);

    require(io_created.status == R_RUNTIME_DARWIN_IO_START_OK && io_created.handle != NULL,
            "create persistent payload root");
    r_library_internal_fs_file_publish(storage, descriptor, options, io_created.handle);
}

static RTestFsPayloadFile create_file(RRuntimeAllocator *allocator,
                                      const unsigned char *bytes,
                                      size_t length,
                                      RStdFsAccess access,
                                      _Bool append) {
    RTestFsPayloadFile fixture = {{0}, "/tmp/r-fs-payload-XXXXXX"};
    RStdFsFileStorage *storage;
    RStdFsOpenFileOptions options = {
        access,
        R_STD_FS_CREATE_EXISTING,
        0,
        append,
        0,
    };
    int descriptor = mkstemp(fixture.path);
    int flags;

    require(descriptor >= 0, "create temporary file");
    require(ftruncate(descriptor, (off_t)0) == 0, "truncate temporary file");
    if (length != 0U) {
        require(pwrite(descriptor, bytes, length, (off_t)0) == (ssize_t)length,
                "initialize temporary file");
    }
    if (append) {
        flags = fcntl(descriptor, F_GETFL);
        require(flags >= 0 && fcntl(descriptor, F_SETFL, flags | O_APPEND) == 0,
                "enable append mode");
    }
    storage = r_library_internal_fs_file_reserve(allocator);
    require(storage != NULL, "reserve file storage");
    publish_file(allocator, storage, descriptor, options);
    fixture.file.storage = storage;
    return fixture;
}

static void destroy_file(RTestFsPayloadFile *fixture) {
    r_std_fs_file_destroy(&fixture->file);
    require(unlink(fixture->path) == 0, "remove temporary file");
}

static RStdIoReadResult await_read(RStdFsTaskStartResult started) {
    RStdIoReadResult result = {0};

    require(started.is_ok && started.task != NULL, "read task start");
    require(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
            "await read task");
    return result;
}

static RStdIoWriteResult await_write(RStdFsTaskStartResult started) {
    RStdIoWriteResult result = {0};

    require(started.is_ok && started.task != NULL, "write task start");
    require(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
            "await write task");
    return result;
}

static RStdIoWriteAllResult await_write_all(RStdFsTaskStartResult started) {
    RStdIoWriteAllResult result = {0};

    require(started.is_ok && started.task != NULL, "write_all task start");
    require(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
            "await write_all task");
    return result;
}

static RStdFsVoidResult await_close(RStdFsTaskStartResult started) {
    RStdFsVoidResult result = {0};

    require(started.is_ok && started.task != NULL, "close task start");
    require(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
            "await close task");
    return result;
}

static void test_ordered_position_and_owners(RRuntimeAllocator *allocator) {
    static const unsigned char initial[] = {'a', 'b', 'c', 'd', 'e', 'f'};
    static const unsigned char suffix[] = {'X', 'Y'};
    static const RStdFsDeadline no_deadline = {0};
    RTestFsPayloadFile fixture =
        create_file(allocator, initial, sizeof(initial), R_STD_FS_ACCESS_READ_WRITE, 0);
    RRuntimeArray first = byte_array(allocator, NULL, 3U, UINT8_C(0xaa));
    RRuntimeArray second = byte_array(allocator, NULL, 3U, UINT8_C(0xbb));
    RStdFsTaskStartResult first_started;
    RStdFsTaskStartResult second_started;
    RStdIoReadResult first_result;
    RStdIoReadResult second_result;
    RStdIoReadResult end_result;
    RStdIoWriteAllResult write_result;
    RRuntimeArray write_buffer;
    RRuntimeArray end_buffer;
    unsigned char contents[sizeof(initial) + sizeof(suffix)];
    void *first_owner = first.data;
    void *second_owner = second.data;
    void *write_owner;

    r_runtime_darwin_io_testing_pause_next_stream_position_barrier();
    first_started = r_std_fs_read(&fixture.file, &first, no_deadline);
    second_started = r_std_fs_read(&fixture.file, &second, no_deadline);
    require(first.data == NULL && second.data == NULL, "ordered reads consume owners");
    r_runtime_darwin_io_testing_wait_for_stream_position_barrier();
    r_runtime_darwin_io_testing_release_stream_position_barrier();
    first_result = await_read(first_started);
    second_result = await_read(second_started);
    require(first_result.kind == R_STD_IO_READ_RESULT_READ && first_result.count == 3U,
            "first ordered read progress");
    require(second_result.kind == R_STD_IO_READ_RESULT_READ && second_result.count == 3U,
            "second ordered read progress");
    require(first_result.buffer.data == first_owner &&
                memcmp(first_result.buffer.data, "abc", 3U) == 0,
            "first ordered read owner and bytes");
    require(second_result.buffer.data == second_owner &&
                memcmp(second_result.buffer.data, "def", 3U) == 0,
            "second ordered read owner and bytes");
    r_runtime_array_destroy(&first_result.buffer);
    r_runtime_array_destroy(&second_result.buffer);

    write_buffer = byte_array(allocator, suffix, sizeof(suffix), 0U);
    write_owner = write_buffer.data;
    write_result = await_write_all(r_std_fs_write_all(&fixture.file, &write_buffer, no_deadline));
    require(write_buffer.data == NULL, "write_all consumes owner");
    require(write_result.kind == R_STD_IO_WRITE_ALL_RESULT_WRITTEN &&
                write_result.written == sizeof(suffix),
            "write_all complete progress");
    require(write_result.buffer.data == write_owner &&
                memcmp(write_result.buffer.data, suffix, sizeof(suffix)) == 0,
            "write_all returns immutable owner");
    r_runtime_array_destroy(&write_result.buffer);
    require(pread(r_library_internal_fs_file_descriptor(&fixture.file),
                  contents,
                  sizeof(contents),
                  (off_t)0) == (ssize_t)sizeof(contents),
            "read written file bytes");
    require(memcmp(contents, "abcdefXY", sizeof(contents)) == 0,
            "ordered write uses shared position");

    end_buffer = byte_array(allocator, NULL, 2U, UINT8_C(0xcc));
    end_result = await_read(r_std_fs_read(&fixture.file, &end_buffer, no_deadline));
    require(end_result.kind == R_STD_IO_READ_RESULT_END && end_result.count == 0U,
            "end of file is distinct outcome");
    require(((unsigned char *)end_result.buffer.data)[0] == UINT8_C(0xcc) &&
                ((unsigned char *)end_result.buffer.data)[1] == UINT8_C(0xcc),
            "end of file preserves bytes");
    r_runtime_array_destroy(&end_result.buffer);
    destroy_file(&fixture);
}

static void test_partial_read_preserves_tail_and_advances_position(RRuntimeAllocator *allocator) {
    static const unsigned char initial[] = {'m', 'n'};
    static const RStdFsDeadline no_deadline = {0};
    RTestFsPayloadFile fixture =
        create_file(allocator, initial, sizeof(initial), R_STD_FS_ACCESS_READ, 0);
    RRuntimeArray buffer = byte_array(allocator, NULL, 4U, UINT8_C(0xcc));
    RRuntimeArray end_buffer;
    RStdIoReadResult result;
    RStdIoReadResult end_result;
    void *owner = buffer.data;

    result = await_read(r_std_fs_read(&fixture.file, &buffer, no_deadline));
    require(result.kind == R_STD_IO_READ_RESULT_READ && result.count == sizeof(initial),
            "short file produces partial read progress");
    require(result.buffer.data == owner &&
                memcmp(result.buffer.data, initial, sizeof(initial)) == 0 &&
                ((unsigned char *)result.buffer.data)[2] == UINT8_C(0xcc) &&
                ((unsigned char *)result.buffer.data)[3] == UINT8_C(0xcc),
            "partial read returns the same owner and preserves its untouched tail");
    r_runtime_array_destroy(&result.buffer);

    end_buffer = byte_array(allocator, NULL, 1U, UINT8_C(0xdd));
    end_result = await_read(r_std_fs_read(&fixture.file, &end_buffer, no_deadline));
    require(end_result.kind == R_STD_IO_READ_RESULT_END && end_result.count == 0U &&
                ((unsigned char *)end_result.buffer.data)[0] == UINT8_C(0xdd),
            "partial read advances the shared position exactly to end of file");
    r_runtime_array_destroy(&end_result.buffer);
    destroy_file(&fixture);
}

static void test_append_observation_position(RRuntimeAllocator *allocator) {
    static const unsigned char initial[] = {'a', 'b', 'c'};
    static const unsigned char first_append[] = {'Z'};
    static const unsigned char second_append[] = {'Y'};
    static const RStdFsDeadline no_deadline = {0};
    RTestFsPayloadFile fixture =
        create_file(allocator, initial, sizeof(initial), R_STD_FS_ACCESS_READ_WRITE, 1);
    RRuntimeArray write_buffer = byte_array(allocator, first_append, 1U, 0U);
    RRuntimeArray read_buffer;
    RStdIoWriteAllResult write_all_result;
    RStdIoWriteResult write_result;
    RStdIoReadResult read_result;
    unsigned char contents[5];

    write_all_result =
        await_write_all(r_std_fs_write_all(&fixture.file, &write_buffer, no_deadline));
    require(write_all_result.kind == R_STD_IO_WRITE_ALL_RESULT_WRITTEN &&
                write_all_result.written == 1U,
            "append write_all succeeds");
    r_runtime_array_destroy(&write_all_result.buffer);
    read_buffer = byte_array(allocator, NULL, 2U, 0U);
    read_result = await_read(r_std_fs_read(&fixture.file, &read_buffer, no_deadline));
    require(read_result.kind == R_STD_IO_READ_RESULT_READ && read_result.count == 2U &&
                memcmp(read_result.buffer.data, "ab", 2U) == 0,
            "append does not advance observation position");
    r_runtime_array_destroy(&read_result.buffer);

    write_buffer = byte_array(allocator, second_append, 1U, 0U);
    write_result = await_write(r_std_fs_write(&fixture.file, &write_buffer, no_deadline));
    require(write_result.kind == R_STD_IO_WRITE_RESULT_WRITTEN && write_result.count == 1U,
            "second append succeeds");
    r_runtime_array_destroy(&write_result.buffer);
    read_buffer = byte_array(allocator, NULL, 2U, 0U);
    read_result = await_read(r_std_fs_read(&fixture.file, &read_buffer, no_deadline));
    require(read_result.kind == R_STD_IO_READ_RESULT_READ && read_result.count == 2U &&
                memcmp(read_result.buffer.data, "cZ", 2U) == 0,
            "later append still preserves observation position");
    r_runtime_array_destroy(&read_result.buffer);
    require(pread(r_library_internal_fs_file_descriptor(&fixture.file),
                  contents,
                  sizeof(contents),
                  (off_t)0) == (ssize_t)sizeof(contents),
            "read append contents");
    require(memcmp(contents, "abcZY", sizeof(contents)) == 0, "append writes select current end");
    destroy_file(&fixture);
}

static void test_noop_validation_and_start_failure(RRuntimeAllocator *allocator) {
    static const unsigned char initial[] = {'R'};
    RTestFsPayloadFile fixture =
        create_file(allocator, initial, sizeof(initial), R_STD_FS_ACCESS_READ_WRITE, 0);
    RRuntimeArray empty = byte_array(allocator, NULL, 0U, 0U);
    RRuntimeArray rejected = byte_array(allocator, initial, sizeof(initial), 0U);
    RStdTimeInstantResult now = r_std_time_monotonic_now();
    RStdIoWriteResult empty_result;
    RStdIoWriteResult rejected_result;

    require(now.is_ok, "read monotonic clock");
    empty_result =
        await_write(r_std_fs_write(&fixture.file, &empty, (RStdFsDeadline){1, now.value}));
    require(empty_result.kind == R_STD_IO_WRITE_RESULT_WRITTEN && empty_result.count == 0U,
            "valid empty write precedes expired deadline");
    r_runtime_array_destroy(&empty_result.buffer);
    destroy_file(&fixture);

    fixture = create_file(allocator, initial, sizeof(initial), R_STD_FS_ACCESS_READ, 0);

    rejected_result = await_write(r_std_fs_write(&fixture.file, &rejected, (RStdFsDeadline){0}));
    require(rejected_result.kind == R_STD_IO_WRITE_RESULT_FAILED &&
                rejected_result.error.code == R_STD_IO_ERROR_INVALID_OPERATION &&
                rejected_result.count == 0U,
            "write rejects read-only file");
    r_runtime_array_destroy(&rejected_result.buffer);
    destroy_file(&fixture);
}

static void test_immediate_result_precedes_executor_cancellation(RRuntimeAllocator *allocator) {
    static const unsigned char initial[] = {'R'};
    RTestFsPayloadFile fixture =
        create_file(allocator, initial, sizeof(initial), R_STD_FS_ACCESS_READ, 0);
    RRuntimeArray buffer = byte_array(allocator, NULL, 1U, UINT8_C(0xa5));
    RTestFsPayloadStartContext start_context = {&fixture.file, &buffer, {0}};
    RTestFsPayloadStopContext stop_context = {0};
    RStdIoReadResult result = {0};
    pthread_t start_thread;
    pthread_t stop_thread;
    void *owner = buffer.data;

    buffer.length = 0U;
    r_library_internal_fs_payload_testing_pause_before_immediate_select(1);
    require(pthread_create(&start_thread, NULL, start_immediate_read, &start_context) == 0,
            "start paused immediate read");
    require(wait_for_payload_test_condition(
                r_library_internal_fs_payload_testing_immediate_select_reached),
            "immediate read reached terminal selection");
    require(pthread_create(&stop_thread, NULL, stop_executor, &stop_context) == 0,
            "start concurrent executor stop");
    require(wait_for_executor_stopping(), "executor selected cancellation before publication");
    r_library_internal_fs_payload_testing_pause_before_immediate_select(0);
    require(pthread_join(start_thread, NULL) == 0, "join immediate read start");
    require(pthread_join(stop_thread, NULL) == 0 && stop_context.stopped,
            "join concurrent executor stop");
    require(start_context.started.is_ok && start_context.started.task != NULL &&
                buffer.data == NULL,
            "immediate read start consumes its owner");
    require(r_runtime_task_await(&start_context.started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
            "earlier immediate result replaces later executor cancellation");
    require(result.kind == R_STD_IO_READ_RESULT_READ && result.count == 0U &&
                result.buffer.data == owner && result.buffer.length == 0U,
            "immediate read returns the unchanged owner");
    require(r_runtime_executor_lifecycle_start(allocator) == R_RUNTIME_EXECUTOR_START_OK,
            "restart executor after immediate-result race");
    r_runtime_array_destroy(&result.buffer);
    destroy_file(&fixture);
}

static void test_start_allocation_failure_sweep(RRuntimeAllocator *allocator) {
    static const unsigned char initial[] = {'R'};
    static const RStdFsDeadline no_deadline = {0};
    const uint64_t sweep_limit = UINT64_C(64);
    uint64_t fail_at;
    _Bool reached_success = 0;

    for (fail_at = UINT64_C(1); fail_at <= sweep_limit; ++fail_at) {
        RTestFsPayloadFile fixture =
            create_file(allocator, initial, sizeof(initial), R_STD_FS_ACCESS_READ, 0);
        RRuntimeArray staged = byte_array(allocator, NULL, sizeof(initial), UINT8_C(0xa5));
        RRuntimeArray staged_snapshot = staged;
        unsigned char staged_bytes[sizeof(initial)];
        RStdFsTaskStartResult started;
        RRuntimeDarwinIoHandle *payload_io = fixture.file.storage->handle.payload_io;

        require(payload_io != NULL, "file materialization publishes its persistent payload root");
        (void)memcpy(staged_bytes, staged.data, sizeof(staged_bytes));
        r_runtime_allocator_set_failure(allocator, fail_at);
        started = r_std_fs_read(&fixture.file, &staged, no_deadline);
        r_runtime_allocator_set_failure(allocator, UINT64_C(0));
        if (started.is_ok) {
            RStdIoReadResult result;

            require(started.task != NULL && staged.data == NULL,
                    "first non-failing allocation sweep start consumes owner");
            require(fixture.file.storage->handle.payload_io == payload_io,
                    "successful task start retains the materialized payload root");
            result = await_read(started);
            require(result.kind == R_STD_IO_READ_RESULT_READ && result.count == sizeof(initial) &&
                        memcmp(result.buffer.data, initial, sizeof(initial)) == 0,
                    "first non-failing allocation sweep operation completes");
            r_runtime_array_destroy(&result.buffer);
            destroy_file(&fixture);
            reached_success = 1;
            break;
        }

        require(started.task == NULL && started.error == R_STD_ASYNC_START_ALLOCATION_FAILED,
                "every pre-commit injected failure maps to allocation_failed");
        require(memcmp(&staged, &staged_snapshot, sizeof(staged)) == 0 &&
                    memcmp(staged.data, staged_bytes, sizeof(staged_bytes)) == 0,
                "every pre-commit injected failure preserves descriptor and bytes");
        require(fixture.file.storage->handle.payload_io == payload_io,
                "start rollback leaves the file-owned payload root unchanged");
        {
            RRuntimeArray proof = byte_array(allocator, NULL, sizeof(initial), UINT8_C(0x5a));
            RStdIoReadResult proof_result =
                await_read(r_std_fs_read(&fixture.file, &proof, no_deadline));

            require(proof_result.kind == R_STD_IO_READ_RESULT_READ &&
                        proof_result.count == sizeof(initial) &&
                        memcmp(proof_result.buffer.data, initial, sizeof(initial)) == 0,
                    "post-failure operation proves FIFO and reservation cleanup");
            require(fixture.file.storage->handle.payload_io == payload_io,
                    "post-failure retry reuses the materialized payload root");
            r_runtime_array_destroy(&proof_result.buffer);
        }
        r_runtime_array_destroy(&staged);
        destroy_file(&fixture);
    }
    require(reached_success, "allocation sweep reaches first successful task start");
}

static void set_file_size_limit(struct rlimit *saved_limit, void (**saved_handler)(int)) {
    struct rlimit limited;

    require(getrlimit(RLIMIT_FSIZE, saved_limit) == 0, "read file-size limit");
    limited = *saved_limit;
    require(limited.rlim_max == RLIM_INFINITY || limited.rlim_max >= (rlim_t)65536U,
            "file-size hard limit permits partial fixture");
    limited.rlim_cur = (rlim_t)65536U;
    require(setrlimit(RLIMIT_FSIZE, &limited) == 0, "set partial file-size limit");
    *saved_handler = signal(SIGXFSZ, SIG_IGN);
    require(*saved_handler != SIG_ERR, "ignore partial file-size signal");
}

static void restore_file_size_limit(const struct rlimit *saved_limit, void (*saved_handler)(int)) {
    require(setrlimit(RLIMIT_FSIZE, saved_limit) == 0, "restore file-size limit");
    require(signal(SIGXFSZ, saved_handler) != SIG_ERR, "restore file-size signal");
}

static void test_partial_progress_advances_position(RRuntimeAllocator *allocator) {
    const size_t payload_size = 256U * 1024U;
    static const unsigned char marker[] = {'Q'};
    static const RStdFsDeadline no_deadline = {0};
    RTestFsPayloadFile fixture = create_file(allocator, NULL, 0U, R_STD_FS_ACCESS_READ_WRITE, 0);
    RRuntimeArray buffer = byte_array(allocator, NULL, payload_size, UINT8_C(0x5a));
    RRuntimeArray marker_buffer;
    RStdIoWriteAllResult partial;
    RStdIoWriteAllResult marker_result;
    struct rlimit saved_limit;
    void (*saved_handler)(int) = SIG_DFL;
    unsigned char observed = 0U;
    void *owner = buffer.data;

    set_file_size_limit(&saved_limit, &saved_handler);
    partial = await_write_all(r_std_fs_write_all(&fixture.file, &buffer, no_deadline));
    restore_file_size_limit(&saved_limit, saved_handler);
    require(partial.kind == R_STD_IO_WRITE_ALL_RESULT_FAILED && partial.written > 0U &&
                partial.written < payload_size &&
                partial.error.code == R_STD_IO_ERROR_RESOURCE_EXHAUSTED,
            "write_all reports positive partial native failure");
    require(partial.buffer.data == owner &&
                ((unsigned char *)partial.buffer.data)[0] == UINT8_C(0x5a) &&
                ((unsigned char *)partial.buffer.data)[payload_size - 1U] == UINT8_C(0x5a),
            "partial write_all returns immutable owner");
    marker_buffer = byte_array(allocator, marker, sizeof(marker), 0U);
    marker_result = await_write_all(r_std_fs_write_all(&fixture.file, &marker_buffer, no_deadline));
    require(marker_result.kind == R_STD_IO_WRITE_ALL_RESULT_WRITTEN &&
                marker_result.written == sizeof(marker),
            "write after partial failure succeeds");
    require(pread(r_library_internal_fs_file_descriptor(&fixture.file),
                  &observed,
                  1U,
                  (off_t)partial.written) == 1 &&
                observed == marker[0],
            "partial failure advances shared position exactly");
    r_runtime_array_destroy(&partial.buffer);
    r_runtime_array_destroy(&marker_result.buffer);
    destroy_file(&fixture);
}

static void test_single_write_positive_partial(RRuntimeAllocator *allocator) {
    const size_t payload_size = 256U * 1024U;
    static const RStdFsDeadline no_deadline = {0};
    RTestFsPayloadFile fixture = create_file(allocator, NULL, 0U, R_STD_FS_ACCESS_READ_WRITE, 0);
    RRuntimeArray buffer = byte_array(allocator, NULL, payload_size, UINT8_C(0x6b));
    RStdIoWriteResult partial;
    struct rlimit saved_limit;
    void (*saved_handler)(int) = SIG_DFL;
    void *owner = buffer.data;

    set_file_size_limit(&saved_limit, &saved_handler);
    partial = await_write(r_std_fs_write(&fixture.file, &buffer, no_deadline));
    restore_file_size_limit(&saved_limit, saved_handler);
    require(partial.count > 0U && partial.count < payload_size,
            "single write publishes positive partial progress");
    require(partial.kind == R_STD_IO_WRITE_RESULT_WRITTEN ||
                (partial.kind == R_STD_IO_WRITE_RESULT_FAILED &&
                 partial.error.code == R_STD_IO_ERROR_RESOURCE_EXHAUSTED),
            "single partial write has exact success or native-failure outcome");
    require(partial.buffer.data == owner &&
                ((unsigned char *)partial.buffer.data)[0] == UINT8_C(0x6b) &&
                ((unsigned char *)partial.buffer.data)[payload_size - 1U] == UINT8_C(0x6b),
            "single partial write returns immutable owner");
    r_runtime_array_destroy(&partial.buffer);
    destroy_file(&fixture);
}

static void test_deadline_after_partial_progress(RRuntimeAllocator *allocator) {
    const size_t payload_size = 4U * 1024U * 1024U;
    RStdFsFileStorage *storage;
    RStdFsFile file = {0};
    RStdFsOpenFileOptions options = {
        R_STD_FS_ACCESS_WRITE,
        R_STD_FS_CREATE_EXISTING,
        0,
        1,
        0,
    };
    RRuntimeArray buffer = byte_array(allocator, NULL, payload_size, UINT8_C(0x7c));
    RStdTimeInstantResult now = r_std_time_monotonic_now();
    RStdTimeInstantResult deadline;
    RStdIoWriteAllResult result;
    void *owner = buffer.data;
    int descriptors[2];

    require(pipe(descriptors) == 0, "create partial deadline pipe");
    require(now.is_ok, "read partial deadline instant");
    deadline =
        r_std_time_instant_add(now.value, (RStdTimeDuration){INT64_C(0), UINT32_C(100000000)});
    require(deadline.is_ok, "construct partial write deadline");
    storage = r_library_internal_fs_file_reserve(allocator);
    require(storage != NULL, "reserve partial deadline file storage");
    publish_file(allocator, storage, descriptors[1], options);
    file.storage = storage;
    result =
        await_write_all(r_std_fs_write_all(&file, &buffer, (RStdFsDeadline){1, deadline.value}));
    require(result.kind == R_STD_IO_WRITE_ALL_RESULT_FAILED &&
                result.error.code == R_STD_IO_ERROR_TIMED_OUT && result.written > 0U &&
                result.written < payload_size,
            "deadline preserves positive partial write progress");
    require(result.buffer.data == owner &&
                ((unsigned char *)result.buffer.data)[0] == UINT8_C(0x7c) &&
                ((unsigned char *)result.buffer.data)[payload_size - 1U] == UINT8_C(0x7c),
            "partial deadline returns immutable owner");
    r_runtime_array_destroy(&result.buffer);
    r_std_fs_file_destroy(&file);
    require(close(descriptors[0]) == 0, "close partial deadline pipe reader");
}

static void test_deadline_during_positioned_barrier(RRuntimeAllocator *allocator) {
    static const unsigned char initial[] = {'a', 'b', 'c', 'd'};
    static const RStdFsDeadline no_deadline = {0};
    const struct timespec wait_time = {0, 250000000L};
    RTestFsPayloadFile fixture =
        create_file(allocator, initial, sizeof(initial), R_STD_FS_ACCESS_READ_WRITE, 0);
    RLibraryFsHandleStorage *storage = &fixture.file.storage->handle;
    RRuntimeArray buffer = byte_array(allocator, (const unsigned char *)"Z", 1U, 0U);
    RRuntimeArray proof;
    RStdTimeInstantResult now = r_std_time_monotonic_now();
    RStdTimeDurationResult duration =
        r_std_time_duration_from_parts(INT64_C(0), UINT32_C(200000000));
    RStdTimeInstantResult expires;
    RStdIoWriteResult write_result;
    RStdIoReadResult read_result;
    RStdFsTaskStartResult write_started;
    unsigned char observed[sizeof(initial)];
    void *owner = buffer.data;
    size_t applied;

    require(now.is_ok && duration.is_ok, "construct payload deadline");
    expires = r_std_time_instant_add(now.value, duration.value);
    require(expires.is_ok, "add payload deadline");
    applied = r_library_internal_fs_payload_testing_position_cancel_applications();
    r_runtime_darwin_io_testing_pause_next_stream_position_barrier();
    write_started = r_std_fs_write(&fixture.file, &buffer, (RStdFsDeadline){1, expires.value});
    require(write_started.is_ok && write_started.task != NULL && buffer.data == NULL,
            "deadline write starts and consumes owner");
    r_runtime_darwin_io_testing_wait_for_stream_position_barrier();
    require(nanosleep(&wait_time, NULL) == 0, "wait for payload deadline");
    require(wait_for_position_cancellation(storage, R_LIBRARY_FS_POSITION_CANCEL_DEADLINE, applied),
            "deadline cancellation reaches positioned request");
    r_runtime_darwin_io_testing_release_stream_position_barrier();
    write_result = await_write(write_started);
    require(write_result.kind == R_STD_IO_WRITE_RESULT_FAILED &&
                write_result.error.code == R_STD_IO_ERROR_TIMED_OUT && write_result.count == 0U,
            "positioned write reports timed_out");
    require(write_result.buffer.data == owner &&
                ((unsigned char *)write_result.buffer.data)[0] == (unsigned char)'Z',
            "deadline returns the unchanged write owner");
    require(pread(r_library_internal_fs_file_descriptor(&fixture.file),
                  observed,
                  sizeof(observed),
                  (off_t)0) == (ssize_t)sizeof(observed) &&
                memcmp(observed, initial, sizeof(initial)) == 0,
            "deadline before positioned submission performs no payload write");
    r_runtime_array_destroy(&write_result.buffer);

    proof = byte_array(allocator, NULL, 1U, UINT8_C(0xee));
    read_result = await_read(r_std_fs_read(&fixture.file, &proof, no_deadline));
    require(read_result.kind == R_STD_IO_READ_RESULT_READ && read_result.count == 1U &&
                ((unsigned char *)read_result.buffer.data)[0] == initial[0],
            "deadline leaves logical position unchanged and queue usable");
    r_runtime_array_destroy(&read_result.buffer);
    destroy_file(&fixture);
}

static void test_close_during_positioned_barrier(RRuntimeAllocator *allocator) {
    static const unsigned char initial[] = {'a', 'b', 'c', 'd'};
    static const RStdFsDeadline no_deadline = {0};
    RTestFsPayloadFile fixture =
        create_file(allocator, initial, sizeof(initial), R_STD_FS_ACCESS_READ_WRITE, 0);
    RLibraryFsHandleStorage *storage = &fixture.file.storage->handle;
    RRuntimeArray buffer = byte_array(allocator, NULL, sizeof(initial), UINT8_C(0xdd));
    RStdFsTaskStartResult read_started;
    RStdFsTaskStartResult close_started;
    RStdIoReadResult read_result;
    RStdFsVoidResult close_result;
    void *owner = buffer.data;
    size_t applied;

    applied = r_library_internal_fs_payload_testing_position_cancel_applications();
    r_runtime_darwin_io_testing_pause_next_stream_position_barrier();
    read_started = r_std_fs_read(&fixture.file, &buffer, no_deadline);
    require(read_started.is_ok && buffer.data == NULL, "close-cancelled read starts");
    r_runtime_darwin_io_testing_wait_for_stream_position_barrier();
    close_started = r_std_fs_close_file(&fixture.file, no_deadline);
    require(close_started.is_ok && fixture.file.storage == NULL, "close task consumes file view");
    require(wait_for_position_cancellation(storage, R_LIBRARY_FS_POSITION_CANCEL_CLOSE, applied),
            "close cancellation reaches positioned request");
    r_runtime_darwin_io_testing_release_stream_position_barrier();
    read_result = await_read(read_started);
    close_result = await_close(close_started);
    require(read_result.kind == R_STD_IO_READ_RESULT_FAILED &&
                read_result.error.code == R_STD_IO_ERROR_CANCELLED && read_result.count == 0U,
            "active close cancellation reports cancelled");
    require(read_result.buffer.data == owner &&
                ((unsigned char *)read_result.buffer.data)[0] == UINT8_C(0xdd) &&
                ((unsigned char *)read_result.buffer.data)[sizeof(initial) - 1U] == UINT8_C(0xdd),
            "close before positioned submission returns an untouched read owner");
    require(close_result.r_tag == UINT32_C(0), "close waits for cancelled payload");
    r_runtime_array_destroy(&read_result.buffer);
    require(unlink(fixture.path) == 0, "remove explicitly closed temporary file");
}

static void test_task_cancel_during_positioned_barrier(RRuntimeAllocator *allocator) {
    static const unsigned char initial[] = {'a', 'b', 'c', 'd'};
    static const unsigned char replacement[] = {'Z'};
    static const RStdFsDeadline no_deadline = {0};
    RTestFsPayloadFile fixture =
        create_file(allocator, initial, sizeof(initial), R_STD_FS_ACCESS_READ_WRITE, 0);
    RLibraryFsHandleStorage *storage = &fixture.file.storage->handle;
    RRuntimeArray buffer = byte_array(allocator, replacement, sizeof(replacement), 0U);
    RRuntimeArray proof;
    RStdFsTaskStartResult write_started;
    RStdIoReadResult read_result;
    unsigned char observed[sizeof(initial)];
    size_t applied;

    applied = r_library_internal_fs_payload_testing_position_cancel_applications();
    r_runtime_darwin_io_testing_pause_next_stream_position_barrier();
    write_started = r_std_fs_write(&fixture.file, &buffer, no_deadline);
    require(write_started.is_ok && write_started.task != NULL && buffer.data == NULL,
            "task-cancelled write starts and consumes owner");
    r_runtime_darwin_io_testing_wait_for_stream_position_barrier();
    r_runtime_task_cancel(&write_started.task);
    require(write_started.task == NULL, "task cancellation consumes observation");
    require(wait_for_position_cancellation(storage, R_LIBRARY_FS_POSITION_CANCEL_TASK, applied),
            "task cancellation reaches positioned request");
    r_runtime_darwin_io_testing_release_stream_position_barrier();

    proof = byte_array(allocator, NULL, 1U, UINT8_C(0xee));
    read_result = await_read(r_std_fs_read(&fixture.file, &proof, no_deadline));
    require(read_result.kind == R_STD_IO_READ_RESULT_READ && read_result.count == 1U &&
                ((unsigned char *)read_result.buffer.data)[0] == initial[0],
            "task cancellation leaves logical position unchanged and queue usable");
    r_runtime_array_destroy(&read_result.buffer);
    require(pread(r_library_internal_fs_file_descriptor(&fixture.file),
                  observed,
                  sizeof(observed),
                  (off_t)0) == (ssize_t)sizeof(observed) &&
                memcmp(observed, initial, sizeof(initial)) == 0,
            "task cancellation before positioned submission performs no payload write");
    destroy_file(&fixture);
}

static void test_native_read_after_task_cancel_selection(RRuntimeAllocator *allocator) {
    static const unsigned char initial[] = {'a', 'b'};
    static const RStdFsDeadline no_deadline = {0};
    RTestFsPayloadFile fixture =
        create_file(allocator, initial, sizeof(initial), R_STD_FS_ACCESS_READ, 0);
    RRuntimeArray first = byte_array(allocator, NULL, 1U, UINT8_C(0xee));
    RRuntimeArray proof;
    RStdFsTaskStartResult first_started;
    RStdFsTaskStartResult proof_started;
    RStdIoReadResult proof_result;

    r_library_internal_fs_payload_testing_pause_before_cancel_report(1);
    r_runtime_darwin_io_testing_pause_next_stream_position_barrier();
    first_started = r_std_fs_read(&fixture.file, &first, no_deadline);
    require(first_started.is_ok && first_started.task != NULL && first.data == NULL,
            "native-after-cancel read starts and consumes owner");
    r_runtime_darwin_io_testing_wait_for_stream_position_barrier();
    r_runtime_task_cancel(&first_started.task);
    require(first_started.task == NULL, "native-after-cancel selection consumes observation");
    require(wait_for_payload_test_condition(
                r_library_internal_fs_payload_testing_cancel_report_reached),
            "task cancel worker pauses before producer report");

    r_runtime_darwin_io_testing_release_stream_position_barrier();
    require(wait_for_payload_test_condition(
                r_library_internal_fs_payload_testing_terminal_after_cancel_observed),
            "native read completes after task cancellation was selected");
    require(r_library_internal_fs_payload_testing_task_cancel_won(),
            "native payload success does not override selected task cancellation");

    proof = byte_array(allocator, NULL, 1U, UINT8_C(0xdd));
    proof_started = r_std_fs_read(&fixture.file, &proof, no_deadline);
    require(proof_started.is_ok && proof_started.task != NULL && proof.data == NULL,
            "follow-up read queues behind cancelled observation");
    r_library_internal_fs_payload_testing_pause_before_cancel_report(0);
    proof_result = await_read(proof_started);
    require(proof_result.kind == R_STD_IO_READ_RESULT_READ && proof_result.count == 1U &&
                ((unsigned char *)proof_result.buffer.data)[0] == initial[1],
            "cancelled observation still advances position by acknowledged native progress");
    r_runtime_array_destroy(&proof_result.buffer);
    destroy_file(&fixture);
}

static void test_task_cancel_after_deadline_selection(RRuntimeAllocator *allocator) {
    static const unsigned char initial[] = {'a', 'b'};
    static const RStdFsDeadline no_deadline = {0};
    const struct timespec wait_time = {0, 300000000L};
    RTestFsPayloadFile fixture =
        create_file(allocator, initial, sizeof(initial), R_STD_FS_ACCESS_READ, 0);
    RRuntimeArray first = byte_array(allocator, NULL, 1U, UINT8_C(0xee));
    RRuntimeArray proof;
    RStdTimeInstantResult now = r_std_time_monotonic_now();
    RStdTimeDurationResult duration =
        r_std_time_duration_from_parts(INT64_C(0), UINT32_C(200000000));
    RStdTimeInstantResult expires;
    RStdFsTaskStartResult first_started;
    RStdIoReadResult proof_result;

    require(now.is_ok && duration.is_ok, "construct deadline-before-task-cancel instant");
    expires = r_std_time_instant_add(now.value, duration.value);
    require(expires.is_ok, "add deadline-before-task-cancel duration");
    r_library_internal_fs_payload_testing_pause_before_cancel_report(1);
    r_runtime_darwin_io_testing_pause_next_stream_position_barrier();
    first_started = r_std_fs_read(&fixture.file, &first, (RStdFsDeadline){1, expires.value});
    require(first_started.is_ok && first_started.task != NULL && first.data == NULL,
            "deadline-before-task-cancel read starts and consumes owner");
    r_runtime_darwin_io_testing_wait_for_stream_position_barrier();
    require(nanosleep(&wait_time, NULL) == 0, "wait for deadline-before-task-cancel timer");
    require(wait_for_payload_test_condition(r_library_internal_fs_payload_testing_deadline_won),
            "deadline selects the payload terminal outcome before acknowledgement");

    r_runtime_task_cancel(&first_started.task);
    require(first_started.task == NULL, "later task cancellation consumes only the observation");
    r_runtime_darwin_io_testing_release_stream_position_barrier();
    require(wait_for_payload_test_condition(
                r_library_internal_fs_payload_testing_terminal_after_cancel_observed),
            "native acknowledgement follows deadline selection");
    require(!r_library_internal_fs_payload_testing_task_cancel_won(),
            "later task cancellation does not replace the selected deadline");
    r_library_internal_fs_payload_testing_pause_before_cancel_report(0);

    proof = byte_array(allocator, NULL, 1U, UINT8_C(0xdd));
    proof_result = await_read(r_std_fs_read(&fixture.file, &proof, no_deadline));
    require(proof_result.kind == R_STD_IO_READ_RESULT_READ && proof_result.count == 1U &&
                ((unsigned char *)proof_result.buffer.data)[0] == initial[0],
            "deadline before positioned submission leaves the shared position unchanged");
    r_runtime_array_destroy(&proof_result.buffer);
    destroy_file(&fixture);
}

static _Bool cancel_retain_reached(void) {
    return r_library_internal_fs_payload_testing_cancel_retain_reached();
}

static _Bool wait_for_unregistered_operations(RLibraryFsHandleStorage *storage) {
    size_t spin;

    for (spin = 0U; spin < 1000000U; ++spin) {
        _Bool drained;

        require(pthread_mutex_lock(&storage->mutex) == 0, "lock operation registry");
        drained = storage->operations == NULL && storage->position_head == NULL;
        require(pthread_mutex_unlock(&storage->mutex) == 0, "unlock operation registry");
        if (drained) {
            return 1;
        }
        (void)sched_yield();
    }
    return 0;
}

static _Bool wait_for_cancel_reports_finished(size_t before) {
    size_t spin;

    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_library_internal_fs_payload_testing_cancel_reports_finished() > before) {
            return 1;
        }
        (void)sched_yield();
    }
    return 0;
}

/* A task cancellation runs on the executor after the task was cancelled and may resume only after
   its operation finished, unregistered and the file was destroyed; the storage reference it took
   while the registration was live keeps the storage until its position cancellation is done. */
static void test_late_task_cancel_outlives_file(RRuntimeAllocator *allocator) {
    static const unsigned char initial[] = {'a', 'b'};
    static const RStdFsDeadline no_deadline = {0};
    RTestFsPayloadFile fixture =
        create_file(allocator, initial, sizeof(initial), R_STD_FS_ACCESS_READ, 0);
    RLibraryFsHandleStorage *storage = &fixture.file.storage->handle;
    RRuntimeArray buffer = byte_array(allocator, NULL, 1U, UINT8_C(0xee));
    RStdFsTaskStartResult started;
    size_t finished = r_library_internal_fs_payload_testing_cancel_reports_finished();

    r_library_internal_fs_payload_testing_pause_after_cancel_retain(1);
    r_runtime_darwin_io_testing_pause_next_stream_position_barrier();
    started = r_std_fs_read(&fixture.file, &buffer, no_deadline);
    require(started.is_ok && started.task != NULL && buffer.data == NULL,
            "late-cancelled read starts and consumes owner");
    r_runtime_darwin_io_testing_wait_for_stream_position_barrier();
    r_runtime_task_cancel(&started.task);
    require(started.task == NULL, "late task cancellation consumes the observation");
    require(wait_for_payload_test_condition(cancel_retain_reached),
            "task cancellation retains the registered storage");
    r_runtime_darwin_io_testing_release_stream_position_barrier();
    require(wait_for_unregistered_operations(storage),
            "read finishes and unregisters before its cancellation resumes");
    destroy_file(&fixture);
    r_library_internal_fs_payload_testing_pause_after_cancel_retain(0);
    require(wait_for_cancel_reports_finished(finished),
            "late cancellation finishes on the storage it retained");
}

typedef struct RTestFsPositionPumpStress {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    size_t next_activation;
    size_t callback_depth;
    size_t maximum_callback_depth;
    _Bool head_entered;
    _Bool head_released;
    _Bool order_failed;
} RTestFsPositionPumpStress;

typedef struct RTestFsPositionPumpOperation {
    _Atomic size_t references;
    RLibraryFsPositionNode node;
    RLibraryFsOperationRegistration registration;
    RLibraryFsHandleStorage *storage;
    RTestFsPositionPumpStress *stress;
    size_t index;
    RLibraryFsPositionUpdate update;
    uint64_t value;
    _Bool pause;
} RTestFsPositionPumpOperation;

static void position_pump_operation_retain(void *context) {
    RTestFsPositionPumpOperation *operation = context;
    size_t current = atomic_load_explicit(&operation->references, memory_order_relaxed);

    for (;;) {
        if (current == SIZE_MAX) {
            abort();
        }
        if (atomic_compare_exchange_weak_explicit(&operation->references,
                                                  &current,
                                                  current + 1U,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
            return;
        }
    }
}

static void position_pump_operation_release(void *context) {
    RTestFsPositionPumpOperation *operation = context;
    size_t previous = atomic_fetch_sub_explicit(&operation->references, 1U, memory_order_acq_rel);

    if (previous <= 1U) {
        abort();
    }
}

static void
position_pump_operation_cancel(void *context, RLibraryFsPositionCancelReason reason, _Bool active) {
    (void)context;
    (void)reason;
    (void)active;
    abort();
}

static void position_pump_operation_close_cancel(void *context) {
    RTestFsPositionPumpOperation *operation = context;

    r_library_internal_fs_position_cancel(&operation->node, R_LIBRARY_FS_POSITION_CANCEL_CLOSE);
}

static void position_pump_operation_activate(void *context, uint64_t position) {
    RTestFsPositionPumpOperation *operation = context;
    RTestFsPositionPumpStress *stress = operation->stress;

    if (operation->index != stress->next_activation || stress->callback_depth == SIZE_MAX ||
        stress->next_activation == SIZE_MAX) {
        stress->order_failed = 1;
    }
    stress->next_activation += 1U;
    stress->callback_depth += 1U;
    if (stress->callback_depth > stress->maximum_callback_depth) {
        stress->maximum_callback_depth = stress->callback_depth;
    }
    if (position != operation->index - (operation->index == 0U ? 0U : 1U)) {
        stress->order_failed = 1;
    }
    if (operation->pause) {
        if (pthread_mutex_lock(&stress->mutex) != 0) {
            abort();
        }
        stress->head_entered = 1;
        if (pthread_cond_broadcast(&stress->condition) != 0) {
            abort();
        }
        while (!stress->head_released) {
            if (pthread_cond_wait(&stress->condition, &stress->mutex) != 0) {
                abort();
            }
        }
        if (pthread_mutex_unlock(&stress->mutex) != 0) {
            abort();
        }
    }
    if (!r_library_internal_fs_position_activation_begin(&operation->node)) {
        abort();
    }
    r_library_internal_fs_position_activation_commit(&operation->node);
    r_library_internal_fs_position_finish(&operation->node, operation->update, operation->value);
    r_library_internal_fs_operation_unregister(operation->storage, &operation->registration);
    if (stress->callback_depth == 0U) {
        abort();
    }
    stress->callback_depth -= 1U;
}

static void position_pump_operation_reserve(RTestFsPositionPumpOperation *operation,
                                            RLibraryFsHandleStorage *storage,
                                            RTestFsPositionPumpStress *stress,
                                            size_t index,
                                            _Bool pause,
                                            int expected_descriptor) {
    RStdFsAccess access;
    int descriptor;
    _Bool append;

    (void)memset(operation, 0, sizeof(*operation));
    atomic_init(&operation->references, 1U);
    operation->storage = storage;
    operation->stress = stress;
    operation->index = index;
    operation->update = index == 0U ? R_LIBRARY_FS_POSITION_KEEP : R_LIBRARY_FS_POSITION_SET;
    operation->value = (uint64_t)index;
    operation->pause = pause;
    require(r_library_internal_fs_position_reserve(storage,
                                                   &operation->node,
                                                   &operation->registration,
                                                   position_pump_operation_activate,
                                                   position_pump_operation_cancel,
                                                   operation,
                                                   position_pump_operation_close_cancel,
                                                   position_pump_operation_retain,
                                                   position_pump_operation_release,
                                                   &descriptor,
                                                   &access,
                                                   &append),
            "reserve position pump operation");
    require(descriptor == expected_descriptor && access == R_STD_FS_ACCESS_READ_WRITE && !append,
            "position pump reservation snapshot");
}

static void *publish_position_pump_head(void *context) {
    RTestFsPositionPumpOperation *operation = context;

    r_library_internal_fs_position_publish(&operation->node);
    return NULL;
}

static void test_non_recursive_position_pump(RRuntimeAllocator *allocator) {
    const size_t seek_count = 4096U;
    RTestFsPositionPumpStress stress;
    RTestFsPositionPumpOperation *operations;
    RStdFsFileStorage *file_storage;
    RStdFsOpenFileOptions options = {
        R_STD_FS_ACCESS_READ_WRITE,
        R_STD_FS_CREATE_EXISTING,
        0,
        0,
        0,
    };
    pthread_t thread;
    size_t index;
    int descriptor;

    (void)memset(&stress, 0, sizeof(stress));
    require(pthread_mutex_init(&stress.mutex, NULL) == 0, "initialize position pump mutex");
    require(pthread_cond_init(&stress.condition, NULL) == 0, "initialize position pump condition");
    operations = calloc(seek_count + 1U, sizeof(*operations));
    require(operations != NULL, "allocate position pump operations");
    file_storage = r_library_internal_fs_file_reserve(allocator);
    require(file_storage != NULL, "reserve position pump file storage");
    descriptor = open("/dev/null", O_RDWR | O_CLOEXEC);
    require(descriptor >= 0, "open position pump descriptor");
    publish_file(allocator, file_storage, descriptor, options);

    /* The paused head models an outstanding read; all following nodes are synchronous seeks. */
    position_pump_operation_reserve(
        &operations[0], &file_storage->handle, &stress, 0U, 1, descriptor);
    require(pthread_create(&thread, NULL, publish_position_pump_head, &operations[0]) == 0,
            "publish paused position pump head");
    require(pthread_mutex_lock(&stress.mutex) == 0, "lock paused position pump head");
    while (!stress.head_entered) {
        require(pthread_cond_wait(&stress.condition, &stress.mutex) == 0,
                "wait for paused position pump head");
    }
    require(pthread_mutex_unlock(&stress.mutex) == 0, "unlock paused position pump head");

    for (index = 1U; index <= seek_count; ++index) {
        position_pump_operation_reserve(
            &operations[index], &file_storage->handle, &stress, index, 0, descriptor);
        r_library_internal_fs_position_publish(&operations[index].node);
    }
    require(stress.next_activation == 1U && stress.callback_depth == 1U,
            "synchronous seeks remain queued behind paused read");
    require(pthread_mutex_lock(&stress.mutex) == 0, "lock position pump release");
    stress.head_released = 1;
    require(pthread_cond_broadcast(&stress.condition) == 0, "release position pump head");
    require(pthread_mutex_unlock(&stress.mutex) == 0, "unlock position pump release");
    require(pthread_join(thread, NULL) == 0, "join position pump thread");

    require(!stress.order_failed && stress.next_activation == seek_count + 1U,
            "position pump preserves FIFO position sequence");
    require(stress.callback_depth == 0U && stress.maximum_callback_depth == 1U,
            "position pump keeps synchronous callback depth constant");
    for (index = 0U; index <= seek_count; ++index) {
        require(atomic_load_explicit(&operations[index].references, memory_order_acquire) == 1U,
                "position pump releases callback owners exactly once");
        require(!operations[index].registration.registered,
                "position pump unregisters every synchronous operation");
    }
    require(pthread_mutex_lock(&file_storage->handle.mutex) == 0, "lock final position pump state");
    require(file_storage->handle.position == (uint64_t)seek_count,
            "position pump applies every synchronous seek");
    require(!file_storage->handle.position_pumping, "position pump clears its reentrancy guard");
    require(pthread_mutex_unlock(&file_storage->handle.mutex) == 0,
            "unlock final position pump state");
    r_library_internal_fs_file_storage_release(file_storage);
    free(operations);
    require(pthread_cond_destroy(&stress.condition) == 0, "destroy position pump condition");
    require(pthread_mutex_destroy(&stress.mutex) == 0, "destroy position pump mutex");
}

/* R-SLIB-ASYNC-0019: the payload handle of a regular file belongs to the file payload adapter; a
   FIFO keeps a Dispatch I/O handle, so a read that waits for data holds no admission slot. */
static void test_payload_engine_follows_file_type(RRuntimeAllocator *allocator) {
    char directory[] = "/tmp/r-fs-engine-XXXXXX";
    char fifo_path[64];
    char file_path[64];
    RRuntimeDarwinIoHandleCreateResult created;
    RRuntimeDarwinIoBufferResult buffer;
    RRuntimeDarwinIoPrepareResult preparation;
    RRuntimeDarwinIoSubmitResult submission;
    RRuntimeDarwinIoResult result;
    RRuntimeDarwinIoBuffer returned;
    RRuntimeDarwinIoSubmitResult closing;
    int regular;
    int fifo;
    unsigned int spin;

    require(mkdtemp(directory) != NULL, "create engine test directory");
    (void)snprintf(fifo_path, sizeof(fifo_path), "%s/fifo", directory);
    (void)snprintf(file_path, sizeof(file_path), "%s/file", directory);
    require(mkfifo(fifo_path, 0600) == 0, "create engine test fifo");
    regular = open(file_path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    require(regular >= 0 && write(regular, "regular", 7U) == 7, "create engine test file");
    fifo = open(fifo_path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
    require(fifo >= 0, "open engine test fifo");

    created =
        r_library_internal_fs_payload_handle_create(allocator, regular, R_RUNTIME_DARWIN_IO_STREAM);
    require(created.status == R_RUNTIME_DARWIN_IO_START_OK, "regular payload handle");
    buffer = r_runtime_darwin_io_buffer_allocate(allocator, 7U);
    require(buffer.status == R_RUNTIME_DARWIN_IO_START_OK, "regular read buffer");
    r_runtime_darwin_io_testing_hold_file_transfers();
    preparation = r_runtime_darwin_io_prepare_read_some(
        created.handle, (off_t)0, &buffer.buffer, UINT64_C(0));
    require(preparation.status == R_RUNTIME_DARWIN_IO_START_OK &&
                r_runtime_darwin_io_prepared_set_stream_position(preparation.prepared, (off_t)0),
            "prepare regular read");
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer.buffer);
    require(submission.status == R_RUNTIME_DARWIN_IO_START_OK, "activate regular read");
    r_runtime_darwin_io_testing_wait_for_held_file_transfers(1U);
    require(r_runtime_darwin_io_testing_file_transfers_admitted() == 1U,
            "regular read is admitted to the file payload adapter");
    r_runtime_darwin_io_testing_release_file_transfers();
    result = r_runtime_darwin_io_request_wait(submission.request);
    require(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE &&
                result.bytes_transferred == 7U,
            "regular read completes");
    returned = r_runtime_darwin_io_request_take_buffer(submission.request);
    r_runtime_darwin_io_buffer_release(&returned);
    r_runtime_darwin_io_request_release(submission.request);
    closing = r_runtime_darwin_io_submit_close(created.handle);
    require(closing.status == R_RUNTIME_DARWIN_IO_START_OK, "close regular handle");
    (void)r_runtime_darwin_io_request_wait(closing.request);
    r_runtime_darwin_io_request_release(closing.request);
    r_runtime_darwin_io_handle_release(created.handle);

    created =
        r_library_internal_fs_payload_handle_create(allocator, fifo, R_RUNTIME_DARWIN_IO_STREAM);
    require(created.status == R_RUNTIME_DARWIN_IO_START_OK, "fifo payload handle");
    buffer = r_runtime_darwin_io_buffer_allocate(allocator, 4U);
    require(buffer.status == R_RUNTIME_DARWIN_IO_START_OK, "fifo read buffer");
    preparation = r_runtime_darwin_io_prepare_read_some(
        created.handle, (off_t)0, &buffer.buffer, UINT64_C(0));
    require(preparation.status == R_RUNTIME_DARWIN_IO_START_OK, "prepare fifo read");
    submission = r_runtime_darwin_io_prepared_activate(&preparation.prepared, &buffer.buffer);
    require(submission.status == R_RUNTIME_DARWIN_IO_START_OK, "activate fifo read");
    for (spin = 0U; spin < 20U; ++spin) {
        require(r_runtime_darwin_io_testing_file_transfers_admitted() == 0U &&
                    r_runtime_darwin_io_testing_file_transfers_waiting() == 0U,
                "fifo read takes no admission slot");
        (void)usleep(1000U);
    }
    require(r_runtime_darwin_io_request_state(submission.request) ==
                R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE,
            "fifo read waits for data");
    require(write(fifo, "fifo", 4U) == 4, "write fifo data");
    result = r_runtime_darwin_io_request_wait(submission.request);
    require(result.terminal_event == R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE &&
                result.bytes_transferred == 4U,
            "fifo read completes after data");
    returned = r_runtime_darwin_io_request_take_buffer(submission.request);
    require(memcmp(returned.data, "fifo", 4U) == 0, "fifo read data");
    r_runtime_darwin_io_buffer_release(&returned);
    r_runtime_darwin_io_request_release(submission.request);
    closing = r_runtime_darwin_io_submit_close(created.handle);
    require(closing.status == R_RUNTIME_DARWIN_IO_START_OK, "close fifo handle");
    (void)r_runtime_darwin_io_request_wait(closing.request);
    r_runtime_darwin_io_request_release(closing.request);
    r_runtime_darwin_io_handle_release(created.handle);

    require(close(fifo) == 0 && close(regular) == 0, "close engine test descriptors");
    require(unlink(fifo_path) == 0 && unlink(file_path) == 0 && rmdir(directory) == 0,
            "remove engine test files");
}

int main(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsServiceStartResult service;

    r_runtime_allocator_initialize(&allocator);
    require(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK,
            "start task executor");
    service = r_runtime_darwin_fs_service_start(&allocator, 64U);
    require(service.status == R_RUNTIME_DARWIN_FS_START_OK, "start filesystem service");
    test_ordered_position_and_owners(&allocator);
    test_partial_read_preserves_tail_and_advances_position(&allocator);
    test_append_observation_position(&allocator);
    test_noop_validation_and_start_failure(&allocator);
    test_immediate_result_precedes_executor_cancellation(&allocator);
    test_start_allocation_failure_sweep(&allocator);
    test_partial_progress_advances_position(&allocator);
    test_single_write_positive_partial(&allocator);
    test_deadline_after_partial_progress(&allocator);
    test_deadline_during_positioned_barrier(&allocator);
    test_close_during_positioned_barrier(&allocator);
    test_task_cancel_during_positioned_barrier(&allocator);
    test_native_read_after_task_cancel_selection(&allocator);
    test_task_cancel_after_deadline_selection(&allocator);
    test_late_task_cancel_outlives_file(&allocator);
    test_non_recursive_position_pump(&allocator);
    test_payload_engine_follows_file_type(&allocator);
    require(r_runtime_executor_lifecycle_stop(), "stop task executor");
    r_runtime_darwin_fs_service_stop();
    return 0;
}
