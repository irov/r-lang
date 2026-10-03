#include "r_runtime_darwin_fs_lane.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/attr.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

typedef struct TestDirectory {
    char path[64];
    int descriptor;
} TestDirectory;

typedef struct TestBeneathRace {
    int directory_fd;
    _Atomic _Bool stop;
    _Atomic _Bool failed;
} TestBeneathRace;

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

static RRuntimeDarwinFsLane *create_lane(RRuntimeAllocator *allocator, size_t capacity) {
    RRuntimeDarwinFsLaneCreateResult result;

    r_runtime_allocator_initialize(allocator);
    result = r_runtime_darwin_fs_lane_create(allocator, capacity);

    if (result.status != R_RUNTIME_DARWIN_FS_START_OK || result.lane == NULL) {
        (void)fprintf(stderr,
                      "lane creation failed: status=%d native_error=%d\n",
                      (int)result.status,
                      result.native_error);
        return NULL;
    }
    return result.lane;
}

static int create_test_directory(TestDirectory *directory) {
    static const char path_template[] = "/tmp/r-fs-lane-XXXXXX";

    (void)memset(directory, 0, sizeof(*directory));
    directory->descriptor = -1;
    (void)memcpy(directory->path, path_template, sizeof(path_template));
    if (mkdtemp(directory->path) == NULL) {
        return -1;
    }
    directory->descriptor = open(directory->path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory->descriptor < 0) {
        (void)rmdir(directory->path);
        return -1;
    }
    return 0;
}

static void destroy_test_directory(TestDirectory *directory) {
    if (directory->descriptor >= 0) {
        (void)close(directory->descriptor);
        directory->descriptor = -1;
    }
    if (directory->path[0] != '\0') {
        (void)rmdir(directory->path);
        directory->path[0] = '\0';
    }
}

static int create_file_at(int directory_fd, const char *path) {
    static const unsigned char bytes[] = {0x52U, 0x20U, 0x30U, 0x2eU, 0x31U};
    int descriptor = openat(
        directory_fd, path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    size_t offset = 0U;

    if (descriptor < 0) {
        return -1;
    }
    while (offset < sizeof(bytes)) {
        ssize_t count = write(descriptor, bytes + offset, sizeof(bytes) - offset);

        if (count <= 0) {
            (void)close(descriptor);
            return -1;
        }
        offset += (size_t)count;
    }
    return descriptor;
}

static int find_staging_entry(int directory_fd, char *name, size_t name_capacity) {
    DIR *stream;
    struct dirent *entry;
    int duplicate = openat(directory_fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    int found = 0;

    if (duplicate < 0 || (name == NULL && name_capacity != 0U)) {
        return -1;
    }
    stream = fdopendir(duplicate);
    if (stream == NULL) {
        (void)close(duplicate);
        return -1;
    }
    while ((entry = readdir(stream)) != NULL) {
        if (strncmp(entry->d_name,
                    R_RUNTIME_DARWIN_FS_STAGING_PREFIX,
                    sizeof(R_RUNTIME_DARWIN_FS_STAGING_PREFIX) - 1U) == 0) {
            size_t length = strlen(entry->d_name);

            if (name != NULL) {
                if (length >= name_capacity) {
                    (void)closedir(stream);
                    return -1;
                }
                (void)memcpy(name, entry->d_name, length + 1U);
            }
            found = 1;
            break;
        }
    }
    if (closedir(stream) != 0) {
        return -1;
    }
    return found;
}

static int directory_has_staging_entry(int directory_fd) {
    return find_staging_entry(directory_fd, NULL, 0U);
}

static RRuntimeDarwinFsSubmitResult activate_prepared(RRuntimeDarwinFsPrepareResult *preparation) {
    RRuntimeDarwinFsSubmitResult result = {0};

    if (preparation->status != R_RUNTIME_DARWIN_FS_START_OK || preparation->prepared == NULL) {
        result.status = preparation->status;
        result.native_error = preparation->native_error;
        return result;
    }
    return r_runtime_darwin_fs_prepared_activate(&preparation->prepared);
}

static _Bool confinement_error(int native_error) {
    return native_error == ELOOP || native_error == ENOTCAPABLE;
}

static void *swap_directory_and_symlink(void *opaque_context) {
    TestBeneathRace *context = opaque_context;

    while (!atomic_load_explicit(&context->stop, memory_order_acquire)) {
        if (renameatx_np(
                context->directory_fd, "pivot", context->directory_fd, "pivot-link", RENAME_SWAP) !=
                0 ||
            renameatx_np(
                context->directory_fd, "pivot", context->directory_fd, "pivot-link", RENAME_SWAP) !=
                0) {
            atomic_store_explicit(&context->failed, 1, memory_order_release);
            return NULL;
        }
    }
    return NULL;
}

static int check_native_success(RRuntimeDarwinFsSubmitResult submission,
                                RRuntimeDarwinFsOperation operation,
                                RRuntimeDarwinFsResult *result) {
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK);
    CHECK(submission.request != NULL);
    *result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result->operation == operation);
    CHECK(result->terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE);
    CHECK(result->native_entered);
    CHECK(result->native_error == 0);
    return 0;
}

static int test_capacity_and_queued_cancellation(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsLaneCreateResult invalid;
    RRuntimeDarwinFsLane *lane;
    RRuntimeDarwinFsSubmitResult first;
    RRuntimeDarwinFsSubmitResult second;
    RRuntimeDarwinFsSubmitResult overflow;
    RRuntimeDarwinFsResult result;
    int descriptor;

    r_runtime_allocator_initialize(&allocator);
    invalid = r_runtime_darwin_fs_lane_create(&allocator, 0U);
    CHECK(invalid.status == R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT);
    CHECK(invalid.lane == NULL);
    invalid =
        r_runtime_darwin_fs_lane_create(&allocator, R_RUNTIME_DARWIN_FS_MAX_PENDING_REQUESTS + 1U);
    CHECK(invalid.status == R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT);
    CHECK(invalid.lane == NULL);

    lane = create_lane(&allocator, 2U);
    CHECK(lane != NULL);
    CHECK(r_runtime_darwin_fs_lane_worker_count(lane) == 4U);
    CHECK(r_runtime_darwin_fs_lane_capacity(lane) == 2U);
    r_runtime_darwin_fs_lane_testing_pause_dequeue(lane, 1);
    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    CHECK(descriptor >= 0);

    first = r_runtime_darwin_fs_submit_fstat(lane, descriptor);
    second = r_runtime_darwin_fs_submit_fstat(lane, descriptor);
    overflow = r_runtime_darwin_fs_submit_fstat(lane, descriptor);
    CHECK(first.status == R_RUNTIME_DARWIN_FS_START_OK);
    CHECK(second.status == R_RUNTIME_DARWIN_FS_START_OK);
    CHECK(overflow.status == R_RUNTIME_DARWIN_FS_START_QUEUE_FULL);
    CHECK(overflow.request == NULL);
    CHECK(r_runtime_darwin_fs_request_submission_sequence(first.request) == UINT64_C(1));
    CHECK(r_runtime_darwin_fs_request_submission_sequence(second.request) == UINT64_C(2));

    CHECK(r_runtime_darwin_fs_request_cancel(first.request));
    CHECK(!r_runtime_darwin_fs_request_deadline_expired(first.request));
    CHECK(r_runtime_darwin_fs_request_cancel(second.request));
    result = r_runtime_darwin_fs_request_wait(first.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_CANCELLED);
    CHECK(!result.native_entered);
    CHECK(r_runtime_darwin_fs_request_entry_sequence(first.request) == UINT64_C(0));
    result = r_runtime_darwin_fs_request_wait(second.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_CANCELLED);
    CHECK(!result.native_entered);
    CHECK(r_runtime_darwin_fs_request_entry_sequence(second.request) == UINT64_C(0));

    r_runtime_darwin_fs_request_release(first.request);
    r_runtime_darwin_fs_request_release(second.request);
    CHECK(close(descriptor) == 0);
    r_runtime_darwin_fs_lane_destroy(lane);
    return 0;
}

static int test_fifo_and_parallel_entry(void) {
    enum {
        request_count = 8
    };
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsSubmitResult submissions[request_count];
    RRuntimeDarwinFsLane *lane = create_lane(&allocator, request_count);
    int descriptor;
    size_t index;

    CHECK(lane != NULL);
    r_runtime_darwin_fs_lane_testing_pause_before_native(lane, 1);
    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    CHECK(descriptor >= 0);
    for (index = 0U; index < request_count; ++index) {
        submissions[index] = r_runtime_darwin_fs_submit_fstat(lane, descriptor);
        CHECK(submissions[index].status == R_RUNTIME_DARWIN_FS_START_OK);
    }
    for (index = 0U; index < 4U; ++index) {
        r_runtime_darwin_fs_request_testing_wait_for_state(submissions[index].request,
                                                           R_RUNTIME_DARWIN_FS_REQUEST_ENTERED);
        CHECK(r_runtime_darwin_fs_request_entry_sequence(submissions[index].request) ==
              (uint64_t)(index + 1U));
    }
    CHECK(r_runtime_darwin_fs_request_state(submissions[4].request) ==
          R_RUNTIME_DARWIN_FS_REQUEST_QUEUED);
    r_runtime_darwin_fs_lane_testing_pause_before_native(lane, 0);
    for (index = 0U; index < request_count; ++index) {
        RRuntimeDarwinFsResult result;

        CHECK(check_native_success(submissions[index], R_RUNTIME_DARWIN_FS_FSTAT, &result) == 0);
        CHECK(r_runtime_darwin_fs_request_entry_sequence(submissions[index].request) ==
              (uint64_t)(index + 1U));
        r_runtime_darwin_fs_request_release(submissions[index].request);
    }
    CHECK(close(descriptor) == 0);
    r_runtime_darwin_fs_lane_destroy(lane);
    return 0;
}

static int test_entered_cancel_and_deadline_acknowledgement(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsLane *lane = create_lane(&allocator, 2U);
    RRuntimeDarwinFsSubmitResult cancelled;
    RRuntimeDarwinFsSubmitResult timed_out;
    RRuntimeDarwinFsResult result;
    int descriptor;

    CHECK(lane != NULL);
    r_runtime_darwin_fs_lane_testing_pause_before_native(lane, 1);
    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    CHECK(descriptor >= 0);
    cancelled = r_runtime_darwin_fs_submit_fstat(lane, descriptor);
    timed_out = r_runtime_darwin_fs_submit_fstat(lane, descriptor);
    CHECK(cancelled.status == R_RUNTIME_DARWIN_FS_START_OK);
    CHECK(timed_out.status == R_RUNTIME_DARWIN_FS_START_OK);
    r_runtime_darwin_fs_request_testing_wait_for_state(cancelled.request,
                                                       R_RUNTIME_DARWIN_FS_REQUEST_ENTERED);
    r_runtime_darwin_fs_request_testing_wait_for_state(timed_out.request,
                                                       R_RUNTIME_DARWIN_FS_REQUEST_ENTERED);

    CHECK(r_runtime_darwin_fs_request_cancel(cancelled.request));
    CHECK(!r_runtime_darwin_fs_request_deadline_expired(cancelled.request));
    CHECK(r_runtime_darwin_fs_request_deadline_expired(timed_out.request));
    CHECK(!r_runtime_darwin_fs_request_cancel(timed_out.request));
    CHECK(r_runtime_darwin_fs_request_state(cancelled.request) ==
          R_RUNTIME_DARWIN_FS_REQUEST_ENTERED);
    CHECK(r_runtime_darwin_fs_request_state(timed_out.request) ==
          R_RUNTIME_DARWIN_FS_REQUEST_ENTERED);

    r_runtime_darwin_fs_lane_testing_pause_before_native(lane, 0);
    result = r_runtime_darwin_fs_request_wait(cancelled.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_CANCELLED);
    CHECK(result.native_entered);
    CHECK(result.native_error == 0);
    CHECK(!result.committed);
    result = r_runtime_darwin_fs_request_wait(timed_out.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_TIMED_OUT);
    CHECK(result.native_entered);
    CHECK(result.native_error == 0);
    CHECK(!result.committed);

    r_runtime_darwin_fs_request_release(cancelled.request);
    r_runtime_darwin_fs_request_release(timed_out.request);
    CHECK(close(descriptor) == 0);
    r_runtime_darwin_fs_lane_destroy(lane);
    return 0;
}

static int test_cancelled_enumeration_is_cached(void) {
    TestDirectory directory;
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsLane *lane;
    RRuntimeDarwinFsSubmitResult submission;
    RRuntimeDarwinFsResult result;
    struct attrlist attributes;
    size_t buffer_size = 0U;
    int descriptor;

    CHECK(create_test_directory(&directory) == 0);
    descriptor = create_file_at(directory.descriptor, "entry");
    CHECK(descriptor >= 0);
    CHECK(close(descriptor) == 0);
    lane = create_lane(&allocator, 1U);
    CHECK(lane != NULL);
    r_runtime_darwin_fs_lane_testing_pause_before_native(lane, 1);
    (void)memset(&attributes, 0, sizeof(attributes));
    attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
    attributes.commonattr = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME;
    submission =
        r_runtime_darwin_fs_submit_enumerate(lane, directory.descriptor, &attributes, 4096U);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK);
    r_runtime_darwin_fs_request_testing_wait_for_state(submission.request,
                                                       R_RUNTIME_DARWIN_FS_REQUEST_ENTERED);
    CHECK(r_runtime_darwin_fs_request_cancel(submission.request));
    CHECK(r_runtime_darwin_fs_request_enumeration_data(submission.request, &buffer_size) == NULL);
    CHECK(buffer_size == 0U);
    CHECK(r_runtime_darwin_fs_request_enumeration_entry_count(submission.request) == INT64_C(-1));
    r_runtime_darwin_fs_lane_testing_pause_before_native(lane, 0);
    result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_CANCELLED);
    CHECK(result.native_entered);
    CHECK(r_runtime_darwin_fs_request_enumeration_data(submission.request, &buffer_size) != NULL);
    CHECK(buffer_size == 4096U);
    CHECK(r_runtime_darwin_fs_request_enumeration_entry_count(submission.request) >= INT64_C(0));

    r_runtime_darwin_fs_request_release(submission.request);
    r_runtime_darwin_fs_lane_destroy(lane);
    CHECK(unlinkat(directory.descriptor, "entry", 0) == 0);
    destroy_test_directory(&directory);
    return 0;
}

static int test_committed_native_result_wins_cancel(void) {
    TestDirectory directory;
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsLane *lane;
    RRuntimeDarwinFsSubmitResult durability;
    RRuntimeDarwinFsSubmitResult cancelled_namespace_change;
    RRuntimeDarwinFsSubmitResult seek;
    RRuntimeDarwinFsResult result;
    int descriptor;

    CHECK(create_test_directory(&directory) == 0);
    descriptor = create_file_at(directory.descriptor, "seekable");
    CHECK(descriptor >= 0);
    lane = create_lane(&allocator, 3U);
    CHECK(lane != NULL);
    r_runtime_darwin_fs_lane_testing_pause_before_native(lane, 1);
    seek = r_runtime_darwin_fs_submit_seek(lane, descriptor, 0, SEEK_SET);
    cancelled_namespace_change =
        r_runtime_darwin_fs_submit_mkdir_at(lane, directory.descriptor, "committed", S_IRWXU);
    durability = r_runtime_darwin_fs_submit_fsync(lane, descriptor);
    CHECK(seek.status == R_RUNTIME_DARWIN_FS_START_OK);
    CHECK(cancelled_namespace_change.status == R_RUNTIME_DARWIN_FS_START_OK);
    CHECK(durability.status == R_RUNTIME_DARWIN_FS_START_OK);
    r_runtime_darwin_fs_request_testing_wait_for_state(seek.request,
                                                       R_RUNTIME_DARWIN_FS_REQUEST_ENTERED);
    r_runtime_darwin_fs_request_testing_wait_for_state(cancelled_namespace_change.request,
                                                       R_RUNTIME_DARWIN_FS_REQUEST_ENTERED);
    r_runtime_darwin_fs_request_testing_wait_for_state(durability.request,
                                                       R_RUNTIME_DARWIN_FS_REQUEST_ENTERED);
    CHECK(r_runtime_darwin_fs_request_cancel(seek.request));
    CHECK(r_runtime_darwin_fs_request_cancel(cancelled_namespace_change.request));
    CHECK(r_runtime_darwin_fs_request_cancel(durability.request));
    r_runtime_darwin_fs_lane_testing_pause_before_native(lane, 0);
    result = r_runtime_darwin_fs_request_wait(seek.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE);
    CHECK(result.native_entered);
    CHECK(result.native_error == 0);
    CHECK(result.committed);
    CHECK(result.native_return_value == INT64_C(0));
    CHECK(!r_runtime_darwin_fs_request_cancel(seek.request));
    result = r_runtime_darwin_fs_request_wait(cancelled_namespace_change.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_CANCELLED);
    CHECK(result.native_entered);
    CHECK(!result.committed);
    result = r_runtime_darwin_fs_request_wait(durability.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.committed);

    r_runtime_darwin_fs_request_release(seek.request);
    r_runtime_darwin_fs_request_release(cancelled_namespace_change.request);
    r_runtime_darwin_fs_request_release(durability.request);
    r_runtime_darwin_fs_lane_destroy(lane);
    CHECK(close(descriptor) == 0);
    CHECK(unlinkat(directory.descriptor, "seekable", 0) == 0);
    errno = 0;
    CHECK(faccessat(directory.descriptor, "committed", F_OK, AT_SYMLINK_NOFOLLOW) != 0 &&
          errno == ENOENT);
    destroy_test_directory(&directory);
    return 0;
}

static int test_open_commit_classification(void) {
    TestDirectory directory;
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsLane *lane;
    RRuntimeDarwinFsSubmitResult existing;
    RRuntimeDarwinFsSubmitResult created;
    RRuntimeDarwinFsResult result;
    int descriptor;
    int opened;

    CHECK(create_test_directory(&directory) == 0);
    descriptor = create_file_at(directory.descriptor, "existing-open-or-create");
    CHECK(descriptor >= 0);
    CHECK(close(descriptor) == 0);
    lane = create_lane(&allocator, 2U);
    CHECK(lane != NULL);
    r_runtime_darwin_fs_lane_testing_pause_before_native(lane, 1);
    existing = r_runtime_darwin_fs_submit_open_at(
        lane, directory.descriptor, "existing-open-or-create", O_RDWR | O_CREAT, S_IRUSR);
    created = r_runtime_darwin_fs_submit_open_at(
        lane, directory.descriptor, "created-open-or-create", O_RDWR | O_CREAT, S_IRUSR);
    CHECK(existing.status == R_RUNTIME_DARWIN_FS_START_OK);
    CHECK(created.status == R_RUNTIME_DARWIN_FS_START_OK);
    r_runtime_darwin_fs_request_testing_wait_for_state(existing.request,
                                                       R_RUNTIME_DARWIN_FS_REQUEST_ENTERED);
    r_runtime_darwin_fs_request_testing_wait_for_state(created.request,
                                                       R_RUNTIME_DARWIN_FS_REQUEST_ENTERED);
    CHECK(r_runtime_darwin_fs_request_cancel(existing.request));
    CHECK(r_runtime_darwin_fs_request_cancel(created.request));
    r_runtime_darwin_fs_lane_testing_pause_before_native(lane, 0);

    result = r_runtime_darwin_fs_request_wait(existing.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_CANCELLED);
    CHECK(result.native_entered);
    CHECK(!result.committed);
    CHECK(r_runtime_darwin_fs_request_take_opened_fd(existing.request) < 0);
    result = r_runtime_darwin_fs_request_wait(created.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.committed);
    opened = r_runtime_darwin_fs_request_opened_fd(created.request);
    CHECK(opened >= 0);
    CHECK(r_runtime_darwin_fs_request_opened_fd(created.request) == opened);
    opened = r_runtime_darwin_fs_request_take_opened_fd(created.request);
    CHECK(opened >= 0);
    CHECK(r_runtime_darwin_fs_request_opened_fd(created.request) < 0);
    CHECK(close(opened) == 0);

    r_runtime_darwin_fs_request_release(existing.request);
    r_runtime_darwin_fs_request_release(created.request);
    r_runtime_darwin_fs_lane_destroy(lane);
    CHECK(unlinkat(directory.descriptor, "existing-open-or-create", 0) == 0);
    CHECK(unlinkat(directory.descriptor, "created-open-or-create", 0) == 0);
    destroy_test_directory(&directory);
    return 0;
}

static int test_open_or_create_race_is_bounded(void) {
    TestDirectory directory;
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsLane *lane;
    RRuntimeDarwinFsSubmitResult submission;
    RRuntimeDarwinFsResult result;
    int descriptor;

    CHECK(create_test_directory(&directory) == 0);
    descriptor = create_file_at(directory.descriptor, "open-or-create-race");
    CHECK(descriptor >= 0);
    CHECK(close(descriptor) == 0);
    lane = create_lane(&allocator, 1U);
    CHECK(lane != NULL);
    r_runtime_darwin_fs_testing_force_open_or_create_race(1);
    submission = r_runtime_darwin_fs_submit_open_at(
        lane, directory.descriptor, "open-or-create-race", O_RDWR | O_CREAT, S_IRUSR);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK);
    result = r_runtime_darwin_fs_request_wait(submission.request);
    r_runtime_darwin_fs_testing_force_open_or_create_race(0);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE);
    CHECK(result.native_entered);
    CHECK(result.native_error == EAGAIN);
    CHECK(!result.committed);
    CHECK(r_runtime_darwin_fs_request_take_opened_fd(submission.request) < 0);

    r_runtime_darwin_fs_request_release(submission.request);
    r_runtime_darwin_fs_lane_destroy(lane);
    CHECK(unlinkat(directory.descriptor, "open-or-create-race", 0) == 0);
    destroy_test_directory(&directory);
    return 0;
}

static int test_atomic_beneath_native_flags(void) {
    TestDirectory directory;
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsLane *lane;
    RRuntimeDarwinFsPrepareResult preparation;
    RRuntimeDarwinFsSubmitResult submission;
    RRuntimeDarwinFsResult result;

    CHECK(create_test_directory(&directory) == 0);
    lane = create_lane(&allocator, 1U);
    CHECK(lane != NULL);
    preparation = r_runtime_darwin_fs_prepare_open_at(
        lane, directory.descriptor, "../", O_RDONLY | O_DIRECTORY, 0, 0, 1, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_FS_START_OK);
    CHECK(preparation.prepared != NULL);
    submission = r_runtime_darwin_fs_prepared_activate(&preparation.prepared);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK);
    CHECK(preparation.prepared == NULL);
    result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE);
    CHECK(result.native_entered);
    CHECK(result.native_error == ENOTCAPABLE);
    CHECK(!result.committed);
    r_runtime_darwin_fs_request_release(submission.request);
    r_runtime_darwin_fs_lane_destroy(lane);
    destroy_test_directory(&directory);
    return 0;
}

static int test_submission_retains_path_and_directory(void) {
    TestDirectory directory;
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsLane *lane;
    RRuntimeDarwinFsSubmitResult submission;
    RRuntimeDarwinFsResult result;
    struct stat metadata;
    char path[] = "original";
    int cleanup_directory;
    int opened;

    CHECK(create_test_directory(&directory) == 0);
    opened = create_file_at(directory.descriptor, path);
    CHECK(opened >= 0);
    CHECK(close(opened) == 0);
    cleanup_directory = fcntl(directory.descriptor, F_DUPFD_CLOEXEC, 0);
    CHECK(cleanup_directory >= 0);
    lane = create_lane(&allocator, 1U);
    CHECK(lane != NULL);
    r_runtime_darwin_fs_lane_testing_pause_dequeue(lane, 1);
    submission = r_runtime_darwin_fs_submit_open_at(lane, directory.descriptor, path, O_RDONLY, 0);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK);
    CHECK(close(directory.descriptor) == 0);
    directory.descriptor = -1;
    path[0] = 'x';
    CHECK(r_runtime_darwin_fs_request_enumeration_data(submission.request, NULL) == NULL);
    r_runtime_darwin_fs_lane_testing_pause_dequeue(lane, 0);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_OPEN_AT, &result) == 0);
    opened = r_runtime_darwin_fs_request_opened_fd(submission.request);
    CHECK(opened >= 0);
    opened = r_runtime_darwin_fs_request_take_opened_fd(submission.request);
    CHECK(opened >= 0);
    CHECK(r_runtime_darwin_fs_request_opened_fd(submission.request) < 0);
    CHECK(fstat(opened, &metadata) == 0);
    CHECK(S_ISREG(metadata.st_mode));

    CHECK(close(opened) == 0);
    r_runtime_darwin_fs_request_release(submission.request);
    r_runtime_darwin_fs_lane_destroy(lane);
    CHECK(unlinkat(cleanup_directory, "original", 0) == 0);
    CHECK(close(cleanup_directory) == 0);
    destroy_test_directory(&directory);
    return 0;
}

static int test_destroy_terminalizes_queued_close(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsLane *lane = create_lane(&allocator, 1U);
    RRuntimeDarwinFsSubmitResult submission;
    RRuntimeDarwinFsResult result;
    int descriptor;
    int recovered;

    CHECK(lane != NULL);
    r_runtime_darwin_fs_lane_testing_pause_dequeue(lane, 1);
    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    CHECK(descriptor >= 0);
    submission = r_runtime_darwin_fs_submit_close(lane, descriptor);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK);
    r_runtime_darwin_fs_lane_destroy(lane);

    result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_STOPPING);
    CHECK(!result.native_entered);
    recovered = r_runtime_darwin_fs_request_take_unclosed_fd(submission.request);
    CHECK(recovered == descriptor);
    CHECK(fcntl(recovered, F_GETFD) >= 0);
    CHECK(r_runtime_darwin_fs_request_take_unclosed_fd(submission.request) == -1);
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(close(recovered) == 0);
    return 0;
}

static int test_typed_native_calls(void) {
    TestDirectory directory;
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsLane *lane;
    RRuntimeDarwinFsSubmitResult submission;
    RRuntimeDarwinFsResult result;
    struct attrlist attributes;
    size_t enumeration_size = 0U;
    const void *enumeration_data;
    int source_descriptor;
    int other_descriptor;
    int moved_descriptor;

    CHECK(create_test_directory(&directory) == 0);
    source_descriptor = create_file_at(directory.descriptor, "source");
    CHECK(source_descriptor >= 0);
    lane = create_lane(&allocator, 8U);
    CHECK(lane != NULL);

    submission = r_runtime_darwin_fs_submit_fstat(lane, source_descriptor);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_FSTAT, &result) == 0);
    CHECK(S_ISREG(result.metadata.st_mode));
    r_runtime_darwin_fs_request_release(submission.request);

    submission = r_runtime_darwin_fs_submit_fstat_at(lane, directory.descriptor, "source");
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_FSTAT_AT, &result) == 0);
    CHECK(S_ISREG(result.metadata.st_mode));
    r_runtime_darwin_fs_request_release(submission.request);

    submission = r_runtime_darwin_fs_submit_fsync(lane, source_descriptor);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_FSYNC, &result) == 0);
    CHECK(result.committed);
    r_runtime_darwin_fs_request_release(submission.request);

    submission = r_runtime_darwin_fs_submit_full_fsync(lane, source_descriptor);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_FULL_FSYNC, &result) == 0);
    CHECK(result.committed);
    r_runtime_darwin_fs_request_release(submission.request);

    submission = r_runtime_darwin_fs_submit_barrier_fsync(lane, source_descriptor);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_BARRIER_FSYNC, &result) == 0);
    CHECK(result.committed);
    r_runtime_darwin_fs_request_release(submission.request);

    /* Library R-SLIB-FS-0016: a lock belongs to the open file description, so a second open of
       the same file conflicts with it although both are in this process; the lane duplicates the
       descriptor for each call and closing the duplicate keeps the lock. */
    {
        int other = openat(directory.descriptor, "source", O_RDWR | O_CLOEXEC);

        CHECK(other >= 0);
        submission = r_runtime_darwin_fs_submit_ofd_lock(
            lane, source_descriptor, F_WRLCK, (off_t)0, (off_t)0);
        CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_OFD_LOCK, &result) == 0);
        CHECK(result.committed);
        r_runtime_darwin_fs_request_release(submission.request);
        submission = r_runtime_darwin_fs_submit_ofd_lock(lane, other, F_RDLCK, (off_t)4, (off_t)8);
        CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK && submission.request != NULL);
        result = r_runtime_darwin_fs_request_wait(submission.request);
        CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE);
        CHECK(result.native_error == EAGAIN || result.native_error == EACCES);
        CHECK(!result.committed);
        r_runtime_darwin_fs_request_release(submission.request);
        submission = r_runtime_darwin_fs_submit_ofd_lock(
            lane, source_descriptor, F_UNLCK, (off_t)0, (off_t)0);
        CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_OFD_LOCK, &result) == 0);
        r_runtime_darwin_fs_request_release(submission.request);
        submission = r_runtime_darwin_fs_submit_ofd_lock(lane, other, F_RDLCK, (off_t)4, (off_t)8);
        CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_OFD_LOCK, &result) == 0);
        r_runtime_darwin_fs_request_release(submission.request);
        submission =
            r_runtime_darwin_fs_submit_ofd_lock(lane, other, (short)99, (off_t)0, (off_t)0);
        CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT);
        CHECK(close(other) == 0);
    }

    submission = r_runtime_darwin_fs_submit_mkdir_at(lane, directory.descriptor, "child", S_IRWXU);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_MKDIR_AT, &result) == 0);
    CHECK(result.committed);
    r_runtime_darwin_fs_request_release(submission.request);

    (void)memset(&attributes, 0, sizeof(attributes));
    attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
    attributes.commonattr = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME;
    submission =
        r_runtime_darwin_fs_submit_enumerate(lane, directory.descriptor, &attributes, 4096U);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_ENUMERATE, &result) == 0);
    CHECK(result.native_return_value >= INT64_C(0));
    enumeration_data =
        r_runtime_darwin_fs_request_enumeration_data(submission.request, &enumeration_size);
    CHECK(enumeration_data != NULL);
    CHECK(enumeration_size == 4096U);
    CHECK(r_runtime_darwin_fs_request_enumeration_entry_count(submission.request) ==
          result.native_return_value);
    r_runtime_darwin_fs_request_release(submission.request);

    submission = r_runtime_darwin_fs_submit_rename_at_no_replace(
        lane, directory.descriptor, "source", directory.descriptor, "destination");
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_RENAME_AT_NO_REPLACE, &result) == 0);
    CHECK(result.committed);
    r_runtime_darwin_fs_request_release(submission.request);

    other_descriptor = create_file_at(directory.descriptor, "other");
    CHECK(other_descriptor >= 0);
    CHECK(close(other_descriptor) == 0);
    submission = r_runtime_darwin_fs_submit_rename_at_no_replace(
        lane, directory.descriptor, "destination", directory.descriptor, "other");
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK);
    result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE);
    CHECK(result.native_entered);
    CHECK(result.native_error == EEXIST);
    CHECK(result.native_return_value == INT64_C(-1));
    CHECK(!result.committed);
    r_runtime_darwin_fs_request_release(submission.request);

    submission = r_runtime_darwin_fs_submit_unlink_at(lane, directory.descriptor, "destination", 0);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_UNLINK_AT, &result) == 0);
    CHECK(result.committed);
    r_runtime_darwin_fs_request_release(submission.request);
    submission = r_runtime_darwin_fs_submit_unlink_at(lane, directory.descriptor, "other", 0);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_UNLINK_AT, &result) == 0);
    r_runtime_darwin_fs_request_release(submission.request);
    submission = r_runtime_darwin_fs_submit_unlink_at(lane, directory.descriptor, "child", 1);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_UNLINK_AT, &result) == 0);
    r_runtime_darwin_fs_request_release(submission.request);

    moved_descriptor = source_descriptor;
    submission = r_runtime_darwin_fs_submit_close(lane, moved_descriptor);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_CLOSE, &result) == 0);
    CHECK(result.committed);
    errno = 0;
    CHECK(fcntl(moved_descriptor, F_GETFD) == -1);
    CHECK(errno == EBADF);
    r_runtime_darwin_fs_request_release(submission.request);

    r_runtime_darwin_fs_lane_destroy(lane);
    destroy_test_directory(&directory);
    return 0;
}

static int test_beneath_namespace_and_metadata(void) {
    TestDirectory directory;
    TestDirectory outside;
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsLane *lane;
    RRuntimeDarwinFsPrepareResult preparation;
    RRuntimeDarwinFsSubmitResult submission;
    RRuntimeDarwinFsResult result;
    struct stat metadata;
    char target[128];
    char link_value[128];
    ssize_t link_length;
    uint64_t cleanup_count;
    int descriptor;

    CHECK(create_test_directory(&directory) == 0);
    CHECK(create_test_directory(&outside) == 0);
    descriptor = create_file_at(outside.descriptor, "target");
    CHECK(descriptor >= 0);
    CHECK(close(descriptor) == 0);
    CHECK(snprintf(target, sizeof(target), "%s/target", outside.path) > 0);
    CHECK(symlinkat(target, directory.descriptor, "file-link") == 0);
    CHECK(symlinkat(outside.path, directory.descriptor, "directory-link") == 0);
    CHECK(mkdirat(directory.descriptor, "remove-as-file", S_IRWXU) == 0);
    descriptor = create_file_at(directory.descriptor, "remove-as-directory");
    CHECK(descriptor >= 0);
    CHECK(close(descriptor) == 0);
    lane = create_lane(&allocator, 8U);
    CHECK(lane != NULL);

    preparation = r_runtime_darwin_fs_prepare_fstat_at(
        lane, directory.descriptor, "file-link", 1, UINT64_C(0));
    submission = activate_prepared(&preparation);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_FSTAT_AT, &result) == 0);
    CHECK(S_ISLNK(result.metadata.st_mode));
    r_runtime_darwin_fs_request_release(submission.request);

    preparation = r_runtime_darwin_fs_prepare_unlink_at(
        lane, directory.descriptor, "file-link", 0, 1, UINT64_C(0));
    submission = activate_prepared(&preparation);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_UNLINK_AT, &result) == 0);
    CHECK(result.committed);
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(fstatat(outside.descriptor, "target", &metadata, 0) == 0);
    CHECK(S_ISREG(metadata.st_mode));

    preparation = r_runtime_darwin_fs_prepare_unlink_at(
        lane, directory.descriptor, "directory-link", 1, 1, UINT64_C(0));
    submission = activate_prepared(&preparation);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK);
    result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE);
    CHECK(result.native_error == ENOTDIR);
    CHECK(!result.committed);
    r_runtime_darwin_fs_request_release(submission.request);

    CHECK(symlinkat(target, directory.descriptor, "rename-link") == 0);
    preparation = r_runtime_darwin_fs_prepare_rename_at_no_replace(lane,
                                                                   directory.descriptor,
                                                                   "rename-link",
                                                                   directory.descriptor,
                                                                   "renamed-link",
                                                                   1,
                                                                   UINT64_C(0));
    submission = activate_prepared(&preparation);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_RENAME_AT_NO_REPLACE, &result) == 0);
    CHECK(result.committed);
    r_runtime_darwin_fs_request_release(submission.request);
    link_length = readlinkat(directory.descriptor, "renamed-link", link_value, sizeof(link_value));
    CHECK(link_length == (ssize_t)strlen(target));
    CHECK(memcmp(link_value, target, (size_t)link_length) == 0);
    CHECK(fstatat(outside.descriptor, "target", &metadata, 0) == 0);

    preparation = r_runtime_darwin_fs_prepare_unlink_at(
        lane, directory.descriptor, "remove-as-file", 0, 1, UINT64_C(0));
    submission = activate_prepared(&preparation);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK);
    result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE);
    CHECK(result.native_error == EPERM);
    CHECK(!result.committed);
    r_runtime_darwin_fs_request_release(submission.request);

    preparation = r_runtime_darwin_fs_prepare_unlink_at(
        lane, directory.descriptor, "remove-as-directory", 1, 1, UINT64_C(0));
    submission = activate_prepared(&preparation);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK);
    result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE);
    CHECK(result.native_error == ENOTDIR);
    CHECK(!result.committed);
    r_runtime_darwin_fs_request_release(submission.request);

    preparation = r_runtime_darwin_fs_prepare_fstat_at(
        lane, directory.descriptor, "directory-link/target", 1, UINT64_C(0));
    submission = activate_prepared(&preparation);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK);
    result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE);
    CHECK(confinement_error(result.native_error));
    r_runtime_darwin_fs_request_release(submission.request);

    preparation = r_runtime_darwin_fs_prepare_unlink_at(
        lane, directory.descriptor, "directory-link/target", 0, 1, UINT64_C(0));
    submission = activate_prepared(&preparation);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK);
    result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE);
    CHECK(confinement_error(result.native_error));
    r_runtime_darwin_fs_request_release(submission.request);

    preparation = r_runtime_darwin_fs_prepare_create_directory_at(
        lane, directory.descriptor, "one/two/three", S_IRWXU, 1, 1, UINT64_C(0));
    submission = activate_prepared(&preparation);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_MKDIR_AT, &result) == 0);
    CHECK(result.committed);
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(fstatat(directory.descriptor, "one/two/three", &metadata, 0) == 0);
    CHECK(S_ISDIR(metadata.st_mode));
    CHECK((metadata.st_mode & (S_IRWXG | S_IRWXO)) == 0);
    CHECK(directory_has_staging_entry(directory.descriptor) == 0);

    preparation = r_runtime_darwin_fs_prepare_create_directory_at(
        lane, directory.descriptor, "invalid-mode", ACCESSPERMS, 0, 1, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT);
    CHECK(preparation.prepared == NULL);

    preparation = r_runtime_darwin_fs_prepare_create_directory_at(
        lane, directory.descriptor, "one/two/three", S_IRWXU, 1, 1, UINT64_C(0));
    submission = activate_prepared(&preparation);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_MKDIR_AT, &result) == 0);
    CHECK(!result.committed);
    r_runtime_darwin_fs_request_release(submission.request);

    cleanup_count = r_runtime_darwin_fs_testing_staging_cleanup_count();
    preparation = r_runtime_darwin_fs_prepare_create_directory_at(
        lane, directory.descriptor, "missing/child", S_IRWXU, 0, 1, UINT64_C(0));
    submission = activate_prepared(&preparation);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK);
    result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE);
    CHECK(result.native_error == ENOENT);
    CHECK(!result.committed);
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(r_runtime_darwin_fs_testing_staging_cleanup_count() == cleanup_count + UINT64_C(1));
    CHECK(directory_has_staging_entry(directory.descriptor) == 0);

    r_runtime_darwin_fs_testing_force_staging_collisions(R_RUNTIME_DARWIN_FS_CREATE_RETRY_LIMIT);
    preparation = r_runtime_darwin_fs_prepare_create_directory_at(
        lane, directory.descriptor, "collision-exhausted", S_IRWXU, 0, 1, UINT64_C(0));
    submission = activate_prepared(&preparation);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK);
    result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE);
    CHECK(result.native_error == EAGAIN);
    CHECK(!result.committed);
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(directory_has_staging_entry(directory.descriptor) == 0);

    r_runtime_darwin_fs_testing_force_staging_collisions(R_RUNTIME_DARWIN_FS_CREATE_RETRY_LIMIT -
                                                         1U);
    preparation = r_runtime_darwin_fs_prepare_create_directory_at(
        lane, directory.descriptor, "collision-success", S_IRWXU, 0, 1, UINT64_C(0));
    submission = activate_prepared(&preparation);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_MKDIR_AT, &result) == 0);
    CHECK(result.committed);
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(fstatat(directory.descriptor, "collision-success", &metadata, 0) == 0);
    CHECK((metadata.st_mode & (S_IRWXG | S_IRWXO)) == 0);
    CHECK(directory_has_staging_entry(directory.descriptor) == 0);

    r_runtime_darwin_fs_lane_destroy(lane);
    CHECK(unlinkat(directory.descriptor, "renamed-link", 0) == 0);
    CHECK(unlinkat(directory.descriptor, "directory-link", 0) == 0);
    CHECK(unlinkat(directory.descriptor, "remove-as-file", AT_REMOVEDIR) == 0);
    CHECK(unlinkat(directory.descriptor, "remove-as-directory", 0) == 0);
    CHECK(unlinkat(directory.descriptor, "one/two/three", AT_REMOVEDIR) == 0);
    CHECK(unlinkat(directory.descriptor, "one/two", AT_REMOVEDIR) == 0);
    CHECK(unlinkat(directory.descriptor, "one", AT_REMOVEDIR) == 0);
    CHECK(unlinkat(directory.descriptor, "collision-success", AT_REMOVEDIR) == 0);
    CHECK(unlinkat(outside.descriptor, "target", 0) == 0);
    destroy_test_directory(&outside);
    destroy_test_directory(&directory);
    return 0;
}

static int test_staging_cancel_deadline_and_commit(void) {
    TestDirectory directory;
    TestDirectory outside;
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsLane *lane;
    RRuntimeDarwinFsPrepareResult preparation;
    RRuntimeDarwinFsSubmitResult submission;
    RRuntimeDarwinFsResult result;
    struct stat metadata;
    uint64_t native_sequence;
    uint64_t cleanup_count;
    size_t spin;
    _Bool reached;
    _Bool signal_accepted;
    _Bool staging_mode_valid;
    _Bool target_absent;
    _Bool outside_clear;
    char staging_name[NAME_MAX + 1U];
    mode_t previous_umask;
    int staging_found;

    CHECK(create_test_directory(&directory) == 0);
    CHECK(create_test_directory(&outside) == 0);
    lane = create_lane(&allocator, 3U);
    CHECK(lane != NULL);

    cleanup_count = r_runtime_darwin_fs_testing_staging_cleanup_count();
    previous_umask = umask(0022);
    r_runtime_darwin_fs_testing_pause_after_staging(1);
    preparation = r_runtime_darwin_fs_prepare_create_directory_at(
        lane, directory.descriptor, "cancelled-stage", S_IRWXU, 0, 1, UINT64_C(0));
    submission = activate_prepared(&preparation);
    reached = 0;
    if (submission.status == R_RUNTIME_DARWIN_FS_START_OK) {
        for (spin = 0U; spin < 1000000U; ++spin) {
            if (r_runtime_darwin_fs_testing_staging_reached()) {
                reached = 1;
                break;
            }
            (void)sched_yield();
        }
    }
    staging_found =
        reached ? find_staging_entry(directory.descriptor, staging_name, sizeof(staging_name)) : -1;
    staging_mode_valid =
        staging_found == 1 &&
        fstatat(directory.descriptor, staging_name, &metadata, AT_SYMLINK_NOFOLLOW) == 0 &&
        S_ISDIR(metadata.st_mode) &&
        (metadata.st_mode & ACCESSPERMS) == (S_IRWXU & ~(mode_t)0022) &&
        (metadata.st_mode & (S_ISUID | S_ISGID | S_ISVTX)) == 0;
    errno = 0;
    target_absent =
        fstatat(directory.descriptor, "cancelled-stage", &metadata, AT_SYMLINK_NOFOLLOW) != 0 &&
        errno == ENOENT;
    outside_clear = directory_has_staging_entry(outside.descriptor) == 0;
    (void)umask(previous_umask);
    signal_accepted = reached && r_runtime_darwin_fs_request_cancel(submission.request);
    r_runtime_darwin_fs_testing_pause_after_staging(0);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK);
    CHECK(reached);
    CHECK(staging_found == 1);
    CHECK(staging_mode_valid);
    CHECK(target_absent);
    CHECK(outside_clear);
    CHECK(signal_accepted);
    result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_CANCELLED);
    CHECK(result.native_entered);
    CHECK(!result.committed);
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(r_runtime_darwin_fs_testing_staging_cleanup_count() == cleanup_count + UINT64_C(1));
    CHECK(fstatat(directory.descriptor, "cancelled-stage", &metadata, 0) != 0 && errno == ENOENT);
    CHECK(directory_has_staging_entry(directory.descriptor) == 0);

    cleanup_count = r_runtime_darwin_fs_testing_staging_cleanup_count();
    r_runtime_darwin_fs_testing_pause_after_staging(1);
    preparation = r_runtime_darwin_fs_prepare_create_directory_at(
        lane, directory.descriptor, "timed-stage", S_IRWXU, 0, 1, UINT64_C(0));
    submission = activate_prepared(&preparation);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK);
    reached = 0;
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_runtime_darwin_fs_testing_staging_reached()) {
            reached = 1;
            break;
        }
        (void)sched_yield();
    }
    signal_accepted = reached && r_runtime_darwin_fs_request_deadline_expired(submission.request);
    r_runtime_darwin_fs_testing_pause_after_staging(0);
    CHECK(reached);
    CHECK(signal_accepted);
    result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_TIMED_OUT);
    CHECK(result.native_entered);
    CHECK(!result.committed);
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(r_runtime_darwin_fs_testing_staging_cleanup_count() == cleanup_count + UINT64_C(1));
    CHECK(fstatat(directory.descriptor, "timed-stage", &metadata, 0) != 0 && errno == ENOENT);
    CHECK(directory_has_staging_entry(directory.descriptor) == 0);

    cleanup_count = r_runtime_darwin_fs_testing_staging_cleanup_count();
    r_runtime_darwin_fs_lane_testing_pause_after_native(lane, 1);
    native_sequence = r_runtime_darwin_fs_lane_testing_native_sequence(lane);
    preparation = r_runtime_darwin_fs_prepare_create_directory_at(
        lane, directory.descriptor, "committed-stage", S_IRWXU, 0, 1, UINT64_C(0));
    submission = activate_prepared(&preparation);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK);
    reached = 0;
    for (spin = 0U; spin < 1000000U; ++spin) {
        if (r_runtime_darwin_fs_lane_testing_native_sequence(lane) != native_sequence) {
            reached = 1;
            break;
        }
        (void)sched_yield();
    }
    signal_accepted = reached && r_runtime_darwin_fs_request_cancel(submission.request);
    r_runtime_darwin_fs_lane_testing_pause_after_native(lane, 0);
    CHECK(reached);
    CHECK(signal_accepted);
    result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE);
    CHECK(result.native_error == 0);
    CHECK(result.committed);
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(r_runtime_darwin_fs_testing_staging_cleanup_count() == cleanup_count);
    CHECK(fstatat(directory.descriptor, "committed-stage", &metadata, 0) == 0);
    CHECK(S_ISDIR(metadata.st_mode));
    CHECK(directory_has_staging_entry(directory.descriptor) == 0);

    r_runtime_darwin_fs_lane_destroy(lane);
    CHECK(unlinkat(directory.descriptor, "committed-stage", AT_REMOVEDIR) == 0);
    destroy_test_directory(&outside);
    destroy_test_directory(&directory);
    return 0;
}

static int test_beneath_symlink_swap_race(void) {
    TestDirectory directory;
    TestDirectory outside;
    TestBeneathRace context;
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsLane *lane;
    RRuntimeDarwinFsPrepareResult preparation;
    RRuntimeDarwinFsSubmitResult submission;
    RRuntimeDarwinFsResult result;
    struct stat metadata;
    pthread_t thread;
    size_t iteration;
    _Bool race_failed = 0;
    int descriptor;

    CHECK(create_test_directory(&directory) == 0);
    CHECK(create_test_directory(&outside) == 0);
    CHECK(mkdirat(directory.descriptor, "pivot", S_IRWXU) == 0);
    descriptor = create_file_at(directory.descriptor, "pivot/victim");
    CHECK(descriptor >= 0);
    CHECK(close(descriptor) == 0);
    descriptor = create_file_at(outside.descriptor, "victim");
    CHECK(descriptor >= 0);
    CHECK(close(descriptor) == 0);
    CHECK(symlinkat(outside.path, directory.descriptor, "pivot-link") == 0);
    lane = create_lane(&allocator, 1U);
    CHECK(lane != NULL);
    context.directory_fd = directory.descriptor;
    atomic_init(&context.stop, 0);
    atomic_init(&context.failed, 0);
    CHECK(pthread_create(&thread, NULL, swap_directory_and_symlink, &context) == 0);

    for (iteration = 0U; iteration < 512U; ++iteration) {
        preparation = r_runtime_darwin_fs_prepare_unlink_at(
            lane, directory.descriptor, "pivot/victim", 0, 1, UINT64_C(0));
        submission = activate_prepared(&preparation);
        if (submission.status != R_RUNTIME_DARWIN_FS_START_OK) {
            race_failed = 1;
            break;
        }
        result = r_runtime_darwin_fs_request_wait(submission.request);
        if (result.terminal_event != R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE ||
            (result.native_error != 0 && result.native_error != ENOENT &&
             !confinement_error(result.native_error))) {
            race_failed = 1;
        }
        r_runtime_darwin_fs_request_release(submission.request);
        if (fstatat(outside.descriptor, "victim", &metadata, 0) != 0 ||
            !S_ISREG(metadata.st_mode)) {
            race_failed = 1;
        }
        if (race_failed) {
            break;
        }
    }
    atomic_store_explicit(&context.stop, 1, memory_order_release);
    CHECK(pthread_join(thread, NULL) == 0);
    CHECK(!atomic_load_explicit(&context.failed, memory_order_acquire));
    CHECK(!race_failed);

    r_runtime_darwin_fs_lane_destroy(lane);
    errno = 0;
    if (unlinkat(directory.descriptor, "pivot/victim", 0) != 0) {
        CHECK(errno == ENOENT);
    }
    CHECK(unlinkat(directory.descriptor, "pivot", AT_REMOVEDIR) == 0);
    CHECK(unlinkat(directory.descriptor, "pivot-link", 0) == 0);
    CHECK(unlinkat(outside.descriptor, "victim", 0) == 0);
    destroy_test_directory(&outside);
    destroy_test_directory(&directory);
    return 0;
}

static int test_prepared_seek_late_binding(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsLane *lane;
    RRuntimeDarwinFsPrepareResult preparation;
    RRuntimeDarwinFsPreparedRequest *prepared_alias;
    RRuntimeDarwinFsSubmitResult submission;
    RRuntimeDarwinFsResult result;
    TestDirectory directory;
    int descriptor;

    CHECK(create_test_directory(&directory) == 0);
    descriptor = create_file_at(directory.descriptor, "late-seek");
    CHECK(descriptor >= 0);
    lane = create_lane(&allocator, 4U);
    CHECK(lane != NULL);

    preparation =
        r_runtime_darwin_fs_prepare_seek(lane, descriptor, (off_t)0, SEEK_SET, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_FS_START_OK && preparation.prepared != NULL);
    CHECK(!r_runtime_darwin_fs_prepared_set_seek(preparation.prepared, (off_t)2, SEEK_CUR));
    CHECK(r_runtime_darwin_fs_prepared_set_seek(preparation.prepared, (off_t)2, SEEK_SET));
    prepared_alias = preparation.prepared;
    submission = activate_prepared(&preparation);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK && submission.request != NULL);
    CHECK(preparation.prepared == NULL);
    CHECK(!r_runtime_darwin_fs_prepared_set_seek(prepared_alias, (off_t)1, SEEK_SET));
    result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_FS_SEEK);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE);
    CHECK(result.native_error == 0 && result.native_return_value == (off_t)2);
    CHECK(lseek(descriptor, (off_t)0, SEEK_CUR) == (off_t)2);
    r_runtime_darwin_fs_request_release(submission.request);

    preparation =
        r_runtime_darwin_fs_prepare_seek(lane, descriptor, (off_t)0, SEEK_SET, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_FS_START_OK && preparation.prepared != NULL);
    CHECK(r_runtime_darwin_fs_prepared_set_seek(preparation.prepared, (off_t)-1, SEEK_END));
    submission = activate_prepared(&preparation);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_SEEK, &result) == 0);
    CHECK(result.native_return_value == (off_t)4);
    CHECK(lseek(descriptor, (off_t)0, SEEK_CUR) == (off_t)4);
    r_runtime_darwin_fs_request_release(submission.request);

    preparation = r_runtime_darwin_fs_prepare_fstat(lane, descriptor, UINT64_C(0));
    CHECK(preparation.status == R_RUNTIME_DARWIN_FS_START_OK && preparation.prepared != NULL);
    CHECK(!r_runtime_darwin_fs_prepared_set_seek(preparation.prepared, (off_t)0, SEEK_SET));
    r_runtime_darwin_fs_prepared_abort(&preparation.prepared);

    r_runtime_darwin_fs_lane_destroy(lane);
    CHECK(close(descriptor) == 0);
    CHECK(unlinkat(directory.descriptor, "late-seek", 0) == 0);
    destroy_test_directory(&directory);
    return 0;
}

static int test_late_bound_compound_operations(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsLane *lane;
    RRuntimeDarwinFsPrepareResult preparation;
    RRuntimeDarwinFsPreparedRequest *cleanup = NULL;
    RRuntimeDarwinFsSubmitResult submission;
    RRuntimeDarwinFsResult result;
    TestDirectory directory;
    struct stat metadata;
    char staging_name[R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY];
    char staging_source[R_RUNTIME_DARWIN_FS_STAGING_SOURCE_CAPACITY];
    int payload_descriptor;
    int file_descriptor;
    int formatted;
    uint64_t cleanup_count;

    CHECK(create_test_directory(&directory) == 0);
    lane = create_lane(&allocator, 16U);
    CHECK(lane != NULL);

    cleanup_count = r_runtime_darwin_fs_testing_staging_cleanup_count();
    preparation = r_runtime_darwin_fs_prepare_file_stage_late_bound(lane);
    CHECK(preparation.status == R_RUNTIME_DARWIN_FS_START_OK && preparation.prepared != NULL);
    r_runtime_darwin_fs_prepared_abort(&preparation.prepared);
    CHECK(preparation.prepared == NULL);
    CHECK(r_runtime_darwin_fs_testing_staging_cleanup_count() == cleanup_count);
    CHECK(directory_has_staging_entry(directory.descriptor) == 0);

    preparation = r_runtime_darwin_fs_prepare_file_stage_late_bound(lane);
    CHECK(preparation.status == R_RUNTIME_DARWIN_FS_START_OK && preparation.prepared != NULL);
    CHECK(r_runtime_darwin_fs_prepared_bind_file_stage(preparation.prepared, directory.descriptor));
    CHECK(
        !r_runtime_darwin_fs_prepared_bind_file_stage(preparation.prepared, directory.descriptor));
    submission = activate_prepared(&preparation);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_FILE_STAGE_CREATE, &result) == 0);
    CHECK(!result.committed);

    payload_descriptor = -1;
    CHECK(r_runtime_darwin_fs_request_take_file_stage(
        submission.request, &payload_descriptor, staging_name, &cleanup));
    CHECK(payload_descriptor >= 0 && cleanup != NULL);
    CHECK(strncmp(staging_name,
                  R_RUNTIME_DARWIN_FS_STAGING_PREFIX,
                  sizeof(R_RUNTIME_DARWIN_FS_STAGING_PREFIX) - 1U) == 0);
    CHECK(fstat(payload_descriptor, &metadata) == 0 && S_ISREG(metadata.st_mode));
    CHECK(fstatat(directory.descriptor, staging_name, &metadata, AT_SYMLINK_NOFOLLOW) == 0 &&
          S_ISDIR(metadata.st_mode));
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(close(payload_descriptor) == 0);
    payload_descriptor = -1;

    submission = r_runtime_darwin_fs_prepared_activate(&cleanup);
    CHECK(cleanup == NULL);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_FILE_STAGE_CLEANUP, &result) == 0);
    CHECK(result.committed);
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(r_runtime_darwin_fs_testing_staging_cleanup_count() == cleanup_count + UINT64_C(1));
    CHECK(directory_has_staging_entry(directory.descriptor) == 0);
    CHECK(fstat(directory.descriptor, &metadata) == 0 && S_ISDIR(metadata.st_mode));

    file_descriptor = create_file_at(directory.descriptor, "late-bound-fsync");
    CHECK(file_descriptor >= 0);
    preparation = r_runtime_darwin_fs_prepare_fsync_late_bound(lane, 0);
    CHECK(preparation.status == R_RUNTIME_DARWIN_FS_START_OK && preparation.prepared != NULL);
    CHECK(r_runtime_darwin_fs_prepared_bind_fsync(preparation.prepared, file_descriptor));
    submission = activate_prepared(&preparation);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_FSYNC, &result) == 0);
    CHECK(result.committed);
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(fstat(file_descriptor, &metadata) == 0 && S_ISREG(metadata.st_mode));

    preparation = r_runtime_darwin_fs_prepare_full_fsync_directory_at_late_bound(lane, ".", 1);
    CHECK(preparation.status == R_RUNTIME_DARWIN_FS_START_OK && preparation.prepared != NULL);
    CHECK(r_runtime_darwin_fs_prepared_bind_full_fsync_directory_at(preparation.prepared,
                                                                    directory.descriptor));
    submission = activate_prepared(&preparation);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_FULL_FSYNC_DIRECTORY_AT, &result) ==
          0);
    CHECK(result.committed);
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(fstat(directory.descriptor, &metadata) == 0 && S_ISDIR(metadata.st_mode));

    cleanup_count = r_runtime_darwin_fs_testing_staging_cleanup_count();
    preparation = r_runtime_darwin_fs_prepare_file_stage_late_bound(lane);
    CHECK(preparation.status == R_RUNTIME_DARWIN_FS_START_OK && preparation.prepared != NULL);
    CHECK(r_runtime_darwin_fs_prepared_bind_file_stage(preparation.prepared, directory.descriptor));
    submission = activate_prepared(&preparation);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_FILE_STAGE_CREATE, &result) == 0);
    payload_descriptor = -1;
    CHECK(r_runtime_darwin_fs_request_take_file_stage(
        submission.request, &payload_descriptor, staging_name, &cleanup));
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(payload_descriptor >= 0 && cleanup != NULL);
    CHECK(close(payload_descriptor) == 0);
    payload_descriptor = -1;

    formatted = snprintf(staging_source,
                         sizeof(staging_source),
                         "%s/%s",
                         staging_name,
                         R_RUNTIME_DARWIN_FS_STAGING_PAYLOAD_NAME);
    CHECK(formatted >= 0 && (size_t)formatted == sizeof(staging_source) - 1U);
    preparation =
        r_runtime_darwin_fs_prepare_staging_rename_late_bound(lane, "late-bound-published", 1);
    CHECK(preparation.status == R_RUNTIME_DARWIN_FS_START_OK && preparation.prepared != NULL);
    CHECK(r_runtime_darwin_fs_prepared_bind_staging_rename(
        preparation.prepared, directory.descriptor, staging_source));
    submission = activate_prepared(&preparation);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_RENAME_AT_NO_REPLACE, &result) == 0);
    CHECK(result.committed);
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(fstatat(directory.descriptor, "late-bound-published", &metadata, AT_SYMLINK_NOFOLLOW) ==
          0);
    CHECK(S_ISREG(metadata.st_mode));

    submission = r_runtime_darwin_fs_prepared_activate(&cleanup);
    CHECK(cleanup == NULL);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_FILE_STAGE_CLEANUP, &result) == 0);
    CHECK(result.committed);
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(r_runtime_darwin_fs_testing_staging_cleanup_count() == cleanup_count + UINT64_C(1));
    CHECK(directory_has_staging_entry(directory.descriptor) == 0);
    CHECK(fstat(directory.descriptor, &metadata) == 0 && S_ISDIR(metadata.st_mode));

    r_runtime_darwin_fs_lane_destroy(lane);
    CHECK(close(file_descriptor) == 0);
    CHECK(unlinkat(directory.descriptor, "late-bound-fsync", 0) == 0);
    CHECK(unlinkat(directory.descriptor, "late-bound-published", 0) == 0);
    destroy_test_directory(&directory);
    return 0;
}

static int test_file_stage_transfer_cleanup_and_cancel(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsLane *lane;
    RRuntimeDarwinFsPrepareResult preparation;
    RRuntimeDarwinFsPreparedRequest *cleanup = NULL;
    RRuntimeDarwinFsSubmitResult submission;
    RRuntimeDarwinFsResult result;
    TestDirectory directory;
    struct stat metadata;
    char name[R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY];
    int payload_descriptor = -1;
    uint64_t cleanup_count;
    size_t attempt;

    CHECK(create_test_directory(&directory) == 0);
    lane = create_lane(&allocator, 8U);
    CHECK(lane != NULL);

    preparation = r_runtime_darwin_fs_prepare_file_stage(lane, directory.descriptor, UINT64_C(0));
    submission = activate_prepared(&preparation);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_FILE_STAGE_CREATE, &result) == 0);
    CHECK(!result.committed);
    CHECK(r_runtime_darwin_fs_request_take_file_stage(
        submission.request, &payload_descriptor, name, &cleanup));
    CHECK(payload_descriptor >= 0 && cleanup != NULL);
    CHECK(strncmp(name,
                  R_RUNTIME_DARWIN_FS_STAGING_PREFIX,
                  sizeof(R_RUNTIME_DARWIN_FS_STAGING_PREFIX) - 1U) == 0);
    CHECK(fstatat(directory.descriptor, name, &metadata, AT_SYMLINK_NOFOLLOW) == 0 &&
          S_ISDIR(metadata.st_mode));
    CHECK((metadata.st_mode & (mode_t)ACCESSPERMS) == S_IRWXU);
    CHECK(fstat(payload_descriptor, &metadata) == 0 && S_ISREG(metadata.st_mode));
    CHECK((metadata.st_mode & (mode_t)ACCESSPERMS) == (S_IRUSR | S_IWUSR));
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(close(payload_descriptor) == 0);
    preparation.prepared = cleanup;
    preparation.status = R_RUNTIME_DARWIN_FS_START_OK;
    preparation.native_error = 0;
    submission = activate_prepared(&preparation);
    cleanup = NULL;
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK && submission.request != NULL);
    CHECK(!r_runtime_darwin_fs_request_cancel(submission.request));
    result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result.operation == R_RUNTIME_DARWIN_FS_FILE_STAGE_CLEANUP);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE &&
          result.native_error == 0 && result.committed);
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(directory_has_staging_entry(directory.descriptor) == 0);

    cleanup_count = r_runtime_darwin_fs_testing_staging_cleanup_count();
    r_runtime_darwin_fs_testing_pause_after_staging(1);
    preparation = r_runtime_darwin_fs_prepare_file_stage(lane, directory.descriptor, UINT64_C(0));
    submission = activate_prepared(&preparation);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK && submission.request != NULL);
    for (attempt = 0U; attempt != 100000U; ++attempt) {
        if (r_runtime_darwin_fs_testing_staging_reached()) {
            break;
        }
        (void)sched_yield();
    }
    CHECK(attempt != 100000U);
    CHECK(r_runtime_darwin_fs_request_cancel(submission.request));
    r_runtime_darwin_fs_testing_pause_after_staging(0);
    result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_CANCELLED);
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(r_runtime_darwin_fs_testing_staging_cleanup_count() == cleanup_count + UINT64_C(1));
    CHECK(directory_has_staging_entry(directory.descriptor) == 0);

    r_runtime_darwin_fs_testing_force_staging_collisions(R_RUNTIME_DARWIN_FS_CREATE_RETRY_LIMIT);
    preparation = r_runtime_darwin_fs_prepare_file_stage(lane, directory.descriptor, UINT64_C(0));
    submission = activate_prepared(&preparation);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_OK && submission.request != NULL);
    result = r_runtime_darwin_fs_request_wait(submission.request);
    CHECK(result.terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE &&
          result.native_error == EAGAIN && !result.committed);
    r_runtime_darwin_fs_request_release(submission.request);
    CHECK(directory_has_staging_entry(directory.descriptor) == 0);

    preparation =
        r_runtime_darwin_fs_prepare_full_fsync_directory_at(lane, directory.descriptor, ".", 1);
    submission = activate_prepared(&preparation);
    CHECK(check_native_success(submission, R_RUNTIME_DARWIN_FS_FULL_FSYNC_DIRECTORY_AT, &result) ==
          0);
    CHECK(result.committed);
    r_runtime_darwin_fs_request_release(submission.request);

    r_runtime_darwin_fs_lane_destroy(lane);
    destroy_test_directory(&directory);
    return 0;
}

static int test_allocator_failure_sweep(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsLaneCreateResult lane_result;
    RRuntimeDarwinFsLane *lane;
    RRuntimeDarwinFsSubmitResult submission;
    struct attrlist attributes;
    int descriptor;
    int directory_descriptor;

    r_runtime_allocator_initialize(&allocator);
    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    lane_result = r_runtime_darwin_fs_lane_create(&allocator, 1U);
    CHECK(lane_result.status == R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED);
    CHECK(lane_result.lane == NULL);
    CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));

    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    lane_result = r_runtime_darwin_fs_lane_create(&allocator, 1U);
    CHECK(lane_result.status == R_RUNTIME_DARWIN_FS_START_OK);
    CHECK(lane_result.lane != NULL);
    lane = lane_result.lane;
    descriptor = open("/dev/null", O_RDONLY | O_CLOEXEC);
    directory_descriptor = open("/tmp", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    CHECK(descriptor >= 0);
    CHECK(directory_descriptor >= 0);

    r_runtime_allocator_set_failure(&allocator, UINT64_C(1));
    submission = r_runtime_darwin_fs_submit_fstat(lane, descriptor);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED);
    CHECK(submission.request == NULL);
    CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(1));

    r_runtime_allocator_set_failure(&allocator, UINT64_C(2));
    submission = r_runtime_darwin_fs_submit_open_at(
        lane, directory_descriptor, "allocation-failure", O_RDONLY, 0);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED);
    CHECK(submission.request == NULL);
    CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(2));

    (void)memset(&attributes, 0, sizeof(attributes));
    attributes.bitmapcount = ATTR_BIT_MAP_COUNT;
    attributes.commonattr = ATTR_CMN_RETURNED_ATTRS | ATTR_CMN_NAME;
    r_runtime_allocator_set_failure(&allocator, UINT64_C(2));
    submission =
        r_runtime_darwin_fs_submit_enumerate(lane, directory_descriptor, &attributes, 4096U);
    CHECK(submission.status == R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED);
    CHECK(submission.request == NULL);
    CHECK(r_runtime_allocator_attempt_count(&allocator) == UINT64_C(2));

    r_runtime_allocator_set_failure(&allocator, UINT64_C(0));
    CHECK(close(descriptor) == 0);
    CHECK(close(directory_descriptor) == 0);
    r_runtime_darwin_fs_lane_destroy(lane);
    return 0;
}

int main(void) {
    CHECK(test_capacity_and_queued_cancellation() == 0);
    CHECK(test_fifo_and_parallel_entry() == 0);
    CHECK(test_entered_cancel_and_deadline_acknowledgement() == 0);
    CHECK(test_cancelled_enumeration_is_cached() == 0);
    CHECK(test_committed_native_result_wins_cancel() == 0);
    CHECK(test_open_commit_classification() == 0);
    CHECK(test_open_or_create_race_is_bounded() == 0);
    CHECK(test_atomic_beneath_native_flags() == 0);
    CHECK(test_submission_retains_path_and_directory() == 0);
    CHECK(test_destroy_terminalizes_queued_close() == 0);
    CHECK(test_typed_native_calls() == 0);
    CHECK(test_beneath_namespace_and_metadata() == 0);
    CHECK(test_staging_cancel_deadline_and_commit() == 0);
    CHECK(test_beneath_symlink_swap_race() == 0);
    CHECK(test_prepared_seek_late_binding() == 0);
    CHECK(test_late_bound_compound_operations() == 0);
    CHECK(test_file_stage_transfer_cleanup_and_cancel() == 0);
    CHECK(test_allocator_failure_sweep() == 0);
    (void)puts("Darwin filesystem adapter lane tests passed");
    return 0;
}
