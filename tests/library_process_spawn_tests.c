#include "r_std_process.h"

#include "r_library_process_internal.h"
#include "r_runtime_allocator.h"
#include "r_runtime_darwin_process.h"
#include "r_runtime_task.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#ifndef R_PROCESS_SPAWN_HELPER_PATH
#error "R_PROCESS_SPAWN_HELPER_PATH must name the deterministic helper executable"
#endif

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef struct RTestDirectory {
    char path[PATH_MAX];
} RTestDirectory;

typedef struct RTestStopContext {
    _Bool stopped;
} RTestStopContext;

static RStdStringView string_view(const char *text) {
    return (RStdStringView){(const uint8_t *)text, strlen(text)};
}

static RTestDirectory create_test_directory(void) {
    RTestDirectory directory = {{0}};

    (void)snprintf(
        directory.path, sizeof(directory.path), "/tmp/r-process-spawn-%ld-XXXXXX", (long)getpid());
    if (mkdtemp(directory.path) == NULL) {
        directory.path[0] = '\0';
    }
    return directory;
}

static _Bool
path_join(char *result, size_t result_capacity, const char *directory, const char *name) {
    const int written = snprintf(result, result_capacity, "%s/%s", directory, name);

    return written >= 0 && (size_t)written < result_capacity;
}

static RStdProcessCommand create_command(RRuntimeAllocator *allocator, const char *executable) {
    RRuntimeAllocator path_allocator;
    RStdFsPathResult path;
    RStdProcessCommandResult command;

    r_runtime_allocator_initialize(&path_allocator);
    path = r_std_fs_path_from_utf8(&path_allocator, string_view(executable));
    if (path.status != R_STD_FS_CALL_SUCCESS) {
        return (RStdProcessCommand){NULL};
    }
    command = r_std_process_command_create(allocator, &path.value);
    r_std_fs_path_destroy(&path.value);
    return command.status == R_STD_PROCESS_CALL_SUCCESS ? command.value
                                                        : (RStdProcessCommand){NULL};
}

static _Bool add_argument(RStdProcessCommand *command, const char *value) {
    return r_std_process_arg(command, string_view(value)).status == R_STD_PROCESS_CALL_SUCCESS;
}

static _Bool set_environment(RStdProcessCommand *command, const char *name, const char *value) {
    return r_std_process_environment(command, string_view(name), string_view(value)).status ==
           R_STD_PROCESS_CALL_SUCCESS;
}

static _Bool set_working_directory(RStdProcessCommand *command, const char *directory) {
    RRuntimeAllocator path_allocator;
    RStdFsPathResult path;
    RStdProcessVoidResult result;

    r_runtime_allocator_initialize(&path_allocator);
    path = r_std_fs_path_from_utf8(&path_allocator, string_view(directory));
    if (path.status != R_STD_FS_CALL_SUCCESS) {
        return 0;
    }
    result = r_std_process_working_directory(command, &path.value);
    r_std_fs_path_destroy(&path.value);
    return result.status == R_STD_PROCESS_CALL_SUCCESS;
}

static _Bool await_spawn(RStdProcessTaskStartResult started, RStdProcessSpawnResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static RStdProcessDeadline deadline_after_milliseconds(uint32_t milliseconds) {
    RStdTimeInstantResult now = r_std_time_monotonic_now();
    RStdTimeDurationResult duration =
        r_std_time_duration_from_parts((int64_t)(milliseconds / UINT32_C(1000)),
                                       (milliseconds % UINT32_C(1000)) * UINT32_C(1000000));
    RStdTimeInstantResult deadline;

    if (!now.is_ok || !duration.is_ok) {
        abort();
    }
    deadline = r_std_time_instant_add(now.value, duration.value);
    if (!deadline.is_ok) {
        abort();
    }
    return (RStdProcessDeadline){1, deadline.value};
}

static void probe_task_body(RRuntimeTaskExecution *execution, void *payload, void *result) {
    (void)execution;
    (void)payload;
    (void)result;
}

static void *stop_executor(void *context_pointer) {
    RTestStopContext *context = context_pointer;

    context->stopped = r_runtime_executor_lifecycle_stop();
    return NULL;
}

static _Bool begin_executor_stop(pthread_t *thread, RTestStopContext *context) {
    const RRuntimeTypeInfo empty_type = {0U, 1U, NULL, NULL};

    context->stopped = 0;
    if (pthread_create(thread, NULL, stop_executor, context) != 0) {
        return 0;
    }
    for (;;) {
        RRuntimeTaskPrepareResult prepared =
            r_runtime_task_start_prepare(empty_type, empty_type, probe_task_body);

        if (prepared.status == R_RUNTIME_TASK_START_RUNTIME_STOPPING) {
            return 1;
        }
        if (prepared.status != R_RUNTIME_TASK_START_OK || prepared.transaction == NULL) {
            return 0;
        }
        r_runtime_task_start_abort(&prepared.transaction);
        (void)sched_yield();
    }
}

static _Bool state_equal(RLibraryProcessState left, RLibraryProcessState right) {
    return memcmp(&left, &right, sizeof(left)) == 0;
}

static _Bool read_file(const char *path, char *bytes, size_t capacity) {
    int descriptor = open(path, O_RDONLY | O_CLOEXEC);
    size_t size = 0U;

    if (descriptor < 0 || capacity == 0U) {
        return 0;
    }
    for (;;) {
        ssize_t count = read(descriptor, bytes + size, capacity - size - 1U);

        if (count < (ssize_t)0) {
            if (errno == EINTR) {
                continue;
            }
            (void)close(descriptor);
            return 0;
        }
        if (count == (ssize_t)0) {
            break;
        }
        size += (size_t)count;
        if (size == capacity - 1U) {
            (void)close(descriptor);
            return 0;
        }
    }
    bytes[size] = '\0';
    return close(descriptor) == 0;
}

static _Bool wait_for_path(const char *path) {
    struct timespec start;
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &start) != 0) {
        return 0;
    }
    for (;;) {
        if (access(path, F_OK) == 0) {
            return 1;
        }
        if (errno != ENOENT || clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            return 0;
        }
        if (now.tv_sec - start.tv_sec >= 5) {
            return 0;
        }
        (void)sched_yield();
    }
}

static int test_exact_arguments_environment_and_cwd(void) {
    RTestDirectory directory = create_test_directory();
    RRuntimeAllocator command_allocator;
    RStdProcessCommand command;
    RStdProcessTaskStartResult started;
    RStdProcessSpawnResult result = {0};
    RRuntimeDarwinProcessChild *native_child;
    char report_path[PATH_MAX];
    char report[8192];
    char canonical_directory[PATH_MAX];
    char expected_cwd[PATH_MAX + 16];
    char expected_argv0[PATH_MAX + 32];

    R_TEST_CHECK(directory.path[0] != '\0');
    R_TEST_CHECK(path_join(report_path, sizeof(report_path), directory.path, "report.txt"));
    r_runtime_allocator_initialize(&command_allocator);
    command = create_command(&command_allocator, R_PROCESS_SPAWN_HELPER_PATH);
    R_TEST_CHECK(command.storage != NULL);
    r_std_process_clear_environment(&command);
    R_TEST_CHECK(set_environment(&command, "R_PROCESS_SPAWN_VALUE", "two words"));
    R_TEST_CHECK(add_argument(&command, "--report"));
    R_TEST_CHECK(add_argument(&command, report_path));
    R_TEST_CHECK(add_argument(&command, "alpha beta"));
    R_TEST_CHECK(add_argument(&command, ""));
    R_TEST_CHECK(add_argument(&command, "asterisk*"));
    R_TEST_CHECK(set_working_directory(&command, directory.path));
    r_std_process_set_stdio(&command,
                            (RStdProcessStdio){R_STD_PROCESS_PIPE_NULL_DEVICE,
                                               R_STD_PROCESS_PIPE_NULL_DEVICE,
                                               R_STD_PROCESS_PIPE_NULL_DEVICE});

    started = r_std_process_spawn(&command, deadline_after_milliseconds(UINT32_C(1000)));
    R_TEST_CHECK(started.is_ok && command.storage == NULL);
    R_TEST_CHECK(await_spawn(started, &result));
    R_TEST_CHECK(result.kind == R_STD_PROCESS_SPAWN_RESULT_SPAWNED &&
                 result.child.storage != NULL && result.command.storage == NULL &&
                 r_std_process_id(&result.child) != UINT64_C(0));
    native_child = r_library_internal_process_child_testing_native(&result.child);
    R_TEST_CHECK(native_child != NULL);
    r_runtime_darwin_process_child_testing_wait_reaped(native_child);
    R_TEST_CHECK(read_file(report_path, report, sizeof(report)));
    R_TEST_CHECK(realpath(directory.path, canonical_directory) != NULL);
    R_TEST_CHECK(snprintf(expected_cwd, sizeof(expected_cwd), "cwd=%s\n", canonical_directory) > 0);
    R_TEST_CHECK(strstr(report, expected_cwd) != NULL);
    R_TEST_CHECK(strstr(report, "env=two words\n") != NULL);
    R_TEST_CHECK(snprintf(expected_argv0,
                          sizeof(expected_argv0),
                          "arg0[%zu]=%s\n",
                          strlen(R_PROCESS_SPAWN_HELPER_PATH),
                          R_PROCESS_SPAWN_HELPER_PATH) > 0);
    R_TEST_CHECK(strstr(report, expected_argv0) != NULL);
    R_TEST_CHECK(strstr(report, "arg3[10]=alpha beta\n") != NULL);
    R_TEST_CHECK(strstr(report, "arg4[0]=\n") != NULL);
    R_TEST_CHECK(strstr(report, "arg5[9]=asterisk*\n") != NULL);
    r_std_process_child_destroy(&result.child);
    R_TEST_CHECK(r_runtime_darwin_process_testing_registry_count() == 0U);
    R_TEST_CHECK(unlink(report_path) == 0);
    R_TEST_CHECK(rmdir(directory.path) == 0);
    return 0;
}

static int test_no_path_and_missing_executable(void) {
    RTestDirectory directory = create_test_directory();
    RRuntimeAllocator command_allocator;
    RStdProcessCommand command;
    RLibraryProcessState before;
    RStdProcessSpawnResult result = {0};

    R_TEST_CHECK(directory.path[0] != '\0');
    r_runtime_allocator_initialize(&command_allocator);
    command = create_command(&command_allocator, "r_process_spawn_helper");
    R_TEST_CHECK(command.storage != NULL);
    r_std_process_clear_environment(&command);
    R_TEST_CHECK(set_environment(&command, "PATH", R_PROCESS_SPAWN_HELPER_PATH));
    R_TEST_CHECK(set_working_directory(&command, directory.path));
    before = r_library_internal_process_command_state(&command);
    R_TEST_CHECK(await_spawn(r_std_process_spawn(&command, (RStdProcessDeadline){0}), &result));
    R_TEST_CHECK(result.kind == R_STD_PROCESS_SPAWN_RESULT_FAILED &&
                 result.error.code == R_STD_PROCESS_ERROR_NOT_FOUND &&
                 result.error.native_code == (int64_t)ENOENT && result.command.storage != NULL &&
                 state_equal(before, r_library_internal_process_command_state(&result.command)));
    r_std_process_command_destroy(&result.command);
    r_runtime_darwin_process_testing_wait_idle();
    R_TEST_CHECK(rmdir(directory.path) == 0);
    return 0;
}

static int test_expired_deadline_returns_command(void) {
    RRuntimeAllocator command_allocator;
    RStdProcessCommand command;
    RLibraryProcessState before;
    RStdProcessSpawnResult result = {0};

    r_runtime_allocator_initialize(&command_allocator);
    command = create_command(&command_allocator, R_PROCESS_SPAWN_HELPER_PATH);
    R_TEST_CHECK(command.storage != NULL);
    before = r_library_internal_process_command_state(&command);
    R_TEST_CHECK(await_spawn(
        r_std_process_spawn(&command, (RStdProcessDeadline){1, {INT64_C(0), UINT32_C(0)}}),
        &result));
    R_TEST_CHECK(result.kind == R_STD_PROCESS_SPAWN_RESULT_FAILED &&
                 result.error.code == R_STD_PROCESS_ERROR_TIMED_OUT &&
                 result.error.native_code == INT64_C(0) && result.command.storage != NULL &&
                 state_equal(before, r_library_internal_process_command_state(&result.command)));
    r_std_process_command_destroy(&result.command);
    return 0;
}

static int test_invalid_deadline_returns_command(void) {
    RRuntimeAllocator command_allocator;
    RStdProcessCommand command;
    RLibraryProcessState before;
    RStdProcessSpawnResult result = {0};

    r_runtime_allocator_initialize(&command_allocator);
    command = create_command(&command_allocator, R_PROCESS_SPAWN_HELPER_PATH);
    R_TEST_CHECK(command.storage != NULL);
    before = r_library_internal_process_command_state(&command);
    R_TEST_CHECK(await_spawn(
        r_std_process_spawn(
            &command, (RStdProcessDeadline){1, {INT64_C(1), R_STD_TIME_NANOSECONDS_PER_SECOND}}),
        &result));
    R_TEST_CHECK(result.kind == R_STD_PROCESS_SPAWN_RESULT_FAILED &&
                 result.error.code == R_STD_PROCESS_ERROR_OTHER &&
                 result.error.native_code == INT64_C(0) && result.command.storage != NULL &&
                 state_equal(before, r_library_internal_process_command_state(&result.command)));
    r_std_process_command_destroy(&result.command);
    return 0;
}

static int test_native_reservation_failures_preserve_command(void) {
    const RRuntimeDarwinProcessFailureStage stages[] = {
        R_RUNTIME_DARWIN_PROCESS_FAIL_PIPE,
        R_RUNTIME_DARWIN_PROCESS_FAIL_PIPE_IO,
        R_RUNTIME_DARWIN_PROCESS_FAIL_DEADLINE_SOURCE,
    };
    size_t index;

    for (index = 0U; index < sizeof(stages) / sizeof(stages[0]); ++index) {
        RRuntimeAllocator command_allocator;
        RStdProcessCommand command;
        RLibraryProcessState before;
        RLibraryProcessState after;
        RStdProcessTaskStartResult started;

        r_runtime_allocator_initialize(&command_allocator);
        command = create_command(&command_allocator, R_PROCESS_SPAWN_HELPER_PATH);
        R_TEST_CHECK(command.storage != NULL);
        r_std_process_set_stdio(&command,
                                (RStdProcessStdio){R_STD_PROCESS_PIPE_PIPED,
                                                   R_STD_PROCESS_PIPE_INHERIT,
                                                   R_STD_PROCESS_PIPE_INHERIT});
        before = r_library_internal_process_command_state(&command);
        r_runtime_darwin_process_testing_fail_next(stages[index], EMFILE);
        started = r_std_process_spawn(&command,
                                      stages[index] == R_RUNTIME_DARWIN_PROCESS_FAIL_DEADLINE_SOURCE
                                          ? deadline_after_milliseconds(UINT32_C(1000))
                                          : (RStdProcessDeadline){0});
        after = r_library_internal_process_command_state(&command);
        R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                     started.error == R_STD_ASYNC_START_ALLOCATION_FAILED &&
                     state_equal(before, after));
        r_std_process_command_destroy(&command);
    }
    return 0;
}

static int test_allocator_sweep_preserves_command(RRuntimeAllocator *executor_allocator) {
    RTestDirectory directory = create_test_directory();
    uint64_t fail_at;
    _Bool reached_success = 0;

    R_TEST_CHECK(directory.path[0] != '\0');
    for (fail_at = UINT64_C(1); fail_at < UINT64_C(80); ++fail_at) {
        RRuntimeAllocator command_allocator;
        RStdProcessCommand command;
        RLibraryProcessState before;
        RStdProcessTaskStartResult started;
        RStdProcessSpawnResult result = {0};
        char marker_path[PATH_MAX];

        R_TEST_CHECK(path_join(marker_path, sizeof(marker_path), directory.path, "sweep-marker"));
        (void)unlink(marker_path);
        r_runtime_allocator_initialize(&command_allocator);
        command = create_command(&command_allocator, R_PROCESS_SPAWN_HELPER_PATH);
        R_TEST_CHECK(command.storage != NULL && add_argument(&command, "--marker") &&
                     add_argument(&command, marker_path));
        r_std_process_set_stdio(&command,
                                (RStdProcessStdio){R_STD_PROCESS_PIPE_PIPED,
                                                   R_STD_PROCESS_PIPE_PIPED,
                                                   R_STD_PROCESS_PIPE_PIPED});
        before = r_library_internal_process_command_state(&command);
        r_runtime_allocator_set_failure(executor_allocator, fail_at);
        started = r_std_process_spawn(&command, (RStdProcessDeadline){0});
        if (!started.is_ok) {
            R_TEST_CHECK(started.task == NULL &&
                         started.error == R_STD_ASYNC_START_ALLOCATION_FAILED &&
                         state_equal(before, r_library_internal_process_command_state(&command)) &&
                         access(marker_path, F_OK) != 0 && errno == ENOENT);
            r_std_process_command_destroy(&command);
            continue;
        }
        R_TEST_CHECK(command.storage == NULL && await_spawn(started, &result));
        R_TEST_CHECK(result.kind == R_STD_PROCESS_SPAWN_RESULT_SPAWNED);
        r_runtime_darwin_process_child_testing_wait_reaped(
            r_library_internal_process_child_testing_native(&result.child));
        R_TEST_CHECK(wait_for_path(marker_path));
        r_std_process_child_destroy(&result.child);
        R_TEST_CHECK(unlink(marker_path) == 0);
        reached_success = 1;
        break;
    }
    r_runtime_allocator_set_failure(executor_allocator, UINT64_C(0));
    R_TEST_CHECK(reached_success && fail_at > UINT64_C(1));
    R_TEST_CHECK(r_runtime_darwin_process_testing_registry_count() == 0U);
    R_TEST_CHECK(rmdir(directory.path) == 0);
    return 0;
}

static int test_suspended_rollbacks_have_no_child_effect(void) {
    const RRuntimeDarwinProcessFailureStage stages[] = {
        R_RUNTIME_DARWIN_PROCESS_FAIL_PROCESS_SOURCE,
        R_RUNTIME_DARWIN_PROCESS_FAIL_CONTINUE,
    };
    RTestDirectory directory = create_test_directory();
    size_t index;

    R_TEST_CHECK(directory.path[0] != '\0');
    for (index = 0U; index < sizeof(stages) / sizeof(stages[0]); ++index) {
        RRuntimeAllocator command_allocator;
        RStdProcessCommand command;
        RStdProcessSpawnResult result = {0};
        char marker_path[PATH_MAX];
        uint64_t reap_before = r_runtime_darwin_process_testing_reap_count();

        R_TEST_CHECK(
            snprintf(marker_path, sizeof(marker_path), "%s/rollback-%zu", directory.path, index) >
            0);
        r_runtime_allocator_initialize(&command_allocator);
        command = create_command(&command_allocator, R_PROCESS_SPAWN_HELPER_PATH);
        R_TEST_CHECK(command.storage != NULL && add_argument(&command, "--marker") &&
                     add_argument(&command, marker_path));
        r_runtime_darwin_process_testing_fail_next(stages[index], ENOMEM);
        R_TEST_CHECK(await_spawn(r_std_process_spawn(&command, (RStdProcessDeadline){0}), &result));
        R_TEST_CHECK(result.kind == R_STD_PROCESS_SPAWN_RESULT_FAILED &&
                     result.error.code == R_STD_PROCESS_ERROR_RESOURCE_EXHAUSTED &&
                     result.error.native_code == (int64_t)ENOMEM &&
                     result.command.storage != NULL && access(marker_path, F_OK) != 0 &&
                     errno == ENOENT);
        r_std_process_command_destroy(&result.command);
        r_runtime_darwin_process_testing_wait_idle();
        R_TEST_CHECK(r_runtime_darwin_process_testing_reap_count() == reap_before + UINT64_C(1));
    }
    R_TEST_CHECK(rmdir(directory.path) == 0);
    return 0;
}

static int test_piped_endpoints_remain_child_owned(void) {
    RTestDirectory directory = create_test_directory();
    RRuntimeAllocator command_allocator;
    RStdProcessCommand command;
    RStdProcessSpawnResult result = {0};
    RRuntimeDarwinProcessChild *native_child;
    char marker_path[PATH_MAX];

    R_TEST_CHECK(directory.path[0] != '\0' &&
                 path_join(marker_path, sizeof(marker_path), directory.path, "pipe-marker"));
    r_runtime_allocator_initialize(&command_allocator);
    command = create_command(&command_allocator, R_PROCESS_SPAWN_HELPER_PATH);
    R_TEST_CHECK(command.storage != NULL && add_argument(&command, "--marker") &&
                 add_argument(&command, marker_path));
    r_std_process_set_stdio(&command,
                            (RStdProcessStdio){R_STD_PROCESS_PIPE_PIPED,
                                               R_STD_PROCESS_PIPE_PIPED,
                                               R_STD_PROCESS_PIPE_PIPED});
    R_TEST_CHECK(await_spawn(r_std_process_spawn(&command, (RStdProcessDeadline){0}), &result));
    R_TEST_CHECK(result.kind == R_STD_PROCESS_SPAWN_RESULT_SPAWNED);
    native_child = r_library_internal_process_child_testing_native(&result.child);
    R_TEST_CHECK(native_child != NULL &&
                 r_runtime_darwin_process_child_testing_pipe_count(native_child) == 3U);
    r_runtime_darwin_process_child_testing_wait_reaped(native_child);
    R_TEST_CHECK(wait_for_path(marker_path) &&
                 r_runtime_darwin_process_child_testing_pipe_count(native_child) == 3U);
    r_std_process_child_destroy(&result.child);
    R_TEST_CHECK(r_runtime_darwin_process_testing_registry_count() == 0U);
    R_TEST_CHECK(unlink(marker_path) == 0 && rmdir(directory.path) == 0);
    return 0;
}

static int test_cancel_before_commit_rolls_back(void) {
    RTestDirectory directory = create_test_directory();
    RRuntimeAllocator command_allocator;
    RStdProcessCommand command;
    RStdProcessTaskStartResult started;
    char marker_path[PATH_MAX];
    uint64_t reap_before;

    R_TEST_CHECK(directory.path[0] != '\0' &&
                 path_join(marker_path, sizeof(marker_path), directory.path, "cancel-marker"));
    r_runtime_allocator_initialize(&command_allocator);
    command = create_command(&command_allocator, R_PROCESS_SPAWN_HELPER_PATH);
    R_TEST_CHECK(command.storage != NULL && add_argument(&command, "--marker") &&
                 add_argument(&command, marker_path));
    reap_before = r_runtime_darwin_process_testing_reap_count();
    r_runtime_darwin_process_testing_pause_next_before_commit();
    started = r_std_process_spawn(&command, (RStdProcessDeadline){0});
    R_TEST_CHECK(started.is_ok && started.task != NULL && command.storage == NULL);
    r_runtime_darwin_process_testing_wait_before_commit();
    r_runtime_task_cancel(&started.task);
    R_TEST_CHECK(started.task == NULL);
    r_runtime_darwin_process_testing_release_before_commit();
    r_runtime_darwin_process_testing_wait_idle();
    R_TEST_CHECK(access(marker_path, F_OK) != 0 && errno == ENOENT &&
                 r_runtime_darwin_process_testing_reap_count() == reap_before + UINT64_C(1));
    R_TEST_CHECK(rmdir(directory.path) == 0);
    return 0;
}

static int test_deadline_before_executor_cancel_wins(RRuntimeAllocator *executor_allocator) {
    RTestDirectory directory = create_test_directory();
    RRuntimeAllocator command_allocator;
    RStdProcessCommand command;
    RLibraryProcessState before;
    RStdProcessTaskStartResult started;
    RStdProcessSpawnResult result = {0};
    RTestStopContext stop_context;
    pthread_t stop_thread;
    char marker_path[PATH_MAX];
    uint64_t deadline_count;
    uint64_t reap_before;

    R_TEST_CHECK(directory.path[0] != '\0' &&
                 path_join(marker_path, sizeof(marker_path), directory.path, "deadline-marker"));
    r_runtime_allocator_initialize(&command_allocator);
    command = create_command(&command_allocator, R_PROCESS_SPAWN_HELPER_PATH);
    R_TEST_CHECK(command.storage != NULL && add_argument(&command, "--marker") &&
                 add_argument(&command, marker_path));
    before = r_library_internal_process_command_state(&command);
    deadline_count = r_runtime_darwin_process_testing_deadline_count();
    reap_before = r_runtime_darwin_process_testing_reap_count();
    r_runtime_darwin_process_testing_pause_next_before_commit();
    started = r_std_process_spawn(&command, deadline_after_milliseconds(UINT32_C(100)));
    R_TEST_CHECK(started.is_ok && started.task != NULL && command.storage == NULL);
    r_runtime_darwin_process_testing_wait_before_commit();
    r_runtime_darwin_process_testing_wait_deadline_after(deadline_count);
    R_TEST_CHECK(begin_executor_stop(&stop_thread, &stop_context));
    r_runtime_darwin_process_testing_release_before_commit();
    R_TEST_CHECK(pthread_join(stop_thread, NULL) == 0 && stop_context.stopped);
    R_TEST_CHECK(await_spawn(started, &result));
    R_TEST_CHECK(result.kind == R_STD_PROCESS_SPAWN_RESULT_FAILED &&
                 result.error.code == R_STD_PROCESS_ERROR_TIMED_OUT &&
                 result.error.native_code == INT64_C(0) && result.command.storage != NULL &&
                 state_equal(before, r_library_internal_process_command_state(&result.command)) &&
                 access(marker_path, F_OK) != 0 && errno == ENOENT);
    r_std_process_command_destroy(&result.command);
    r_runtime_darwin_process_testing_wait_idle();
    R_TEST_CHECK(r_runtime_darwin_process_testing_reap_count() == reap_before + UINT64_C(1));
    R_TEST_CHECK(rmdir(directory.path) == 0);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(executor_allocator) ==
                 R_RUNTIME_EXECUTOR_START_OK);
    return 0;
}

static int test_drop_registers_reap_and_shutdown_kills(void) {
    RTestDirectory directory = create_test_directory();
    RRuntimeAllocator command_allocator;
    RStdProcessCommand command;
    RStdProcessSpawnResult result = {0};
    char marker_path[PATH_MAX];
    pid_t process_id;
    uint64_t reap_before;

    R_TEST_CHECK(directory.path[0] != '\0' &&
                 path_join(marker_path, sizeof(marker_path), directory.path, "hold-marker"));
    r_runtime_allocator_initialize(&command_allocator);
    command = create_command(&command_allocator, R_PROCESS_SPAWN_HELPER_PATH);
    R_TEST_CHECK(command.storage != NULL && add_argument(&command, "--hold") &&
                 add_argument(&command, marker_path));
    R_TEST_CHECK(await_spawn(r_std_process_spawn(&command, (RStdProcessDeadline){0}), &result));
    R_TEST_CHECK(result.kind == R_STD_PROCESS_SPAWN_RESULT_SPAWNED && wait_for_path(marker_path));
    process_id = (pid_t)r_std_process_id(&result.child);
    reap_before = r_runtime_darwin_process_testing_reap_count();
    r_std_process_child_destroy(&result.child);
    R_TEST_CHECK(kill(process_id, 0) == 0 &&
                 r_runtime_darwin_process_testing_registry_count() == 1U);
    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
    r_runtime_darwin_process_lifecycle_stop();
    errno = 0;
    R_TEST_CHECK(waitpid(process_id, NULL, WNOHANG) < (pid_t)0 && errno == ECHILD &&
                 r_runtime_darwin_process_testing_reap_count() == reap_before + UINT64_C(1));
    R_TEST_CHECK(unlink(marker_path) == 0 && rmdir(directory.path) == 0);
    return 0;
}

int main(void) {
    RRuntimeAllocator executor_allocator;
    RRuntimeDarwinProcessStartResult process_start;

    r_runtime_allocator_initialize(&executor_allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&executor_allocator) ==
                 R_RUNTIME_EXECUTOR_START_OK);
    process_start = r_runtime_darwin_process_lifecycle_start(&executor_allocator);
    R_TEST_CHECK(process_start.status == R_RUNTIME_DARWIN_PROCESS_START_OK);
    R_TEST_CHECK(test_exact_arguments_environment_and_cwd() == 0);
    R_TEST_CHECK(test_no_path_and_missing_executable() == 0);
    R_TEST_CHECK(test_expired_deadline_returns_command() == 0);
    R_TEST_CHECK(test_invalid_deadline_returns_command() == 0);
    R_TEST_CHECK(test_native_reservation_failures_preserve_command() == 0);
    R_TEST_CHECK(test_allocator_sweep_preserves_command(&executor_allocator) == 0);
    R_TEST_CHECK(test_suspended_rollbacks_have_no_child_effect() == 0);
    R_TEST_CHECK(test_piped_endpoints_remain_child_owned() == 0);
    R_TEST_CHECK(test_cancel_before_commit_rolls_back() == 0);
    R_TEST_CHECK(test_deadline_before_executor_cancel_wins(&executor_allocator) == 0);
    R_TEST_CHECK(test_drop_registers_reap_and_shutdown_kills() == 0);
    return 0;
}
