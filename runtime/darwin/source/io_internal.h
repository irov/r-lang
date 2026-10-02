#ifndef R_RUNTIME_DARWIN_IO_INTERNAL_H
#define R_RUNTIME_DARWIN_IO_INTERNAL_H

#include "r_runtime_darwin_io.h"

#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

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

#endif
