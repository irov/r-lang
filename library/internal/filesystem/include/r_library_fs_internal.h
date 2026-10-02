#ifndef R_LIBRARY_FS_INTERNAL_H
#define R_LIBRARY_FS_INTERNAL_H

#include "r_runtime_array.h"
#include "r_runtime_darwin_fs_lane.h"
#include "r_runtime_darwin_io.h"
#include "r_std_fs.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

struct RStdFsPathStorage {
    RRuntimeAllocator *allocator;
    size_t length;
    uint8_t bytes[];
};

typedef struct RLibraryFsOperationRegistration RLibraryFsOperationRegistration;
typedef struct RLibraryFsPositionNode RLibraryFsPositionNode;
typedef void (*RLibraryFsDrainFn)(void *context);
typedef void (*RLibraryFsOperationCancelFn)(void *context);
/*
 * Retain/release are infallible, allocation-free and non-blocking stable-control-block
 * operations. They shall not re-enter any operation on the associated filesystem handle.
 */
typedef void (*RLibraryFsOperationRetainFn)(void *context);
typedef void (*RLibraryFsOperationReleaseFn)(void *context);
typedef void (*RLibraryFsPositionDeadlineExpiredFn)(void *context);
typedef struct RLibraryFsPositionDeadlineToken RLibraryFsPositionDeadlineToken;

typedef struct RLibraryFsPositionDeadline {
    RLibraryFsPositionDeadlineToken *token;
} RLibraryFsPositionDeadline;

typedef enum RLibraryFsPositionCancelReason {
    R_LIBRARY_FS_POSITION_CANCEL_TASK = 0,
    R_LIBRARY_FS_POSITION_CANCEL_CLOSE,
    R_LIBRARY_FS_POSITION_CANCEL_DEADLINE
} RLibraryFsPositionCancelReason;

typedef enum RLibraryFsPositionUpdate {
    R_LIBRARY_FS_POSITION_KEEP = 0,
    R_LIBRARY_FS_POSITION_ADVANCE,
    R_LIBRARY_FS_POSITION_SET
} RLibraryFsPositionUpdate;

typedef enum RLibraryFsPositionState {
    R_LIBRARY_FS_POSITION_RESERVED = 0,
    R_LIBRARY_FS_POSITION_READY,
    R_LIBRARY_FS_POSITION_ACTIVATING,
    R_LIBRARY_FS_POSITION_ACTIVE,
    R_LIBRARY_FS_POSITION_FINISHED
} RLibraryFsPositionState;

/*
 * An activation callback performs allocation-free late binding, then calls activation_begin.
 * A false begin result prohibits native submission and is followed by one inactive cancel callback
 * after the activation callback returns. After a true begin, the callback either commits and
 * finishes an allocation-free synchronous result, or stores its cancelable native request and
 * calls activation_commit exactly once before arming native completion.
 */
typedef void (*RLibraryFsPositionActivateFn)(void *context, uint64_t position);
typedef void (*RLibraryFsPositionCancelFn)(void *context,
                                           RLibraryFsPositionCancelReason reason,
                                           _Bool active);

typedef struct RLibraryFsHandleStorage {
    _Atomic size_t references;
    RRuntimeAllocator *allocator;
    size_t allocation_alignment;
    pthread_mutex_t mutex;
    RLibraryFsOperationRegistration *operations;
    RLibraryFsPositionNode *position_head;
    RLibraryFsPositionNode *position_tail;
    RLibraryFsDrainFn drain;
    void *drain_context;
    int descriptor;
    RStdFsAccess access;
    uint64_t position;
    RRuntimeDarwinIoHandle *payload_io;
    _Bool append;
    _Bool position_pumping;
    _Bool close_reserved;
    _Bool terminal;
} RLibraryFsHandleStorage;

struct RLibraryFsOperationRegistration {
    RLibraryFsOperationRegistration *next;
    RLibraryFsHandleStorage *storage;
    RLibraryFsOperationCancelFn cancel;
    RLibraryFsOperationRetainFn retain_context;
    RLibraryFsOperationReleaseFn release_context;
    void *cancel_context;
    RLibraryFsPositionNode *position;
    _Bool registered;
    _Bool cancel_requested;
};

struct RLibraryFsPositionNode {
    RLibraryFsPositionNode *next;
    RLibraryFsHandleStorage *storage;
    RLibraryFsPositionActivateFn activate;
    RLibraryFsPositionCancelFn cancel;
    RLibraryFsOperationRetainFn retain_context;
    RLibraryFsOperationReleaseFn release_context;
    void *context;
    RLibraryFsPositionState state;
    RLibraryFsPositionCancelReason cancel_reason;
    _Bool activation_begun;
    _Bool cancel_pending;
    _Bool cancel_delivered;
};

struct RStdFsDirectoryStorage {
    RLibraryFsHandleStorage handle;
};

struct RStdFsFileStorage {
    RLibraryFsHandleStorage handle;
};

struct RStdFsDirectoryIterStorage {
    RRuntimeAllocator *allocator;
    RRuntimeDarwinFsEnumerationBuffer cache;
    size_t cache_offset;
    int64_t cache_index;
    int descriptor;
};

typedef enum RLibraryFsRelativePathStatus {
    R_LIBRARY_FS_RELATIVE_PATH_VALID = 0,
    R_LIBRARY_FS_RELATIVE_PATH_EMPTY,
    R_LIBRARY_FS_RELATIVE_PATH_ABSOLUTE,
    R_LIBRARY_FS_RELATIVE_PATH_EMPTY_COMPONENT,
    R_LIBRARY_FS_RELATIVE_PATH_CURRENT_COMPONENT,
    R_LIBRARY_FS_RELATIVE_PATH_PARENT_COMPONENT,
    R_LIBRARY_FS_RELATIVE_PATH_RESERVED_COMPONENT
} RLibraryFsRelativePathStatus;

RStdFsCallStatus r_library_internal_fs_path_create(RRuntimeAllocator *allocator,
                                                   const uint8_t *bytes,
                                                   size_t length,
                                                   RStdFsPath *result,
                                                   RStdAllocError *error);
_Bool r_library_internal_fs_path_has_reserved_component(const RStdFsPath *path);
const uint8_t *r_library_internal_fs_path_bytes(const RStdFsPath *path);
size_t r_library_internal_fs_path_length(const RStdFsPath *path);
RRuntimeAllocator *r_library_internal_fs_path_allocator(const RStdFsPath *path);
size_t r_library_internal_fs_first_nul(const uint8_t *bytes, size_t length);
RLibraryFsRelativePathStatus
r_library_internal_fs_validate_beneath_relative(const RStdFsPath *path);

RStdFsDirectoryStorage *r_library_internal_fs_directory_reserve(RRuntimeAllocator *allocator);
RStdFsFileStorage *r_library_internal_fs_file_reserve(RRuntimeAllocator *allocator);
RStdFsDirectoryIterStorage *
r_library_internal_fs_directory_iter_reserve(RRuntimeAllocator *allocator);
void r_library_internal_fs_directory_publish(RStdFsDirectoryStorage *storage, int descriptor);
void r_library_internal_fs_file_publish(RStdFsFileStorage *storage,
                                        int descriptor,
                                        RStdFsOpenFileOptions options,
                                        RRuntimeDarwinIoHandle *payload_io);
void r_library_internal_fs_directory_iter_publish(RStdFsDirectoryIterStorage *storage,
                                                  int descriptor);
int r_library_internal_fs_directory_descriptor(const RStdFsDirectory *directory);
int r_library_internal_fs_file_descriptor(const RStdFsFile *file);
RStdFsAccess r_library_internal_fs_file_access(const RStdFsFile *file);
RLibraryFsHandleStorage *
r_library_internal_fs_directory_handle_storage(const RStdFsDirectory *directory);
RLibraryFsHandleStorage *r_library_internal_fs_file_handle_storage(const RStdFsFile *file);
void r_library_internal_fs_directory_storage_release(RStdFsDirectoryStorage *storage);
void r_library_internal_fs_file_storage_release(RStdFsFileStorage *storage);
void r_library_internal_fs_directory_iter_storage_release(RStdFsDirectoryIterStorage *storage);
_Bool r_library_internal_fs_handle_retain(RLibraryFsHandleStorage *storage);
/* Adds one reference to a storage that an operation registration still holds: the caller saw the
   registration live under its own lock, before any unregistration could drop that reference. */
void r_library_internal_fs_handle_retain_registered(RLibraryFsHandleStorage *storage);
void r_library_internal_fs_handle_release(RLibraryFsHandleStorage *storage);
_Bool r_library_internal_fs_operation_register(RLibraryFsHandleStorage *storage,
                                               RLibraryFsOperationRegistration *registration,
                                               RRuntimeDarwinFsRequest *request);
_Bool r_library_internal_fs_position_reserve(RLibraryFsHandleStorage *storage,
                                             RLibraryFsPositionNode *node,
                                             RLibraryFsOperationRegistration *registration,
                                             RLibraryFsPositionActivateFn activate,
                                             RLibraryFsPositionCancelFn cancel,
                                             void *context,
                                             RLibraryFsOperationCancelFn close_cancel,
                                             RLibraryFsOperationRetainFn retain_context,
                                             RLibraryFsOperationReleaseFn release_context,
                                             int *descriptor,
                                             RStdFsAccess *access,
                                             _Bool *append);
/* Caller holds storage->mutex. This variant does not unlock it on either outcome. */
_Bool r_library_internal_fs_position_reserve_locked(RLibraryFsHandleStorage *storage,
                                                    RLibraryFsPositionNode *node,
                                                    RLibraryFsOperationRegistration *registration,
                                                    RLibraryFsPositionActivateFn activate,
                                                    RLibraryFsPositionCancelFn cancel,
                                                    void *context,
                                                    RLibraryFsOperationCancelFn close_cancel,
                                                    RLibraryFsOperationRetainFn retain_context,
                                                    RLibraryFsOperationReleaseFn release_context,
                                                    int *descriptor,
                                                    RStdFsAccess *access,
                                                    _Bool *append);
/* Caller holds storage->mutex. Success returns one retained view of the published payload root. */
RRuntimeDarwinIoStartStatus r_library_internal_fs_payload_io_retain_locked(
    RLibraryFsHandleStorage *storage, RRuntimeDarwinIoHandle **payload_io, int *native_error);
void r_library_internal_fs_position_publish(RLibraryFsPositionNode *node);
void r_library_internal_fs_position_abort(RLibraryFsPositionNode *node);
_Bool r_library_internal_fs_position_activation_begin(RLibraryFsPositionNode *node);
void r_library_internal_fs_position_activation_commit(RLibraryFsPositionNode *node);
void r_library_internal_fs_position_cancel(RLibraryFsPositionNode *node,
                                           RLibraryFsPositionCancelReason reason);
/* For a late caller (task cancellation, deadline or close callback) that may race the operation's
   unregistration: storage carries a reference taken with
   r_library_internal_fs_handle_retain_registered, which the caller releases afterwards. A node
   that no longer belongs to storage is left alone. */
void r_library_internal_fs_position_cancel_retained(RLibraryFsHandleStorage *storage,
                                                    RLibraryFsPositionNode *node,
                                                    RLibraryFsPositionCancelReason reason);
void r_library_internal_fs_position_finish(RLibraryFsPositionNode *node,
                                           RLibraryFsPositionUpdate update,
                                           uint64_t value);
/*
 * Calls for one wrapper are externally serialized. destroy releases the wrapper owner without
 * waiting for an already entered expiry callback; the independent token and retained context stay
 * alive through source cancellation acknowledgement. continuous_deadline is a valid future
 * std.time instant in the continuous-clock domain. Dispatch uptime timers use bounded intervals;
 * first activation and every wake recompute the continuous remaining duration before expiry can be
 * delivered.
 */
_Bool r_library_internal_fs_position_deadline_initialize(
    RLibraryFsPositionDeadline *deadline,
    RRuntimeAllocator *allocator,
    RStdTimeInstant continuous_deadline,
    RLibraryFsPositionDeadlineExpiredFn expired,
    RLibraryFsOperationRetainFn retain_context,
    RLibraryFsOperationReleaseFn release_context,
    void *context);
void r_library_internal_fs_position_deadline_activate(RLibraryFsPositionDeadline *deadline);
void r_library_internal_fs_position_deadline_cancel(RLibraryFsPositionDeadline *deadline);
void r_library_internal_fs_position_deadline_destroy(RLibraryFsPositionDeadline *deadline);
void r_library_internal_fs_operation_unregister(RLibraryFsHandleStorage *storage,
                                                RLibraryFsOperationRegistration *registration);
/*
 * Reserves the handle for an exact two-phase close without consuming it. The returned payload_io
 * is borrowed from storage until either abort_close or begin_close. While reserved, new operations
 * are rejected, but already registered operations retain their ordinary progress and cancellation
 * contracts.
 */
_Bool r_library_internal_fs_handle_reserve_close(RLibraryFsHandleStorage *storage,
                                                 int *descriptor,
                                                 RRuntimeDarwinIoHandle **payload_io);
void r_library_internal_fs_handle_abort_close(RLibraryFsHandleStorage *storage);
_Bool r_library_internal_fs_handle_begin_close(RLibraryFsHandleStorage *storage,
                                               RLibraryFsDrainFn drain,
                                               void *drain_context,
                                               int *descriptor,
                                               RRuntimeDarwinIoHandle **payload_io,
                                               _Bool *already_drained);

RStdFsTaskStartResult r_library_internal_fs_open_directory(const RStdFsPath *path,
                                                           RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_open_directory_beneath(const RStdFsDirectory *root,
                                                                   const RStdFsPath *relative,
                                                                   RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_open_file(const RStdFsPath *path,
                                                      RStdFsOpenFileOptions options,
                                                      RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_open_file_beneath(const RStdFsDirectory *root,
                                                              const RStdFsPath *relative,
                                                              RStdFsOpenFileOptions options,
                                                              RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_create_directory(const RStdFsPath *path,
                                                             _Bool recursive,
                                                             RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_create_directory_beneath(const RStdFsDirectory *root,
                                                                     const RStdFsPath *relative,
                                                                     _Bool recursive,
                                                                     RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_file_metadata(const RStdFsFile *file,
                                                          RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_metadata(const RStdFsPath *path,
                                                     RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_metadata_beneath(const RStdFsDirectory *root,
                                                             const RStdFsPath *relative,
                                                             RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_remove_file_beneath(const RStdFsDirectory *root,
                                                                const RStdFsPath *relative,
                                                                RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_remove_directory_beneath(const RStdFsDirectory *root,
                                                                     const RStdFsPath *relative,
                                                                     RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_rename_beneath(const RStdFsDirectory *source_root,
                                                           const RStdFsPath *source,
                                                           const RStdFsDirectory *destination_root,
                                                           const RStdFsPath *destination,
                                                           RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_seek(const RStdFsFile *file,
                                                 RStdFsSeekOrigin origin,
                                                 int64_t offset,
                                                 RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_flush(const RStdFsFile *file, RStdFsDeadline deadline);
RStdFsTaskStartResult
r_library_internal_fs_sync(const RStdFsFile *file, RStdFsSyncLevel level, RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_try_lock(const RStdFsFile *file,
                                                     RStdFsLockKind kind,
                                                     uint64_t start,
                                                     uint64_t length,
                                                     RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_lock(const RStdFsFile *file,
                                                 RStdFsLockKind kind,
                                                 uint64_t start,
                                                 uint64_t length,
                                                 RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_unlock(const RStdFsFile *file,
                                                   uint64_t start,
                                                   uint64_t length,
                                                   RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_read_at(const RStdFsFile *file,
                                                    uint64_t offset,
                                                    RRuntimeArray *buffer,
                                                    RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_write_all_at(const RStdFsFile *file,
                                                         uint64_t offset,
                                                         RRuntimeArray *buffer,
                                                         RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_read_at_into(const RStdFsFile *file,
                                                         uint64_t offset,
                                                         RStdFsMutableBytes target,
                                                         RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_write_all_at_from(const RStdFsFile *file,
                                                              uint64_t offset,
                                                              RStdFsConstBytes source,
                                                              RStdFsDeadline deadline);
RStdFsTaskStartResult
r_library_internal_fs_read(const RStdFsFile *file, RRuntimeArray *buffer, RStdFsDeadline deadline);
RStdFsTaskStartResult
r_library_internal_fs_read_file(const RStdFsPath *path, size_t limit, RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_read_file_beneath(const RStdFsDirectory *root,
                                                              const RStdFsPath *relative,
                                                              size_t limit,
                                                              RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_write_file_atomic_no_replace(const RStdFsPath *path,
                                                                         RRuntimeArray *data,
                                                                         RStdFsDeadline deadline);
RStdFsTaskStartResult
r_library_internal_fs_write_file_atomic_no_replace_beneath(const RStdFsDirectory *root,
                                                           const RStdFsPath *relative,
                                                           RRuntimeArray *data,
                                                           RStdFsDeadline deadline);
RStdFsTaskStartResult
r_library_internal_fs_write(const RStdFsFile *file, RRuntimeArray *buffer, RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_write_all(const RStdFsFile *file,
                                                      RRuntimeArray *buffer,
                                                      RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_read_into(const RStdFsFile *file,
                                                      RStdFsMutableBytes target,
                                                      RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_write_from(const RStdFsFile *file,
                                                       RStdFsConstBytes source,
                                                       RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_write_all_from(const RStdFsFile *file,
                                                           RStdFsConstBytes source,
                                                           RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_close_file(RStdFsFile *file, RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_close_directory(RStdFsDirectory *directory,
                                                            RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_iterate(const RStdFsDirectory *directory,
                                                    RStdFsDeadline deadline);
RStdFsTaskStartResult r_library_internal_fs_next(RStdFsDirectoryIter *iterator,
                                                 RStdFsDeadline deadline);

#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
void r_library_internal_fs_close_testing_pause_before_deadline_check(_Bool paused);
_Bool r_library_internal_fs_close_testing_deadline_check_reached(void);
void r_library_internal_fs_close_testing_pause_before_finalize_selection(_Bool paused);
_Bool r_library_internal_fs_close_testing_finalize_selection_reached(void);
_Bool r_library_internal_fs_close_testing_finalize_selection_completed(void);
void r_library_internal_fs_close_testing_pause_before_cancel_report(_Bool paused);
_Bool r_library_internal_fs_close_testing_cancel_report_reached(void);
void r_library_internal_fs_close_testing_pause_after_duty_publish(_Bool paused);
unsigned int r_library_internal_fs_close_testing_duty_publish_waiter_count(void);
_Bool r_library_internal_fs_close_testing_deadline_selected(void);
_Bool r_library_internal_fs_close_testing_deadline_reported(void);
enum {
    R_LIBRARY_INTERNAL_FS_CLOSE_TEST_COMPONENT_NONE = 0U,
    R_LIBRARY_INTERNAL_FS_CLOSE_TEST_COMPONENT_FS = 1U,
    R_LIBRARY_INTERNAL_FS_CLOSE_TEST_COMPONENT_IO = 2U
};
void r_library_internal_fs_close_testing_pause_after_component_error_selection(_Bool paused);
unsigned int r_library_internal_fs_close_testing_selected_error_component(void);
void r_library_internal_fs_payload_testing_pause_before_cancel_report(_Bool paused);
_Bool r_library_internal_fs_payload_testing_cancel_report_reached(void);
void r_library_internal_fs_payload_testing_pause_before_immediate_select(_Bool paused);
_Bool r_library_internal_fs_payload_testing_immediate_select_reached(void);
/* Pauses task cancellation after it took its storage reference and before it cancels the position
   node, so a test can let the operation finish and its file go first. */
void r_library_internal_fs_payload_testing_pause_after_cancel_retain(_Bool paused);
_Bool r_library_internal_fs_payload_testing_cancel_retain_reached(void);
_Bool r_library_internal_fs_payload_testing_terminal_after_cancel_observed(void);
_Bool r_library_internal_fs_payload_testing_task_cancel_won(void);
_Bool r_library_internal_fs_payload_testing_deadline_won(void);
/* Counts position cancellations whose native request signal (or inactive completion) is done;
   cancel_delivered on the node is published before that work starts. */
size_t r_library_internal_fs_payload_testing_position_cancel_applications(void);
/* Counts task cancellation reports of payload operations that have returned. */
size_t r_library_internal_fs_payload_testing_cancel_reports_finished(void);
void r_library_internal_fs_read_file_testing_pause_before_start_deadline_check(_Bool paused);
_Bool r_library_internal_fs_read_file_testing_start_deadline_check_reached(void);
void r_library_internal_fs_read_file_testing_pause_before_cancel_report(_Bool paused);
_Bool r_library_internal_fs_read_file_testing_cancel_report_reached(void);
void r_library_internal_fs_read_file_testing_pause_before_close_completion(_Bool paused);
_Bool r_library_internal_fs_read_file_testing_close_completion_reached(void);
void r_library_internal_fs_read_file_testing_pause_before_read_activation(_Bool paused);
_Bool r_library_internal_fs_read_file_testing_read_activation_reached(void);
void r_library_internal_fs_read_file_testing_pause_after_read_activation(_Bool paused);
_Bool r_library_internal_fs_read_file_testing_after_read_activation_reached(void);
void r_library_internal_fs_read_file_testing_pause_before_read_completion(_Bool paused);
_Bool r_library_internal_fs_read_file_testing_read_completion_reached(void);
void r_library_internal_fs_read_file_testing_reset_observations(void);
uint64_t r_library_internal_fs_read_file_testing_read_submission_count(void);
_Bool r_library_internal_fs_read_file_testing_read_activation_cancel_observed(void);
_Bool r_library_internal_fs_read_file_testing_cancel_reported_observed(void);
_Bool r_library_internal_fs_read_file_testing_deadline_expired_observed(void);
uint64_t r_library_internal_fs_read_file_testing_acknowledgement_count(void);
_Bool r_library_internal_fs_read_file_testing_limit_after_cancel_observed(void);
_Bool r_library_internal_fs_read_file_testing_limit_forced_completion(void);
void r_library_internal_fs_atomic_write_testing_reset(void);
void r_library_internal_fs_atomic_write_testing_pause_before_cancel_report(_Bool paused);
_Bool r_library_internal_fs_atomic_write_testing_cancel_report_reached(void);
void r_library_internal_fs_atomic_write_testing_pause_before_publication_activation(_Bool paused);
_Bool r_library_internal_fs_atomic_write_testing_publication_activation_reached(void);
_Bool r_library_internal_fs_atomic_write_testing_publication_activation_observed(void);
_Bool r_library_internal_fs_atomic_write_testing_trigger_publication_deadline(void);
void r_library_internal_fs_atomic_write_testing_pause_before_publication_commit(_Bool paused);
_Bool r_library_internal_fs_atomic_write_testing_publication_commit_reached(void);
void r_library_internal_fs_atomic_write_testing_pause_after_failure_select(_Bool paused);
_Bool r_library_internal_fs_atomic_write_testing_failure_select_reached(void);
uint64_t r_library_internal_fs_atomic_write_testing_failure_selection_count(void);
uint64_t r_library_internal_fs_atomic_write_testing_final_phase_count(void);
uint64_t r_library_internal_fs_atomic_write_testing_acknowledgement_count(void);
void r_library_internal_fs_control_testing_pause_before_publish(_Bool paused);
_Bool r_library_internal_fs_control_testing_publish_reached(void);
_Bool r_library_internal_fs_control_testing_cancel_reached(void);
void r_library_internal_fs_control_testing_pause_after_logical_commit(_Bool paused);
_Bool r_library_internal_fs_control_testing_logical_commit_reached(void);
_Bool r_library_internal_fs_testing_parse_directory_entry(RStdFsDirectoryIterStorage *storage,
                                                          RStdFsDirectoryEntry *entry,
                                                          RStdFsError *error);
#endif

#endif
