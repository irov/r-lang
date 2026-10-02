#ifndef R_RUNTIME_DARWIN_FS_LANE_H
#define R_RUNTIME_DARWIN_FS_LANE_H

#include "r_runtime_allocator.h"

#include <stddef.h>
#include <stdint.h>
#include <sys/attr.h>
#include <sys/stat.h>
#include <sys/types.h>

typedef struct RRuntimeDarwinFsLane RRuntimeDarwinFsLane;
typedef struct RRuntimeDarwinFsRequest RRuntimeDarwinFsRequest;
typedef RRuntimeDarwinFsRequest RRuntimeDarwinFsPreparedRequest;

typedef void (*RRuntimeDarwinFsCompletionFn)(RRuntimeDarwinFsRequest *request, void *context);

#define R_RUNTIME_DARWIN_FS_MAX_PENDING_REQUESTS ((size_t)65536U)
#define R_RUNTIME_DARWIN_FS_PATH_SCAN_LIMIT ((size_t)(1024U * 1024U))
#define R_RUNTIME_DARWIN_FS_CREATE_RETRY_LIMIT 16U
#define R_RUNTIME_DARWIN_FS_STAGING_PREFIX ".r-dir-stage-"
#define R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY ((size_t)30U)
#define R_RUNTIME_DARWIN_FS_STAGING_PAYLOAD_NAME "payload"
#define R_RUNTIME_DARWIN_FS_STAGING_SOURCE_CAPACITY                                                \
    (R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY + sizeof(R_RUNTIME_DARWIN_FS_STAGING_PAYLOAD_NAME))

typedef enum RRuntimeDarwinFsOperation {
    R_RUNTIME_DARWIN_FS_OPEN_AT = 1,
    R_RUNTIME_DARWIN_FS_CLOSE,
    R_RUNTIME_DARWIN_FS_FSTAT,
    R_RUNTIME_DARWIN_FS_FSTAT_AT,
    R_RUNTIME_DARWIN_FS_SEEK,
    R_RUNTIME_DARWIN_FS_ENUMERATE,
    R_RUNTIME_DARWIN_FS_MKDIR_AT,
    R_RUNTIME_DARWIN_FS_UNLINK_AT,
    R_RUNTIME_DARWIN_FS_RENAME_AT_NO_REPLACE,
    R_RUNTIME_DARWIN_FS_FSYNC,
    R_RUNTIME_DARWIN_FS_FULL_FSYNC,
    R_RUNTIME_DARWIN_FS_FILE_STAGE_CREATE,
    R_RUNTIME_DARWIN_FS_FILE_STAGE_CLEANUP,
    R_RUNTIME_DARWIN_FS_FULL_FSYNC_DIRECTORY_AT,
    R_RUNTIME_DARWIN_FS_BARRIER_FSYNC,
    R_RUNTIME_DARWIN_FS_OFD_LOCK
} RRuntimeDarwinFsOperation;

typedef enum RRuntimeDarwinFsRequestState {
    R_RUNTIME_DARWIN_FS_REQUEST_PREPARED = 1,
    R_RUNTIME_DARWIN_FS_REQUEST_QUEUED,
    R_RUNTIME_DARWIN_FS_REQUEST_ENTERED,
    R_RUNTIME_DARWIN_FS_REQUEST_TERMINAL
} RRuntimeDarwinFsRequestState;

typedef enum RRuntimeDarwinFsTerminalEvent {
    R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE = 1,
    R_RUNTIME_DARWIN_FS_TERMINAL_CANCELLED,
    R_RUNTIME_DARWIN_FS_TERMINAL_TIMED_OUT,
    R_RUNTIME_DARWIN_FS_TERMINAL_STOPPING
} RRuntimeDarwinFsTerminalEvent;

typedef enum RRuntimeDarwinFsStartStatus {
    R_RUNTIME_DARWIN_FS_START_OK = 0,
    R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT,
    R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED,
    R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED,
    R_RUNTIME_DARWIN_FS_START_QUEUE_FULL,
    R_RUNTIME_DARWIN_FS_START_RUNTIME_STOPPING
} RRuntimeDarwinFsStartStatus;

typedef struct RRuntimeDarwinFsLaneCreateResult {
    RRuntimeDarwinFsLane *lane;
    RRuntimeDarwinFsStartStatus status;
    int native_error;
} RRuntimeDarwinFsLaneCreateResult;

typedef struct RRuntimeDarwinFsSubmitResult {
    RRuntimeDarwinFsRequest *request;
    RRuntimeDarwinFsStartStatus status;
    int native_error;
} RRuntimeDarwinFsSubmitResult;

typedef struct RRuntimeDarwinFsPrepareResult {
    RRuntimeDarwinFsPreparedRequest *prepared;
    RRuntimeDarwinFsStartStatus status;
    int native_error;
} RRuntimeDarwinFsPrepareResult;

typedef struct RRuntimeDarwinFsServiceStartResult {
    RRuntimeDarwinFsStartStatus status;
    int native_error;
} RRuntimeDarwinFsServiceStartResult;

typedef struct RRuntimeDarwinFsResult {
    RRuntimeDarwinFsOperation operation;
    RRuntimeDarwinFsTerminalEvent terminal_event;
    int native_error;
    int operation_native_error;
    int64_t native_return_value;
    uint64_t terminal_event_sequence;
    uint64_t native_event_sequence;
    _Bool native_entered;
    _Bool committed;
    struct stat metadata;
} RRuntimeDarwinFsResult;

typedef struct RRuntimeDarwinFsEnumerationBuffer {
    RRuntimeAllocator *allocator;
    void *data;
    size_t capacity;
    int64_t entry_count;
} RRuntimeDarwinFsEnumerationBuffer;

/*
 * Ownership: success returns one owned lane. Failure returns null. allocator must outlive the
 * lane and every returned request. Capacity is the exact maximum number of requests waiting in
 * the FIFO; four entered requests are additional. Capacity must not exceed
 * R_RUNTIME_DARWIN_FS_MAX_PENDING_REQUESTS.
 */
RRuntimeDarwinFsLaneCreateResult r_runtime_darwin_fs_lane_create(RRuntimeAllocator *allocator,
                                                                 size_t capacity);

/*
 * Ownership: consumes the lane owner, rejects new submissions, terminalizes queued work,
 * waits for all four workers, and leaves already returned request owners valid.
 */
void r_runtime_darwin_fs_lane_destroy(RRuntimeDarwinFsLane *lane);

size_t r_runtime_darwin_fs_lane_worker_count(const RRuntimeDarwinFsLane *lane);
size_t r_runtime_darwin_fs_lane_capacity(const RRuntimeDarwinFsLane *lane);

/*
 * Reserves a copied path, retained directory descriptor, queue position and optional suspended
 * deadline before task commit. Activation cannot allocate. follow_final_symlink and beneath are
 * mutually constrained by the caller: a beneath preparation shall not follow the final link.
 */
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_open_at(RRuntimeDarwinFsLane *lane,
                                                                  int directory_fd,
                                                                  const char *path,
                                                                  int flags,
                                                                  mode_t mode,
                                                                  _Bool follow_final_symlink,
                                                                  _Bool beneath,
                                                                  uint64_t timeout_nanoseconds);

/*
 * These preparations copy every path, duplicate every descriptor, reserve queue capacity and
 * prepare the optional deadline before task commit. Activation is allocation-free. A beneath
 * directory creation accepts only S_IRWXU because its private staging entry is published with
 * exactly that target mode, subject to the process umask.
 */
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_fstat(RRuntimeDarwinFsLane *lane,
                                                                int descriptor,
                                                                uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_fstat_at(RRuntimeDarwinFsLane *lane,
                                                                   int directory_fd,
                                                                   const char *path,
                                                                   _Bool beneath,
                                                                   uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_prepare_create_directory_at(RRuntimeDarwinFsLane *lane,
                                                int directory_fd,
                                                const char *path,
                                                mode_t mode,
                                                _Bool recursive,
                                                _Bool beneath,
                                                uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_unlink_at(RRuntimeDarwinFsLane *lane,
                                                                    int directory_fd,
                                                                    const char *path,
                                                                    _Bool remove_directory,
                                                                    _Bool beneath,
                                                                    uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_prepare_rename_at_no_replace(RRuntimeDarwinFsLane *lane,
                                                 int source_directory_fd,
                                                 const char *source_path,
                                                 int destination_directory_fd,
                                                 const char *destination_path,
                                                 _Bool beneath,
                                                 uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_close(RRuntimeDarwinFsLane *lane,
                                                                uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_seek(RRuntimeDarwinFsLane *lane,
                                                               int descriptor,
                                                               off_t offset,
                                                               int whence,
                                                               uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_prepare_enumerate(RRuntimeDarwinFsLane *lane,
                                      int directory_fd,
                                      const struct attrlist *attributes,
                                      size_t buffer_size,
                                      uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_fsync(RRuntimeDarwinFsLane *lane,
                                                                int descriptor,
                                                                _Bool full_durability,
                                                                uint64_t timeout_nanoseconds);
/* fcntl F_BARRIERFSYNC: the writes before it reach the media before the writes after it. */
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_barrier_fsync(
    RRuntimeDarwinFsLane *lane, int descriptor, uint64_t timeout_nanoseconds);
/*
 * fcntl F_OFD_SETLK over a retained duplicate of descriptor, which shares its open file
 * description and therefore its locks: type is F_RDLCK, F_WRLCK or F_UNLCK over length bytes from
 * start, zero length reaching past every end. Never waits: a conflicting lock of another open file
 * description completes with EAGAIN or EACCES.
 */
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_ofd_lock(RRuntimeDarwinFsLane *lane,
                                                                   int descriptor,
                                                                   short type,
                                                                   off_t start,
                                                                   off_t length,
                                                                   uint64_t timeout_nanoseconds);

/*
 * Creates one private 0700 staging directory directly beneath directory_fd and one 0600 regular
 * payload file inside it. The generated staging name uses the reserved runtime prefix. Success
 * owns the namespace entry, payload descriptor and one reserved cleanup request until
 * request_take_file_stage transfers them together. Cancellation before terminal publication
 * removes the private entry on the lane.
 */
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_file_stage(RRuntimeDarwinFsLane *lane,
                                                                     int directory_fd,
                                                                     uint64_t timeout_nanoseconds);

/*
 * Removes a transferred staging payload if it still exists and then its private directory. This
 * cleanup request is an internal non-cancellable commit and accepts only generated staging names.
 */
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_file_stage_cleanup(
    RRuntimeDarwinFsLane *lane, int directory_fd, const char *staging_name);

/* Opens the directory at activation and applies F_FULLFSYNC to that resolved identity. */
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_full_fsync_directory_at(
    RRuntimeDarwinFsLane *lane, int directory_fd, const char *path, _Bool beneath);

/*
 * Allocation-complete late-bound preparations for compound operations. Each success reserves its
 * request storage, copied path state, deadline state and bounded lane admission. Binding only
 * borrows descriptors owned by the compound operation and cannot allocate or duplicate them.
 */
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_prepare_file_stage_late_bound(RRuntimeDarwinFsLane *lane);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_prepare_fsync_late_bound(RRuntimeDarwinFsLane *lane, _Bool full_durability);
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_staging_rename_late_bound(
    RRuntimeDarwinFsLane *lane, const char *destination_path, _Bool beneath);
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_full_fsync_directory_at_late_bound(
    RRuntimeDarwinFsLane *lane, const char *path, _Bool beneath);

_Bool r_runtime_darwin_fs_prepared_bind_file_stage(RRuntimeDarwinFsPreparedRequest *prepared,
                                                   int directory_fd);
_Bool r_runtime_darwin_fs_prepared_bind_fsync(RRuntimeDarwinFsPreparedRequest *prepared,
                                              int descriptor);
_Bool r_runtime_darwin_fs_prepared_bind_staging_rename(RRuntimeDarwinFsPreparedRequest *prepared,
                                                       int directory_fd,
                                                       const char *staging_source);
_Bool r_runtime_darwin_fs_prepared_bind_full_fsync_directory_at(
    RRuntimeDarwinFsPreparedRequest *prepared, int directory_fd);

/* Ownership: success consumes prepared and returns one owned active request. */
RRuntimeDarwinFsSubmitResult
r_runtime_darwin_fs_prepared_activate(RRuntimeDarwinFsPreparedRequest **prepared);

/*
 * Allocation-free late binding for a prepared seek. Only SEEK_SET and SEEK_END are accepted;
 * the request must still be in the prepared state. This is used by serialized file-position
 * schedulers whose exact offset is not known until the request reaches the queue head.
 */
_Bool r_runtime_darwin_fs_prepared_set_seek(RRuntimeDarwinFsPreparedRequest *prepared,
                                            off_t offset,
                                            int whence);

/*
 * Ownership: after the language Move commit, success consumes *descriptor into the close request
 * and sets it to -1. Failure leaves it unchanged. A close request always enters native cleanup;
 * cancellation and deadline selection never skip descriptor release.
 */
RRuntimeDarwinFsSubmitResult
r_runtime_darwin_fs_prepared_activate_close(RRuntimeDarwinFsPreparedRequest **prepared,
                                            int *descriptor);

/* Ownership: consumes and releases an unactivated preparation. */
void r_runtime_darwin_fs_prepared_abort(RRuntimeDarwinFsPreparedRequest **prepared);

/*
 * Ownership: path is call-bounded and copied. directory_fd is retained independently
 * before queue commit. Success returns one owned request.
 */
RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_open_at(
    RRuntimeDarwinFsLane *lane, int directory_fd, const char *path, int flags, mode_t mode);

/*
 * Ownership: descriptor is consumed only on successful queue commit. If queued work is
 * cancelled or stopped before entry, request_take_unclosed_fd can recover it.
 */
RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_close(RRuntimeDarwinFsLane *lane,
                                                              int descriptor);

/* Ownership: each descriptor is independently retained before queue commit. */
RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_fstat(RRuntimeDarwinFsLane *lane,
                                                              int descriptor);
RRuntimeDarwinFsSubmitResult
r_runtime_darwin_fs_submit_fstat_at(RRuntimeDarwinFsLane *lane, int directory_fd, const char *path);
RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_seek(RRuntimeDarwinFsLane *lane,
                                                             int descriptor,
                                                             off_t offset,
                                                             int whence);

/*
 * Ownership: attributes is copied, directory_fd is retained and the metadata buffer is
 * request-owned. buffer_size must be positive. No payload read operation is performed.
 */
RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_enumerate(RRuntimeDarwinFsLane *lane,
                                                                  int directory_fd,
                                                                  const struct attrlist *attributes,
                                                                  size_t buffer_size);

RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_mkdir_at(RRuntimeDarwinFsLane *lane,
                                                                 int directory_fd,
                                                                 const char *path,
                                                                 mode_t mode);
RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_unlink_at(RRuntimeDarwinFsLane *lane,
                                                                  int directory_fd,
                                                                  const char *path,
                                                                  _Bool remove_directory);
RRuntimeDarwinFsSubmitResult
r_runtime_darwin_fs_submit_rename_at_no_replace(RRuntimeDarwinFsLane *lane,
                                                int source_directory_fd,
                                                const char *source_path,
                                                int destination_directory_fd,
                                                const char *destination_path);
RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_fsync(RRuntimeDarwinFsLane *lane,
                                                              int descriptor);
RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_full_fsync(RRuntimeDarwinFsLane *lane,
                                                                   int descriptor);
RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_barrier_fsync(RRuntimeDarwinFsLane *lane,
                                                                      int descriptor);
RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_ofd_lock(
    RRuntimeDarwinFsLane *lane, int descriptor, short type, off_t start, off_t length);

/* The first accepted cancellation/deadline event is linearized. */
_Bool r_runtime_darwin_fs_request_cancel(RRuntimeDarwinFsRequest *request);
_Bool r_runtime_darwin_fs_request_deadline_expired(RRuntimeDarwinFsRequest *request);

/*
 * Registers one callback. It runs exactly once on the lane's separate completion queue after the
 * request becomes terminal. The callback owns no request reference beyond its invocation.
 */
_Bool r_runtime_darwin_fs_request_set_completion(RRuntimeDarwinFsRequest *request,
                                                 RRuntimeDarwinFsCompletionFn completion,
                                                 void *context);

RRuntimeDarwinFsRequestState r_runtime_darwin_fs_request_state(RRuntimeDarwinFsRequest *request);
RRuntimeDarwinFsResult r_runtime_darwin_fs_request_wait(RRuntimeDarwinFsRequest *request);
uint64_t r_runtime_darwin_fs_request_submission_sequence(RRuntimeDarwinFsRequest *request);
uint64_t r_runtime_darwin_fs_request_entry_sequence(RRuntimeDarwinFsRequest *request);

/*
 * Ownership: returns a call-bounded descriptor snapshot for a successful OPEN_AT result while the
 * request retains ownership; otherwise returns -1. The snapshot is invalid after request release,
 * discard_opened_fd or take_opened_fd.
 */
int r_runtime_darwin_fs_request_opened_fd(RRuntimeDarwinFsRequest *request);

/*
 * Ownership: consumes and synchronously closes a successful OPEN_AT result descriptor while the
 * request remains live. Returns false when no such owned descriptor exists.
 */
_Bool r_runtime_darwin_fs_request_discard_opened_fd(RRuntimeDarwinFsRequest *request);

/* Ownership: transfers a successful OPEN_AT result descriptor once; otherwise returns -1. */
int r_runtime_darwin_fs_request_take_opened_fd(RRuntimeDarwinFsRequest *request);

/*
 * Ownership: before the request's final owner is released, atomically transfers a successful
 * file-stage payload descriptor and its mandatory cleanup duty. On entry *payload_descriptor must
 * be -1 and *cleanup_prepared must be null. staging_name receives a NUL-terminated generated
 * component. False leaves all outputs unchanged.
 */
_Bool r_runtime_darwin_fs_request_take_file_stage(
    RRuntimeDarwinFsRequest *request,
    int *payload_descriptor,
    char staging_name[R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY],
    RRuntimeDarwinFsPreparedRequest **cleanup_prepared);

/*
 * Ownership: transfers a CLOSE descriptor only when the request terminalized before
 * native entry; otherwise returns -1.
 */
int r_runtime_darwin_fs_request_take_unclosed_fd(RRuntimeDarwinFsRequest *request);

/*
 * Borrowed until request release. Available after a successful native enumeration, including a
 * result cached when cancellation/deadline won before publication. Size is the allocation capacity
 * passed to enumerate.
 */
const void *r_runtime_darwin_fs_request_enumeration_data(RRuntimeDarwinFsRequest *request,
                                                         size_t *size);

/* Returns the cached native entry count, or -1 when no completed enumeration is cached. */
int64_t r_runtime_darwin_fs_request_enumeration_entry_count(RRuntimeDarwinFsRequest *request);

/*
 * Ownership: transfers a completed enumeration buffer exactly once. The empty result owns
 * nothing. The caller releases a nonempty transfer with
 * r_runtime_darwin_fs_enumeration_buffer_release.
 */
RRuntimeDarwinFsEnumerationBuffer
r_runtime_darwin_fs_request_take_enumeration_buffer(RRuntimeDarwinFsRequest *request);
void r_runtime_darwin_fs_enumeration_buffer_release(RRuntimeDarwinFsEnumerationBuffer *buffer);

/* Ownership: creates one independent request owner; reference overflow is a contract failure. */
void r_runtime_darwin_fs_request_retain(RRuntimeDarwinFsRequest *request);

/* Ownership: consumes one request owner. */
void r_runtime_darwin_fs_request_release(RRuntimeDarwinFsRequest *request);

/*
 * Hosted process service. Start captures the process current-directory identity and creates the
 * normative four-worker lane. Stop requires the task executor to have drained first.
 */
RRuntimeDarwinFsServiceStartResult r_runtime_darwin_fs_service_start(RRuntimeAllocator *allocator,
                                                                     size_t capacity);
void r_runtime_darwin_fs_service_stop(void);

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_open(const char *path,
                                         int flags,
                                         mode_t mode,
                                         _Bool follow_final_symlink,
                                         uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_open_beneath(int directory_fd,
                                                 const char *relative_path,
                                                 int flags,
                                                 mode_t mode,
                                                 uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_metadata(const char *path, uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_file_metadata(int descriptor, uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_service_prepare_metadata_beneath(
    int directory_fd, const char *relative_path, uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_service_prepare_create_directory(
    const char *path, mode_t mode, _Bool recursive, uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_create_directory_beneath(int directory_fd,
                                                             const char *relative_path,
                                                             mode_t mode,
                                                             _Bool recursive,
                                                             uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_remove_beneath(int directory_fd,
                                                   const char *relative_path,
                                                   _Bool remove_directory,
                                                   uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_rename_beneath(int source_directory_fd,
                                                   const char *source_path,
                                                   int destination_directory_fd,
                                                   const char *destination_path,
                                                   uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_close(uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_service_prepare_seek(
    int descriptor, off_t offset, int whence, uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_enumerate(int directory_fd,
                                              const struct attrlist *attributes,
                                              size_t buffer_size,
                                              uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_service_prepare_fsync(
    int descriptor, _Bool full_durability, uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_barrier_fsync(int descriptor, uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_service_prepare_ofd_lock(
    int descriptor, short type, off_t start, off_t length, uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_file_stage(int directory_fd, uint64_t timeout_nanoseconds);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_file_stage_cleanup(int directory_fd, const char *staging_name);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_full_fsync_directory_beneath(int directory_fd,
                                                                 const char *relative_path);

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_service_prepare_file_stage_late_bound(void);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_fsync_late_bound(_Bool full_durability);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_staging_rename_late_bound(const char *destination_path,
                                                              _Bool beneath);
RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_full_fsync_directory_at_late_bound(const char *path,
                                                                       _Bool beneath);

#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
void r_runtime_darwin_fs_lane_testing_pause_dequeue(RRuntimeDarwinFsLane *lane, _Bool paused);
void r_runtime_darwin_fs_lane_testing_pause_before_native(RRuntimeDarwinFsLane *lane, _Bool paused);
void r_runtime_darwin_fs_lane_testing_pause_after_native(RRuntimeDarwinFsLane *lane, _Bool paused);
uint64_t r_runtime_darwin_fs_lane_testing_entry_sequence(RRuntimeDarwinFsLane *lane);
uint64_t r_runtime_darwin_fs_lane_testing_native_sequence(RRuntimeDarwinFsLane *lane);
uint64_t r_runtime_darwin_fs_lane_testing_signal_count(RRuntimeDarwinFsLane *lane);
void r_runtime_darwin_fs_request_testing_wait_for_state(RRuntimeDarwinFsRequest *request,
                                                        RRuntimeDarwinFsRequestState state);
void r_runtime_darwin_fs_testing_force_open_or_create_race(_Bool enabled);
void r_runtime_darwin_fs_testing_force_staging_collisions(unsigned int count);
void r_runtime_darwin_fs_testing_pause_after_staging(_Bool paused);
_Bool r_runtime_darwin_fs_testing_staging_reached(void);
uint64_t r_runtime_darwin_fs_testing_staging_cleanup_count(void);
uint64_t r_runtime_darwin_fs_testing_open_result_cleanup_count(void);
int r_runtime_darwin_fs_testing_last_open_result_cleanup_fd(void);
void r_runtime_darwin_fs_service_testing_pause_dequeue(_Bool paused);
void r_runtime_darwin_fs_service_testing_pause_before_native(_Bool paused);
void r_runtime_darwin_fs_service_testing_pause_after_native(_Bool paused);
uint64_t r_runtime_darwin_fs_service_testing_entry_sequence(void);
uint64_t r_runtime_darwin_fs_service_testing_native_sequence(void);
uint64_t r_runtime_darwin_fs_service_testing_signal_count(void);
#endif

#endif
