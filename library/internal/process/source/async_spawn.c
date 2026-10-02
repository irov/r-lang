#include "r_library_process_internal.h"

#include "r_runtime_0_1.h"
#include "r_runtime_darwin_event.h"
#include "r_runtime_darwin_process.h"
#include "r_runtime_task.h"
#include "r_runtime_type.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

typedef struct RLibraryProcessSpawnControl {
    pthread_mutex_t mutex;
    RRuntimeDarwinProcessSpawn *request;
    RRuntimeTaskExternalExecution *execution;
    RStdProcessSpawnResult *result;
    RRuntimeDarwinProcessSpawnResult native_result;
    RRuntimeDarwinProcessChild *native_child;
    _Atomic _Bool creation_commit_selected;
    _Bool callback_released;
    _Bool cancel_reported;
    _Bool completion_selected;
    _Bool finalized;
} RLibraryProcessSpawnControl;

typedef struct RLibraryProcessSpawnPayload {
    RStdProcessCommand *staged_command;
    RStdProcessCommand command;
    RLibraryProcessNativeText text;
    RStdProcessChildStorage *child_storage;
    RLibraryProcessSpawnControl *control;
    RStdProcessDeadline deadline;
    RStdProcessError immediate_error;
    uint64_t immediate_event_sequence;
    _Bool immediate;
} RLibraryProcessSpawnPayload;

typedef struct RLibraryProcessSpawnFinalizeAction {
    RRuntimeDarwinProcessSpawnResult native_result;
    RRuntimeDarwinProcessChild *native_child;
    _Bool completion_selected;
    _Bool ready;
} RLibraryProcessSpawnFinalizeAction;

_Noreturn static void spawn_panic(void) {
    r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION, (RRuntimeSourceSpan){0U, 0U, 0U});
}

static RStdProcessTaskStartResult task_start_failure(RRuntimeTaskStartStatus status) {
    RStdProcessTaskStartResult result = {0};

    switch (status) {
    case R_RUNTIME_TASK_START_ALLOCATION_FAILED:
        result.error = R_STD_ASYNC_START_REFUSAL();
        return result;
    case R_RUNTIME_TASK_START_RUNTIME_STOPPING:
        result.error = R_STD_ASYNC_START_RUNTIME_STOPPING;
        return result;
    case R_RUNTIME_TASK_START_OK:
    case R_RUNTIME_TASK_START_INVALID:
        spawn_panic();
    }
    spawn_panic();
}

static void spawn_result_move(void *destination_pointer, void *source_pointer) {
    RStdProcessSpawnResult *destination = destination_pointer;
    RStdProcessSpawnResult *source = source_pointer;

    *destination = *source;
    source->child.storage = NULL;
    source->child.identity = UINT64_C(0);
    source->command.storage = NULL;
}

static void spawn_result_drop(void *value) {
    RStdProcessSpawnResult *result = value;

    r_library_internal_process_child_destroy(&result->child);
    r_library_internal_process_command_destroy(&result->command);
}

static RRuntimeTypeInfo spawn_result_type(void) {
    return (RRuntimeTypeInfo){
        sizeof(RStdProcessSpawnResult),
        _Alignof(RStdProcessSpawnResult),
        spawn_result_move,
        spawn_result_drop,
    };
}

static RLibraryProcessSpawnControl *spawn_control_create(RRuntimeAllocator *allocator) {
    RLibraryProcessSpawnControl *control = NULL;

    if (allocator == NULL ||
        r_runtime_allocator_allocate(allocator,
                                     sizeof(*control),
                                     _Alignof(RLibraryProcessSpawnControl),
                                     (void **)&control) != R_RUNTIME_ALLOCATION_OK) {
        return NULL;
    }
    (void)memset(control, 0, sizeof(*control));
    atomic_init(&control->creation_commit_selected, 0);
    if (pthread_mutex_init(&control->mutex, NULL) != 0) {
        r_runtime_allocator_deallocate(control, _Alignof(RLibraryProcessSpawnControl));
        return NULL;
    }
    return control;
}

static void spawn_control_destroy(RLibraryProcessSpawnControl *control) {
    RRuntimeDarwinProcessSpawn *request;

    if (control == NULL) {
        return;
    }
    if (pthread_mutex_lock(&control->mutex) != 0) {
        spawn_panic();
    }
    request = control->request;
    control->request = NULL;
    if (control->native_child != NULL) {
        (void)pthread_mutex_unlock(&control->mutex);
        spawn_panic();
    }
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        spawn_panic();
    }
    if (request != NULL) {
        r_runtime_darwin_process_spawn_abort(&request);
    }
    if (pthread_mutex_destroy(&control->mutex) != 0) {
        spawn_panic();
    }
    r_runtime_allocator_deallocate(control, _Alignof(RLibraryProcessSpawnControl));
}

static void spawn_payload_move(void *destination_pointer, void *source_pointer) {
    RLibraryProcessSpawnPayload *destination = destination_pointer;
    RLibraryProcessSpawnPayload *source = source_pointer;

    (void)memset(destination, 0, sizeof(*destination));
    destination->text = source->text;
    (void)memset(&source->text, 0, sizeof(source->text));
    destination->child_storage = source->child_storage;
    source->child_storage = NULL;
    destination->control = source->control;
    source->control = NULL;
    destination->deadline = source->deadline;
    destination->immediate_error = source->immediate_error;
    destination->immediate_event_sequence = source->immediate_event_sequence;
    destination->immediate = source->immediate;
    if (source->staged_command == NULL) {
        spawn_panic();
    }
    r_library_internal_process_command_move(&destination->command, source->staged_command);
    source->staged_command = NULL;
}

static void spawn_payload_drop(void *value) {
    RLibraryProcessSpawnPayload *payload = value;

    spawn_control_destroy(payload->control);
    payload->control = NULL;
    r_library_internal_process_child_storage_destroy(payload->child_storage);
    payload->child_storage = NULL;
    r_library_internal_process_native_text_destroy(&payload->text);
    r_library_internal_process_command_destroy(&payload->command);
}

static RStdProcessError process_error_from_native(int native_error) {
    RStdProcessErrorCode code;

    switch (native_error) {
    case ENOENT:
        code = R_STD_PROCESS_ERROR_NOT_FOUND;
        break;
    case EACCES:
    case EPERM:
        code = R_STD_PROCESS_ERROR_PERMISSION_DENIED;
        break;
    case EAGAIN:
    case ENOMEM:
    case EMFILE:
    case ENFILE:
    case ENOSPC:
        code = R_STD_PROCESS_ERROR_RESOURCE_EXHAUSTED;
        break;
    case ENOEXEC:
    case ENOTDIR:
    case ESRCH:
        code = R_STD_PROCESS_ERROR_SPAWN_FAILED;
        break;
#if defined(EBADARCH)
    case EBADARCH:
        code = R_STD_PROCESS_ERROR_SPAWN_FAILED;
        break;
#endif
#if defined(EBADMACHO)
    case EBADMACHO:
        code = R_STD_PROCESS_ERROR_SPAWN_FAILED;
        break;
#endif
#if defined(EBADEXEC)
    case EBADEXEC:
        code = R_STD_PROCESS_ERROR_SPAWN_FAILED;
        break;
#endif
    default:
        code = R_STD_PROCESS_ERROR_OTHER;
        break;
    }
    return r_library_internal_process_error(code, (int64_t)native_error);
}

static RLibraryProcessSpawnFinalizeAction
spawn_finalize_action_locked(RLibraryProcessSpawnControl *control) {
    RLibraryProcessSpawnFinalizeAction action = {0};
    const uint64_t cancellation_sequence =
        r_runtime_task_external_cancellation_sequence(control->execution);

    if (control->callback_released &&
        (cancellation_sequence == UINT64_C(0) || control->cancel_reported) && !control->finalized) {
        control->finalized = 1;
        action.native_result = control->native_result;
        action.native_child = control->native_child;
        action.completion_selected = control->completion_selected;
        action.ready = 1;
        control->native_child = NULL;
    }
    return action;
}

static void spawn_fill_result(RLibraryProcessSpawnPayload *payload,
                              RLibraryProcessSpawnFinalizeAction action) {
    RStdProcessSpawnResult *result = payload->control->result;

    if (!action.completion_selected) {
        if (action.native_child != NULL) {
            r_runtime_darwin_process_child_release(action.native_child);
        }
        return;
    }
    (void)memset(result, 0, sizeof(*result));
    if (payload->immediate) {
        if (action.native_child != NULL) {
            spawn_panic();
        }
        result->kind = R_STD_PROCESS_SPAWN_RESULT_FAILED;
        result->error = payload->immediate_error;
        r_library_internal_process_command_move(&result->command, &payload->command);
        return;
    }
    if (action.native_result.spawned) {
        if (action.native_child == NULL || payload->child_storage == NULL) {
            spawn_panic();
        }
        r_library_internal_process_child_publish(payload->child_storage, action.native_child);
        result->kind = R_STD_PROCESS_SPAWN_RESULT_SPAWNED;
        result->child.storage = payload->child_storage;
        result->child.identity = r_runtime_darwin_process_child_identity(action.native_child);
        payload->child_storage = NULL;
        r_library_internal_process_command_destroy(&payload->command);
    } else {
        result->kind = R_STD_PROCESS_SPAWN_RESULT_FAILED;
        if (action.native_result.terminal_event == R_RUNTIME_DARWIN_PROCESS_TERMINAL_TIMED_OUT) {
            result->error =
                r_library_internal_process_error(R_STD_PROCESS_ERROR_TIMED_OUT, INT64_C(0));
        } else if (action.native_result.terminal_event ==
                   R_RUNTIME_DARWIN_PROCESS_TERMINAL_NATIVE) {
            result->error = process_error_from_native(action.native_result.native_error);
        } else {
            spawn_panic();
        }
        r_library_internal_process_command_move(&result->command, &payload->command);
    }
}

static void spawn_finalize(RLibraryProcessSpawnPayload *payload,
                           RLibraryProcessSpawnFinalizeAction action) {
    if (!action.ready) {
        return;
    }
    spawn_fill_result(payload, action);
    r_runtime_task_external_acknowledge(payload->control->execution);
}

static _Bool spawn_creation_commit(RRuntimeDarwinProcessSpawn *request,
                                   void *context_pointer,
                                   uint64_t commit_sequence,
                                   uint64_t *earlier_cancellation_sequence) {
    RLibraryProcessSpawnPayload *payload = context_pointer;
    RLibraryProcessSpawnControl *control = payload->control;
    _Bool selected;

    if (control == NULL || control->request != request || commit_sequence == UINT64_C(0) ||
        earlier_cancellation_sequence == NULL) {
        spawn_panic();
    }
    selected =
        r_runtime_task_external_try_select_completion_at(control->execution, commit_sequence);
    if (selected) {
        atomic_store_explicit(&control->creation_commit_selected, 1, memory_order_release);
    } else {
        *earlier_cancellation_sequence =
            r_runtime_task_external_cancellation_sequence(control->execution);
    }
    return selected;
}

static void spawn_native_completed(RRuntimeDarwinProcessSpawn *request, void *context_pointer) {
    RLibraryProcessSpawnPayload *payload = context_pointer;
    RLibraryProcessSpawnControl *control = payload->control;
    const RRuntimeDarwinProcessSpawnResult native_result =
        r_runtime_darwin_process_spawn_result(request);
    RRuntimeDarwinProcessChild *native_child = NULL;
    RLibraryProcessSpawnFinalizeAction action;
    _Bool completion_selected = 0;

    if (native_result.spawned) {
        native_child = r_runtime_darwin_process_spawn_take_child(request);
        if (native_child == NULL) {
            spawn_panic();
        }
    }
    if (native_result.creation_commit_selected) {
        completion_selected =
            atomic_load_explicit(&control->creation_commit_selected, memory_order_acquire);
        if (!completion_selected) {
            spawn_panic();
        }
    } else if (native_result.terminal_event != R_RUNTIME_DARWIN_PROCESS_TERMINAL_CANCELLED) {
        completion_selected = r_runtime_task_external_try_select_completion_at(
            control->execution, native_result.terminal_event_sequence);
    }
    if (pthread_mutex_lock(&control->mutex) != 0) {
        spawn_panic();
    }
    if (control->request != request || control->callback_released) {
        (void)pthread_mutex_unlock(&control->mutex);
        spawn_panic();
    }
    control->request = NULL;
    control->native_result = native_result;
    control->native_child = native_child;
    control->callback_released = 1;
    control->completion_selected = completion_selected;
    action = spawn_finalize_action_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        spawn_panic();
    }
    r_runtime_darwin_process_spawn_release(request);
    spawn_finalize(payload, action);
}

static void spawn_external_cancel(RRuntimeTaskExternalExecution *execution, void *payload_pointer) {
    RLibraryProcessSpawnPayload *payload = payload_pointer;
    RLibraryProcessSpawnControl *control = payload->control;
    RLibraryProcessSpawnFinalizeAction action;
    const uint64_t cancellation_sequence = r_runtime_task_external_cancellation_sequence(execution);

    if (control == NULL || control->execution != execution ||
        cancellation_sequence == UINT64_C(0) || pthread_mutex_lock(&control->mutex) != 0) {
        spawn_panic();
    }
    if (control->cancel_reported) {
        (void)pthread_mutex_unlock(&control->mutex);
        spawn_panic();
    }
    if (control->request != NULL) {
        (void)r_runtime_darwin_process_spawn_cancel(control->request, cancellation_sequence);
    }
    control->cancel_reported = 1;
    action = spawn_finalize_action_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        spawn_panic();
    }
    spawn_finalize(payload, action);
}

static void spawn_complete_immediate(RLibraryProcessSpawnPayload *payload,
                                     RRuntimeTaskExternalExecution *execution) {
    RLibraryProcessSpawnControl *control = payload->control;
    RLibraryProcessSpawnFinalizeAction action;
    const uint64_t cancellation_before_ready =
        r_runtime_task_external_cancellation_sequence(execution);
    const _Bool completion_selected = r_runtime_task_external_try_select_completion_at(
        execution, payload->immediate_event_sequence);

    if (pthread_mutex_lock(&control->mutex) != 0) {
        spawn_panic();
    }
    control->native_result = (RRuntimeDarwinProcessSpawnResult){
        R_RUNTIME_DARWIN_PROCESS_TERMINAL_TIMED_OUT,
        0,
        payload->immediate_event_sequence,
        0,
        0,
    };
    control->callback_released = 1;
    control->completion_selected = completion_selected;
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        spawn_panic();
    }
    r_runtime_task_external_start_ready(execution);
    if (cancellation_before_ready != UINT64_C(0) && completion_selected) {
        if (pthread_mutex_lock(&control->mutex) != 0) {
            spawn_panic();
        }
        control->cancel_reported = 1;
        if (pthread_mutex_unlock(&control->mutex) != 0) {
            spawn_panic();
        }
    }
    if (pthread_mutex_lock(&control->mutex) != 0) {
        spawn_panic();
    }
    action = spawn_finalize_action_locked(control);
    if (pthread_mutex_unlock(&control->mutex) != 0) {
        spawn_panic();
    }
    spawn_finalize(payload, action);
}

static void spawn_external_start(RRuntimeTaskExternalExecution *execution,
                                 void *payload_pointer,
                                 void *result_pointer) {
    RLibraryProcessSpawnPayload *payload = payload_pointer;
    RLibraryProcessSpawnControl *control = payload->control;
    RRuntimeDarwinProcessSpawn *request;
    RStdProcessError deadline_error = {0};
    uint64_t ignored_timeout = UINT64_C(0);
    RLibraryProcessDeadlineStatus deadline_status;

    control->execution = execution;
    control->result = result_pointer;
    if (payload->immediate) {
        spawn_complete_immediate(payload, execution);
        return;
    }
    request = control->request;
    if (request == NULL) {
        spawn_panic();
    }
    deadline_status = r_library_internal_process_deadline_timeout(
        payload->deadline, &ignored_timeout, &deadline_error);
    if (deadline_status != R_LIBRARY_PROCESS_DEADLINE_READY) {
        control->request = NULL;
        r_runtime_darwin_process_spawn_abort(&request);
        payload->immediate = 1;
        payload->immediate_error = deadline_error;
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        spawn_complete_immediate(payload, execution);
        return;
    }
    if (!r_runtime_darwin_process_spawn_bind(
            request, spawn_creation_commit, spawn_native_completed, payload)) {
        spawn_panic();
    }
    r_runtime_task_external_start_ready(execution);
    if (!r_runtime_darwin_process_spawn_activate(request)) {
        spawn_panic();
    }
}

static RRuntimeDarwinProcessStdioMode native_stdio_mode(RStdProcessPipeMode mode) {
    switch (mode) {
    case R_STD_PROCESS_PIPE_INHERIT:
        return R_RUNTIME_DARWIN_PROCESS_STDIO_INHERIT;
    case R_STD_PROCESS_PIPE_NULL_DEVICE:
        return R_RUNTIME_DARWIN_PROCESS_STDIO_NULL_DEVICE;
    case R_STD_PROCESS_PIPE_PIPED:
        return R_RUNTIME_DARWIN_PROCESS_STDIO_PIPED;
    }
    spawn_panic();
}

static RRuntimeTaskStartStatus spawn_reserve_native(RLibraryProcessSpawnPayload *payload,
                                                    RRuntimeAllocator *allocator) {
    const RStdProcessStdio stdio = r_library_internal_process_stdio(payload->staged_command);
    RRuntimeDarwinProcessSpawnOptions options;
    RRuntimeDarwinProcessPrepareResult preparation;
    RStdProcessError deadline_error = {0};
    uint64_t timeout_nanoseconds = UINT64_C(0);
    RLibraryProcessDeadlineStatus deadline_status;

    deadline_status = r_library_internal_process_deadline_timeout(
        payload->deadline, &timeout_nanoseconds, &deadline_error);
    if (deadline_status != R_LIBRARY_PROCESS_DEADLINE_READY) {
        payload->immediate = 1;
        payload->immediate_error = deadline_error;
        payload->immediate_event_sequence = r_runtime_darwin_event_sequence_next();
        return R_RUNTIME_TASK_START_OK;
    }
    options = (RRuntimeDarwinProcessSpawnOptions){
        payload->text.executable,
        payload->text.arguments,
        payload->text.environment,
        payload->text.working_directory,
        r_library_internal_process_current_directory(payload->staged_command),
        native_stdio_mode(stdio.input),
        native_stdio_mode(stdio.output),
        native_stdio_mode(stdio.error),
        timeout_nanoseconds,
    };
    preparation = r_runtime_darwin_process_spawn_prepare(allocator, &options);
    switch (preparation.status) {
    case R_RUNTIME_DARWIN_PROCESS_START_OK:
        payload->control->request = preparation.request;
        return R_RUNTIME_TASK_START_OK;
    case R_RUNTIME_DARWIN_PROCESS_START_ALLOCATION_FAILED:
        return R_RUNTIME_TASK_START_ALLOCATION_FAILED;
    case R_RUNTIME_DARWIN_PROCESS_START_RUNTIME_STOPPING:
        return R_RUNTIME_TASK_START_RUNTIME_STOPPING;
    case R_RUNTIME_DARWIN_PROCESS_START_INVALID:
        spawn_panic();
    }
    spawn_panic();
}

RStdProcessTaskStartResult r_library_internal_process_spawn(RStdProcessCommand *command,
                                                            RStdProcessDeadline deadline) {
    const RRuntimeTypeInfo payload_type = {
        sizeof(RLibraryProcessSpawnPayload),
        _Alignof(RLibraryProcessSpawnPayload),
        spawn_payload_move,
        spawn_payload_drop,
    };
    RLibraryProcessSpawnPayload payload = {0};
    RRuntimeTaskPrepareResult task_preparation;
    RRuntimeTaskStartResult started;
    RRuntimeAllocator *allocator;
    RLibraryProcessNativeTextStatus text_status;
    RRuntimeTaskStartStatus native_status;
    RStdProcessTaskStartResult result = {0};

    payload.staged_command = command;
    payload.deadline = deadline;
    task_preparation = r_runtime_task_external_start_prepare(
        payload_type, spawn_result_type(), spawn_external_start, spawn_external_cancel);
    if (task_preparation.status != R_RUNTIME_TASK_START_OK) {
        return task_start_failure(task_preparation.status);
    }
    allocator = r_runtime_task_start_allocator(task_preparation.transaction);
    if (allocator == NULL) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        spawn_panic();
    }
    text_status = r_library_internal_process_native_text_create(command, &payload.text);
    if (text_status != R_LIBRARY_PROCESS_NATIVE_TEXT_OK) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        if (text_status == R_LIBRARY_PROCESS_NATIVE_TEXT_RESOURCE_EXHAUSTED) {
            return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
        }
        spawn_panic();
    }
    payload.child_storage = r_library_internal_process_child_reserve(allocator);
    payload.control = spawn_control_create(allocator);
    if (payload.child_storage == NULL || payload.control == NULL) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        spawn_payload_drop(&payload);
        return task_start_failure(R_RUNTIME_TASK_START_ALLOCATION_FAILED);
    }
    native_status = spawn_reserve_native(&payload, allocator);
    if (native_status != R_RUNTIME_TASK_START_OK) {
        r_runtime_task_start_abort(&task_preparation.transaction);
        spawn_payload_drop(&payload);
        return task_start_failure(native_status);
    }
    started = r_runtime_task_start_commit(&task_preparation.transaction, &payload);
    if (started.status != R_RUNTIME_TASK_START_OK) {
        spawn_payload_drop(&payload);
        return task_start_failure(started.status);
    }
    result.is_ok = 1;
    result.task = started.task;
    return result;
}
