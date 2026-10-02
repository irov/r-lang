#include "r_std_process.h"

#include "r_library_process_internal.h"
#include "r_runtime_allocator.h"
#include "r_runtime_array.h"
#include "r_runtime_darwin_process.h"
#include "r_runtime_task.h"

#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef R_PROCESS_SPAWN_HELPER_PATH
#error "R_PROCESS_SPAWN_HELPER_PATH must name the deterministic helper executable"
#endif

#define R_TEST_TAKE_THREAD_COUNT 24U

#define R_TEST_CHECK(expression)                                                                   \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #expression);   \
            return 1;                                                                              \
        }                                                                                          \
    } while (0)

typedef enum RTestTakeKind {
    R_TEST_TAKE_STDIN = 0,
    R_TEST_TAKE_STDOUT,
    R_TEST_TAKE_STDERR
} RTestTakeKind;

typedef struct RTestTakeThread {
    RStdProcessChild *child;
    _Atomic size_t *ready_count;
    _Atomic _Bool *start;
    RRuntimeDarwinIoHandle *handle;
    RTestTakeKind kind;
} RTestTakeThread;

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

static _Bool await_spawn(RStdProcessTaskStartResult started, RStdProcessSpawnResult *result) {
    return started.is_ok && started.task != NULL &&
           r_runtime_task_await(&started.task, result) == R_RUNTIME_TASK_AWAIT_OK &&
           started.task == NULL;
}

static RStdProcessChild spawn_child(RStdProcessStdio stdio, const char *mode) {
    RRuntimeAllocator command_allocator;
    RStdProcessCommand command;
    RStdProcessSpawnResult result = {0};

    r_runtime_allocator_initialize(&command_allocator);
    command = create_command(&command_allocator);
    if (command.storage == NULL) {
        return (RStdProcessChild){0};
    }
    if (mode != NULL &&
        r_std_process_arg(&command, string_view(mode)).status != R_STD_PROCESS_CALL_SUCCESS) {
        r_std_process_command_destroy(&command);
        return (RStdProcessChild){0};
    }
    r_std_process_set_stdio(&command, stdio);
    if (!await_spawn(r_std_process_spawn(&command, (RStdProcessDeadline){0}), &result)) {
        r_std_process_command_destroy(&command);
        return (RStdProcessChild){0};
    }
    if (result.kind != R_STD_PROCESS_SPAWN_RESULT_SPAWNED) {
        r_std_process_command_destroy(&result.command);
        return (RStdProcessChild){0};
    }
    return result.child;
}

static RRuntimeArray byte_array(RRuntimeAllocator *allocator, uint8_t value) {
    const RRuntimeTypeInfo byte_type = {
        sizeof(uint8_t),
        _Alignof(uint8_t),
        NULL,
        NULL,
    };
    RRuntimeArray result = {0};

    if (r_runtime_array_with_capacity(&result, allocator, byte_type, 1U) != R_RUNTIME_ARRAY_OK) {
        return (RRuntimeArray){0};
    }
    result.length = 1U;
    ((uint8_t *)result.data)[0] = value;
    return result;
}

static _Bool close_input(RStdIoInput *input) {
    RStdIoTaskStartResult started = r_std_io_close_input(input, (RStdIoDeadline){0});
    RStdIoVoidResult result = {0};

    return started.is_ok && started.task != NULL && input->handle == NULL &&
           r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK &&
           result.r_tag == UINT32_C(0);
}

static _Bool close_output(RStdIoOutput *output) {
    RStdIoTaskStartResult started = r_std_io_close_output(output, (RStdIoDeadline){0});
    RStdIoVoidResult result = {0};

    return started.is_ok && started.task != NULL && output->handle == NULL &&
           r_runtime_task_await(&started.task, &result) == R_RUNTIME_TASK_AWAIT_OK &&
           result.r_tag == UINT32_C(0);
}

static void *take_thread(void *context_pointer) {
    RTestTakeThread *context = context_pointer;

    (void)atomic_fetch_add_explicit(context->ready_count, 1U, memory_order_acq_rel);
    while (!atomic_load_explicit(context->start, memory_order_acquire)) {
        (void)sched_yield();
    }
    switch (context->kind) {
    case R_TEST_TAKE_STDIN: {
        RStdProcessOutputOption result = r_std_process_take_stdin(context->child);

        context->handle = result.has_value ? result.value.handle : NULL;
        break;
    }
    case R_TEST_TAKE_STDOUT: {
        RStdProcessInputOption result = r_std_process_take_stdout(context->child);

        context->handle = result.has_value ? result.value.handle : NULL;
        break;
    }
    case R_TEST_TAKE_STDERR: {
        RStdProcessInputOption result = r_std_process_take_stderr(context->child);

        context->handle = result.has_value ? result.value.handle : NULL;
        break;
    }
    }
    return NULL;
}

static RRuntimeDarwinIoHandle *race_take(RStdProcessChild *child, RTestTakeKind kind) {
    RTestTakeThread contexts[R_TEST_TAKE_THREAD_COUNT];
    pthread_t threads[R_TEST_TAKE_THREAD_COUNT];
    _Atomic size_t ready_count;
    _Atomic _Bool start;
    RRuntimeDarwinIoHandle *winner = NULL;
    size_t winner_count = 0U;
    size_t index;

    atomic_init(&ready_count, 0U);
    atomic_init(&start, 0);
    for (index = 0U; index < R_TEST_TAKE_THREAD_COUNT; ++index) {
        contexts[index] = (RTestTakeThread){child, &ready_count, &start, NULL, kind};
        if (pthread_create(&threads[index], NULL, take_thread, &contexts[index]) != 0) {
            abort();
        }
    }
    while (atomic_load_explicit(&ready_count, memory_order_acquire) != R_TEST_TAKE_THREAD_COUNT) {
        (void)sched_yield();
    }
    atomic_store_explicit(&start, 1, memory_order_release);
    for (index = 0U; index < R_TEST_TAKE_THREAD_COUNT; ++index) {
        if (pthread_join(threads[index], NULL) != 0) {
            abort();
        }
        if (contexts[index].handle != NULL) {
            winner = contexts[index].handle;
            winner_count += 1U;
        }
    }
    if (winner_count != 1U) {
        abort();
    }
    return winner;
}

static int test_none_states(void) {
    RStdProcessChild child;
    RStdProcessChild moved = {0};
    RStdProcessInputOption input;
    RStdProcessOutputOption output;

    child = spawn_child((RStdProcessStdio){R_STD_PROCESS_PIPE_INHERIT,
                                           R_STD_PROCESS_PIPE_NULL_DEVICE,
                                           R_STD_PROCESS_PIPE_PIPED},
                        NULL);
    R_TEST_CHECK(child.storage != NULL);
    output = r_std_process_take_stdin(&child);
    R_TEST_CHECK(!output.has_value && output.value.handle == NULL);
    input = r_std_process_take_stdout(&child);
    R_TEST_CHECK(!input.has_value && input.value.handle == NULL);
    input = r_std_process_take_stderr(&child);
    R_TEST_CHECK(input.has_value && input.value.handle != NULL);
    {
        RStdIoInput extracted = input.value;

        input = r_std_process_take_stderr(&child);
        R_TEST_CHECK(!input.has_value && input.value.handle == NULL && close_input(&extracted));
    }
    r_library_internal_process_child_move(&moved, &child);
    R_TEST_CHECK(!r_std_process_take_stdin(&child).has_value &&
                 !r_std_process_take_stdout(&child).has_value &&
                 !r_std_process_take_stderr(&child).has_value);
    r_std_process_child_destroy(&moved);
    r_runtime_darwin_process_testing_wait_idle();
    return 0;
}

static int test_allocation_free_take(RRuntimeAllocator *allocator) {
    RStdProcessChild child = spawn_child((RStdProcessStdio){R_STD_PROCESS_PIPE_PIPED,
                                                            R_STD_PROCESS_PIPE_NULL_DEVICE,
                                                            R_STD_PROCESS_PIPE_NULL_DEVICE},
                                         NULL);
    RStdProcessOutputOption output;

    R_TEST_CHECK(child.storage != NULL);
    r_runtime_darwin_process_child_testing_wait_reaped(
        r_library_internal_process_child_testing_native(&child));
    r_runtime_allocator_set_failure(allocator, UINT64_C(1));
    output = r_std_process_take_stdin(&child);
    R_TEST_CHECK(output.has_value && output.value.handle != NULL &&
                 r_runtime_allocator_attempt_count(allocator) == UINT64_C(0));
    r_runtime_allocator_set_failure(allocator, UINT64_C(0));
    R_TEST_CHECK(close_output(&output.value));
    r_std_process_child_destroy(&child);
    r_runtime_darwin_process_testing_wait_idle();
    return 0;
}

static int test_parallel_exactly_once(void) {
    RStdProcessChild child = spawn_child((RStdProcessStdio){R_STD_PROCESS_PIPE_PIPED,
                                                            R_STD_PROCESS_PIPE_PIPED,
                                                            R_STD_PROCESS_PIPE_PIPED},
                                         NULL);
    RRuntimeDarwinProcessChild *native_child;
    RRuntimeDarwinIoHandle *input_handle;
    RRuntimeDarwinIoHandle *output_handle;
    RRuntimeDarwinIoHandle *error_handle;
    RStdIoOutput input;
    RStdIoInput output;
    RStdIoInput error;

    R_TEST_CHECK(child.storage != NULL);
    native_child = r_library_internal_process_child_testing_native(&child);
    R_TEST_CHECK(native_child != NULL);
    r_runtime_darwin_process_child_testing_wait_reaped(native_child);
    input_handle = race_take(&child, R_TEST_TAKE_STDIN);
    R_TEST_CHECK(input_handle != NULL &&
                 r_runtime_darwin_process_child_testing_pipe_count(native_child) == 2U);
    output_handle = race_take(&child, R_TEST_TAKE_STDOUT);
    R_TEST_CHECK(output_handle != NULL && output_handle != input_handle &&
                 r_runtime_darwin_process_child_testing_pipe_count(native_child) == 1U);
    error_handle = race_take(&child, R_TEST_TAKE_STDERR);
    R_TEST_CHECK(error_handle != NULL && error_handle != input_handle &&
                 error_handle != output_handle &&
                 r_runtime_darwin_process_child_testing_pipe_count(native_child) == 0U);
    input = (RStdIoOutput){input_handle};
    output = (RStdIoInput){output_handle};
    error = (RStdIoInput){error_handle};
    R_TEST_CHECK(close_output(&input) && close_input(&output) && close_input(&error));
    r_std_process_child_destroy(&child);
    r_runtime_darwin_process_testing_wait_idle();
    return 0;
}

static int test_extracted_lifetime_and_direction(RRuntimeAllocator *allocator) {
    RStdProcessChild child = spawn_child((RStdProcessStdio){R_STD_PROCESS_PIPE_PIPED,
                                                            R_STD_PROCESS_PIPE_PIPED,
                                                            R_STD_PROCESS_PIPE_PIPED},
                                         "--pipe-triplet");
    RStdProcessOutputOption process_input;
    RStdProcessInputOption process_output;
    RStdProcessInputOption process_error;
    RRuntimeArray output_buffer;
    RRuntimeArray error_buffer;
    RRuntimeArray write_buffer;
    RStdIoTaskStartResult output_started;
    RStdIoTaskStartResult error_started;
    RStdIoWriteAllResult write_result = {0};
    RStdIoReadResult output_result = {0};
    RStdIoReadResult error_result = {0};

    R_TEST_CHECK(child.storage != NULL);
    process_input = r_std_process_take_stdin(&child);
    process_output = r_std_process_take_stdout(&child);
    process_error = r_std_process_take_stderr(&child);
    R_TEST_CHECK(process_input.has_value && process_output.has_value && process_error.has_value);
    r_std_process_child_destroy(&child);
    output_buffer = byte_array(allocator, UINT8_C(0));
    error_buffer = byte_array(allocator, UINT8_C(0));
    write_buffer = byte_array(allocator, UINT8_C(0x51));
    R_TEST_CHECK(output_buffer.data != NULL && error_buffer.data != NULL &&
                 write_buffer.data != NULL);
    output_started = r_std_io_read(&process_output.value, &output_buffer, (RStdIoDeadline){0});
    error_started = r_std_io_read(&process_error.value, &error_buffer, (RStdIoDeadline){0});
    R_TEST_CHECK(output_started.is_ok && error_started.is_ok && output_buffer.data == NULL &&
                 error_buffer.data == NULL);
    {
        RStdIoTaskStartResult write_started =
            r_std_io_write_all(&process_input.value, &write_buffer, (RStdIoDeadline){0});

        R_TEST_CHECK(
            write_started.is_ok && write_started.task != NULL && write_buffer.data == NULL &&
            r_runtime_task_await(&write_started.task, &write_result) == R_RUNTIME_TASK_AWAIT_OK &&
            write_result.kind == R_STD_IO_WRITE_ALL_RESULT_WRITTEN && write_result.written == 1U);
    }
    r_runtime_array_destroy(&write_result.buffer);
    R_TEST_CHECK(close_output(&process_input.value));
    R_TEST_CHECK(r_runtime_task_await(&output_started.task, &output_result) ==
                     R_RUNTIME_TASK_AWAIT_OK &&
                 output_result.kind == R_STD_IO_READ_RESULT_READ && output_result.count == 1U &&
                 ((const uint8_t *)output_result.buffer.data)[0] == UINT8_C(0x51));
    R_TEST_CHECK(r_runtime_task_await(&error_started.task, &error_result) ==
                     R_RUNTIME_TASK_AWAIT_OK &&
                 error_result.kind == R_STD_IO_READ_RESULT_READ && error_result.count == 1U &&
                 ((const uint8_t *)error_result.buffer.data)[0] == UINT8_C(0x71));
    r_runtime_array_destroy(&output_result.buffer);
    r_runtime_array_destroy(&error_result.buffer);
    R_TEST_CHECK(close_input(&process_output.value) && close_input(&process_error.value));
    r_runtime_darwin_process_testing_wait_idle();
    return 0;
}

static int test_shutdown_owned_is_none(RRuntimeAllocator *allocator) {
    RStdProcessChild child = spawn_child((RStdProcessStdio){R_STD_PROCESS_PIPE_PIPED,
                                                            R_STD_PROCESS_PIPE_PIPED,
                                                            R_STD_PROCESS_PIPE_PIPED},
                                         "--pause");
    uint64_t attempts_before;

    R_TEST_CHECK(child.storage != NULL);
    R_TEST_CHECK(r_runtime_executor_lifecycle_stop());
    r_runtime_darwin_process_lifecycle_stop();
    attempts_before = r_runtime_allocator_attempt_count(allocator);
    R_TEST_CHECK(!r_std_process_take_stdin(&child).has_value &&
                 !r_std_process_take_stdout(&child).has_value &&
                 !r_std_process_take_stderr(&child).has_value &&
                 r_runtime_allocator_attempt_count(allocator) == attempts_before);
    r_std_process_child_destroy(&child);
    return 0;
}

int main(void) {
    RRuntimeAllocator allocator;
    RRuntimeDarwinProcessStartResult process_start;

    r_runtime_allocator_initialize(&allocator);
    R_TEST_CHECK(r_runtime_executor_lifecycle_start(&allocator) == R_RUNTIME_EXECUTOR_START_OK);
    process_start = r_runtime_darwin_process_lifecycle_start(&allocator);
    R_TEST_CHECK(process_start.status == R_RUNTIME_DARWIN_PROCESS_START_OK);
    R_TEST_CHECK(test_none_states() == 0);
    R_TEST_CHECK(test_allocation_free_take(&allocator) == 0);
    R_TEST_CHECK(test_parallel_exactly_once() == 0);
    R_TEST_CHECK(test_extracted_lifetime_and_direction(&allocator) == 0);
    R_TEST_CHECK(test_shutdown_owned_is_none(&allocator) == 0);
    return 0;
}
