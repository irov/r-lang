#include "r_runtime_darwin_io.h"

#include "io_internal.h"
#include <errno.h>
#include <pthread.h>
#include <stddef.h>
#include <unistd.h>

typedef struct RRuntimeDarwinIoProcessConsole {
    pthread_mutex_t mutex;
    RRuntimeDarwinIoHandle *input;
    RRuntimeDarwinIoHandle *output;
    RRuntimeDarwinIoHandle *error;
    _Bool started;
} RRuntimeDarwinIoProcessConsole;

static RRuntimeDarwinIoProcessConsole process_console = {
    PTHREAD_MUTEX_INITIALIZER,
    NULL,
    NULL,
    NULL,
    0,
};

static RRuntimeDarwinIoConsoleStartResult console_start_result(RRuntimeDarwinIoStartStatus status,
                                                               int native_error) {
    RRuntimeDarwinIoConsoleStartResult result;

    result.status = status;
    result.native_error = native_error;
    return result;
}

RRuntimeDarwinIoConsoleStartResult
r_runtime_darwin_io_process_console_start(RRuntimeAllocator *allocator) {
    RRuntimeDarwinIoHandleCreateResult input;
    RRuntimeDarwinIoHandleCreateResult output;
    RRuntimeDarwinIoHandleCreateResult error;

    if (pthread_mutex_lock(&process_console.mutex) != 0) {
        return console_start_result(R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT, EINVAL);
    }
    if (process_console.started || process_console.input != NULL ||
        process_console.output != NULL || process_console.error != NULL) {
        (void)pthread_mutex_unlock(&process_console.mutex);
        return console_start_result(R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT, EALREADY);
    }
    input = r_runtime_darwin_io_internal_handle_create_runtime_root(
        allocator, STDIN_FILENO, R_RUNTIME_DARWIN_IO_STREAM);
    if (input.status != R_RUNTIME_DARWIN_IO_START_OK) {
        (void)pthread_mutex_unlock(&process_console.mutex);
        return console_start_result(input.status, input.native_error);
    }
    output = r_runtime_darwin_io_internal_handle_create_runtime_root(
        allocator, STDOUT_FILENO, R_RUNTIME_DARWIN_IO_STREAM);
    if (output.status != R_RUNTIME_DARWIN_IO_START_OK) {
        (void)pthread_mutex_unlock(&process_console.mutex);
        r_runtime_darwin_io_internal_handle_release_runtime_root_and_wait(input.handle, 1);
        return console_start_result(output.status, output.native_error);
    }
    error = r_runtime_darwin_io_internal_handle_create_runtime_root(
        allocator, STDERR_FILENO, R_RUNTIME_DARWIN_IO_STREAM);
    if (error.status != R_RUNTIME_DARWIN_IO_START_OK) {
        (void)pthread_mutex_unlock(&process_console.mutex);
        r_runtime_darwin_io_internal_handle_release_runtime_root_and_wait(output.handle, 0);
        r_runtime_darwin_io_internal_handle_release_runtime_root_and_wait(input.handle, 1);
        return console_start_result(error.status, error.native_error);
    }
    process_console.input = input.handle;
    process_console.output = output.handle;
    process_console.error = error.handle;
    process_console.started = 1;
    if (pthread_mutex_unlock(&process_console.mutex) != 0) {
        abort();
    }
    return console_start_result(R_RUNTIME_DARWIN_IO_START_OK, 0);
}

void r_runtime_darwin_io_process_console_stop(void) {
    RRuntimeDarwinIoHandle *input;
    RRuntimeDarwinIoHandle *output;
    RRuntimeDarwinIoHandle *error;

    if (pthread_mutex_lock(&process_console.mutex) != 0) {
        abort();
    }
    if (!process_console.started) {
        if (pthread_mutex_unlock(&process_console.mutex) != 0) {
            abort();
        }
        return;
    }
    input = process_console.input;
    output = process_console.output;
    error = process_console.error;
    process_console.input = NULL;
    process_console.output = NULL;
    process_console.error = NULL;
    process_console.started = 0;
    if (pthread_mutex_unlock(&process_console.mutex) != 0) {
        abort();
    }

    r_runtime_darwin_io_internal_handle_stop_accepting(error);
    r_runtime_darwin_io_internal_handle_stop_accepting(output);
    r_runtime_darwin_io_internal_handle_stop_accepting(input);

    /* Drain already submitted output, including queued flush barriers, before root release. */
    r_runtime_darwin_io_internal_handle_wait_for_requests(error);
    r_runtime_darwin_io_internal_handle_wait_for_requests(output);
    r_runtime_darwin_io_internal_handle_release_runtime_root_and_wait(error, 0);
    r_runtime_darwin_io_internal_handle_release_runtime_root_and_wait(output, 0);
    r_runtime_darwin_io_internal_cancel_all_requests(input);
    r_runtime_darwin_io_internal_handle_wait_for_requests(input);
    r_runtime_darwin_io_internal_handle_release_runtime_root_and_wait(input, 1);
}

static RRuntimeDarwinIoHandle *process_console_retain(RRuntimeDarwinIoHandle *handle) {
    if (!r_runtime_darwin_io_handle_retain_view(handle)) {
        return NULL;
    }
    return handle;
}

RRuntimeDarwinIoHandle *r_runtime_darwin_io_process_stdin_retain(void) {
    RRuntimeDarwinIoHandle *handle;

    if (pthread_mutex_lock(&process_console.mutex) != 0) {
        return NULL;
    }
    handle = process_console.started ? process_console_retain(process_console.input) : NULL;
    if (pthread_mutex_unlock(&process_console.mutex) != 0) {
        abort();
    }
    return handle;
}

RRuntimeDarwinIoHandle *r_runtime_darwin_io_process_stdout_retain(void) {
    RRuntimeDarwinIoHandle *handle;

    if (pthread_mutex_lock(&process_console.mutex) != 0) {
        return NULL;
    }
    handle = process_console.started ? process_console_retain(process_console.output) : NULL;
    if (pthread_mutex_unlock(&process_console.mutex) != 0) {
        abort();
    }
    return handle;
}

RRuntimeDarwinIoHandle *r_runtime_darwin_io_process_stderr_retain(void) {
    RRuntimeDarwinIoHandle *handle;

    if (pthread_mutex_lock(&process_console.mutex) != 0) {
        return NULL;
    }
    handle = process_console.started ? process_console_retain(process_console.error) : NULL;
    if (pthread_mutex_unlock(&process_console.mutex) != 0) {
        abort();
    }
    return handle;
}
