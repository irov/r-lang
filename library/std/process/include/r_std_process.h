#ifndef R_STD_PROCESS_H
#define R_STD_PROCESS_H

#include "r_runtime_allocator.h"
#include "r_runtime_task.h"
#include "r_std_async.h"
#include "r_std_error_types.h"
#include "r_std_fs.h"
#include "r_std_io.h"
#include "r_std_string.h"
#include "r_std_time.h"

#include <stdint.h>

typedef struct RStdProcessCommandStorage RStdProcessCommandStorage;
typedef struct RStdProcessChildStorage RStdProcessChildStorage;

/*
 * Opaque Move-only, Send, not-Sync command builder. storage is moved only by compiler-generated
 * glue; a nonempty value owns exactly one private command graph.
 */
typedef struct RStdProcessCommand {
    RStdProcessCommandStorage *storage;
} RStdProcessCommand;

/*
 * Opaque Move-only, Send+Sync child handle. storage retains the native child lifecycle; identity
 * is the immutable u64 mapping captured at successful spawn commit.
 */
typedef struct RStdProcessChild {
    RStdProcessChildStorage *storage;
    uint64_t identity;
} RStdProcessChild;

/* Canonical monomorphic C representations of the R pipe-extraction option results. */
typedef struct RStdProcessInputOption {
    _Bool has_value;
    RStdIoInput value;
} RStdProcessInputOption;

typedef struct RStdProcessOutputOption {
    _Bool has_value;
    RStdIoOutput value;
} RStdProcessOutputOption;

typedef enum RStdProcessErrorCode {
    R_STD_PROCESS_ERROR_INVALID_COMMAND = 0,
    R_STD_PROCESS_ERROR_INVALID_ARGUMENT = 1,
    R_STD_PROCESS_ERROR_NOT_FOUND = 2,
    R_STD_PROCESS_ERROR_PERMISSION_DENIED = 3,
    R_STD_PROCESS_ERROR_RESOURCE_EXHAUSTED = 4,
    R_STD_PROCESS_ERROR_SPAWN_FAILED = 5,
    R_STD_PROCESS_ERROR_NOT_RUNNING = 6,
    R_STD_PROCESS_ERROR_CANCELLED = 7,
    R_STD_PROCESS_ERROR_TIMED_OUT = 8,
    R_STD_PROCESS_ERROR_UNSUPPORTED = 9,
    R_STD_PROCESS_ERROR_OTHER = 10
} RStdProcessErrorCode;

typedef struct RStdProcessError {
    RStdProcessErrorCode code;
    int64_t native_code;
} RStdProcessError;

typedef enum RStdProcessPipeMode {
    R_STD_PROCESS_PIPE_INHERIT = 0,
    R_STD_PROCESS_PIPE_NULL_DEVICE = 1,
    R_STD_PROCESS_PIPE_PIPED = 2
} RStdProcessPipeMode;

typedef struct RStdProcessStdio {
    RStdProcessPipeMode input;
    RStdProcessPipeMode output;
    RStdProcessPipeMode error;
} RStdProcessStdio;

typedef enum RStdProcessTerminationKind {
    R_STD_PROCESS_TERMINATION_EXITED = 0,
    R_STD_PROCESS_TERMINATION_SIGNALLED = 1,
    R_STD_PROCESS_TERMINATION_OTHER = 2
} RStdProcessTerminationKind;

typedef struct RStdProcessExitStatus {
    RStdProcessTerminationKind kind;
    int32_t code;
    _Bool success;
} RStdProcessExitStatus;

/* CONTRACT_VIOLATION is an internal C ABI state and is not an R result alternative. */
typedef enum RStdProcessCallStatus {
    R_STD_PROCESS_CALL_SUCCESS = 0,
    R_STD_PROCESS_CALL_ERROR = 1,
    R_STD_PROCESS_CALL_CONTRACT_VIOLATION = 2
} RStdProcessCallStatus;

typedef struct RStdProcessCommandResult {
    RStdProcessCallStatus status;
    RStdProcessError error;
    RStdProcessCommand value;
} RStdProcessCommandResult;

typedef struct RStdProcessVoidResult {
    RStdProcessCallStatus status;
    RStdProcessError error;
} RStdProcessVoidResult;

typedef enum RStdProcessSpawnResultKind {
    R_STD_PROCESS_SPAWN_RESULT_SPAWNED = 0,
    R_STD_PROCESS_SPAWN_RESULT_FAILED = 1
} RStdProcessSpawnResultKind;

typedef struct RStdProcessSpawnResult {
    RStdProcessSpawnResultKind kind;
    RStdProcessChild child;
    RStdProcessError error;
    RStdProcessCommand command;
} RStdProcessSpawnResult;

typedef enum RStdProcessWaitResultKind {
    R_STD_PROCESS_WAIT_RESULT_EXITED = 0,
    R_STD_PROCESS_WAIT_RESULT_FAILED = 1
} RStdProcessWaitResultKind;

typedef struct RStdProcessWaitResult {
    RStdProcessWaitResultKind kind;
    RStdProcessExitStatus status;
    RStdProcessError error;
    RStdProcessChild child;
} RStdProcessWaitResult;

typedef struct RStdProcessDeadline {
    _Bool has_value;
    RStdTimeInstant value;
} RStdProcessDeadline;

typedef struct RStdProcessTaskStartResult {
    _Bool is_ok;
    RRuntimeTask *task;
    RStdAsyncStartError error;
} RStdProcessTaskStartResult;

/*
 * Ownership: executable is a shared call-bounded path borrow. Success returns the sole owner of a
 * deep command snapshot retained by allocator. Failure leaves executable unchanged and exposes no
 * partial command.
 */
RStdProcessCommandResult r_std_process_command_create(RRuntimeAllocator *allocator,
                                                      const RStdFsPath *executable);

/*
 * Ownership: command is an exclusive call-bounded borrow and value is a shared UTF-8 borrow.
 * Success appends one independent owned argument. Failure preserves command exactly.
 */
RStdProcessVoidResult r_std_process_arg(RStdProcessCommand *command, RStdStringView value);

/*
 * Ownership: command is exclusive; name and value are shared call-bounded UTF-8 borrows. Success
 * owns independent copies. Failure preserves command exactly.
 */
RStdProcessVoidResult
r_std_process_environment(RStdProcessCommand *command, RStdStringView name, RStdStringView value);

/*
 * Ownership: command is exclusive and name is a shared call-bounded UTF-8 borrow. Success masks
 * the captured or explicit key. Failure preserves command exactly.
 */
RStdProcessVoidResult r_std_process_remove_environment(RStdProcessCommand *command,
                                                       RStdStringView name);

/* Ownership: command is exclusive. Existing environment entries are dropped exactly once. */
void r_std_process_clear_environment(RStdProcessCommand *command);

/*
 * Ownership: command is exclusive and path is a shared call-bounded borrow. Success stores a deep
 * path owner. Failure preserves command exactly.
 */
RStdProcessVoidResult r_std_process_working_directory(RStdProcessCommand *command,
                                                      const RStdFsPath *path);

/* Ownership: command is exclusive and policy is Copy. No allocation occurs. */
void r_std_process_set_stdio(RStdProcessCommand *command, RStdProcessStdio policy);

/*
 * Eager exact spawn. A successful task start consumes command; every start failure preserves its
 * complete owner. The result returns either the sole child view or that same command with error.
 */
RStdProcessTaskStartResult r_std_process_spawn(RStdProcessCommand *command,
                                               RStdProcessDeadline deadline);

/*
 * Atomically transfers each configured parent pipe endpoint at most once. None denotes an
 * inherited, null-device, already extracted or closed endpoint. No allocation or native query is
 * performed, and an extracted handle is independent of later child destruction.
 */
RStdProcessOutputOption r_std_process_take_stdin(RStdProcessChild *child);
RStdProcessInputOption r_std_process_take_stdout(RStdProcessChild *child);
RStdProcessInputOption r_std_process_take_stderr(RStdProcessChild *child);

/*
 * Eager exclusive observation. Successful start consumes child. A deadline/native failure result
 * returns the same retryable owner; successful reap returns its totalized exit status.
 */
RStdProcessTaskStartResult r_std_process_wait(RStdProcessChild *child,
                                              RStdProcessDeadline deadline);

/*
 * Eager forceful-termination request over a call-bounded shared borrow. Successful start retains
 * the native identity independently; accepted termination is the non-cancellable commit.
 */
RStdProcessTaskStartResult r_std_process_terminate(const RStdProcessChild *child,
                                                   RStdProcessDeadline deadline);

/* Exact non-allocating adaptation of std.process::process_error to std.error::error. */
RStdError r_std_process_as_error(RStdProcessError value);

/*
 * Ownership: child is a shared call-bounded borrow. Returns the captured identity without
 * allocation, native query or mutation.
 */
uint64_t r_std_process_id(const RStdProcessChild *child);

/*
 * Performs orderly hosted-runtime shutdown, maps status through the target process-status
 * mapping and invokes the C17 normal-termination primitive exactly once. Never returns.
 */
_Noreturn void r_std_process_exit(int32_t status);

/* Immediately terminates through the target C17 abnormal-termination primitive. */
_Noreturn void r_std_process_abort(void);

/* Private compiler move/drop ABI used by the inline typed glue below. */
void r_library_internal_process_command_move(RStdProcessCommand *destination,
                                             RStdProcessCommand *source);
void r_library_internal_process_command_destroy(RStdProcessCommand *command);
void r_library_internal_process_child_move(RStdProcessChild *destination, RStdProcessChild *source);
void r_library_internal_process_child_destroy(RStdProcessChild *child);

static inline void r_std_process_command_move_initialize(RStdProcessCommand *destination,
                                                         RStdProcessCommand *source) {
    r_library_internal_process_command_move(destination, source);
}

static inline void r_std_process_command_destroy(RStdProcessCommand *command) {
    r_library_internal_process_command_destroy(command);
}

static inline void r_std_process_child_move_initialize(RStdProcessChild *destination,
                                                       RStdProcessChild *source) {
    r_library_internal_process_child_move(destination, source);
}

static inline void r_std_process_child_destroy(RStdProcessChild *child) {
    r_library_internal_process_child_destroy(child);
}

static inline void r_std_process_spawn_result_destroy(RStdProcessSpawnResult *result) {
    if (result->kind == R_STD_PROCESS_SPAWN_RESULT_SPAWNED) {
        r_std_process_child_destroy(&result->child);
    } else {
        r_std_process_command_destroy(&result->command);
    }
    *result = (RStdProcessSpawnResult){0};
}

static inline void r_std_process_spawn_result_move_initialize(RStdProcessSpawnResult *destination,
                                                              RStdProcessSpawnResult *source) {
    *destination = *source;
    *source = (RStdProcessSpawnResult){0};
}

static inline void r_std_process_wait_result_destroy(RStdProcessWaitResult *result) {
    if (result->kind == R_STD_PROCESS_WAIT_RESULT_FAILED) {
        r_std_process_child_destroy(&result->child);
    }
    *result = (RStdProcessWaitResult){0};
}

static inline void r_std_process_wait_result_move_initialize(RStdProcessWaitResult *destination,
                                                             RStdProcessWaitResult *source) {
    *destination = *source;
    *source = (RStdProcessWaitResult){0};
}

#endif
