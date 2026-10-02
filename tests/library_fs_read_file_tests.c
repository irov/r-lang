#include "r_std_fs.h"

#include "r_library_fs_internal.h"
#include "r_runtime_darwin_fs_lane.h"
#include "r_runtime_darwin_io.h"
#include "r_runtime_task.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stddef.h>
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

static int write_file_at(int directory, const char *name, const uint8_t *bytes, size_t length) {
    int descriptor =
        openat(directory, name, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, S_IRUSR | S_IWUSR);
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

static int await_metadata(RStdFsTaskStartResult started, RStdFsMetadataResult *result) {
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

static int await_array(RStdFsTaskStartResult started, RStdFsArrayResult *result) {
    if (!started.is_ok || started.task == NULL) {
        return 0;
    }
    return r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK;
}

static int
array_is_exact_u8(const RRuntimeArray *array, const uint8_t *expected, size_t expected_length) {
    return array->allocator != NULL && array->element.size == sizeof(uint8_t) &&
           array->element.alignment == _Alignof(uint8_t) &&
           array->element.move_initialize == NULL && array->element.drop == NULL &&
           array->length == expected_length && array->capacity >= array->length &&
           ((expected_length == 0U && array->data == NULL) ||
            (expected_length != 0U && array->data != NULL &&
             memcmp(array->data, expected, expected_length) == 0));
}

static int error_array_is_empty(const RStdFsArrayResult *result) {
    return result->r_tag == UINT32_C(1);
}

typedef _Bool (*RTestConditionFn)(void);
typedef uint64_t (*RTestCountFn)(void);

typedef struct RTestReadStartThread {
    const RStdFsPath *path;
    size_t limit;
    RStdFsDeadline deadline;
    RStdFsTaskStartResult result;
} RTestReadStartThread;

typedef struct RTestExecutorStopContext {
    _Bool stopped;
} RTestExecutorStopContext;

static void *start_read_file_thread(void *context_pointer) {
    RTestReadStartThread *context = context_pointer;

    context->result = r_std_fs_read_file(context->path, context->limit, context->deadline);
    return NULL;
}

static void probe_task_body(RRuntimeTaskExecution *execution, void *payload, void *result) {
    (void)execution;
    (void)payload;
    (void)result;
}

static void *stop_executor_thread(void *context_pointer) {
    RTestExecutorStopContext *context = context_pointer;

    context->stopped = r_runtime_executor_lifecycle_stop();
    return NULL;
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

static int wait_for_condition(RTestConditionFn condition) {
    size_t attempt;

    for (attempt = 0U; attempt != 5000U; ++attempt) {
        if (condition()) {
            return 1;
        }
        sleep_milliseconds(1L);
    }
    return 0;
}

static int wait_for_count(RTestCountFn count, uint64_t minimum) {
    size_t attempt;

    for (attempt = 0U; attempt != 5000U; ++attempt) {
        if (count() >= minimum) {
            return 1;
        }
        sleep_milliseconds(1L);
    }
    return 0;
}

static int wait_for_executor_stop_selection(void) {
    const RRuntimeTypeInfo empty_type = {0U, 1U, NULL, NULL};
    size_t attempt;

    for (attempt = 0U; attempt != 5000U; ++attempt) {
        RRuntimeTaskPrepareResult preparation =
            r_runtime_task_start_prepare(empty_type, empty_type, probe_task_body);

        if (preparation.status == R_RUNTIME_TASK_START_RUNTIME_STOPPING) {
            return 1;
        }
        if (preparation.status != R_RUNTIME_TASK_START_OK || preparation.transaction == NULL) {
            return 0;
        }
        r_runtime_task_start_abort(&preparation.transaction);
        sleep_milliseconds(1L);
    }
    return 0;
}

static int wait_for_fs_native_sequence_change(uint64_t initial_sequence) {
    size_t attempt;

    for (attempt = 0U; attempt != 5000U; ++attempt) {
        if (r_runtime_darwin_fs_service_testing_native_sequence() != initial_sequence) {
            return 1;
        }
        sleep_milliseconds(1L);
    }
    return 0;
}

static int wait_for_fs_entry_sequence_change(uint64_t initial_sequence) {
    size_t attempt;

    for (attempt = 0U; attempt != 5000U; ++attempt) {
        if (r_runtime_darwin_fs_service_testing_entry_sequence() != initial_sequence) {
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

static int test_metadata_contract(RRuntimeAllocator *allocator,
                                  const char *regular_path,
                                  const char *symlink_path,
                                  const char *symlink_loop_path,
                                  size_t regular_size) {
    const RStdFsDeadline no_deadline = {0};
    RStdFsPathResult path = test_path(allocator, regular_path);
    RStdFsMetadataResult result = {0};

    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(await_metadata(r_std_fs_metadata(&path.value, no_deadline), &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(0) &&
                 result.r_payload.r_ok.kind == R_STD_FS_FILE_KIND_REGULAR &&
                 result.r_payload.r_ok.size == regular_size);
    r_std_fs_path_destroy(&path.value);

    path = test_path(allocator, symlink_path);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(await_metadata(r_std_fs_metadata(&path.value, no_deadline), &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(0) &&
                 result.r_payload.r_ok.kind == R_STD_FS_FILE_KIND_SYMLINK);
    r_std_fs_path_destroy(&path.value);

    path = test_path(allocator, symlink_loop_path);
    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    (void)memset(&result, 0, sizeof(result));
    R_TEST_CHECK(await_metadata(r_std_fs_metadata(&path.value, no_deadline), &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(1) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_TOO_MANY_LINKS);
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int expect_read_success(RRuntimeAllocator *allocator,
                               const char *path_text,
                               size_t limit,
                               const uint8_t *expected,
                               size_t expected_length) {
    const RStdFsDeadline no_deadline = {0};
    RStdFsPathResult path = test_path(allocator, path_text);
    RStdFsArrayResult result = {0};

    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(await_array(r_std_fs_read_file(&path.value, limit, no_deadline), &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(0) &&
                 array_is_exact_u8(&result.r_payload.r_ok, expected, expected_length));
    r_runtime_array_destroy(&result.r_payload.r_ok);
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int expect_read_error(RRuntimeAllocator *allocator,
                             const char *path_text,
                             size_t limit,
                             RStdFsErrorCode expected_error) {
    const RStdFsDeadline no_deadline = {0};
    RStdFsPathResult path = test_path(allocator, path_text);
    RStdFsArrayResult result = {0};

    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(await_array(r_std_fs_read_file(&path.value, limit, no_deadline), &result));
    R_TEST_CHECK(error_array_is_empty(&result) && result.r_payload.r_err.code == expected_error);
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_beneath_contract(RRuntimeAllocator *allocator,
                                 const char *root_path_text,
                                 const uint8_t *expected,
                                 size_t expected_length) {
    const RStdFsDeadline no_deadline = {0};
    RStdFsPathResult root_path = test_path(allocator, root_path_text);
    RStdFsDirectoryResult directory_result = {0};
    RStdFsDirectory root = {0};
    RStdFsPathResult relative;
    RStdFsArrayResult result = {0};

    R_TEST_CHECK(root_path.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(
        await_directory(r_std_fs_open_directory(&root_path.value, no_deadline), &directory_result));
    R_TEST_CHECK(directory_result.r_tag == UINT32_C(0));
    root = directory_result.r_payload.r_ok;
    directory_result.r_payload.r_ok.storage = NULL;
    r_std_fs_path_destroy(&root_path.value);

    relative = test_path(allocator, "regular");
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(await_array(
        r_std_fs_read_file_beneath(&root, &relative.value, expected_length, no_deadline), &result));
    R_TEST_CHECK(result.r_tag == UINT32_C(0) &&
                 array_is_exact_u8(&result.r_payload.r_ok, expected, expected_length));
    r_runtime_array_destroy(&result.r_payload.r_ok);
    r_std_fs_path_destroy(&relative.value);

    relative = test_path(allocator, "final-link");
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    (void)memset(&result, 0, sizeof(result));
    R_TEST_CHECK(await_array(
        r_std_fs_read_file_beneath(&root, &relative.value, SIZE_MAX, no_deadline), &result));
    R_TEST_CHECK(error_array_is_empty(&result) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_RELATIVE_PATH);
    r_std_fs_path_destroy(&relative.value);

    relative = test_path(allocator, "intermediate-link/nested");
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS);
    (void)memset(&result, 0, sizeof(result));
    R_TEST_CHECK(await_array(
        r_std_fs_read_file_beneath(&root, &relative.value, SIZE_MAX, no_deadline), &result));
    R_TEST_CHECK(error_array_is_empty(&result) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_INVALID_RELATIVE_PATH);
    r_std_fs_path_destroy(&relative.value);
    r_std_fs_directory_destroy(&root);
    return 0;
}

static int test_start_failure_preserves_path(RRuntimeAllocator *allocator, const char *path_text) {
    const RStdFsDeadline no_deadline = {0};
    RStdFsPathResult path = test_path(allocator, path_text);
    RStdFsTaskStartResult started;
    RStdFsStringResult snapshot;

    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS && path.value.storage != NULL);
    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    started = r_std_fs_read_file(&path.value, SIZE_MAX, no_deadline);
    R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                 started.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
    R_TEST_CHECK(path.value.storage != NULL);
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    snapshot = r_std_fs_path_to_utf8(&path.value);
    R_TEST_CHECK(snapshot.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(r_std_string_as_str(&snapshot.value).length == strlen(path_text));
    r_runtime_string_destroy(&snapshot.value);
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_expired_deadline(RRuntimeAllocator *allocator, const char *path_text) {
    RStdFsPathResult path = test_path(allocator, path_text);
    RStdTimeInstantResult now = r_std_time_monotonic_now();
    RStdFsArrayResult result = {0};

    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS && now.is_ok);
    R_TEST_CHECK(await_array(
        r_std_fs_read_file(&path.value, SIZE_MAX, (RStdFsDeadline){1, now.value}), &result));
    R_TEST_CHECK(error_array_is_empty(&result) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_TIMED_OUT &&
                 result.r_payload.r_err.native_code == INT64_C(0));
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_closed_precedes_expired_deadline(RRuntimeAllocator *allocator,
                                                 const char *root_path_text) {
    const RStdFsDeadline no_deadline = {0};
    RStdFsPathResult root_path = test_path(allocator, root_path_text);
    RStdFsDirectoryResult directory_result = {0};
    RStdFsDirectory root = {0};
    RStdFsPathResult relative;
    RStdTimeInstantResult now;
    RStdFsArrayResult result = {0};
    RLibraryFsHandleStorage *storage;
    RRuntimeDarwinIoHandle *payload_io = NULL;
    int descriptor = -1;
    int awaited;

    R_TEST_CHECK(root_path.status == R_STD_FS_CALL_SUCCESS);
    R_TEST_CHECK(
        await_directory(r_std_fs_open_directory(&root_path.value, no_deadline), &directory_result));
    R_TEST_CHECK(directory_result.r_tag == UINT32_C(0));
    root = directory_result.r_payload.r_ok;
    directory_result.r_payload.r_ok.storage = NULL;
    r_std_fs_path_destroy(&root_path.value);

    storage = r_library_internal_fs_directory_handle_storage(&root);
    R_TEST_CHECK(storage != NULL);
    R_TEST_CHECK(r_library_internal_fs_handle_reserve_close(storage, &descriptor, &payload_io));
    R_TEST_CHECK(descriptor >= 0 && payload_io == NULL);
    relative = test_path(allocator, "regular");
    now = r_std_time_monotonic_now();
    R_TEST_CHECK(relative.status == R_STD_FS_CALL_SUCCESS && now.is_ok);
    awaited = await_array(r_std_fs_read_file_beneath(
                              &root, &relative.value, SIZE_MAX, (RStdFsDeadline){1, now.value}),
                          &result);
    r_library_internal_fs_handle_abort_close(storage);
    R_TEST_CHECK(awaited);
    R_TEST_CHECK(error_array_is_empty(&result) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_CLOSED &&
                 result.r_payload.r_err.native_code == INT64_C(0));
    r_std_fs_path_destroy(&relative.value);
    r_std_fs_directory_destroy(&root);
    return 0;
}

static int test_deadline_rechecked_before_open(RRuntimeAllocator *allocator,
                                               const char *path_text) {
    RStdFsPathResult path = test_path(allocator, path_text);
    RStdTimeInstantResult now = r_std_time_monotonic_now();
    RStdTimeDurationResult delay = r_std_time_duration_from_parts(INT64_C(0), UINT32_C(250000000));
    RStdTimeInstantResult future;
    RTestReadStartThread start_context = {0};
    pthread_t start_thread;
    RStdFsArrayResult result = {0};
    uint64_t native_before;
    uint64_t native_after;
    int reached = 0;
    int awaited;
    int thread_created;
    int thread_joined = 0;

    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS && now.is_ok && delay.is_ok);
    future = r_std_time_instant_add(now.value, delay.value);
    R_TEST_CHECK(future.is_ok);
    r_library_internal_fs_read_file_testing_pause_before_start_deadline_check(1);
    native_before = r_runtime_darwin_fs_service_testing_native_sequence();
    start_context.path = &path.value;
    start_context.limit = SIZE_MAX;
    start_context.deadline = (RStdFsDeadline){1, future.value};
    thread_created =
        pthread_create(&start_thread, NULL, start_read_file_thread, &start_context) == 0;
    if (thread_created) {
        reached = wait_for_condition(
            r_library_internal_fs_read_file_testing_start_deadline_check_reached);
    }
    if (reached) {
        sleep_milliseconds(300L);
    }
    r_library_internal_fs_read_file_testing_pause_before_start_deadline_check(0);
    if (thread_created) {
        thread_joined = pthread_join(start_thread, NULL) == 0;
    }
    awaited = await_array(start_context.result, &result);
    native_after = r_runtime_darwin_fs_service_testing_native_sequence();

    R_TEST_CHECK(thread_created && thread_joined && reached && awaited);
    R_TEST_CHECK(error_array_is_empty(&result) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_TIMED_OUT &&
                 result.r_payload.r_err.native_code == INT64_C(0));
    R_TEST_CHECK(native_after == native_before);
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_close_precedes_first_read(RRuntimeAllocator *allocator,
                                          const char *path_text,
                                          const uint8_t *expected,
                                          size_t expected_length) {
    const RStdFsDeadline no_deadline = {0};
    RStdFsPathResult path = test_path(allocator, path_text);
    RStdFsTaskStartResult started = {0};
    RStdFsArrayResult result = {0};
    uint64_t submissions_at_close = UINT64_MAX;
    int reached = 0;
    int awaited;

    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    r_library_internal_fs_read_file_testing_reset_observations();
    r_library_internal_fs_read_file_testing_pause_before_close_completion(1);
    started = r_std_fs_read_file(&path.value, expected_length, no_deadline);
    if (started.is_ok && started.task != NULL) {
        reached =
            wait_for_condition(r_library_internal_fs_read_file_testing_close_completion_reached);
    }
    submissions_at_close = r_library_internal_fs_read_file_testing_read_submission_count();
    r_library_internal_fs_read_file_testing_pause_before_close_completion(0);
    awaited = await_array(started, &result);

    R_TEST_CHECK(reached && submissions_at_close == UINT64_C(0) && awaited);
    R_TEST_CHECK(result.r_tag == UINT32_C(0) &&
                 array_is_exact_u8(&result.r_payload.r_ok, expected, expected_length));
    R_TEST_CHECK(r_library_internal_fs_read_file_testing_read_submission_count() > UINT64_C(0));
    r_runtime_array_destroy(&result.r_payload.r_ok);
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_deadline_during_close(RRuntimeAllocator *allocator, const char *path_text) {
    RStdFsPathResult path = test_path(allocator, path_text);
    RStdFsDeadline deadline = {0};
    RStdFsTaskStartResult started = {0};
    RStdFsArrayResult result = {0};
    uint64_t submissions;
    int close_reached = 0;
    int deadline_reached = 0;
    int awaited;

    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS &&
                 deadline_after_milliseconds(UINT32_C(250), &deadline));
    r_library_internal_fs_read_file_testing_reset_observations();
    r_library_internal_fs_read_file_testing_pause_before_close_completion(1);
    started = r_std_fs_read_file(&path.value, SIZE_MAX, deadline);
    if (started.is_ok && started.task != NULL) {
        close_reached =
            wait_for_condition(r_library_internal_fs_read_file_testing_close_completion_reached);
    }
    if (close_reached) {
        deadline_reached =
            wait_for_condition(r_library_internal_fs_read_file_testing_deadline_expired_observed);
    }
    submissions = r_library_internal_fs_read_file_testing_read_submission_count();
    r_library_internal_fs_read_file_testing_pause_before_close_completion(0);
    awaited = await_array(started, &result);

    R_TEST_CHECK(close_reached && deadline_reached && awaited);
    R_TEST_CHECK(error_array_is_empty(&result) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_TIMED_OUT &&
                 result.r_payload.r_err.native_code == INT64_C(0));
    R_TEST_CHECK(submissions == UINT64_C(0));
    R_TEST_CHECK(r_library_internal_fs_read_file_testing_acknowledgement_count() == UINT64_C(1));
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_deadline_during_active_read(RRuntimeAllocator *allocator, const char *path_text) {
    RStdFsPathResult path = test_path(allocator, path_text);
    RStdFsDeadline deadline = {0};
    RStdFsTaskStartResult started = {0};
    RStdFsArrayResult result = {0};
    int activation_reached = 0;
    int deadline_reached = 0;
    int awaited;

    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS &&
                 deadline_after_milliseconds(UINT32_C(250), &deadline));
    r_library_internal_fs_read_file_testing_reset_observations();
    r_library_internal_fs_read_file_testing_pause_after_read_activation(1);
    started = r_std_fs_read_file(&path.value, SIZE_MAX, deadline);
    if (started.is_ok && started.task != NULL) {
        activation_reached = wait_for_condition(
            r_library_internal_fs_read_file_testing_after_read_activation_reached);
    }
    if (activation_reached) {
        deadline_reached =
            wait_for_condition(r_library_internal_fs_read_file_testing_deadline_expired_observed);
    }
    r_library_internal_fs_read_file_testing_pause_after_read_activation(0);
    awaited = await_array(started, &result);

    R_TEST_CHECK(activation_reached && deadline_reached && awaited);
    R_TEST_CHECK(error_array_is_empty(&result) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_TIMED_OUT &&
                 result.r_payload.r_err.native_code == INT64_C(0));
    R_TEST_CHECK(r_library_internal_fs_read_file_testing_read_submission_count() == UINT64_C(1));
    R_TEST_CHECK(r_library_internal_fs_read_file_testing_acknowledgement_count() == UINT64_C(1));
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_materialization_failure_stages(RRuntimeAllocator *allocator,
                                               const char *path_text,
                                               const uint8_t *expected,
                                               size_t expected_length) {
    static const RRuntimeDarwinIoHandleCreateFailureStage stages[] = {
        R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_ALLOCATION,
        R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_AFTER_DUPLICATE,
        R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_AFTER_QUEUE,
    };
    const RStdFsDeadline no_deadline = {0};
    size_t index;

    for (index = 0U; index != sizeof(stages) / sizeof(stages[0]); ++index) {
        RStdFsPathResult path = test_path(allocator, path_text);
        RStdFsArrayResult result = {0};

        R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
        r_runtime_darwin_io_testing_fail_handle_create_stage(stages[index]);
        R_TEST_CHECK(
            await_array(r_std_fs_read_file(&path.value, expected_length, no_deadline), &result));
        r_runtime_darwin_io_testing_fail_handle_create_stage(
            R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_NONE);
        R_TEST_CHECK(error_array_is_empty(&result) &&
                     result.r_payload.r_err.code == R_STD_FS_ERROR_RESOURCE_EXHAUSTED);
        if (stages[index] == R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_ALLOCATION) {
            R_TEST_CHECK(result.r_payload.r_err.native_code == INT64_C(0));
        } else {
            R_TEST_CHECK(result.r_payload.r_err.native_code == ENOMEM);
        }
        r_std_fs_path_destroy(&path.value);
        R_TEST_CHECK(expect_read_success(
                         allocator, path_text, expected_length, expected, expected_length) == 0);
    }
    return 0;
}

static int test_root_cleanup_failure(RRuntimeAllocator *allocator,
                                     const char *path_text,
                                     const uint8_t *expected,
                                     size_t expected_length) {
    const RStdFsDeadline no_deadline = {0};
    RStdFsPathResult path = test_path(allocator, path_text);
    RStdFsTaskStartResult started;
    RStdFsArrayResult result = {0};
    int awaited;

    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    r_runtime_darwin_io_testing_force_root_cleanup_error(EIO);
    started = r_std_fs_read_file(&path.value, expected_length, no_deadline);
    awaited = await_array(started, &result);
    r_runtime_darwin_io_testing_force_root_cleanup_error(0);

    R_TEST_CHECK(awaited);
    R_TEST_CHECK(error_array_is_empty(&result) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_OTHER &&
                 result.r_payload.r_err.native_code == EIO);
    r_std_fs_path_destroy(&path.value);
    R_TEST_CHECK(
        expect_read_success(allocator, path_text, expected_length, expected, expected_length) == 0);
    return 0;
}

static int test_allocation_failure_sweep(RRuntimeAllocator *allocator,
                                         const char *path_text,
                                         const uint8_t *expected,
                                         size_t expected_length) {
    const RStdFsDeadline no_deadline = {0};
    const uint64_t sweep_limit = UINT64_C(64);
    RStdFsPathResult path = test_path(allocator, path_text);
    uint64_t fail_at;
    _Bool reached_uninjected_success = 0;

    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    for (fail_at = UINT64_C(1); fail_at <= sweep_limit; ++fail_at) {
        RStdFsTaskStartResult started;
        RStdFsArrayResult result = {0};
        uint64_t attempts;
        int awaited = 0;

        r_runtime_allocator_set_failure(allocator, fail_at);
        started = r_std_fs_read_file(&path.value, expected_length, no_deadline);
        if (started.is_ok) {
            awaited = await_array(started, &result);
        }
        attempts = r_runtime_allocator_attempt_count(allocator);
        r_runtime_allocator_set_failure(allocator, UINT64_C(0));

        R_TEST_CHECK(path.value.storage != NULL);
        if (!started.is_ok) {
            R_TEST_CHECK(started.task == NULL &&
                         started.error == R_STD_ASYNC_START_ALLOCATION_FAILED);
        } else {
            R_TEST_CHECK(awaited);
            if (result.r_tag == UINT32_C(0)) {
                R_TEST_CHECK(array_is_exact_u8(&result.r_payload.r_ok, expected, expected_length));
                r_runtime_array_destroy(&result.r_payload.r_ok);
                if (attempts < fail_at) {
                    reached_uninjected_success = 1;
                    break;
                }
            } else {
                R_TEST_CHECK(error_array_is_empty(&result) &&
                             result.r_payload.r_err.code == R_STD_FS_ERROR_RESOURCE_EXHAUSTED);
            }
        }
        R_TEST_CHECK(expect_read_success(
                         allocator, path_text, expected_length, expected, expected_length) == 0);
    }
    R_TEST_CHECK(reached_uninjected_success);
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_cancel_before_read_activation(RRuntimeAllocator *allocator, const char *path_text) {
    const RStdFsDeadline no_deadline = {0};
    RStdFsPathResult path = test_path(allocator, path_text);
    RStdFsTaskStartResult started = {0};
    int activation_reached = 0;
    int cancel_reached = 0;
    int cancel_observed = 0;
    int acknowledged = 0;
    uint64_t submissions;

    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    r_library_internal_fs_read_file_testing_reset_observations();
    r_library_internal_fs_read_file_testing_pause_before_read_activation(1);
    r_library_internal_fs_read_file_testing_pause_before_cancel_report(1);
    started = r_std_fs_read_file(&path.value, SIZE_MAX, no_deadline);
    if (started.is_ok && started.task != NULL) {
        activation_reached =
            wait_for_condition(r_library_internal_fs_read_file_testing_read_activation_reached);
        r_runtime_task_cancel(&started.task);
        cancel_reached =
            wait_for_condition(r_library_internal_fs_read_file_testing_cancel_report_reached);
    }
    r_library_internal_fs_read_file_testing_pause_before_read_activation(0);
    if (activation_reached) {
        cancel_observed = wait_for_condition(
            r_library_internal_fs_read_file_testing_read_activation_cancel_observed);
    }
    submissions = r_library_internal_fs_read_file_testing_read_submission_count();
    r_library_internal_fs_read_file_testing_pause_before_cancel_report(0);
    if (cancel_reached) {
        acknowledged = wait_for_count(r_library_internal_fs_read_file_testing_acknowledgement_count,
                                      UINT64_C(1));
    }

    R_TEST_CHECK(started.is_ok && started.task == NULL);
    R_TEST_CHECK(activation_reached && cancel_reached && cancel_observed && acknowledged);
    R_TEST_CHECK(submissions == UINT64_C(0));
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_native_limit_precedes_task_cancellation(RRuntimeAllocator *allocator,
                                                        const char *path_text) {
    const RStdFsDeadline no_deadline = {0};
    RStdFsPathResult path = test_path(allocator, path_text);
    RStdFsTaskStartResult started = {0};
    RStdFsArrayResult result = {0};
    RTestExecutorStopContext stop_context = {0};
    pthread_t stop_thread;
    int completion_reached = 0;
    int stop_thread_created = 0;
    int stop_selected = 0;
    int stop_thread_joined = 0;
    int limit_observed = 0;
    int awaited = 0;
    int restarted = 0;
    uint64_t submissions;
    _Bool forced_completion;

    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    r_library_internal_fs_read_file_testing_reset_observations();
    r_library_internal_fs_read_file_testing_pause_before_read_completion(1);
    started = r_std_fs_read_file(&path.value, 0U, no_deadline);
    if (started.is_ok && started.task != NULL) {
        completion_reached =
            wait_for_condition(r_library_internal_fs_read_file_testing_read_completion_reached);
    }
    if (completion_reached) {
        stop_thread_created =
            pthread_create(&stop_thread, NULL, stop_executor_thread, &stop_context) == 0;
    }
    if (stop_thread_created) {
        stop_selected = wait_for_executor_stop_selection();
    }
    r_library_internal_fs_read_file_testing_pause_before_read_completion(0);
    if (stop_thread_created) {
        stop_thread_joined = pthread_join(stop_thread, NULL) == 0;
    }
    if (started.task != NULL) {
        awaited = r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK;
    }
    if (stop_thread_joined && stop_context.stopped) {
        restarted = r_runtime_executor_lifecycle_start(allocator) == R_RUNTIME_EXECUTOR_START_OK;
    }
    if (awaited) {
        limit_observed =
            wait_for_condition(r_library_internal_fs_read_file_testing_limit_after_cancel_observed);
    }
    submissions = r_library_internal_fs_read_file_testing_read_submission_count();
    forced_completion = r_library_internal_fs_read_file_testing_limit_forced_completion();

    R_TEST_CHECK(started.is_ok && started.task == NULL && awaited);
    R_TEST_CHECK(completion_reached && stop_thread_created && stop_selected && stop_thread_joined &&
                 stop_context.stopped && restarted);
    R_TEST_CHECK(error_array_is_empty(&result) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_FILE_TOO_LARGE);
    R_TEST_CHECK(limit_observed && submissions == UINT64_C(1) && !forced_completion);
    R_TEST_CHECK(r_library_internal_fs_read_file_testing_acknowledgement_count() == UINT64_C(1));
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_native_open_precedes_task_cancellation(RRuntimeAllocator *allocator,
                                                       const char *path_text) {
    const RStdFsDeadline no_deadline = {0};
    RStdFsPathResult path = test_path(allocator, path_text);
    RStdFsTaskStartResult started = {0};
    RStdFsArrayResult result = {0};
    RTestExecutorStopContext stop_context = {0};
    pthread_t stop_thread;
    uint64_t native_sequence;
    int native_reached = 0;
    int stop_thread_created = 0;
    int stop_selected = 0;
    int stop_thread_joined = 0;
    int awaited = 0;
    int restarted = 0;

    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    r_library_internal_fs_read_file_testing_reset_observations();
    r_runtime_darwin_fs_service_testing_pause_after_native(1);
    native_sequence = r_runtime_darwin_fs_service_testing_native_sequence();
    started = r_std_fs_read_file(&path.value, SIZE_MAX, no_deadline);
    if (started.is_ok && started.task != NULL) {
        native_reached = wait_for_fs_native_sequence_change(native_sequence);
    }
    if (native_reached) {
        stop_thread_created =
            pthread_create(&stop_thread, NULL, stop_executor_thread, &stop_context) == 0;
    }
    if (stop_thread_created) {
        stop_selected = wait_for_executor_stop_selection();
    }
    r_runtime_darwin_fs_service_testing_pause_after_native(0);
    if (stop_thread_created) {
        stop_thread_joined = pthread_join(stop_thread, NULL) == 0;
    }
    if (started.task != NULL) {
        awaited = r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK;
    }
    if (stop_thread_joined && stop_context.stopped) {
        restarted = r_runtime_executor_lifecycle_start(allocator) == R_RUNTIME_EXECUTOR_START_OK;
    }

    R_TEST_CHECK(started.is_ok && started.task == NULL && awaited);
    R_TEST_CHECK(native_reached && stop_thread_created && stop_selected && stop_thread_joined &&
                 stop_context.stopped && restarted);
    R_TEST_CHECK(error_array_is_empty(&result) &&
                 result.r_payload.r_err.code == R_STD_FS_ERROR_NOT_FOUND &&
                 result.r_payload.r_err.native_code == ENOENT);
    R_TEST_CHECK(r_library_internal_fs_read_file_testing_acknowledgement_count() == UINT64_C(1));
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_task_cancellation_precedes_open_result(RRuntimeAllocator *allocator,
                                                       const char *path_text) {
    const RStdFsDeadline no_deadline = {0};
    RStdFsPathResult path = test_path(allocator, path_text);
    RStdFsTaskStartResult started = {0};
    RStdFsArrayResult result = {0};
    RTestExecutorStopContext stop_context = {0};
    pthread_t stop_thread;
    RRuntimeTaskAwaitStatus await_status = R_RUNTIME_TASK_AWAIT_INVALID;
    uint64_t entry_sequence;
    int entry_reached = 0;
    int stop_thread_created = 0;
    int stop_selected = 0;
    int stop_thread_joined = 0;
    int restarted = 0;

    R_TEST_CHECK(path.status == R_STD_FS_CALL_SUCCESS);
    r_library_internal_fs_read_file_testing_reset_observations();
    r_runtime_darwin_fs_service_testing_pause_before_native(1);
    entry_sequence = r_runtime_darwin_fs_service_testing_entry_sequence();
    started = r_std_fs_read_file(&path.value, SIZE_MAX, no_deadline);
    if (started.is_ok && started.task != NULL) {
        entry_reached = wait_for_fs_entry_sequence_change(entry_sequence);
    }
    if (entry_reached) {
        stop_thread_created =
            pthread_create(&stop_thread, NULL, stop_executor_thread, &stop_context) == 0;
    }
    if (stop_thread_created) {
        stop_selected = wait_for_executor_stop_selection();
    }
    r_runtime_darwin_fs_service_testing_pause_before_native(0);
    if (stop_thread_created) {
        stop_thread_joined = pthread_join(stop_thread, NULL) == 0;
    }
    if (started.task != NULL) {
        await_status = r_runtime_task_await(&started.task, &result);
    }
    if (stop_thread_joined && stop_context.stopped) {
        restarted = r_runtime_executor_lifecycle_start(allocator) == R_RUNTIME_EXECUTOR_START_OK;
    }

    R_TEST_CHECK(started.is_ok && started.task == NULL &&
                 await_status == R_RUNTIME_TASK_AWAIT_CANCELLED);
    R_TEST_CHECK(entry_reached && stop_thread_created && stop_selected && stop_thread_joined &&
                 stop_context.stopped && restarted);
    R_TEST_CHECK(r_library_internal_fs_read_file_testing_acknowledgement_count() == UINT64_C(1));
    r_std_fs_path_destroy(&path.value);
    return 0;
}

static int test_read_file_public_contract(void) {
    static const uint8_t regular_bytes[] = {
        UINT8_C(0x52), UINT8_C(0x00), UINT8_C(0x7f), UINT8_C(0x80), UINT8_C(0xff)};
    static const size_t multi_length = 131073U;
    char temporary_directory[] = "/tmp/r-fs-read-file-XXXXXX";
    char regular_path[1024];
    char empty_path[1024];
    char missing_path[1024];
    char symlink_path[1024];
    char symlink_loop_path[1024];
    uint8_t *multi_bytes;
    RRuntimeAllocator allocator;
    RRuntimeDarwinFsServiceStartResult service;
    int root_descriptor;
    size_t index;

    R_TEST_CHECK(mkdtemp(temporary_directory) != NULL);
    root_descriptor = open(temporary_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    R_TEST_CHECK(root_descriptor >= 0);
    R_TEST_CHECK(write_file_at(root_descriptor, "regular", regular_bytes, sizeof(regular_bytes)));
    R_TEST_CHECK(write_file_at(root_descriptor, "empty", regular_bytes, 0U));
    multi_bytes = malloc(multi_length);
    R_TEST_CHECK(multi_bytes != NULL);
    for (index = 0U; index < multi_length; ++index) {
        multi_bytes[index] = (uint8_t)(index % 251U);
    }
    R_TEST_CHECK(write_file_at(root_descriptor, "multi", multi_bytes, multi_length));
    R_TEST_CHECK(mkdirat(root_descriptor, "child", S_IRWXU) == 0);
    R_TEST_CHECK(
        write_file_at(root_descriptor, "child/nested", regular_bytes, sizeof(regular_bytes)));
    R_TEST_CHECK(symlinkat("regular", root_descriptor, "final-link") == 0);
    R_TEST_CHECK(symlinkat("child", root_descriptor, "intermediate-link") == 0);
    R_TEST_CHECK(symlinkat("loop-b", root_descriptor, "loop-a") == 0);
    R_TEST_CHECK(symlinkat("loop-a", root_descriptor, "loop-b") == 0);
    R_TEST_CHECK(close(root_descriptor) == 0);

    R_TEST_CHECK(snprintf(regular_path, sizeof(regular_path), "%s/regular", temporary_directory) >
                 0);
    R_TEST_CHECK(snprintf(empty_path, sizeof(empty_path), "%s/empty", temporary_directory) > 0);
    R_TEST_CHECK(snprintf(missing_path, sizeof(missing_path), "%s/missing", temporary_directory) >
                 0);
    R_TEST_CHECK(
        snprintf(symlink_path, sizeof(symlink_path), "%s/final-link", temporary_directory) > 0);
    R_TEST_CHECK(snprintf(symlink_loop_path,
                          sizeof(symlink_loop_path),
                          "%s/loop-a/entry",
                          temporary_directory) > 0);

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    service = r_runtime_darwin_fs_service_start(&allocator, 64U);
    R_TEST_CHECK(service.status == R_RUNTIME_DARWIN_FS_START_OK);

    R_TEST_CHECK(
        test_metadata_contract(
            &allocator, regular_path, symlink_path, symlink_loop_path, sizeof(regular_bytes)) == 0);
    R_TEST_CHECK(expect_read_success(&allocator, empty_path, 0U, regular_bytes, 0U) == 0);
    R_TEST_CHECK(expect_read_success(&allocator,
                                     regular_path,
                                     sizeof(regular_bytes),
                                     regular_bytes,
                                     sizeof(regular_bytes)) == 0);
    R_TEST_CHECK(expect_read_success(&allocator,
                                     symlink_path,
                                     sizeof(regular_bytes),
                                     regular_bytes,
                                     sizeof(regular_bytes)) == 0);
    R_TEST_CHECK(expect_read_error(
                     &allocator, temporary_directory, SIZE_MAX, R_STD_FS_ERROR_IS_DIRECTORY) == 0);
    R_TEST_CHECK(expect_read_error(&allocator, regular_path, 0U, R_STD_FS_ERROR_FILE_TOO_LARGE) ==
                 0);
    R_TEST_CHECK(expect_read_error(&allocator,
                                   regular_path,
                                   sizeof(regular_bytes) - 1U,
                                   R_STD_FS_ERROR_FILE_TOO_LARGE) == 0);
    {
        char multi_path[1024];

        R_TEST_CHECK(snprintf(multi_path, sizeof(multi_path), "%s/multi", temporary_directory) > 0);
        R_TEST_CHECK(expect_read_success(
                         &allocator, multi_path, multi_length, multi_bytes, multi_length) == 0);
    }
    R_TEST_CHECK(test_beneath_contract(
                     &allocator, temporary_directory, regular_bytes, sizeof(regular_bytes)) == 0);
    R_TEST_CHECK(test_start_failure_preserves_path(&allocator, regular_path) == 0);
    R_TEST_CHECK(test_closed_precedes_expired_deadline(&allocator, temporary_directory) == 0);
    R_TEST_CHECK(test_materialization_failure_stages(
                     &allocator, regular_path, regular_bytes, sizeof(regular_bytes)) == 0);
    R_TEST_CHECK(test_root_cleanup_failure(
                     &allocator, regular_path, regular_bytes, sizeof(regular_bytes)) == 0);
    R_TEST_CHECK(test_allocation_failure_sweep(
                     &allocator, regular_path, regular_bytes, sizeof(regular_bytes)) == 0);
    R_TEST_CHECK(test_expired_deadline(&allocator, regular_path) == 0);
    R_TEST_CHECK(test_deadline_rechecked_before_open(&allocator, regular_path) == 0);
    R_TEST_CHECK(test_close_precedes_first_read(
                     &allocator, regular_path, regular_bytes, sizeof(regular_bytes)) == 0);
    R_TEST_CHECK(test_deadline_during_close(&allocator, regular_path) == 0);
    R_TEST_CHECK(test_deadline_during_active_read(&allocator, regular_path) == 0);
    R_TEST_CHECK(test_cancel_before_read_activation(&allocator, regular_path) == 0);
    R_TEST_CHECK(test_native_limit_precedes_task_cancellation(&allocator, regular_path) == 0);
    R_TEST_CHECK(test_native_open_precedes_task_cancellation(&allocator, missing_path) == 0);
    R_TEST_CHECK(test_task_cancellation_precedes_open_result(&allocator, regular_path) == 0);

    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
    r_runtime_darwin_fs_service_stop();
    free(multi_bytes);

    root_descriptor = open(temporary_directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    R_TEST_CHECK(root_descriptor >= 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "regular", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "empty", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "multi", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "child/nested", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "final-link", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "intermediate-link", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "loop-a", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "loop-b", 0) == 0);
    R_TEST_CHECK(unlinkat(root_descriptor, "child", AT_REMOVEDIR) == 0);
    R_TEST_CHECK(close(root_descriptor) == 0);
    R_TEST_CHECK(rmdir(temporary_directory) == 0);
    return 0;
}

int main(void) {
    return test_read_file_public_contract();
}
