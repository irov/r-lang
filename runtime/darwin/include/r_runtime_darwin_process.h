#ifndef R_RUNTIME_DARWIN_PROCESS_H
#define R_RUNTIME_DARWIN_PROCESS_H

#include "r_runtime_allocator.h"
#include "r_runtime_darwin_io.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct RRuntimeDarwinProcessSpawn RRuntimeDarwinProcessSpawn;
typedef struct RRuntimeDarwinProcessChild RRuntimeDarwinProcessChild;
typedef struct RRuntimeDarwinProcessWait RRuntimeDarwinProcessWait;

typedef enum RRuntimeDarwinProcessPipe {
    R_RUNTIME_DARWIN_PROCESS_PIPE_STDIN = 0,
    R_RUNTIME_DARWIN_PROCESS_PIPE_STDOUT,
    R_RUNTIME_DARWIN_PROCESS_PIPE_STDERR
} RRuntimeDarwinProcessPipe;

typedef enum RRuntimeDarwinProcessStdioMode {
    R_RUNTIME_DARWIN_PROCESS_STDIO_INHERIT = 0,
    R_RUNTIME_DARWIN_PROCESS_STDIO_NULL_DEVICE,
    R_RUNTIME_DARWIN_PROCESS_STDIO_PIPED
} RRuntimeDarwinProcessStdioMode;

typedef struct RRuntimeDarwinProcessSpawnOptions {
    const char *executable;
    char *const *arguments;
    char *const *environment;
    const char *working_directory;
    int current_directory;
    RRuntimeDarwinProcessStdioMode input;
    RRuntimeDarwinProcessStdioMode output;
    RRuntimeDarwinProcessStdioMode error;
    uint64_t timeout_nanoseconds;
} RRuntimeDarwinProcessSpawnOptions;

typedef enum RRuntimeDarwinProcessStartStatus {
    R_RUNTIME_DARWIN_PROCESS_START_OK = 0,
    R_RUNTIME_DARWIN_PROCESS_START_INVALID,
    R_RUNTIME_DARWIN_PROCESS_START_ALLOCATION_FAILED,
    R_RUNTIME_DARWIN_PROCESS_START_RUNTIME_STOPPING
} RRuntimeDarwinProcessStartStatus;

typedef enum RRuntimeDarwinProcessTerminalEvent {
    R_RUNTIME_DARWIN_PROCESS_TERMINAL_NATIVE = 0,
    R_RUNTIME_DARWIN_PROCESS_TERMINAL_CANCELLED,
    R_RUNTIME_DARWIN_PROCESS_TERMINAL_TIMED_OUT
} RRuntimeDarwinProcessTerminalEvent;

typedef struct RRuntimeDarwinProcessStartResult {
    RRuntimeDarwinProcessStartStatus status;
    int native_error;
} RRuntimeDarwinProcessStartResult;

typedef struct RRuntimeDarwinProcessPrepareResult {
    RRuntimeDarwinProcessSpawn *request;
    RRuntimeDarwinProcessStartStatus status;
    int native_error;
} RRuntimeDarwinProcessPrepareResult;

typedef struct RRuntimeDarwinProcessSpawnResult {
    RRuntimeDarwinProcessTerminalEvent terminal_event;
    int native_error;
    uint64_t terminal_event_sequence;
    _Bool spawned;
    _Bool creation_commit_selected;
} RRuntimeDarwinProcessSpawnResult;

typedef struct RRuntimeDarwinProcessWaitPrepareResult {
    RRuntimeDarwinProcessWait *request;
    RRuntimeDarwinProcessStartStatus status;
    int native_error;
} RRuntimeDarwinProcessWaitPrepareResult;

typedef struct RRuntimeDarwinProcessWaitResult {
    RRuntimeDarwinProcessTerminalEvent terminal_event;
    int native_error;
    int wait_status;
    uint64_t terminal_event_sequence;
    _Bool reaped;
} RRuntimeDarwinProcessWaitResult;

typedef struct RRuntimeDarwinProcessTerminateResult {
    int native_error;
    uint64_t terminal_event_sequence;
    _Bool accepted;
    _Bool already_terminal;
} RRuntimeDarwinProcessTerminateResult;

/* The request borrow is valid only for the duration of either callback. */
typedef _Bool (*RRuntimeDarwinProcessCommitFn)(RRuntimeDarwinProcessSpawn *request,
                                               void *context,
                                               uint64_t commit_sequence,
                                               uint64_t *earlier_cancellation_sequence);
typedef void (*RRuntimeDarwinProcessCompletionFn)(RRuntimeDarwinProcessSpawn *request,
                                                  void *context);
typedef void (*RRuntimeDarwinProcessWaitCompletionFn)(RRuntimeDarwinProcessWait *request,
                                                      void *context);

/* Starts one process registry and its non-executor serial submission queue. */
RRuntimeDarwinProcessStartResult
r_runtime_darwin_process_lifecycle_start(RRuntimeAllocator *allocator);

/*
 * Rejects new preparation, closes every retained parent pipe, terminates each live child and waits
 * for exactly-once reap plus every Dispatch process-source and pipe-root cleanup acknowledgement.
 */
void r_runtime_darwin_process_lifecycle_stop(void);

/*
 * Reserves all fallible spawn state without creating a child or consuming any language owner.
 * Text pointers and their elements remain borrowed until completion. The current-directory fd is
 * duplicated, and every requested parent pipe already owns a Dispatch I/O STREAM view on success.
 */
RRuntimeDarwinProcessPrepareResult
r_runtime_darwin_process_spawn_prepare(RRuntimeAllocator *allocator,
                                       const RRuntimeDarwinProcessSpawnOptions *options);

/* Installs the sole creation-commit arbiter and terminal observer without native submission. */
_Bool r_runtime_darwin_process_spawn_bind(RRuntimeDarwinProcessSpawn *request,
                                          RRuntimeDarwinProcessCommitFn commit,
                                          RRuntimeDarwinProcessCompletionFn completion,
                                          void *context);

/* Allocation-free submission to the private process queue. */
_Bool r_runtime_darwin_process_spawn_activate(RRuntimeDarwinProcessSpawn *request);

/* First-event candidates accepted only before the creation-commit arbitration point. */
_Bool r_runtime_darwin_process_spawn_cancel(RRuntimeDarwinProcessSpawn *request,
                                            uint64_t cancellation_sequence);
_Bool r_runtime_darwin_process_spawn_deadline_expired(RRuntimeDarwinProcessSpawn *request,
                                                      uint64_t deadline_sequence);

/* Callback-only observation and one-time transfer of the successful child view. */
RRuntimeDarwinProcessSpawnResult
r_runtime_darwin_process_spawn_result(RRuntimeDarwinProcessSpawn *request);
RRuntimeDarwinProcessChild *
r_runtime_darwin_process_spawn_take_child(RRuntimeDarwinProcessSpawn *request);

/* Completed requests use release; prepared and unbound requests use abort. */
void r_runtime_darwin_process_spawn_release(RRuntimeDarwinProcessSpawn *request);
void r_runtime_darwin_process_spawn_abort(RRuntimeDarwinProcessSpawn **request);

uint64_t r_runtime_darwin_process_child_identity(const RRuntimeDarwinProcessChild *child);

/*
 * Atomically transfers one configured parent pipe view from a live child view. The operation is
 * allocation-free. A missing, previously transferred or shutdown-owned endpoint returns null.
 */
RRuntimeDarwinIoHandle *r_runtime_darwin_process_child_take_pipe(RRuntimeDarwinProcessChild *child,
                                                                 RRuntimeDarwinProcessPipe pipe);

/*
 * Reserves one exclusive observation request and an optional strict deadline source without
 * consuming the language child owner. Activation is allocation-free. Native process completion,
 * deadline and cancellation are ordered by the shared Darwin event sequence.
 */
RRuntimeDarwinProcessWaitPrepareResult r_runtime_darwin_process_wait_prepare(
    RRuntimeAllocator *allocator, RRuntimeDarwinProcessChild *child, uint64_t timeout_nanoseconds);
_Bool r_runtime_darwin_process_wait_bind(RRuntimeDarwinProcessWait *request,
                                         RRuntimeDarwinProcessWaitCompletionFn completion,
                                         void *context);
_Bool r_runtime_darwin_process_wait_activate(RRuntimeDarwinProcessWait *request);
_Bool r_runtime_darwin_process_wait_cancel(RRuntimeDarwinProcessWait *request,
                                           uint64_t cancellation_sequence);
RRuntimeDarwinProcessWaitResult
r_runtime_darwin_process_wait_result(RRuntimeDarwinProcessWait *request);
void r_runtime_darwin_process_wait_release(RRuntimeDarwinProcessWait *request);
void r_runtime_darwin_process_wait_abort(RRuntimeDarwinProcessWait **request);

/*
 * A successful retain is independent of the call-bounded public child borrow. Termination is a
 * bounded SIGKILL submission; accepted is its non-cancellable commit point.
 */
RRuntimeDarwinProcessChild *
r_runtime_darwin_process_child_operation_retain(RRuntimeDarwinProcessChild *child);
void r_runtime_darwin_process_child_operation_release(RRuntimeDarwinProcessChild *child);
RRuntimeDarwinProcessTerminateResult
r_runtime_darwin_process_child_force_terminate(RRuntimeDarwinProcessChild *child);

/* Consumes one R child view. It closes unextracted pipe views and never terminates the child. */
void r_runtime_darwin_process_child_release(RRuntimeDarwinProcessChild *child);

typedef enum RRuntimeDarwinProcessFailureStage {
    R_RUNTIME_DARWIN_PROCESS_FAIL_NONE = 0,
    R_RUNTIME_DARWIN_PROCESS_FAIL_PIPE,
    R_RUNTIME_DARWIN_PROCESS_FAIL_PIPE_IO,
    R_RUNTIME_DARWIN_PROCESS_FAIL_DEADLINE_SOURCE,
    R_RUNTIME_DARWIN_PROCESS_FAIL_PROCESS_SOURCE,
    R_RUNTIME_DARWIN_PROCESS_FAIL_CONTINUE,
    R_RUNTIME_DARWIN_PROCESS_FAIL_WAIT_DEADLINE_SOURCE
} RRuntimeDarwinProcessFailureStage;

#if defined(R_RUNTIME_DARWIN_PROCESS_TESTING)
void r_runtime_darwin_process_testing_fail_next(RRuntimeDarwinProcessFailureStage stage,
                                                int native_error);
void r_runtime_darwin_process_testing_pause_next_before_commit(void);
void r_runtime_darwin_process_testing_wait_before_commit(void);
void r_runtime_darwin_process_testing_release_before_commit(void);
uint64_t r_runtime_darwin_process_testing_deadline_count(void);
void r_runtime_darwin_process_testing_wait_deadline_after(uint64_t previous_count);
void r_runtime_darwin_process_child_testing_wait_reaped(RRuntimeDarwinProcessChild *child);
size_t r_runtime_darwin_process_child_testing_pipe_count(RRuntimeDarwinProcessChild *child);
size_t r_runtime_darwin_process_testing_registry_count(void);
uint64_t r_runtime_darwin_process_testing_reap_count(void);
void r_runtime_darwin_process_testing_wait_idle(void);
#endif

#endif
