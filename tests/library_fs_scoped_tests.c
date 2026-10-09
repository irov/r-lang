#include "r_std_fs.h"

#include "r_library_fs_internal.h"
#include "r_runtime_darwin_io.h"
#include "r_runtime_task.h"
#include "r_std_time.h"

#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct RTestFsScopedFile {
    RStdFsFile file;
    char path[64];
} RTestFsScopedFile;

static void fail(const char *message) {
    (void)fprintf(stderr, "library fs scoped test failed: %s\n", message);
    exit(EXIT_FAILURE);
}

static void require(_Bool condition, const char *message) {
    if (!condition) {
        fail(message);
    }
}

static RTestFsScopedFile create_file_mode(RRuntimeAllocator *allocator,
                                          const unsigned char *bytes,
                                          size_t length,
                                          _Bool append) {
    RTestFsScopedFile fixture = {{0}, "/tmp/r-fs-scoped-XXXXXX"};
    RStdFsFileStorage *storage;
    RStdFsOpenFileOptions options = {
        R_STD_FS_ACCESS_READ_WRITE,
        R_STD_FS_CREATE_EXISTING,
        0,
        append,
        0,
    };
    RRuntimeDarwinIoHandleCreateResult io_created;
    int descriptor = mkstemp(fixture.path);

    require(descriptor >= 0, "create temporary file");
    require(ftruncate(descriptor, (off_t)0) == 0, "truncate temporary file");
    if (length != 0U) {
        require(pwrite(descriptor, bytes, length, (off_t)0) == (ssize_t)length,
                "initialize temporary file");
    }
    storage = r_library_internal_fs_file_reserve(allocator);
    require(storage != NULL, "reserve file storage");
    io_created =
        r_runtime_darwin_io_handle_create(allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    require(io_created.status == R_RUNTIME_DARWIN_IO_START_OK && io_created.handle != NULL,
            "create persistent payload root");
    r_library_internal_fs_file_publish(storage, descriptor, options, io_created.handle);
    fixture.file.storage = storage;
    return fixture;
}

static RTestFsScopedFile
create_file(RRuntimeAllocator *allocator, const unsigned char *bytes, size_t length) {
    return create_file_mode(allocator, bytes, length, 0);
}

static void destroy_file(RTestFsScopedFile *fixture) {
    r_std_fs_file_destroy(&fixture->file);
    require(unlink(fixture->path) == 0, "remove temporary file");
}

static RStdIoCountResult await_count(RStdFsTaskStartResult started) {
    RStdIoCountResult result = {0};

    require(started.is_ok && started.task != NULL, "count task start");
    require(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
            "await count task");
    return result;
}

static RStdIoVoidResult await_void(RStdFsTaskStartResult started) {
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

static void test_scoped_read_and_write(RRuntimeAllocator *allocator) {
    static const unsigned char initial[] = {'a', 'b', 'c', 'd', 'e', 'f'};
    static const uint8_t suffix[] = {'X', 'Y'};
    static const RStdFsDeadline no_deadline = {0};
    const RStdFsDeadline expired = {1, {0, 0U}};
    RTestFsScopedFile fixture = create_file(allocator, initial, sizeof(initial));
    uint8_t target[4];
    unsigned char contents[sizeof(initial)];
    RStdIoCountResult count_result;
    RStdIoVoidResult void_result;

    /* read_into shares the file position with later operations. */
    (void)memset(target, 0xaa, sizeof(target));
    count_result = await_count(
        r_std_fs_read_into(&fixture.file, (RStdFsMutableBytes){target, 3U}, no_deadline));
    require(count_result.r_tag == UINT32_C(0) && count_result.r_payload.r_value == 3U,
            "read_into progress");
    require(memcmp(target, "abc", 3U) == 0 && target[3] == UINT8_C(0xaa),
            "read_into bytes and untouched tail");

    /* write_all_from writes at the shared position and keeps the source immutable. */
    void_result = await_void(r_std_fs_write_all_from(
        &fixture.file, (RStdFsConstBytes){suffix, sizeof(suffix)}, no_deadline));
    require(void_result.r_tag == UINT32_C(0), "write_all_from complete progress");
    require(pread(r_library_internal_fs_file_descriptor(&fixture.file),
                  contents,
                  sizeof(contents),
                  (off_t)0) == (ssize_t)sizeof(contents),
            "read written file bytes");
    require(memcmp(contents, "abcXYf", sizeof(contents)) == 0,
            "write_all_from used shared position");

    /* write_from reports one positive prefix. */
    count_result = await_count(
        r_std_fs_write_from(&fixture.file, (RStdFsConstBytes){suffix, 1U}, no_deadline));
    require(count_result.r_tag == UINT32_C(0) && count_result.r_payload.r_value == 1U,
            "write_from progress");
    require(pread(r_library_internal_fs_file_descriptor(&fixture.file),
                  contents,
                  sizeof(contents),
                  (off_t)0) == (ssize_t)sizeof(contents) &&
                memcmp(contents, "abcXYX", sizeof(contents)) == 0,
            "write_from used shared position");

    /* End of file completes with count 0 and never touches target. */
    (void)memset(target, 0xcc, sizeof(target));
    count_result = await_count(r_std_fs_read_into(
        &fixture.file, (RStdFsMutableBytes){target, sizeof(target)}, no_deadline));
    require(count_result.r_tag == UINT32_C(0) && count_result.r_payload.r_value == 0U,
            "end of file is count zero");
    require(all_bytes_equal(target, sizeof(target), UINT8_C(0xcc)), "end preserves bytes");

    /* Zero-length views precede an expired deadline; a nonempty one fails before submission. */
    count_result =
        await_count(r_std_fs_read_into(&fixture.file, (RStdFsMutableBytes){target, 0U}, expired));
    require(count_result.r_tag == UINT32_C(0) && count_result.r_payload.r_value == 0U,
            "empty read_into precedes expired deadline");
    void_result =
        await_void(r_std_fs_write_all_from(&fixture.file, (RStdFsConstBytes){suffix, 0U}, expired));
    require(void_result.r_tag == UINT32_C(0), "empty write_all_from precedes expired deadline");
    count_result = await_count(
        r_std_fs_read_into(&fixture.file, (RStdFsMutableBytes){target, sizeof(target)}, expired));
    require(count_result.r_tag == UINT32_C(1) &&
                count_result.r_payload.r_error_00000001.code == R_STD_IO_ERROR_TIMED_OUT &&
                all_bytes_equal(target, sizeof(target), UINT8_C(0xcc)),
            "expired deadline fails read_into without touching target");
    void_result = await_void(r_std_fs_write_all_from(
        &fixture.file, (RStdFsConstBytes){suffix, sizeof(suffix)}, expired));
    require(void_result.r_tag == UINT32_C(1) &&
                void_result.r_payload.r_err.code == R_STD_IO_ERROR_TIMED_OUT,
            "expired deadline fails write_all_from");
    destroy_file(&fixture);
}

/* R-SLIB-FS-0015: positional forms take their offset instead of the shared position, which they
   never change; past the end a read reports end and a write leaves a hole. */
static void test_positional_read_and_write(RRuntimeAllocator *allocator) {
    static const unsigned char initial[] = {'a', 'b', 'c', 'd', 'e', 'f'};
    static const uint8_t patch[] = {'P', 'Q'};
    static const RStdFsDeadline no_deadline = {0};
    RTestFsScopedFile fixture = create_file(allocator, initial, sizeof(initial));
    RTestFsScopedFile appending;
    uint8_t target[4];
    unsigned char contents[10];
    RStdIoCountResult count_result;
    RStdIoVoidResult void_result;

    count_result = await_count(
        r_std_fs_read_into(&fixture.file, (RStdFsMutableBytes){target, 2U}, no_deadline));
    require(count_result.r_tag == UINT32_C(0) && count_result.r_payload.r_value == 2U &&
                memcmp(target, "ab", 2U) == 0,
            "shared read before positional operations");
    count_result = await_count(r_std_fs_read_at_into(
        &fixture.file, UINT64_C(4), (RStdFsMutableBytes){target, sizeof(target)}, no_deadline));
    require(count_result.r_tag == UINT32_C(0) && count_result.r_payload.r_value == 2U &&
                memcmp(target, "ef", 2U) == 0,
            "read_at_into reads at its offset");
    void_result = await_void(r_std_fs_write_all_at_from(
        &fixture.file, UINT64_C(8), (RStdFsConstBytes){patch, sizeof(patch)}, no_deadline));
    require(void_result.r_tag == UINT32_C(0), "write_all_at_from past the end");
    require(pread(r_library_internal_fs_file_descriptor(&fixture.file),
                  contents,
                  sizeof(contents),
                  (off_t)0) == (ssize_t)sizeof(contents) &&
                memcmp(contents, "abcdef\0\0PQ", sizeof(contents)) == 0,
            "write_all_at_from leaves a hole before its offset");
    count_result = await_count(
        r_std_fs_read_into(&fixture.file, (RStdFsMutableBytes){target, 2U}, no_deadline));
    require(count_result.r_tag == UINT32_C(0) && count_result.r_payload.r_value == 2U &&
                memcmp(target, "cd", 2U) == 0,
            "positional operations keep the shared position");
    count_result = await_count(r_std_fs_read_at_into(
        &fixture.file, UINT64_C(100), (RStdFsMutableBytes){target, sizeof(target)}, no_deadline));
    require(count_result.r_tag == UINT32_C(0) && count_result.r_payload.r_value == 0U,
            "read_at_into past the end reports end");
    count_result = await_count(r_std_fs_read_at_into(
        &fixture.file, UINT64_MAX, (RStdFsMutableBytes){target, sizeof(target)}, no_deadline));
    require(count_result.r_tag == UINT32_C(1) &&
                count_result.r_payload.r_error_00000001.code == R_STD_IO_ERROR_INVALID_OPERATION,
            "an offset beyond i64 is invalid_operation");
    destroy_file(&fixture);

    /* An append-mode file reads at an offset but refuses a positional write. */
    appending = create_file_mode(allocator, initial, sizeof(initial), 1);
    count_result = await_count(r_std_fs_read_at_into(
        &appending.file, UINT64_C(1), (RStdFsMutableBytes){target, 2U}, no_deadline));
    require(count_result.r_tag == UINT32_C(0) && count_result.r_payload.r_value == 2U &&
                memcmp(target, "bc", 2U) == 0,
            "read_at_into on an append-mode file");
    void_result = await_void(r_std_fs_write_all_at_from(
        &appending.file, UINT64_C(0), (RStdFsConstBytes){patch, sizeof(patch)}, no_deadline));
    require(void_result.r_tag == UINT32_C(1) &&
                void_result.r_payload.r_err.code == R_STD_IO_ERROR_INVALID_OPERATION,
            "write_all_at_from on an append-mode file is invalid_operation");
    destroy_file(&appending);
}

/* A second open file description of the fixture's file, with the given access. */
static RStdFsFile open_again(RRuntimeAllocator *allocator, const char *path, RStdFsAccess access) {
    RStdFsFile file = {0};
    RStdFsFileStorage *storage;
    RStdFsOpenFileOptions options = {access, R_STD_FS_CREATE_EXISTING, 0, 0, 0};
    RRuntimeDarwinIoHandleCreateResult io_created;
    int flags = access == R_STD_FS_ACCESS_READ ? O_RDONLY : O_RDWR;
    int descriptor = open(path, flags | O_CLOEXEC);

    require(descriptor >= 0, "open the file again");
    storage = r_library_internal_fs_file_reserve(allocator);
    require(storage != NULL, "reserve second file storage");
    io_created =
        r_runtime_darwin_io_handle_create(allocator, descriptor, R_RUNTIME_DARWIN_IO_STREAM);
    require(io_created.status == R_RUNTIME_DARWIN_IO_START_OK && io_created.handle != NULL,
            "create second payload root");
    r_library_internal_fs_file_publish(storage, descriptor, options, io_created.handle);
    file.storage = storage;
    return file;
}

static RStdFsBoolResult await_fs_bool(RStdFsTaskStartResult started) {
    RStdFsBoolResult result = {0};

    require(started.is_ok && started.task != NULL, "bool task start");
    require(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
            "await bool task");
    return result;
}

static RStdFsVoidResult await_fs_void(RStdFsTaskStartResult started) {
    RStdFsVoidResult result = {0};

    require(started.is_ok && started.task != NULL, "fs void task start");
    require(r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK,
            "await fs void task");
    return result;
}

static RStdFsDeadline deadline_after_milliseconds(uint32_t milliseconds) {
    RStdTimeInstantResult now = r_std_time_monotonic_now();
    RStdFsDeadline deadline = {1, {0, 0U}};
    uint64_t nanoseconds;

    require(now.is_ok, "read the monotonic clock");
    nanoseconds =
        (uint64_t)now.value.storage_nanoseconds + (uint64_t)milliseconds * UINT64_C(1000000);
    deadline.value.storage_seconds =
        now.value.storage_seconds + (int64_t)(nanoseconds / UINT64_C(1000000000));
    deadline.value.storage_nanoseconds = (uint32_t)(nanoseconds % UINT64_C(1000000000));
    return deadline;
}

/* R-SLIB-FS-0016: advisory locks belong to the open file description: a second open of the same
   file conflicts with the first; lock retries until the conflicting lock goes, the deadline
   passes or the task is cancelled. */
static void test_locks(RRuntimeAllocator *allocator) {
    static const unsigned char initial[] = {'l', 'o', 'c', 'k'};
    static const RStdFsDeadline no_deadline = {0};
    RTestFsScopedFile fixture = create_file(allocator, initial, sizeof(initial));
    RStdFsFile other = open_again(allocator, fixture.path, R_STD_FS_ACCESS_READ_WRITE);
    RStdFsFile reader = open_again(allocator, fixture.path, R_STD_FS_ACCESS_READ);
    RStdFsBoolResult bool_result;
    RStdFsVoidResult void_result;
    RStdFsTaskStartResult waiting;

    bool_result = await_fs_bool(r_std_fs_try_lock(
        &fixture.file, R_STD_FS_LOCK_KIND_EXCLUSIVE, UINT64_C(0), UINT64_C(0), no_deadline));
    require(bool_result.r_tag == UINT32_C(0) && bool_result.r_payload.r_ok, "exclusive try_lock");
    bool_result = await_fs_bool(r_std_fs_try_lock(
        &other, R_STD_FS_LOCK_KIND_SHARED, UINT64_C(0), UINT64_C(10), no_deadline));
    require(bool_result.r_tag == UINT32_C(0) && !bool_result.r_payload.r_ok,
            "a second open file sees the conflict");
    void_result = await_fs_void(r_std_fs_lock(&other,
                                              R_STD_FS_LOCK_KIND_SHARED,
                                              UINT64_C(0),
                                              UINT64_C(10),
                                              deadline_after_milliseconds(60U)));
    require(void_result.r_tag == UINT32_C(1) &&
                void_result.r_payload.r_err.code == R_STD_FS_ERROR_TIMED_OUT,
            "lock times out while the conflict lasts");

    /* A waiting lock takes the range once the holder unlocks it. */
    waiting =
        r_std_fs_lock(&other, R_STD_FS_LOCK_KIND_EXCLUSIVE, UINT64_C(0), UINT64_C(0), no_deadline);
    require(waiting.is_ok && waiting.task != NULL, "start a waiting lock");
    (void)usleep(20000U);
    void_result =
        await_fs_void(r_std_fs_unlock(&fixture.file, UINT64_C(0), UINT64_C(0), no_deadline));
    require(void_result.r_tag == UINT32_C(0), "unlock");
    void_result = await_fs_void(waiting);
    require(void_result.r_tag == UINT32_C(0), "the waiting lock acquires after unlock");
    bool_result = await_fs_bool(r_std_fs_try_lock(
        &fixture.file, R_STD_FS_LOCK_KIND_SHARED, UINT64_C(2), UINT64_C(1), no_deadline));
    require(bool_result.r_tag == UINT32_C(0) && !bool_result.r_payload.r_ok,
            "the waiting lock now holds the file");

    /* A cancelled waiting lock stops retrying: once the holder unlocks, the lock it waited for
       is never taken, so a third open file can still lock the range. */
    waiting = r_std_fs_lock(
        &fixture.file, R_STD_FS_LOCK_KIND_EXCLUSIVE, UINT64_C(0), UINT64_C(0), no_deadline);
    require(waiting.is_ok && waiting.task != NULL, "start a lock to cancel");
    (void)usleep(20000U);
    r_runtime_task_cancel(&waiting.task);
    require(waiting.task == NULL, "cancel consumes the task");
    (void)usleep(100000U);
    void_result = await_fs_void(r_std_fs_unlock(&other, UINT64_C(0), UINT64_C(0), no_deadline));
    require(void_result.r_tag == UINT32_C(0), "unlock the second file");
    (void)usleep(100000U);
    bool_result = await_fs_bool(r_std_fs_try_lock(
        &reader, R_STD_FS_LOCK_KIND_SHARED, UINT64_C(0), UINT64_C(0), no_deadline));
    require(bool_result.r_tag == UINT32_C(0) && bool_result.r_payload.r_ok,
            "the cancelled lock was never taken");
    bool_result = await_fs_bool(r_std_fs_try_lock(
        &fixture.file, R_STD_FS_LOCK_KIND_SHARED, UINT64_C(0), UINT64_C(0), no_deadline));
    require(bool_result.r_tag == UINT32_C(0) && bool_result.r_payload.r_ok,
            "shared locks of two files coexist");

    /* Shared locks need read access and exclusive locks write access; a range past i64 is
       invalid. */
    bool_result = await_fs_bool(r_std_fs_try_lock(
        &reader, R_STD_FS_LOCK_KIND_EXCLUSIVE, UINT64_C(0), UINT64_C(0), no_deadline));
    require(bool_result.r_tag == UINT32_C(1) &&
                bool_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_OPERATION,
            "an exclusive lock needs write access");
    bool_result = await_fs_bool(r_std_fs_try_lock(
        &fixture.file, R_STD_FS_LOCK_KIND_SHARED, (uint64_t)INT64_MAX, UINT64_C(2), no_deadline));
    require(bool_result.r_tag == UINT32_C(1) &&
                bool_result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_OPERATION,
            "a range past i64 is invalid_operation");
    r_std_fs_file_destroy(&reader);
    r_std_fs_file_destroy(&other);
    destroy_file(&fixture);
}

/* B7-6: the task of a lock is cancelled while the lane worker holds its attempt before the native
   call. The cancellation reaches the attempt, which completes when the worker resumes; until
   then the lock's position turn is active, so the cancellation must not finish the operation
   under it (it aborted in r_library_internal_fs_operation_unregister). */
static void test_lock_cancel_during_attempt(RRuntimeAllocator *allocator) {
    static const unsigned char initial[] = {'l', 'o', 'c', 'k'};
    static const RStdFsDeadline no_deadline = {0};
    RTestFsScopedFile fixture = create_file(allocator, initial, sizeof(initial));
    RStdFsFile other = open_again(allocator, fixture.path, R_STD_FS_ACCESS_READ_WRITE);
    RStdFsFile reader = open_again(allocator, fixture.path, R_STD_FS_ACCESS_READ);
    RStdFsBoolResult bool_result;
    RStdFsVoidResult void_result;
    RStdFsTaskStartResult waiting;
    uint64_t entry_sequence;
    uint64_t signal_count;

    bool_result = await_fs_bool(r_std_fs_try_lock(
        &fixture.file, R_STD_FS_LOCK_KIND_EXCLUSIVE, UINT64_C(0), UINT64_C(0), no_deadline));
    require(bool_result.r_tag == UINT32_C(0) && bool_result.r_payload.r_ok, "hold the file");
    entry_sequence = r_runtime_darwin_fs_service_testing_entry_sequence();
    signal_count = r_runtime_darwin_fs_service_testing_signal_count();
    r_runtime_darwin_fs_service_testing_pause_before_native(1);
    waiting =
        r_std_fs_lock(&other, R_STD_FS_LOCK_KIND_EXCLUSIVE, UINT64_C(0), UINT64_C(0), no_deadline);
    require(waiting.is_ok && waiting.task != NULL, "start a lock whose attempt waits");
    while (r_runtime_darwin_fs_service_testing_entry_sequence() == entry_sequence) {
        (void)sched_yield();
    }
    r_runtime_task_cancel(&waiting.task);
    require(waiting.task == NULL, "cancel consumes the task");
    while (r_runtime_darwin_fs_service_testing_signal_count() == signal_count) {
        (void)sched_yield();
    }
    /* The cancellation callback runs to its end while the attempt is still held. */
    (void)usleep(50000U);
    r_runtime_darwin_fs_service_testing_pause_before_native(0);
    (void)usleep(50000U);
    void_result =
        await_fs_void(r_std_fs_unlock(&fixture.file, UINT64_C(0), UINT64_C(0), no_deadline));
    require(void_result.r_tag == UINT32_C(0), "unlock the holder");
    bool_result = await_fs_bool(r_std_fs_try_lock(
        &reader, R_STD_FS_LOCK_KIND_SHARED, UINT64_C(0), UINT64_C(0), no_deadline));
    require(bool_result.r_tag == UINT32_C(0) && bool_result.r_payload.r_ok,
            "the cancelled lock was never taken");
    r_std_fs_file_destroy(&reader);
    r_std_fs_file_destroy(&other);
    destroy_file(&fixture);
}

int main(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsServiceStartResult service;

    r_runtime_allocator_initialize(&allocator);
    require(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK,
            "start task executor");
    service = r_runtime_darwin_fs_service_start(&allocator, 64U);
    require(service.status == R_RUNTIME_DARWIN_FS_START_OK, "start filesystem service");
    test_scoped_read_and_write(&allocator);
    test_positional_read_and_write(&allocator);
    test_locks(&allocator);
    test_lock_cancel_during_attempt(&allocator);
    require(r_runtime_executor_lifecycle_stop(), "stop task executor");
    r_runtime_darwin_fs_service_stop();
    (void)fprintf(stdout, "library_fs_scoped_tests: ok\n");
    return 0;
}
