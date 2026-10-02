#ifndef R_RUNTIME_DARWIN_IO_H
#define R_RUNTIME_DARWIN_IO_H

#include "r_runtime_allocator.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef struct RRuntimeDarwinIoHandle RRuntimeDarwinIoHandle;
typedef struct RRuntimeDarwinIoRequest RRuntimeDarwinIoRequest;
typedef RRuntimeDarwinIoRequest RRuntimeDarwinIoPreparedRequest;

/* The request borrow is valid only for the duration of the completion callback. */
typedef void (*RRuntimeDarwinIoCompletionFn)(RRuntimeDarwinIoRequest *request, void *context);
/* The context borrow is valid only for the duration of the root-cleanup callback. */
typedef void (*RRuntimeDarwinIoHandleCleanupFn)(void *context, int native_error);

typedef enum RRuntimeDarwinIoType {
    R_RUNTIME_DARWIN_IO_STREAM = 1,
    R_RUNTIME_DARWIN_IO_RANDOM
} RRuntimeDarwinIoType;

typedef enum RRuntimeDarwinIoOperation {
    R_RUNTIME_DARWIN_IO_READ = 1,
    R_RUNTIME_DARWIN_IO_WRITE,
    R_RUNTIME_DARWIN_IO_FLUSH,
    R_RUNTIME_DARWIN_IO_CLOSE,
    R_RUNTIME_DARWIN_IO_SHUTDOWN
} RRuntimeDarwinIoOperation;

typedef enum RRuntimeDarwinIoShutdownDirection {
    R_RUNTIME_DARWIN_IO_SHUTDOWN_INPUT = 1,
    R_RUNTIME_DARWIN_IO_SHUTDOWN_OUTPUT = 2,
    R_RUNTIME_DARWIN_IO_SHUTDOWN_BOTH = 3
} RRuntimeDarwinIoShutdownDirection;

/* Runs once after a requested half-close is published and before later requests may progress. */
typedef void (*RRuntimeDarwinIoShutdownCommitFn)(void *context,
                                                 RRuntimeDarwinIoShutdownDirection direction);

/* Runs once on the shutdown worker after earlier requests finished and before the native
   half-close is entered; false completes the request as cancelled without entering it. */
typedef _Bool (*RRuntimeDarwinIoShutdownEntryFn)(void *context);

typedef enum RRuntimeDarwinIoRequestState {
    R_RUNTIME_DARWIN_IO_REQUEST_PREPARED = 1,
    R_RUNTIME_DARWIN_IO_REQUEST_ACTIVE,
    R_RUNTIME_DARWIN_IO_REQUEST_TERMINAL
} RRuntimeDarwinIoRequestState;

typedef enum RRuntimeDarwinIoTerminalEvent {
    R_RUNTIME_DARWIN_IO_TERMINAL_NATIVE = 1,
    R_RUNTIME_DARWIN_IO_TERMINAL_CANCELLED,
    R_RUNTIME_DARWIN_IO_TERMINAL_TIMED_OUT
} RRuntimeDarwinIoTerminalEvent;

typedef enum RRuntimeDarwinIoStartStatus {
    R_RUNTIME_DARWIN_IO_START_OK = 0,
    R_RUNTIME_DARWIN_IO_START_INVALID_ARGUMENT,
    R_RUNTIME_DARWIN_IO_START_ALLOCATION_FAILED,
    R_RUNTIME_DARWIN_IO_START_NATIVE_RETAIN_FAILED,
    R_RUNTIME_DARWIN_IO_START_HANDLE_CLOSED,
    R_RUNTIME_DARWIN_IO_START_ORDERING_BLOCKED
} RRuntimeDarwinIoStartStatus;

typedef enum RRuntimeDarwinIoPrepareFailureStage {
    R_RUNTIME_DARWIN_IO_PREPARE_FAIL_NONE = 0,
    R_RUNTIME_DARWIN_IO_PREPARE_FAIL_REQUEST,
    R_RUNTIME_DARWIN_IO_PREPARE_FAIL_DEADLINE,
    R_RUNTIME_DARWIN_IO_PREPARE_FAIL_CHANNEL,
    R_RUNTIME_DARWIN_IO_PREPARE_FAIL_WRITE_DATA
} RRuntimeDarwinIoPrepareFailureStage;

#if defined(R_RUNTIME_DARWIN_IO_TESTING)
typedef enum RRuntimeDarwinIoHandleCreateFailureStage {
    R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_NONE = 0,
    R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_ALLOCATION,
    R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_AFTER_DUPLICATE,
    R_RUNTIME_DARWIN_IO_HANDLE_CREATE_FAIL_AFTER_QUEUE
} RRuntimeDarwinIoHandleCreateFailureStage;
#endif

typedef struct RRuntimeDarwinIoBuffer {
    RRuntimeAllocator *allocator;
    unsigned char *data;
    size_t capacity;
    size_t size;
} RRuntimeDarwinIoBuffer;

typedef struct RRuntimeDarwinIoBufferResult {
    RRuntimeDarwinIoBuffer buffer;
    RRuntimeDarwinIoStartStatus status;
} RRuntimeDarwinIoBufferResult;

typedef struct RRuntimeDarwinIoHandleCreateResult {
    RRuntimeDarwinIoHandle *handle;
    RRuntimeDarwinIoStartStatus status;
    int native_error;
} RRuntimeDarwinIoHandleCreateResult;

typedef struct RRuntimeDarwinIoConsoleStartResult {
    RRuntimeDarwinIoStartStatus status;
    int native_error;
} RRuntimeDarwinIoConsoleStartResult;

typedef struct RRuntimeDarwinIoSubmitResult {
    RRuntimeDarwinIoRequest *request;
    RRuntimeDarwinIoStartStatus status;
    int native_error;
} RRuntimeDarwinIoSubmitResult;

typedef struct RRuntimeDarwinIoPrepareResult {
    RRuntimeDarwinIoPreparedRequest *prepared;
    RRuntimeDarwinIoStartStatus status;
    int native_error;
} RRuntimeDarwinIoPrepareResult;

typedef struct RRuntimeDarwinIoResult {
    RRuntimeDarwinIoOperation operation;
    RRuntimeDarwinIoTerminalEvent terminal_event;
    int native_error;
    int operation_native_error;
    uint64_t terminal_event_sequence;
    uint64_t native_event_sequence;
    size_t bytes_transferred;
    size_t bytes_remaining;
    _Bool eof;
    _Bool partial;
    _Bool cleanup_acknowledged;
} RRuntimeDarwinIoResult;

/* Ownership: allocator must outlive the returned buffer and every value that owns it. */
RRuntimeDarwinIoBufferResult r_runtime_darwin_io_buffer_allocate(RRuntimeAllocator *allocator,
                                                                 size_t capacity);

/* Ownership: consumes buffer and resets it to the empty state. */
void r_runtime_darwin_io_buffer_release(RRuntimeDarwinIoBuffer *buffer);

/*
 * Ownership: descriptor is call-bounded and duplicated before success is returned. The original
 * descriptor remains owned by the caller. The allocator must outlive the handle and its requests.
 */
RRuntimeDarwinIoHandleCreateResult r_runtime_darwin_io_handle_create(RRuntimeAllocator *allocator,
                                                                     int descriptor,
                                                                     RRuntimeDarwinIoType type);

/*
 * Ownership: retain_view creates one allocation-free independent R view. handle_release consumes
 * one such view. Active requests hold independent retains in a separate overflow-checked counter
 * and continue through native cleanup acknowledgement. A non-console identity closes after its
 * last view and request retain drain.
 */
_Bool r_runtime_darwin_io_handle_retain_view(RRuntimeDarwinIoHandle *handle);
void r_runtime_darwin_io_handle_release(RRuntimeDarwinIoHandle *handle);

/*
 * Ownership: consumes one view and installs the handle's sole allocation-free cleanup observer.
 * The observer runs exactly once after the root cleanup handler has closed the retained descriptor.
 * Failure leaves the view unchanged and installs no observer.
 */
_Bool r_runtime_darwin_io_handle_release_with_cleanup(RRuntimeDarwinIoHandle *handle,
                                                      RRuntimeDarwinIoHandleCleanupFn cleanup,
                                                      void *context);

/* Snapshot used before the close deadline check; true means a terminal native failure is known. */
_Bool r_runtime_darwin_io_handle_terminal_close_failure(RRuntimeDarwinIoHandle *handle,
                                                        int *native_error);

/*
 * Hosted process-console lifecycle. Start creates runtime-owned roots for duplicated descriptors
 * 0, 1 and 2. Each view operation is allocation-free and returns one independently retained R
 * view. Stop rejects new submissions, drains output and flush barriers, cancels input, releases
 * all roots, and waits for native cleanup acknowledgement.
 */
RRuntimeDarwinIoConsoleStartResult
r_runtime_darwin_io_process_console_start(RRuntimeAllocator *allocator);
void r_runtime_darwin_io_process_console_stop(void);
RRuntimeDarwinIoHandle *r_runtime_darwin_io_process_stdin_retain(void);
RRuntimeDarwinIoHandle *r_runtime_darwin_io_process_stdout_retain(void);
RRuntimeDarwinIoHandle *r_runtime_darwin_io_process_stderr_retain(void);

/*
 * Two-phase native submission. Prepare acquires every fallible request/native object, reserves its
 * per-direction submission position, and takes one independent handle retain without starting
 * payload I/O or consuming buffer. For writes, the suspended dispatch_data object borrows the
 * staged bytes only until activate/abort returns; it never owns, mutates, or releases caller
 * storage. Activate is allocation-free, consumes both reservation and buffer after the language
 * task Move commit, and queues behind earlier same-direction reservations. Abort consumes only the
 * reservation and releases the next committed request to progress.
 */
RRuntimeDarwinIoPrepareResult r_runtime_darwin_io_prepare_read(RRuntimeDarwinIoHandle *handle,
                                                               off_t offset,
                                                               const RRuntimeDarwinIoBuffer *buffer,
                                                               uint64_t timeout_nanoseconds);
RRuntimeDarwinIoPrepareResult
r_runtime_darwin_io_prepare_read_some(RRuntimeDarwinIoHandle *handle,
                                      off_t offset,
                                      const RRuntimeDarwinIoBuffer *buffer,
                                      uint64_t timeout_nanoseconds);
RRuntimeDarwinIoPrepareResult
r_runtime_darwin_io_prepare_write(RRuntimeDarwinIoHandle *handle,
                                  off_t offset,
                                  const RRuntimeDarwinIoBuffer *buffer,
                                  uint64_t timeout_nanoseconds);
RRuntimeDarwinIoPrepareResult
r_runtime_darwin_io_prepare_write_some(RRuntimeDarwinIoHandle *handle,
                                       off_t offset,
                                       const RRuntimeDarwinIoBuffer *buffer,
                                       uint64_t timeout_nanoseconds);

/*
 * Shared-write reservation borrows immutable bytes through activate/abort. After activate, the
 * caller shall retain the backing shared owner through terminal acknowledgement. No buffer owner
 * is transferred to the request and request_take_buffer returns empty.
 */
RRuntimeDarwinIoPrepareResult
r_runtime_darwin_io_prepare_shared_write(RRuntimeDarwinIoHandle *handle,
                                         off_t offset,
                                         const unsigned char *data,
                                         size_t size,
                                         uint64_t timeout_nanoseconds);

/*
 * Precommit reservation for one RANDOM shared write whose descriptor is not available yet.
 * Prepare allocates the private handle and request storage, their synchronization primitives,
 * callback queue, optional deadline, Dispatch data and ordering state without borrowing an fd.
 * The immutable data borrow lasts through activate/abort and, after activation, through terminal
 * acknowledgement.
 */
RRuntimeDarwinIoPrepareResult
r_runtime_darwin_io_prepare_borrowed_random_shared_write(RRuntimeAllocator *allocator,
                                                         off_t offset,
                                                         const unsigned char *data,
                                                         size_t size,
                                                         uint64_t timeout_nanoseconds);

/*
 * Allocation-free with respect to allocator. A valid writable descriptor is borrowed without
 * duplication or ownership transfer while fixed RANDOM root and operation channels are bound.
 * Success consumes *prepared and returns that same request object. Terminal acknowledgement
 * releases the operation channel; release of the returned request releases the private root after
 * any registered request completion callback returns. cleanup then runs exactly once when Dispatch
 * no longer borrows descriptor, so it cannot precede that completion callback. Without a request
 * completion callback, cleanup follows terminal acknowledgement and request release. The caller
 * shall neither modify nor close descriptor until cleanup and still owns and closes it afterward.
 * Validation or
 * root-channel failure leaves *prepared unchanged and does not borrow descriptor. A later
 * channel/activation failure consumes *prepared and still uses cleanup to acknowledge the borrowed
 * descriptor. Generic prepared_abort consumes an unbound reservation and invokes no cleanup
 * callback.
 */
RRuntimeDarwinIoSubmitResult
r_runtime_darwin_io_prepared_bind_activate_borrowed_random_shared_write(
    RRuntimeDarwinIoPreparedRequest **prepared,
    int descriptor,
    RRuntimeDarwinIoHandleCleanupFn cleanup,
    void *context);
RRuntimeDarwinIoPrepareResult r_runtime_darwin_io_prepare_flush(RRuntimeDarwinIoHandle *handle,
                                                                uint64_t timeout_nanoseconds);
RRuntimeDarwinIoPrepareResult r_runtime_darwin_io_prepare_close(RRuntimeDarwinIoHandle *handle,
                                                                uint64_t timeout_nanoseconds);
/*
 * Reserves a STREAM half-close in the selected direction FIFO. The commit callback and context
 * remain borrowed through activation or abort and, after activation, through terminal native
 * acknowledgement. Repeated directions complete successfully without repeating shutdown(2).
 */
RRuntimeDarwinIoPrepareResult
r_runtime_darwin_io_prepare_shutdown(RRuntimeDarwinIoHandle *handle,
                                     RRuntimeDarwinIoShutdownDirection direction,
                                     uint64_t timeout_nanoseconds,
                                     RRuntimeDarwinIoShutdownCommitFn commit,
                                     void *context);

/*
 * Allocation-free late binding of a prepared shutdown's entry gate. The owner of an external task
 * uses it to linearize the half-close against a cancellation selected before the worker enters
 * shutdown(2), whose asynchronous delivery may reach the request only after entry. The caller
 * shall be the sole owner of prepared; the context stays borrowed like the commit context.
 */
_Bool r_runtime_darwin_io_prepared_set_shutdown_entry(RRuntimeDarwinIoPreparedRequest *prepared,
                                                      RRuntimeDarwinIoShutdownEntryFn entry,
                                                      void *context);

RRuntimeDarwinIoSubmitResult
r_runtime_darwin_io_prepared_activate(RRuntimeDarwinIoPreparedRequest **prepared,
                                      RRuntimeDarwinIoBuffer *buffer);

/*
 * Allocation-free late binding for a prepared RANDOM read or write. relative_offset must be
 * nonnegative. The caller shall be the sole owner of prepared and externally serialize this call
 * with activate or abort. Success changes only the future Dispatch I/O offset; it neither submits
 * nor transfers ownership.
 */
_Bool r_runtime_darwin_io_prepared_set_offset(RRuntimeDarwinIoPreparedRequest *prepared,
                                              off_t relative_offset);

/*
 * Allocation-free late binding for a prepared STREAM read or write. absolute_position must be
 * nonnegative. Activation applies the position with lseek inside a barrier on the owning Dispatch
 * I/O channel and submits payload I/O only after that barrier succeeds. Callers shall serialize a
 * positioned request against every read or write that shares its handle until terminal
 * acknowledgement.
 */
_Bool r_runtime_darwin_io_prepared_set_stream_position(RRuntimeDarwinIoPreparedRequest *prepared,
                                                       off_t absolute_position);

/*
 * Close activation consumes only the native reservation. deadline_expired records an already
 * elapsed operation deadline but never skips cancellation, acknowledgement, or view release.
 * The caller still owns and must release the consumed R view after terminal acknowledgement.
 */
RRuntimeDarwinIoSubmitResult
r_runtime_darwin_io_prepared_activate_close(RRuntimeDarwinIoPreparedRequest **prepared,
                                            _Bool deadline_expired);
void r_runtime_darwin_io_prepared_abort(RRuntimeDarwinIoPreparedRequest **prepared);

/*
 * Ownership: a successful start consumes buffer and resets the caller's value. A failed start
 * leaves buffer byte-for-byte unchanged. timeout_nanoseconds == 0 means no deadline.
 */
RRuntimeDarwinIoSubmitResult r_runtime_darwin_io_submit_read(RRuntimeDarwinIoHandle *handle,
                                                             off_t offset,
                                                             RRuntimeDarwinIoBuffer *buffer,
                                                             uint64_t timeout_nanoseconds);
RRuntimeDarwinIoSubmitResult r_runtime_darwin_io_submit_read_some(RRuntimeDarwinIoHandle *handle,
                                                                  off_t offset,
                                                                  RRuntimeDarwinIoBuffer *buffer,
                                                                  uint64_t timeout_nanoseconds);
RRuntimeDarwinIoSubmitResult r_runtime_darwin_io_submit_write(RRuntimeDarwinIoHandle *handle,
                                                              off_t offset,
                                                              RRuntimeDarwinIoBuffer *buffer,
                                                              uint64_t timeout_nanoseconds);
RRuntimeDarwinIoSubmitResult r_runtime_darwin_io_submit_write_some(RRuntimeDarwinIoHandle *handle,
                                                                   off_t offset,
                                                                   RRuntimeDarwinIoBuffer *buffer,
                                                                   uint64_t timeout_nanoseconds);

/*
 * Flush is a Dispatch I/O ordering barrier; filesystem durability remains in the fs lane. Writes
 * and flushes sharing one handle are serviced in reserved submission order without rejecting a
 * later valid operation. Close cancels only operations reserved earlier than its barrier sequence.
 */
RRuntimeDarwinIoSubmitResult r_runtime_darwin_io_submit_flush(RRuntimeDarwinIoHandle *handle,
                                                              uint64_t timeout_nanoseconds);

/*
 * Compatibility one-phase close. It prepares and activates a close with no deadline. Close
 * cancels earlier requests and completes only after their native acknowledgement. It does not
 * consume the caller's R view; the caller releases that view separately.
 */
RRuntimeDarwinIoSubmitResult r_runtime_darwin_io_submit_close(RRuntimeDarwinIoHandle *handle);

/* The first accepted cancellation/deadline event selects the candidate terminal outcome. */
_Bool r_runtime_darwin_io_request_cancel(RRuntimeDarwinIoRequest *request);
_Bool r_runtime_darwin_io_request_deadline_expired(RRuntimeDarwinIoRequest *request);

RRuntimeDarwinIoRequestState r_runtime_darwin_io_request_state(RRuntimeDarwinIoRequest *request);
size_t r_runtime_darwin_io_request_progress(RRuntimeDarwinIoRequest *request);
RRuntimeDarwinIoResult r_runtime_darwin_io_request_wait(RRuntimeDarwinIoRequest *request);

/*
 * Registers the sole non-blocking terminal observer. The callback runs exactly once on the
 * handle's serial Dispatch queue after native cleanup acknowledgement. Registration remains valid
 * when the request became terminal before this call. The request and context are retained by their
 * respective owners through callback return; no allocation is performed.
 */
_Bool r_runtime_darwin_io_request_set_completion(RRuntimeDarwinIoRequest *request,
                                                 RRuntimeDarwinIoCompletionFn completion,
                                                 void *context);

/* Ownership: transfers the request buffer once after terminal completion. */
RRuntimeDarwinIoBuffer r_runtime_darwin_io_request_take_buffer(RRuntimeDarwinIoRequest *request);

/* Ownership: consumes one request owner; an active request is first cancelled. */
void r_runtime_darwin_io_request_release(RRuntimeDarwinIoRequest *request);

#if defined(R_RUNTIME_DARWIN_IO_TESTING)
void r_runtime_darwin_io_testing_fail_prepare_stage(RRuntimeDarwinIoPrepareFailureStage stage);
void r_runtime_darwin_io_testing_force_root_cleanup_error(int native_error);
void r_runtime_darwin_io_testing_force_handle_root_cleanup_error(RRuntimeDarwinIoHandle *handle,
                                                                 int native_error);
void r_runtime_darwin_io_testing_pause_next_root_cleanup_before_report(void);
void r_runtime_darwin_io_testing_wait_for_root_cleanup_before_report(void);
void r_runtime_darwin_io_testing_release_root_cleanup_before_report(void);
void r_runtime_darwin_io_testing_fail_handle_create_stage(
    RRuntimeDarwinIoHandleCreateFailureStage stage);
/* Makes the next handle-create native retain fail with the exact supplied status. */
void r_runtime_darwin_io_testing_fail_next_handle_create_native(int native_error);
void r_runtime_darwin_io_request_testing_wait_for_progress(RRuntimeDarwinIoRequest *request,
                                                           size_t minimum_progress);
/* Pauses the next positioned STREAM barrier until the matching release call. */
void r_runtime_darwin_io_testing_pause_next_stream_position_barrier(void);
/* Waits until the armed positioned STREAM barrier has entered its test pause. */
void r_runtime_darwin_io_testing_wait_for_stream_position_barrier(void);
/* Releases the positioned STREAM barrier observed by the preceding wait call. */
void r_runtime_darwin_io_testing_release_stream_position_barrier(void);
/* Pauses the next native request completion after its event sequence is captured. */
void r_runtime_darwin_io_testing_pause_next_native_completion(void);
/* Waits until the armed native request completion has entered its test pause. */
void r_runtime_darwin_io_testing_wait_for_native_completion(void);
/* Releases the native request completion observed by the preceding wait call. */
void r_runtime_darwin_io_testing_release_native_completion(void);
/* Pauses the next READ_SOME callback after bytes are copied but before terminal selection. */
void r_runtime_darwin_io_testing_pause_next_read_after_progress(void);
/* Waits until the armed READ_SOME callback reaches the post-copy pause. */
void r_runtime_darwin_io_testing_wait_for_read_after_progress(void);
/* Records a cancellation event for the currently paused READ_SOME request. */
_Bool r_runtime_darwin_io_testing_cancel_read_after_progress(void);
/* Records a deadline event for the currently paused READ_SOME request. */
_Bool r_runtime_darwin_io_testing_expire_read_after_progress(void);
/* Releases the READ_SOME callback observed by the preceding wait call. */
void r_runtime_darwin_io_testing_release_read_after_progress(void);
/* Pauses the next write callback after positive progress but before terminal selection. */
void r_runtime_darwin_io_testing_pause_next_write_after_progress(void);
/* Waits until the armed write callback reaches the post-progress pause. */
void r_runtime_darwin_io_testing_wait_for_write_after_progress(void);
/* Records a cancellation event for the currently paused write request. */
_Bool r_runtime_darwin_io_testing_cancel_write_after_progress(void);
/* Records a deadline event for the currently paused write request. */
_Bool r_runtime_darwin_io_testing_expire_write_after_progress(void);
/* Releases the write callback observed by the preceding wait call. */
void r_runtime_darwin_io_testing_release_write_after_progress(void);
/* Makes the next native shutdown attempt fail without publishing a half-close. */
void r_runtime_darwin_io_testing_fail_next_shutdown_native(int native_error);
/* Pauses the next shutdown worker after its pending-event check and before its entry gate. */
void r_runtime_darwin_io_testing_pause_next_shutdown_entry(void);
/* Waits until the armed shutdown worker has entered its test pause. */
void r_runtime_darwin_io_testing_wait_for_shutdown_entry(void);
/* Releases the shutdown worker observed by the preceding wait call. */
void r_runtime_darwin_io_testing_release_shutdown_entry(void);
#endif

#endif
