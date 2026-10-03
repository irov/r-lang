#include "r_std_process.h"

#include "r_library_process_internal.h"
#include "r_runtime_allocator.h"
#include "r_runtime_darwin_process.h"
#include "r_runtime_task.h"

#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

static RStdStringView string_view(const char *text) {
    return (RStdStringView){(const uint8_t *)text, strlen(text)};
}

static RStdProcessCommand create_command(RRuntimeAllocator *allocator) {
    RRuntimeAllocator path_allocator;
    RStdFsPathResult path;
    RStdProcessCommandResult command;

    r_runtime_allocator_initialize(&path_allocator);
    path = r_std_fs_path_from_utf8(&path_allocator, string_view(R_PROCESS_SPAWN_HELPER_PATH));
    if (path.status != R_STD_FS_CALL_SUCCESS) {
        return (RStdProcessCommand){NULL};
    }
    command = r_std_process_command_create(allocator, &path.value);
    r_std_fs_path_destroy(&path.value);
    return command.status == R_STD_PROCESS_CALL_SUCCESS ? command.value
                                                        : (RStdProcessCommand){NULL};
}

static RStdProcessChild spawn_child(RRuntimeAllocator *allocator, const char *argument) {
    RStdProcessCommand command = create_command(allocator);
    RStdProcessTaskStartResult started;
    RStdProcessSpawnResult result = {0};

    if (command.storage == NULL) {
        return (RStdProcessChild){0};
    }
    if (argument != NULL &&
        r_std_process_arg(&command, string_view(argument)).status != R_STD_PROCESS_CALL_SUCCESS) {
        r_std_process_command_destroy(&command);
        return (RStdProcessChild){0};
    }
    r_std_process_set_stdio(&command,
                            (RStdProcessStdio){R_STD_PROCESS_PIPE_NULL_DEVICE,
                                               R_STD_PROCESS_PIPE_NULL_DEVICE,
                                               R_STD_PROCESS_PIPE_NULL_DEVICE});
    started = r_std_process_spawn(&command, (RStdProcessDeadline){0});
    if (!started.is_ok || started.task == NULL || command.storage != NULL ||
        r_runtime_task_await(&started.task, &result) != R_RUNTIME_TASK_AWAIT_OK ||
        result.kind != R_STD_PROCESS_SPAWN_RESULT_SPAWNED) {
        r_runtime_task_destroy(&started.task);
        r_std_process_command_destroy(&command);
        r_std_process_command_destroy(&result.command);
        return (RStdProcessChild){0};
    }
    return result.child;
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

static _Bool await_wait(RStdProcessTaskStartResult started, RStdProcessWaitResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static _Bool await_terminate(RStdProcessTaskStartResult started, RStdProcessVoidResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static int test_wait_exit_status(RRuntimeAllocator *allocator) {
    RStdProcessChild child = spawn_child(allocator, NULL);
    RStdProcessWaitResult result = {0};

    R_TEST_CHECK(child.storage != NULL);
    R_TEST_CHECK(await_wait(r_std_process_wait(&child, (RStdProcessDeadline){0}), &result));
    R_TEST_CHECK(child.storage == NULL && result.kind == R_STD_PROCESS_WAIT_RESULT_EXITED &&
                 result.status.kind == R_STD_PROCESS_TERMINATION_EXITED &&
                 result.status.code == INT32_C(10) && !result.status.success &&
                 result.child.storage == NULL);
    r_runtime_darwin_process_testing_wait_idle();
    return 0;
}

static int test_wait_success_status(RRuntimeAllocator *allocator) {
    RStdProcessChild child = spawn_child(allocator, "--exit-zero");
    RStdProcessWaitResult result = {0};

    R_TEST_CHECK(child.storage != NULL);
    R_TEST_CHECK(await_wait(r_std_process_wait(&child, (RStdProcessDeadline){0}), &result));
    R_TEST_CHECK(result.kind == R_STD_PROCESS_WAIT_RESULT_EXITED &&
                 result.status.kind == R_STD_PROCESS_TERMINATION_EXITED &&
                 result.status.code == INT32_C(0) && result.status.success &&
                 result.child.storage == NULL);
    r_runtime_darwin_process_testing_wait_idle();
    return 0;
}

static int test_timeout_returns_same_child(RRuntimeAllocator *allocator) {
    RStdProcessChild child = spawn_child(allocator, "--pause");
    RStdProcessWaitResult wait_result = {0};
    RStdProcessVoidResult terminate_result = {0};
    uint64_t identity;

    R_TEST_CHECK(child.storage != NULL);
    identity = r_std_process_id(&child);
    R_TEST_CHECK(await_wait(r_std_process_wait(&child, deadline_after_milliseconds(UINT32_C(20))),
                            &wait_result));
    R_TEST_CHECK(child.storage == NULL && wait_result.kind == R_STD_PROCESS_WAIT_RESULT_FAILED &&
                 wait_result.error.code == R_STD_PROCESS_ERROR_TIMED_OUT &&
                 wait_result.error.native_code == INT64_C(0) && wait_result.child.storage != NULL &&
                 r_std_process_id(&wait_result.child) == identity);
    R_TEST_CHECK(await_terminate(
        r_std_process_terminate(&wait_result.child, (RStdProcessDeadline){0}), &terminate_result));
    R_TEST_CHECK(terminate_result.status == R_STD_PROCESS_CALL_SUCCESS);
    R_TEST_CHECK(
        await_wait(r_std_process_wait(&wait_result.child, (RStdProcessDeadline){0}), &wait_result));
    R_TEST_CHECK(wait_result.kind == R_STD_PROCESS_WAIT_RESULT_EXITED &&
                 wait_result.status.kind == R_STD_PROCESS_TERMINATION_SIGNALLED &&
                 wait_result.status.code == (int32_t)SIGKILL && !wait_result.status.success &&
                 wait_result.child.storage == NULL);
    r_runtime_darwin_process_testing_wait_idle();
    return 0;
}

static int test_terminate_already_terminal_then_wait(RRuntimeAllocator *allocator) {
    RStdProcessChild child = spawn_child(allocator, NULL);
    RStdProcessVoidResult terminate_result = {0};
    RStdProcessWaitResult wait_result = {0};
    RRuntimeDarwinProcessChild *native_child;

    R_TEST_CHECK(child.storage != NULL);
    native_child = r_library_internal_process_child_testing_native(&child);
    R_TEST_CHECK(native_child != NULL);
    r_runtime_darwin_process_child_testing_wait_reaped(native_child);
    R_TEST_CHECK(await_terminate(r_std_process_terminate(&child, (RStdProcessDeadline){0}),
                                 &terminate_result));
    R_TEST_CHECK(terminate_result.status == R_STD_PROCESS_CALL_ERROR &&
                 terminate_result.error.code == R_STD_PROCESS_ERROR_NOT_RUNNING &&
                 terminate_result.error.native_code == INT64_C(0) && child.storage != NULL);
    R_TEST_CHECK(await_wait(r_std_process_wait(&child, (RStdProcessDeadline){0}), &wait_result));
    R_TEST_CHECK(wait_result.kind == R_STD_PROCESS_WAIT_RESULT_EXITED &&
                 wait_result.status.kind == R_STD_PROCESS_TERMINATION_EXITED &&
                 wait_result.status.code == INT32_C(10));
    r_runtime_darwin_process_testing_wait_idle();
    return 0;
}

static int test_expired_terminate_preserves_running_child(RRuntimeAllocator *allocator) {
    RStdProcessChild child = spawn_child(allocator, "--pause");
    RStdProcessVoidResult terminate_result = {0};
    RStdProcessWaitResult wait_result = {0};
    pid_t process_id;

    R_TEST_CHECK(child.storage != NULL);
    process_id = (pid_t)r_std_process_id(&child);
    R_TEST_CHECK(await_terminate(
        r_std_process_terminate(&child, (RStdProcessDeadline){1, {INT64_C(0), UINT32_C(0)}}),
        &terminate_result));
    R_TEST_CHECK(terminate_result.status == R_STD_PROCESS_CALL_ERROR &&
                 terminate_result.error.code == R_STD_PROCESS_ERROR_TIMED_OUT &&
                 terminate_result.error.native_code == INT64_C(0) && child.storage != NULL &&
                 kill(process_id, 0) == 0);
    R_TEST_CHECK(await_terminate(r_std_process_terminate(&child, (RStdProcessDeadline){0}),
                                 &terminate_result));
    R_TEST_CHECK(terminate_result.status == R_STD_PROCESS_CALL_SUCCESS);
    R_TEST_CHECK(await_wait(r_std_process_wait(&child, (RStdProcessDeadline){0}), &wait_result));
    R_TEST_CHECK(wait_result.kind == R_STD_PROCESS_WAIT_RESULT_EXITED &&
                 wait_result.status.kind == R_STD_PROCESS_TERMINATION_SIGNALLED &&
                 wait_result.status.code == (int32_t)SIGKILL);
    r_runtime_darwin_process_testing_wait_idle();
    return 0;
}

static int test_wait_start_failure_preserves_child(RRuntimeAllocator *allocator) {
    RStdProcessChild child = spawn_child(allocator, "--pause");
    RStdProcessTaskStartResult started;
    RStdProcessVoidResult terminate_result = {0};
    RStdProcessWaitResult wait_result = {0};
    uint64_t identity;

    R_TEST_CHECK(child.storage != NULL);
    identity = r_std_process_id(&child);
    r_runtime_darwin_process_testing_fail_next(R_RUNTIME_DARWIN_PROCESS_FAIL_WAIT_DEADLINE_SOURCE,
                                               ENOMEM);
    started = r_std_process_wait(&child, deadline_after_milliseconds(UINT32_C(1000)));
    R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                 started.error == R_STD_ASYNC_START_ALLOCATION_FAILED && child.storage != NULL &&
                 r_std_process_id(&child) == identity);
    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    started = r_std_process_wait(&child, (RStdProcessDeadline){0});
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                 started.error == R_STD_ASYNC_START_ALLOCATION_FAILED && child.storage != NULL &&
                 r_std_process_id(&child) == identity);
    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    started = r_std_process_terminate(&child, (RStdProcessDeadline){0});
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    R_TEST_CHECK(!started.is_ok && started.task == NULL &&
                 started.error == R_STD_ASYNC_START_ALLOCATION_FAILED && child.storage != NULL &&
                 r_std_process_id(&child) == identity);
    R_TEST_CHECK(await_terminate(r_std_process_terminate(&child, (RStdProcessDeadline){0}),
                                 &terminate_result));
    R_TEST_CHECK(terminate_result.status == R_STD_PROCESS_CALL_SUCCESS);
    R_TEST_CHECK(await_wait(r_std_process_wait(&child, (RStdProcessDeadline){0}), &wait_result));
    R_TEST_CHECK(wait_result.kind == R_STD_PROCESS_WAIT_RESULT_EXITED);
    r_runtime_darwin_process_testing_wait_idle();
    return 0;
}

static int test_wait_cancel_acknowledges_native_cleanup(RRuntimeAllocator *allocator) {
    RStdProcessChild child = spawn_child(allocator, "--pause");
    RStdProcessTaskStartResult started;
    pid_t process_id;

    R_TEST_CHECK(child.storage != NULL);
    process_id = (pid_t)r_std_process_id(&child);
    started = r_std_process_wait(&child, (RStdProcessDeadline){0});
    R_TEST_CHECK(started.is_ok && started.task != NULL && child.storage == NULL);
    r_runtime_task_cancel(&started.task);
    R_TEST_CHECK(started.task == NULL);
    R_TEST_CHECK(kill(process_id, SIGKILL) == 0);
    r_runtime_darwin_process_testing_wait_idle();
    R_TEST_CHECK(kill(process_id, 0) != 0 && errno == ESRCH);
    return 0;
}

static int test_terminate_reaps_every_child(RRuntimeAllocator *allocator) {
    /* L29-3: the kernel posts the exit event before the child becomes a zombie; every terminated
       child is still reaped, so no wait outlives its child. */
    enum {
        R_TEST_ROUNDS = 48,
        R_TEST_CHILDREN = 16
    };
    RStdProcessChild children[R_TEST_CHILDREN];
    RStdProcessVoidResult terminate_result = {0};
    RStdProcessWaitResult wait_result = {0};
    size_t round;
    size_t index;

    for (round = 0U; round < (size_t)R_TEST_ROUNDS; ++round) {
        for (index = 0U; index < (size_t)R_TEST_CHILDREN; ++index) {
            children[index] = spawn_child(allocator, "--pause");
            R_TEST_CHECK(children[index].storage != NULL);
        }
        for (index = 0U; index < (size_t)R_TEST_CHILDREN; ++index) {
            R_TEST_CHECK(
                await_terminate(r_std_process_terminate(&children[index], (RStdProcessDeadline){0}),
                                &terminate_result));
            R_TEST_CHECK(terminate_result.status == R_STD_PROCESS_CALL_SUCCESS);
        }
        for (index = 0U; index < (size_t)R_TEST_CHILDREN; ++index) {
            R_TEST_CHECK(await_wait(
                r_std_process_wait(&children[index], deadline_after_milliseconds(UINT32_C(5000))),
                &wait_result));
            R_TEST_CHECK(wait_result.kind == R_STD_PROCESS_WAIT_RESULT_EXITED &&
                         wait_result.status.kind == R_STD_PROCESS_TERMINATION_SIGNALLED &&
                         wait_result.status.code == (int32_t)SIGKILL &&
                         wait_result.child.storage == NULL);
        }
    }
    r_runtime_darwin_process_testing_wait_idle();
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
    R_TEST_CHECK(test_wait_exit_status(&executor_allocator) == 0);
    R_TEST_CHECK(test_wait_success_status(&executor_allocator) == 0);
    R_TEST_CHECK(test_timeout_returns_same_child(&executor_allocator) == 0);
    R_TEST_CHECK(test_terminate_already_terminal_then_wait(&executor_allocator) == 0);
    R_TEST_CHECK(test_expired_terminate_preserves_running_child(&executor_allocator) == 0);
    R_TEST_CHECK(test_wait_start_failure_preserves_child(&executor_allocator) == 0);
    R_TEST_CHECK(test_wait_cancel_acknowledges_native_cleanup(&executor_allocator) == 0);
    R_TEST_CHECK(test_terminate_reaps_every_child(&executor_allocator) == 0);
    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
    r_runtime_darwin_process_lifecycle_stop();
    return 0;
}
