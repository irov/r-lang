#include "r_std_fs.h"

#include "r_library_fs_internal.h"
#include "r_runtime_array.h"
#include "r_runtime_darwin_fs_lane.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

static RStdStringView test_view(const char *text) {
    return (RStdStringView){(const uint8_t *)text, strlen(text)};
}

static RStdFsPathResult test_path(RRuntimeAllocator *allocator, const char *text) {
    return r_std_fs_path_from_utf8(allocator, test_view(text));
}

static RRuntimeArray byte_array(RRuntimeAllocator *allocator, const uint8_t *bytes, size_t length) {
    const RRuntimeTypeInfo byte_type = {
        sizeof(uint8_t),
        _Alignof(uint8_t),
        NULL,
        NULL,
    };
    RRuntimeArray array;

    if (r_runtime_array_with_capacity(&array, allocator, byte_type, length) != R_RUNTIME_ARRAY_OK) {
        (void)memset(&array, 0, sizeof(array));
        return array;
    }
    array.length = length;
    if (length != 0U) {
        memcpy(array.data, bytes, length);
    }
    return array;
}

static int await_write_file(RStdFsTaskStartResult started, RStdFsWriteFileResult *result) {
    if (!started.is_ok || started.task == NULL) {
        return 0;
    }
    return r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK;
}

static int await_directory(RStdFsTaskStartResult started, RStdFsDirectoryResult *result) {
    if (!started.is_ok || started.task == NULL) {
        return 0;
    }
    return r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK;
}

static int
read_exact_at(int directory, const char *name, const uint8_t *expected, size_t expected_length) {
    uint8_t buffer[256];
    int descriptor;
    ssize_t count;

    if (expected_length > sizeof(buffer)) {
        return 0;
    }
    descriptor = openat(directory, name, O_RDONLY | O_CLOEXEC);
    if (descriptor < 0) {
        return 0;
    }
    count = read(descriptor, buffer, sizeof(buffer));
    if (close(descriptor) != 0 || count < 0 || (size_t)count != expected_length) {
        return 0;
    }
    return expected_length == 0U || memcmp(buffer, expected, expected_length) == 0;
}

static int write_exact_at(int directory, const char *name, const uint8_t *bytes, size_t length) {
    int descriptor =
        openat(directory, name, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, S_IRUSR | S_IWUSR);
    size_t offset = 0U;

    if (descriptor < 0) {
        return 0;
    }
    while (offset != length) {
        ssize_t written = write(descriptor, bytes + offset, length - offset);

        if (written <= 0) {
            (void)close(descriptor);
            return 0;
        }
        offset += (size_t)written;
    }
    return close(descriptor) == 0;
}

static int no_staging_entries(const char *directory_path) {
    DIR *directory = opendir(directory_path);
    struct dirent *entry;
    int clean = 1;

    if (directory == NULL) {
        return 0;
    }
    while ((entry = readdir(directory)) != NULL) {
        if (strncmp(entry->d_name,
                    R_RUNTIME_DARWIN_FS_STAGING_PREFIX,
                    sizeof(R_RUNTIME_DARWIN_FS_STAGING_PREFIX) - 1U) == 0) {
            clean = 0;
            break;
        }
    }
    if (closedir(directory) != 0) {
        return 0;
    }
    return clean;
}

static void sleep_milliseconds(long milliseconds) {
    struct timespec delay;
    struct timespec remaining;

    delay.tv_sec = milliseconds / 1000L;
    delay.tv_nsec = (milliseconds % 1000L) * 1000000L;
    while (nanosleep(&delay, &remaining) != 0 && errno == EINTR) {
        delay = remaining;
    }
}

static int wait_for_staging_reached(void) {
    size_t attempt;

    for (attempt = 0U; attempt != 5000U; ++attempt) {
        if (r_runtime_darwin_fs_testing_staging_reached()) {
            return 1;
        }
        sleep_milliseconds(1L);
    }
    return 0;
}

static int wait_for_cleanup_count(uint64_t minimum) {
    size_t attempt;

    for (attempt = 0U; attempt != 5000U; ++attempt) {
        if (r_runtime_darwin_fs_testing_staging_cleanup_count() >= minimum) {
            return 1;
        }
        sleep_milliseconds(1L);
    }
    return 0;
}

static int wait_for_condition(_Bool (*condition)(void)) {
    size_t attempt;

    for (attempt = 0U; attempt != 5000U; ++attempt) {
        if (condition()) {
            return 1;
        }
        sleep_milliseconds(1L);
    }
    return 0;
}

static int wait_for_atomic_count(uint64_t (*count)(void), uint64_t minimum) {
    size_t attempt;

    for (attempt = 0U; attempt != 5000U; ++attempt) {
        if (count() >= minimum) {
            return 1;
        }
        sleep_milliseconds(1L);
    }
    return 0;
}

static int wait_for_service_signal_count(uint64_t minimum) {
    size_t attempt;

    for (attempt = 0U; attempt != 5000U; ++attempt) {
        if (r_runtime_darwin_fs_service_testing_signal_count() >= minimum) {
            return 1;
        }
        sleep_milliseconds(1L);
    }
    return 0;
}

static int deadline_after_milliseconds(uint32_t milliseconds, RStdFsDeadline *deadline) {
    RStdTimeInstantResult now = r_std_time_monotonic_now();
    RStdTimeDurationResult delay =
        r_std_time_duration_from_parts((int64_t)(milliseconds / UINT32_C(1000)),
                                       (milliseconds % UINT32_C(1000)) * UINT32_C(1000000));
    RStdTimeInstantResult future;

    if (deadline == NULL || !now.is_ok || !delay.is_ok) {
        return 0;
    }
    future = r_std_time_instant_add(now.value, delay.value);
    if (!future.is_ok) {
        return 0;
    }
    *deadline = (RStdFsDeadline){1, future.value};
    return 1;
}

static int
test_ordinary_success(RRuntimeAllocator *allocator, int root_descriptor, const char *root_path) {
    static const uint8_t bytes[] = {UINT8_C(0x52), UINT8_C(0x00), UINT8_C(0xff)};
    const RStdFsDeadline no_deadline = {0};
    char destination[1024];
    RStdFsPathResult path;
    RRuntimeArray data = byte_array(allocator, bytes, sizeof(bytes));
    void *owner = data.data;
    RStdFsWriteFileResult result = {0};
    uint64_t cleanup_before = r_runtime_darwin_fs_testing_staging_cleanup_count();

    R_TEST_CHECK(data.allocator != NULL);
    R_TEST_CHECK(snprintf(destination, sizeof(destination), "%s/ordinary", root_path) > 0);
    path = test_path(allocator, destination);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(await_write_file(
        r_std_fs_write_file_atomic_no_replace(&path.value, &data, no_deadline), &result));
    R_TEST_CHECK(data.data == NULL && data.allocator == NULL);
    R_TEST_CHECK(result.kind == R_STD_FS_WRITE_FILE_COMMITTED && result.data.data == owner);
    R_TEST_CHECK(result.data.length == sizeof(bytes) &&
                 memcmp(result.data.data, bytes, sizeof(bytes)) == 0);
    R_TEST_CHECK(read_exact_at(root_descriptor, "ordinary", bytes, sizeof(bytes)));
    R_TEST_CHECK(r_runtime_darwin_fs_testing_staging_cleanup_count() ==
                 cleanup_before + UINT64_C(1));
    R_TEST_CHECK(no_staging_entries(root_path));
    r_std_fs_write_file_result_destroy(&result);
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_existing_destination(RRuntimeAllocator *allocator,
                                     int root_descriptor,
                                     const char *root_path) {
    static const uint8_t original[] = {UINT8_C(0x11), UINT8_C(0x22)};
    static const uint8_t replacement[] = {UINT8_C(0xaa), UINT8_C(0xbb), UINT8_C(0xcc)};
    const RStdFsDeadline no_deadline = {0};
    char destination[1024];
    RStdFsPathResult path;
    RRuntimeArray data = byte_array(allocator, replacement, sizeof(replacement));
    void *owner = data.data;
    RStdFsWriteFileResult result = {0};
    uint64_t cleanup_before = r_runtime_darwin_fs_testing_staging_cleanup_count();

    R_TEST_CHECK(write_exact_at(root_descriptor, "existing", original, sizeof(original)));
    R_TEST_CHECK(snprintf(destination, sizeof(destination), "%s/existing", root_path) > 0);
    path = test_path(allocator, destination);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(await_write_file(
        r_std_fs_write_file_atomic_no_replace(&path.value, &data, no_deadline), &result));
    R_TEST_CHECK(result.kind == R_STD_FS_WRITE_FILE_FAILED &&
                 result.error.code == R_STD_FS_ERROR_ALREADY_EXISTS);
    R_TEST_CHECK(result.data.data == owner && result.data.length == sizeof(replacement));
    R_TEST_CHECK(memcmp(result.data.data, replacement, sizeof(replacement)) == 0);
    R_TEST_CHECK(read_exact_at(root_descriptor, "existing", original, sizeof(original)));
    R_TEST_CHECK(r_runtime_darwin_fs_testing_staging_cleanup_count() ==
                 cleanup_before + UINT64_C(1));
    R_TEST_CHECK(no_staging_entries(root_path));
    r_std_fs_write_file_result_destroy(&result);
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int
test_empty_file(RRuntimeAllocator *allocator, int root_descriptor, const char *root_path) {
    const RStdFsDeadline no_deadline = {0};
    char destination[1024];
    RStdFsPathResult path;
    RRuntimeArray data = byte_array(allocator, NULL, 0U);
    RStdFsWriteFileResult result = {0};

    R_TEST_CHECK(data.allocator != NULL);
    R_TEST_CHECK(snprintf(destination, sizeof(destination), "%s/empty", root_path) > 0);
    path = test_path(allocator, destination);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(await_write_file(
        r_std_fs_write_file_atomic_no_replace(&path.value, &data, no_deadline), &result));
    R_TEST_CHECK(result.kind == R_STD_FS_WRITE_FILE_COMMITTED && result.data.length == 0U &&
                 result.data.allocator == allocator);
    R_TEST_CHECK(read_exact_at(root_descriptor, "empty", NULL, 0U));
    R_TEST_CHECK(no_staging_entries(root_path));
    r_std_fs_write_file_result_destroy(&result);
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_beneath(RRuntimeAllocator *allocator, int root_descriptor, const char *root_path) {
    static const uint8_t bytes[] = {UINT8_C(0x42), UINT8_C(0x43)};
    static const uint8_t outside[] = {UINT8_C(0x71)};
    const RStdFsDeadline no_deadline = {0};
    RStdFsPathResult root_path_value = test_path(allocator, root_path);
    RStdFsDirectoryResult directory_result = {0};
    RStdFsDirectory root = {0};
    RStdFsPathResult relative;
    RRuntimeArray data;
    RStdFsWriteFileResult result = {0};
    void *owner;

    R_TEST_CHECK(root_path_value.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(await_directory(r_std_fs_open_directory(&root_path_value.value, no_deadline),
                                 &directory_result));
    R_TEST_CHECK(directory_result.r_tag == UINT32_C(0));
    root = directory_result.r_payload.r_ok;
    directory_result.r_payload.r_ok.storage = NULL;
    r_std_fs_path_destroy(&root_path_value.value);

    R_TEST_CHECK(mkdirat(root_descriptor, "child", S_IRWXU) == 0);
    relative = test_path(allocator, "child/nested");
    data = byte_array(allocator, bytes, sizeof(bytes));
    owner = data.data;
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(await_write_file(
        r_std_fs_write_file_atomic_no_replace_beneath(&root, &relative.value, &data, no_deadline),
        &result));
    R_TEST_CHECK(result.kind == R_STD_FS_WRITE_FILE_COMMITTED && result.data.data == owner);
    R_TEST_CHECK(read_exact_at(root_descriptor, "child/nested", bytes, sizeof(bytes)));
    r_std_fs_write_file_result_destroy(&result);
    r_std_fs_path_destroy(&relative.value);

    R_TEST_CHECK(write_exact_at(root_descriptor, "outside", outside, sizeof(outside)));
    R_TEST_CHECK(symlinkat("outside", root_descriptor, "final-link") == 0);
    relative = test_path(allocator, "final-link");
    data = byte_array(allocator, bytes, sizeof(bytes));
    owner = data.data;
    (void)memset(&result, 0, sizeof(result));
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(await_write_file(
        r_std_fs_write_file_atomic_no_replace_beneath(&root, &relative.value, &data, no_deadline),
        &result));
    R_TEST_CHECK(result.kind == R_STD_FS_WRITE_FILE_FAILED &&
                 result.error.code == R_STD_FS_ERROR_ALREADY_EXISTS && result.data.data == owner);
    R_TEST_CHECK(read_exact_at(root_descriptor, "outside", outside, sizeof(outside)));
    R_TEST_CHECK(no_staging_entries(root_path));
    r_std_fs_write_file_result_destroy(&result);
    r_std_fs_path_destroy(&relative.value);

    R_TEST_CHECK(symlinkat("child", root_descriptor, "intermediate-link") == 0);
    relative = test_path(allocator, "intermediate-link/escape");
    data = byte_array(allocator, bytes, sizeof(bytes));
    owner = data.data;
    (void)memset(&result, 0, sizeof(result));
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(await_write_file(
        r_std_fs_write_file_atomic_no_replace_beneath(&root, &relative.value, &data, no_deadline),
        &result));
    R_TEST_CHECK(result.kind == R_STD_FS_WRITE_FILE_FAILED &&
                 result.error.code == R_STD_FS_ERROR_INVALID_RELATIVE_PATH &&
                 result.data.data == owner);
    R_TEST_CHECK(faccessat(root_descriptor, "child/escape", F_OK, AT_SYMLINK_NOFOLLOW) != 0);
    R_TEST_CHECK(no_staging_entries(root_path));
    r_std_fs_write_file_result_destroy(&result);
    r_std_fs_path_destroy(&relative.value);

    relative = test_path(allocator, "child/retained");
    data = byte_array(allocator, bytes, sizeof(bytes));
    owner = data.data;
    (void)memset(&result, 0, sizeof(result));
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    r_runtime_darwin_fs_testing_pause_after_staging(1);
    {
        RStdFsTaskStartResult started = r_std_fs_write_file_atomic_no_replace_beneath(
            &root, &relative.value, &data, no_deadline);

        R_TEST_CHECK(started.is_ok && started.task != NULL && data.data == NULL);
        R_TEST_CHECK(wait_for_staging_reached());
        r_std_fs_directory_destroy(&root);
        r_runtime_darwin_fs_testing_pause_after_staging(0);
        R_TEST_CHECK(await_write_file(started, &result));
    }
    R_TEST_CHECK(result.kind == R_STD_FS_WRITE_FILE_COMMITTED && result.data.data == owner);
    R_TEST_CHECK(read_exact_at(root_descriptor, "child/retained", bytes, sizeof(bytes)) &&
                 no_staging_entries(root_path));
    r_std_fs_write_file_result_destroy(&result);
    r_std_fs_path_destroy(&relative.value);
    return 0;
}

static int test_start_failure_preserves_owner(RRuntimeAllocator *allocator, const char *root_path) {
    static const uint8_t bytes[] = {UINT8_C(0xde), UINT8_C(0xad)};
    const RStdFsDeadline no_deadline = {0};
    char destination[1024];
    RStdFsPathResult path;
    RRuntimeArray data = byte_array(allocator, bytes, sizeof(bytes));
    RRuntimeArray snapshot = data;
    RStdFsTaskStartResult started;

    R_TEST_CHECK(snprintf(destination, sizeof(destination), "%s/start-failure", root_path) > 0);
    path = test_path(allocator, destination);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    started = r_std_fs_write_file_atomic_no_replace(&path.value, &data, no_deadline);
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                 started.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
    R_TEST_CHECK(memcmp(&data, &snapshot, sizeof(data)) == 0 &&
                 memcmp(data.data, bytes, sizeof(bytes)) == 0);
    r_runtime_array_destroy(&data);
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_staging_collision_failure(RRuntimeAllocator *allocator, const char *root_path) {
    static const uint8_t bytes[] = {UINT8_C(0xc0), UINT8_C(0x11)};
    const RStdFsDeadline no_deadline = {0};
    char destination[1024];
    RStdFsPathResult path;
    RRuntimeArray data = byte_array(allocator, bytes, sizeof(bytes));
    void *owner = data.data;
    RStdFsWriteFileResult result = {0};

    R_TEST_CHECK(snprintf(destination, sizeof(destination), "%s/collision", root_path) > 0);
    path = test_path(allocator, destination);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    r_runtime_darwin_fs_testing_force_staging_collisions(R_RUNTIME_DARWIN_FS_CREATE_RETRY_LIMIT);
    R_TEST_CHECK(await_write_file(
        r_std_fs_write_file_atomic_no_replace(&path.value, &data, no_deadline), &result));
    R_TEST_CHECK(result.kind == R_STD_FS_WRITE_FILE_FAILED &&
                 result.error.code == R_STD_FS_ERROR_RESOURCE_EXHAUSTED &&
                 result.error.native_code == EAGAIN && result.data.data == owner);
    R_TEST_CHECK(no_staging_entries(root_path));
    r_std_fs_write_file_result_destroy(&result);
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_cancel_during_stage(RRuntimeAllocator *allocator, const char *root_path) {
    static const uint8_t bytes[] = {UINT8_C(0xca), UINT8_C(0xfe)};
    const RStdFsDeadline no_deadline = {0};
    char destination[1024];
    RStdFsPathResult path;
    RRuntimeArray data = byte_array(allocator, bytes, sizeof(bytes));
    RStdFsTaskStartResult started;
    uint64_t cleanup_before;

    R_TEST_CHECK(snprintf(destination, sizeof(destination), "%s/cancelled", root_path) > 0);
    path = test_path(allocator, destination);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    cleanup_before = r_runtime_darwin_fs_testing_staging_cleanup_count();
    r_runtime_darwin_fs_testing_pause_after_staging(1);
    started = r_std_fs_write_file_atomic_no_replace(&path.value, &data, no_deadline);
    R_TEST_CHECK(started.is_ok && started.task != NULL && data.data == NULL);
    R_TEST_CHECK(wait_for_staging_reached());
    r_runtime_task_cancel(&started.task);
    R_TEST_CHECK(started.task == NULL);
    r_runtime_darwin_fs_testing_pause_after_staging(0);
    R_TEST_CHECK(wait_for_cleanup_count(cleanup_before + UINT64_C(1)));
    R_TEST_CHECK(faccessat(AT_FDCWD, destination, F_OK, AT_SYMLINK_NOFOLLOW) != 0 &&
                 no_staging_entries(root_path));
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_deadline_during_stage(RRuntimeAllocator *allocator, const char *root_path) {
    static const uint8_t bytes[] = {UINT8_C(0xde), UINT8_C(0xad), UINT8_C(0x11)};
    char destination[1024];
    RStdFsPathResult path;
    RRuntimeArray data = byte_array(allocator, bytes, sizeof(bytes));
    void *owner = data.data;
    RStdFsDeadline deadline;
    RStdFsTaskStartResult started;
    RStdFsWriteFileResult result = {0};

    R_TEST_CHECK(snprintf(destination, sizeof(destination), "%s/deadline", root_path) > 0);
    path = test_path(allocator, destination);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(deadline_after_milliseconds(UINT32_C(50), &deadline));
    r_runtime_darwin_fs_testing_pause_after_staging(1);
    started = r_std_fs_write_file_atomic_no_replace(&path.value, &data, deadline);
    R_TEST_CHECK(started.is_ok && started.task != NULL && data.data == NULL);
    R_TEST_CHECK(wait_for_staging_reached());
    sleep_milliseconds(100L);
    r_runtime_darwin_fs_testing_pause_after_staging(0);
    R_TEST_CHECK(await_write_file(started, &result));
    R_TEST_CHECK(result.kind == R_STD_FS_WRITE_FILE_FAILED &&
                 result.error.code == R_STD_FS_ERROR_TIMED_OUT && result.data.data == owner);
    R_TEST_CHECK(faccessat(AT_FDCWD, destination, F_OK, AT_SYMLINK_NOFOLLOW) != 0 &&
                 no_staging_entries(root_path));
    r_std_fs_write_file_result_destroy(&result);
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_cancel_queued_publication(RRuntimeAllocator *allocator, const char *root_path) {
    static const uint8_t bytes[] = {UINT8_C(0x41), UINT8_C(0x42), UINT8_C(0x43)};
    const RStdFsDeadline no_deadline = {0};
    char destination[1024];
    RStdFsPathResult path;
    RRuntimeArray data = byte_array(allocator, bytes, sizeof(bytes));
    RStdFsTaskStartResult started;
    uint64_t cleanup_before;
    uint64_t signal_before;

    R_TEST_CHECK(snprintf(destination, sizeof(destination), "%s/queued-cancel", root_path) > 0);
    path = test_path(allocator, destination);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    cleanup_before = r_runtime_darwin_fs_testing_staging_cleanup_count();
    r_library_internal_fs_atomic_write_testing_reset();
    r_library_internal_fs_atomic_write_testing_pause_before_publication_activation(1);
    started = r_std_fs_write_file_atomic_no_replace(&path.value, &data, no_deadline);
    R_TEST_CHECK(started.is_ok && started.task != NULL && data.data == NULL);
    R_TEST_CHECK(wait_for_condition(
        r_library_internal_fs_atomic_write_testing_publication_activation_reached));
    r_runtime_darwin_fs_service_testing_pause_dequeue(1);
    r_library_internal_fs_atomic_write_testing_pause_before_publication_activation(0);
    R_TEST_CHECK(wait_for_condition(
        r_library_internal_fs_atomic_write_testing_publication_activation_observed));
    signal_before = r_runtime_darwin_fs_service_testing_signal_count();
    r_runtime_task_cancel(&started.task);
    R_TEST_CHECK(started.task == NULL);
    R_TEST_CHECK(wait_for_service_signal_count(signal_before + UINT64_C(1)));
    r_runtime_darwin_fs_service_testing_pause_dequeue(0);
    R_TEST_CHECK(wait_for_cleanup_count(cleanup_before + UINT64_C(1)));
    R_TEST_CHECK(r_runtime_darwin_fs_testing_staging_cleanup_count() ==
                 cleanup_before + UINT64_C(1));
    R_TEST_CHECK(faccessat(AT_FDCWD, destination, F_OK, AT_SYMLINK_NOFOLLOW) != 0 &&
                 no_staging_entries(root_path));
    r_library_internal_fs_atomic_write_testing_reset();
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_deadline_queued_publication(RRuntimeAllocator *allocator, const char *root_path) {
    static const uint8_t bytes[] = {UINT8_C(0x51), UINT8_C(0x52), UINT8_C(0x53)};
    char destination[1024];
    RStdFsPathResult path;
    RRuntimeArray data = byte_array(allocator, bytes, sizeof(bytes));
    void *owner = data.data;
    RStdFsDeadline deadline;
    RStdFsTaskStartResult started;
    RStdFsWriteFileResult result = {0};
    uint64_t cleanup_before;
    uint64_t signal_before;

    R_TEST_CHECK(snprintf(destination, sizeof(destination), "%s/queued-deadline", root_path) > 0);
    path = test_path(allocator, destination);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(deadline_after_milliseconds(UINT32_C(10000), &deadline));
    cleanup_before = r_runtime_darwin_fs_testing_staging_cleanup_count();
    r_library_internal_fs_atomic_write_testing_reset();
    r_library_internal_fs_atomic_write_testing_pause_before_publication_activation(1);
    started = r_std_fs_write_file_atomic_no_replace(&path.value, &data, deadline);
    R_TEST_CHECK(started.is_ok && started.task != NULL && data.data == NULL);
    R_TEST_CHECK(wait_for_condition(
        r_library_internal_fs_atomic_write_testing_publication_activation_reached));
    r_runtime_darwin_fs_service_testing_pause_dequeue(1);
    r_library_internal_fs_atomic_write_testing_pause_before_publication_activation(0);
    R_TEST_CHECK(wait_for_condition(
        r_library_internal_fs_atomic_write_testing_publication_activation_observed));
    signal_before = r_runtime_darwin_fs_service_testing_signal_count();
    R_TEST_CHECK(r_library_internal_fs_atomic_write_testing_trigger_publication_deadline());
    R_TEST_CHECK(wait_for_service_signal_count(signal_before + UINT64_C(1)));
    r_runtime_darwin_fs_service_testing_pause_dequeue(0);
    R_TEST_CHECK(await_write_file(started, &result));
    R_TEST_CHECK(result.kind == R_STD_FS_WRITE_FILE_FAILED &&
                 result.error.code == R_STD_FS_ERROR_TIMED_OUT && result.data.data == owner);
    R_TEST_CHECK(r_runtime_darwin_fs_testing_staging_cleanup_count() ==
                 cleanup_before + UINT64_C(1));
    R_TEST_CHECK(faccessat(AT_FDCWD, destination, F_OK, AT_SYMLINK_NOFOLLOW) != 0 &&
                 no_staging_entries(root_path));
    r_std_fs_write_file_result_destroy(&result);
    r_library_internal_fs_atomic_write_testing_reset();
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_cancel_deadline_commit_race(RRuntimeAllocator *allocator,
                                            int root_descriptor,
                                            const char *root_path) {
    static const uint8_t bytes[] = {UINT8_C(0x51), UINT8_C(0x52), UINT8_C(0x53)};
    char destination[1024];
    RStdFsPathResult path;
    RRuntimeArray data = byte_array(allocator, bytes, sizeof(bytes));
    RStdFsDeadline deadline;
    RStdFsTaskStartResult started;

    R_TEST_CHECK(snprintf(destination, sizeof(destination), "%s/commit-race", root_path) > 0);
    path = test_path(allocator, destination);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(deadline_after_milliseconds(UINT32_C(10000), &deadline));
    r_library_internal_fs_atomic_write_testing_reset();
    r_library_internal_fs_atomic_write_testing_pause_before_publication_commit(1);
    r_library_internal_fs_atomic_write_testing_pause_before_cancel_report(1);
    started = r_std_fs_write_file_atomic_no_replace(&path.value, &data, deadline);
    R_TEST_CHECK(started.is_ok && started.task != NULL && data.data == NULL);
    R_TEST_CHECK(
        wait_for_condition(r_library_internal_fs_atomic_write_testing_publication_commit_reached));
    r_runtime_task_cancel(&started.task);
    R_TEST_CHECK(started.task == NULL);
    R_TEST_CHECK(
        wait_for_condition(r_library_internal_fs_atomic_write_testing_cancel_report_reached));
    R_TEST_CHECK(r_library_internal_fs_atomic_write_testing_trigger_publication_deadline());
    R_TEST_CHECK(wait_for_atomic_count(
        r_library_internal_fs_atomic_write_testing_failure_selection_count, UINT64_C(1)));
    r_library_internal_fs_atomic_write_testing_pause_before_publication_commit(0);
    r_library_internal_fs_atomic_write_testing_pause_before_cancel_report(0);
    R_TEST_CHECK(wait_for_atomic_count(
        r_library_internal_fs_atomic_write_testing_acknowledgement_count, UINT64_C(1)));
    R_TEST_CHECK(read_exact_at(root_descriptor, "commit-race", bytes, sizeof(bytes)) &&
                 no_staging_entries(root_path));
    r_library_internal_fs_atomic_write_testing_reset();
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_late_failure_selector_wakes_final(RRuntimeAllocator *allocator,
                                                  int root_descriptor,
                                                  const char *root_path) {
    static const uint8_t bytes[] = {UINT8_C(0x61), UINT8_C(0x62), UINT8_C(0x63)};
    char destination[1024];
    RStdFsPathResult path;
    RRuntimeArray data = byte_array(allocator, bytes, sizeof(bytes));
    void *owner = data.data;
    RStdFsDeadline deadline;
    RStdFsTaskStartResult started;
    RStdFsWriteFileResult result = {0};

    R_TEST_CHECK(snprintf(destination, sizeof(destination), "%s/selector-race", root_path) > 0);
    path = test_path(allocator, destination);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(deadline_after_milliseconds(UINT32_C(2000), &deadline));
    r_library_internal_fs_atomic_write_testing_reset();
    r_library_internal_fs_atomic_write_testing_pause_before_publication_commit(1);
    r_library_internal_fs_atomic_write_testing_pause_after_failure_select(1);
    started = r_std_fs_write_file_atomic_no_replace(&path.value, &data, deadline);
    R_TEST_CHECK(started.is_ok && started.task != NULL && data.data == NULL);
    R_TEST_CHECK(
        wait_for_condition(r_library_internal_fs_atomic_write_testing_publication_commit_reached));
    R_TEST_CHECK(
        wait_for_condition(r_library_internal_fs_atomic_write_testing_failure_select_reached));
    r_library_internal_fs_atomic_write_testing_pause_before_publication_commit(0);
    R_TEST_CHECK(wait_for_atomic_count(r_library_internal_fs_atomic_write_testing_final_phase_count,
                                       UINT64_C(1)));
    R_TEST_CHECK(r_library_internal_fs_atomic_write_testing_acknowledgement_count() == UINT64_C(0));
    r_library_internal_fs_atomic_write_testing_pause_after_failure_select(0);
    R_TEST_CHECK(await_write_file(started, &result));
    R_TEST_CHECK(result.kind == R_STD_FS_WRITE_FILE_COMMITTED && result.data.data == owner);
    R_TEST_CHECK(r_library_internal_fs_atomic_write_testing_acknowledgement_count() == UINT64_C(1));
    R_TEST_CHECK(read_exact_at(root_descriptor, "selector-race", bytes, sizeof(bytes)) &&
                 no_staging_entries(root_path));
    r_std_fs_write_file_result_destroy(&result);
    r_library_internal_fs_atomic_write_testing_reset();
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_allocation_failure_sweep(RRuntimeAllocator *allocator,
                                         int root_descriptor,
                                         const char *root_path) {
    static const uint8_t bytes[] = {UINT8_C(0xa1), UINT8_C(0xb2), UINT8_C(0xc3)};
    static const RStdFsDeadline no_deadline = {0};
    const uint64_t sweep_limit = UINT64_C(128);
    char destination[1024];
    RStdFsPathResult path;
    uint64_t fail_at;
    _Bool reached_success = 0;

    R_TEST_CHECK(snprintf(destination, sizeof(destination), "%s/allocation-sweep", root_path) > 0);
    path = test_path(allocator, destination);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    for (fail_at = UINT64_C(1); fail_at <= sweep_limit; ++fail_at) {
        RRuntimeArray data = byte_array(allocator, bytes, sizeof(bytes));
        RRuntimeArray snapshot = data;
        RStdFsTaskStartResult started;
        RStdFsWriteFileResult result = {0};
        uint64_t attempts;

        r_runtime_allocator_set_failure(allocator, fail_at);
        started = r_std_fs_write_file_atomic_no_replace(&path.value, &data, no_deadline);
        if (started.is_ok) {
            R_TEST_CHECK(await_write_file(started, &result));
        }
        attempts = r_runtime_allocator_attempt_count(allocator);
        r_runtime_allocator_set_failure(allocator, UINT64_C(0));

        if (!started.is_ok) {
            R_TEST_CHECK(started.task == NULL &&
                         started.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
            R_TEST_CHECK(memcmp(&data, &snapshot, sizeof(data)) == 0 &&
                         memcmp(data.data, bytes, sizeof(bytes)) == 0);
            r_runtime_array_destroy(&data);
        } else {
            R_TEST_CHECK(data.data == NULL && result.data.data == snapshot.data &&
                         result.data.length == sizeof(bytes) &&
                         result.kind == R_STD_FS_WRITE_FILE_COMMITTED);
            R_TEST_CHECK(memcmp(result.data.data, bytes, sizeof(bytes)) == 0);
            R_TEST_CHECK(read_exact_at(root_descriptor, "allocation-sweep", bytes, sizeof(bytes)));
            R_TEST_CHECK(unlinkat(root_descriptor, "allocation-sweep", 0) == 0);
            if (attempts < fail_at) {
                reached_success = 1;
            }
            r_std_fs_write_file_result_destroy(&result);
        }
        R_TEST_CHECK(no_staging_entries(root_path));
        if (reached_success) {
            break;
        }
    }
    R_TEST_CHECK(reached_success);
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_atomic_write_contract(void) {
    char temporary_directory[] = "/tmp/r-fs-atomic-write-XXXXXX";
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsServiceStartResult service;
    int root_descriptor;

    R_TEST_CHECK(mkdtemp(temporary_directory) != NULL);
    root_descriptor = open(temporary_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    R_TEST_CHECK(root_descriptor >= 0);
    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    service = r_runtime_darwin_fs_service_start(&allocator, 64U);
    R_TEST_CHECK(service.status == R_RUNTIME_DARWIN_FS_START_OK);

    R_TEST_CHECK(test_ordinary_success(&allocator, root_descriptor, temporary_directory) == 0);
    R_TEST_CHECK(test_existing_destination(&allocator, root_descriptor, temporary_directory) == 0);
    R_TEST_CHECK(test_empty_file(&allocator, root_descriptor, temporary_directory) == 0);
    R_TEST_CHECK(test_beneath(&allocator, root_descriptor, temporary_directory) == 0);
    R_TEST_CHECK(test_start_failure_preserves_owner(&allocator, temporary_directory) == 0);
    R_TEST_CHECK(test_staging_collision_failure(&allocator, temporary_directory) == 0);
    R_TEST_CHECK(test_cancel_during_stage(&allocator, temporary_directory) == 0);
    R_TEST_CHECK(test_deadline_during_stage(&allocator, temporary_directory) == 0);
    R_TEST_CHECK(test_cancel_queued_publication(&allocator, temporary_directory) == 0);
    R_TEST_CHECK(test_deadline_queued_publication(&allocator, temporary_directory) == 0);
    R_TEST_CHECK(
        test_cancel_deadline_commit_race(&allocator, root_descriptor, temporary_directory) == 0);
    R_TEST_CHECK(test_late_failure_selector_wakes_final(
                     &allocator, root_descriptor, temporary_directory) == 0);
    R_TEST_CHECK(test_allocation_failure_sweep(&allocator, root_descriptor, temporary_directory) ==
                 0);

    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
    r_runtime_darwin_fs_service_stop();
    R_TEST_CHECK(unlinkat(root_descriptor, "ordinary", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "existing", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "empty", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "child/nested", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "child/retained", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "child", AT_REMOVEDIR) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "final-link", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "intermediate-link", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "outside", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "commit-race", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "selector-race", 0) == 0);
    R_TEST_CHECK(close(root_descriptor) == 0);
    R_TEST_CHECK(rmdir(temporary_directory) == 0);
    return 0;
}

int main(void) {
    return test_atomic_write_contract();
}
