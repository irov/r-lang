#ifndef R_RUNTIME_DARWIN_IO_INTERNAL_H
#define R_RUNTIME_DARWIN_IO_INTERNAL_H

#include "r_runtime_darwin_io.h"

#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* How payload requests of a handle reach the native descriptor. DISPATCH uses Dispatch I/O
   channels; FILE and SOCKET are the direct engines of io_direct.c, which own no Dispatch I/O
   object: their root release defers descriptor cleanup until every direct activity ends. */
typedef enum RRuntimeDarwinIoEngine {
    R_RUNTIME_DARWIN_IO_ENGINE_DISPATCH = 0,
    R_RUNTIME_DARWIN_IO_ENGINE_FILE,
    R_RUNTIME_DARWIN_IO_ENGINE_SOCKET
} RRuntimeDarwinIoEngine;

typedef enum RRuntimeDarwinIoSourceState {
    R_RUNTIME_DARWIN_IO_SOURCE_INACTIVE = 0,
    R_RUNTIME_DARWIN_IO_SOURCE_RESUMED,
    R_RUNTIME_DARWIN_IO_SOURCE_SUSPENDED
} RRuntimeDarwinIoSourceState;

struct RRuntimeDarwinIoHandle {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    size_t references;
    size_t view_count;
    size_t request_count;
    RRuntimeAllocator *allocator;
    dispatch_queue_t callback_queue;
    dispatch_io_t root_channel;
    RRuntimeDarwinIoHandleCleanupFn cleanup_observer;
    void *cleanup_observer_context;
    RRuntimeDarwinIoRequest *requests;
    int retained_descriptor;
    int root_cleanup_error;
#if defined(R_RUNTIME_DARWIN_IO_TESTING)
    int forced_root_cleanup_error;
#endif
    RRuntimeDarwinIoType type;
    uint64_t next_submission_sequence;
    _Bool closed;
    _Bool root_released;
    _Bool root_cleanup_done;
    _Bool runtime_root_owned;
    _Bool console_identity;
    _Bool retained_descriptor_owned;
    _Bool borrowed_shared_write_pipeline;
    _Bool input_shutdown;
    _Bool output_shutdown;
    RRuntimeDarwinIoEngine engine;
    /* Direct activities that may still touch the descriptor: activated requests until their
       finish, and each socket readiness source until its cancel handler. */
    size_t direct_inflight;
    _Bool direct_cleanup_pending;
    _Bool direct_cleanup_scheduled;
    _Bool socket_sources_cancelled;
    /* Socket readiness sources, index 0 for input and 1 for output. */
    dispatch_source_t socket_sources[2];
    RRuntimeDarwinIoSourceState socket_source_states[2];
    RRuntimeDarwinIoRequest *socket_waiting[2];
    /* RANDOM offsets of a FILE handle are relative to the descriptor position when the handle
       took the descriptor, as for a Dispatch I/O RANDOM channel. */
    off_t direct_random_base;
};

struct RRuntimeDarwinIoRequest {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    size_t references;
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoRequest *next;
    RRuntimeDarwinIoOperation operation;
    RRuntimeDarwinIoRequestState state;
    RRuntimeDarwinIoTerminalEvent pending_event;
    RRuntimeDarwinIoTerminalEvent terminal_event;
    RRuntimeDarwinIoBuffer buffer;
    dispatch_io_t operation_channel;
    dispatch_source_t deadline_timer;
    dispatch_data_t prepared_write_data;
    uint64_t submission_sequence;
    size_t requested_size;
    size_t bytes_transferred;
    size_t bytes_remaining;
    RRuntimeDarwinIoCompletionFn completion;
    void *completion_context;
    int operation_error;
    int operation_native_error;
    int cleanup_error;
    uint64_t pending_event_sequence;
    uint64_t terminal_event_sequence;
    uint64_t native_event_sequence;
    _Bool operation_done;
    _Bool cleanup_done;
    _Bool channel_released;
    _Bool deadline_activated;
    _Bool native_scheduled;
    _Bool stream_position_enabled;
    _Bool stream_position_barrier_pending;
    _Bool eof;
    _Bool partial;
    _Bool completion_scheduled;
    _Bool complete_after_progress;
    _Bool progress_stop_requested;
    _Bool buffer_transfer_required;
    _Bool close_completion_owned;
    _Bool close_completion_reported;
    _Bool close_deadline_enabled;
    RRuntimeDarwinIoShutdownDirection shutdown_direction;
    RRuntimeDarwinIoShutdownCommitFn shutdown_commit;
    void *shutdown_context;
    RRuntimeDarwinIoShutdownEntryFn shutdown_entry;
    void *shutdown_entry_context;
    _Bool shutdown_committed;
    /* Event sequence of a direct native completion, taken when its system call returned. */
    uint64_t direct_native_sequence;
    /* A FILE request beyond the admission bound waits in the process-wide admission FIFO of
       io_direct.c; both fields are guarded by that FIFO's mutex. */
    RRuntimeDarwinIoRequest *direct_admission_next;
    _Bool direct_admission_waiting;
    const unsigned char *prepared_buffer_data;
    RRuntimeAllocator *prepared_buffer_allocator;
    size_t prepared_buffer_capacity;
    size_t prepared_buffer_size;
    off_t prepared_offset;
    off_t prepared_stream_position;
};

RRuntimeDarwinIoSubmitResult
r_runtime_darwin_io_internal_submit_failure(RRuntimeDarwinIoStartStatus status, int native_error);
RRuntimeDarwinIoSubmitResult
r_runtime_darwin_io_internal_submit_success(RRuntimeDarwinIoRequest *request);

RRuntimeDarwinIoRequest *
r_runtime_darwin_io_internal_request_create_locked(RRuntimeDarwinIoHandle *handle,
                                                   RRuntimeDarwinIoOperation operation,
                                                   RRuntimeDarwinIoBuffer *buffer,
                                                   RRuntimeDarwinIoStartStatus *status,
                                                   int *native_error);
void r_runtime_darwin_io_internal_request_retain(RRuntimeDarwinIoRequest *request);
void r_runtime_darwin_io_internal_request_release(RRuntimeDarwinIoRequest *request);
void r_runtime_darwin_io_internal_request_abort_start(RRuntimeDarwinIoRequest *request,
                                                      RRuntimeDarwinIoBuffer *buffer);
void r_runtime_darwin_io_internal_request_complete_without_submission(
    RRuntimeDarwinIoRequest *request);
_Bool r_runtime_darwin_io_internal_request_cancel_for_close(RRuntimeDarwinIoRequest *request);
_Bool r_runtime_darwin_io_internal_activate_read(RRuntimeDarwinIoRequest *request);
_Bool r_runtime_darwin_io_internal_activate_write(RRuntimeDarwinIoRequest *request);
_Bool r_runtime_darwin_io_internal_activate_flush(RRuntimeDarwinIoRequest *request);
_Bool r_runtime_darwin_io_internal_activate_shutdown(RRuntimeDarwinIoRequest *request);
void r_runtime_darwin_io_internal_read_stream_position_barrier_done(
    RRuntimeDarwinIoRequest *request, int native_error);
void r_runtime_darwin_io_internal_write_stream_position_barrier_done(
    RRuntimeDarwinIoRequest *request, int native_error);

void r_runtime_darwin_io_internal_handle_release_reference(RRuntimeDarwinIoHandle *handle);
void r_runtime_darwin_io_internal_handle_release_request(RRuntimeDarwinIoHandle *handle);
RRuntimeDarwinIoHandleCreateResult r_runtime_darwin_io_internal_handle_create_runtime_root(
    RRuntimeAllocator *allocator, int descriptor, RRuntimeDarwinIoType type);
RRuntimeDarwinIoHandleCreateResult
r_runtime_darwin_io_internal_handle_prepare_borrowed_random(RRuntimeAllocator *allocator);
RRuntimeDarwinIoStartStatus
r_runtime_darwin_io_internal_handle_bind_borrowed_random(RRuntimeDarwinIoHandle *handle,
                                                         int descriptor,
                                                         RRuntimeDarwinIoHandleCleanupFn cleanup,
                                                         void *context,
                                                         int *native_error);
void r_runtime_darwin_io_internal_handle_release_borrowed_root(RRuntimeDarwinIoHandle *handle);
void r_runtime_darwin_io_internal_handle_release_runtime_root_and_wait(
    RRuntimeDarwinIoHandle *handle, _Bool stop_pending);
void r_runtime_darwin_io_internal_handle_stop_accepting(RRuntimeDarwinIoHandle *handle);
void r_runtime_darwin_io_internal_handle_wait_for_requests(RRuntimeDarwinIoHandle *handle);
void r_runtime_darwin_io_internal_handle_notify_request_terminal(RRuntimeDarwinIoHandle *handle);
void r_runtime_darwin_io_internal_handle_root_cleanup(RRuntimeDarwinIoHandle *handle,
                                                      int native_error);
_Bool r_runtime_darwin_io_internal_handle_has_active_request_locked(RRuntimeDarwinIoHandle *handle);
void r_runtime_darwin_io_internal_schedule_ready_requests(RRuntimeDarwinIoHandle *handle);
void r_runtime_darwin_io_internal_try_complete_console_closes(RRuntimeDarwinIoHandle *handle);
void r_runtime_darwin_io_internal_cancel_all_requests(RRuntimeDarwinIoHandle *handle);

dispatch_io_t
r_runtime_darwin_io_internal_create_operation_channel(RRuntimeDarwinIoRequest *request);
void r_runtime_darwin_io_internal_schedule_stream_position_barrier(
    RRuntimeDarwinIoRequest *request);
_Bool r_runtime_darwin_io_internal_prepare_deadline(RRuntimeDarwinIoRequest *request,
                                                    uint64_t timeout_nanoseconds);
void r_runtime_darwin_io_internal_activate_deadline(RRuntimeDarwinIoRequest *request);
void r_runtime_darwin_io_internal_disarm_deadline(RRuntimeDarwinIoRequest *request);
void r_runtime_darwin_io_internal_close_operation_channel(RRuntimeDarwinIoRequest *request,
                                                          dispatch_io_close_flags_t flags);

/* Caller holds request->mutex; operation_done and cleanup_done are already recorded. */
_Bool r_runtime_darwin_io_internal_request_finalize_locked(RRuntimeDarwinIoRequest *request);
void r_runtime_darwin_io_internal_progress(RRuntimeDarwinIoRequest *request,
                                           size_t bytes_transferred,
                                           size_t bytes_remaining);
void r_runtime_darwin_io_internal_operation_done(RRuntimeDarwinIoRequest *request,
                                                 size_t bytes_transferred,
                                                 size_t bytes_remaining,
                                                 int native_error,
                                                 _Bool eof);
void r_runtime_darwin_io_internal_close_done(RRuntimeDarwinIoRequest *request, int native_error);
void r_runtime_darwin_io_internal_schedule_completion(RRuntimeDarwinIoRequest *request);

RRuntimeDarwinIoPrepareResult
r_runtime_darwin_io_internal_prepare_failure(RRuntimeDarwinIoStartStatus status, int native_error);
RRuntimeDarwinIoPrepareResult
r_runtime_darwin_io_internal_prepare_success(RRuntimeDarwinIoPreparedRequest *prepared);
_Bool r_runtime_darwin_io_internal_testing_should_fail_prepare(
    RRuntimeDarwinIoPrepareFailureStage stage);

/* Direct engines (io_direct.c). activate runs with handle->mutex held and never finalizes: a
   request whose transfer is already over is returned in *finished and finished by
   finish_activated after the caller releases the mutex. */
_Bool r_runtime_darwin_io_internal_direct_activate(RRuntimeDarwinIoRequest *request,
                                                   RRuntimeDarwinIoRequest **finished);
void r_runtime_darwin_io_internal_direct_finish_activated(RRuntimeDarwinIoRequest *request);
/* Called after request_signal recorded a cancellation or deadline for a scheduled direct
   request; the caller holds a request reference. */
void r_runtime_darwin_io_internal_direct_signal(RRuntimeDarwinIoRequest *request);
/* Creates the socket readiness sources of a new SOCKET handle; the handle is not shared yet. */
_Bool r_runtime_darwin_io_internal_direct_create_sources(RRuntimeDarwinIoHandle *handle);
/* The root of a direct handle has been released: cancels its sources and runs root cleanup on
   the callback queue once no direct activity remains. */
void r_runtime_darwin_io_internal_direct_release_root(RRuntimeDarwinIoHandle *handle);
/* Runs completion now when the request is terminal and its completion is registered but not
   yet scheduled; returns whether it ran. */
_Bool r_runtime_darwin_io_internal_deliver_completion(RRuntimeDarwinIoRequest *request);
/* The stream-position barrier pause point, shared with the FILE engine's worker entry. */
void r_runtime_darwin_io_internal_testing_pause_position_entry(void);
/* Testing builds: whether a pause hook that an immediate socket attempt would bypass is armed;
   always false otherwise. */
_Bool r_runtime_darwin_io_internal_testing_read_pause_armed(void);
_Bool r_runtime_darwin_io_internal_testing_write_pause_armed(void);
_Bool r_runtime_darwin_io_internal_testing_native_completion_pause_armed(void);
void r_runtime_darwin_io_internal_testing_pause_read_after_progress(
    RRuntimeDarwinIoRequest *request);
void r_runtime_darwin_io_internal_testing_pause_write_after_progress(
    RRuntimeDarwinIoRequest *request);
void r_runtime_darwin_io_internal_testing_pause_native_completion(void);

#if defined(R_RUNTIME_DARWIN_IO_TESTING)
/* A one-shot test pause point: after arm, the next thread that passes the point stops there
   until the test releases it; release returns once that thread has left. Unarmed points
   cost one atomic load. */
typedef struct RRuntimeDarwinIoTestPause {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    _Atomic _Bool hint;
    _Bool armed;
    _Bool reached;
    _Bool released;
} RRuntimeDarwinIoTestPause;

#define R_RUNTIME_DARWIN_IO_TEST_PAUSE_INITIALIZER                                               \
    { PTHREAD_MUTEX_INITIALIZER, PTHREAD_COND_INITIALIZER, 0, 0, 0, 0 }

void r_runtime_darwin_io_internal_test_pause_arm(RRuntimeDarwinIoTestPause *pause);
void r_runtime_darwin_io_internal_test_pause_wait(RRuntimeDarwinIoTestPause *pause);
void r_runtime_darwin_io_internal_test_pause_release(RRuntimeDarwinIoTestPause *pause);
void r_runtime_darwin_io_internal_test_pause_point(RRuntimeDarwinIoTestPause *pause);
#endif

#endif
