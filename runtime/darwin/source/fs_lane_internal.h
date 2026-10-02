#ifndef R_RUNTIME_DARWIN_FS_LANE_INTERNAL_H
#define R_RUNTIME_DARWIN_FS_LANE_INTERNAL_H

#include "r_runtime_darwin_fs_lane.h"

#include <dispatch/dispatch.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

#define R_RUNTIME_DARWIN_FS_WORKER_COUNT 4

typedef struct RRuntimeDarwinFsNativeResult {
    int64_t return_value;
    int native_error;
    int opened_fd;
    uint64_t event_sequence;
    _Bool committed;
    struct stat metadata;
} RRuntimeDarwinFsNativeResult;

struct RRuntimeDarwinFsRequest {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    size_t references;
    RRuntimeDarwinFsLane *lane;
    RRuntimeDarwinFsRequest *next;
    RRuntimeDarwinFsOperation operation;
    RRuntimeDarwinFsRequestState state;
    RRuntimeDarwinFsTerminalEvent pending_event;
    RRuntimeDarwinFsTerminalEvent terminal_event;
    dispatch_source_t deadline_timer;
    RRuntimeDarwinFsCompletionFn completion;
    void *completion_context;
    uint64_t submission_sequence;
    uint64_t entry_sequence;
    uint64_t pending_event_sequence;
    uint64_t terminal_event_sequence;
    uint64_t native_event_sequence;
    int native_error;
    int operation_native_error;
    int64_t native_return_value;
    int64_t enumeration_entry_count;
    int opened_fd;
    _Bool native_entered;
    _Bool committed;
    _Bool enumeration_cache_valid;
    _Bool deadline_activated;
    _Bool completion_scheduled;
    struct stat metadata;
    union {
        struct {
            int directory_fd;
            char *path;
            int flags;
            mode_t mode;
            _Bool follow_final_symlink;
            _Bool beneath;
        } open_at;
        struct {
            int descriptor;
            _Bool owned;
        } close;
        struct {
            int descriptor;
        } fstat;
        struct {
            int directory_fd;
            char *path;
            int flags;
        } fstat_at;
        struct {
            int descriptor;
            off_t offset;
            int whence;
        } seek;
        struct {
            int directory_fd;
            struct attrlist attributes;
            void *buffer;
            size_t buffer_size;
        } enumerate;
        struct {
            int directory_fd;
            char *path;
            mode_t mode;
            _Bool recursive;
            _Bool beneath;
        } mkdir_at;
        struct {
            int directory_fd;
            char *path;
            int flags;
        } unlink_at;
        struct {
            int source_directory_fd;
            char *source_path;
            int destination_directory_fd;
            char *destination_path;
            unsigned int flags;
            _Bool source_directory_owned;
            _Bool destination_directory_owned;
            _Bool configured;
        } rename_at;
        struct {
            int descriptor;
            _Bool descriptor_owned;
            _Bool configured;
        } sync;
        struct {
            int descriptor;
            short type;
            off_t start;
            off_t length;
            _Bool descriptor_owned;
        } lock;
        struct {
            int directory_fd;
            char name[R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY];
            _Bool owned;
            _Bool directory_owned;
            _Bool configured;
            RRuntimeDarwinFsPreparedRequest *cleanup_prepared;
        } file_stage;
        struct {
            int directory_fd;
            char name[R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY];
            _Bool directory_owned;
            _Bool configured;
        } file_stage_cleanup;
        struct {
            int directory_fd;
            char *path;
            _Bool beneath;
            _Bool directory_owned;
            _Bool configured;
        } sync_directory_at;
    } parameters;
};

struct RRuntimeDarwinFsLane {
    pthread_mutex_t mutex;
    pthread_cond_t queue_condition;
    RRuntimeAllocator *allocator;
    dispatch_queue_t completion_queue;
    pthread_t workers[R_RUNTIME_DARWIN_FS_WORKER_COUNT];
    size_t created_workers;
    size_t references;
    size_t capacity;
    size_t queued_count;
    size_t reserved_count;
    RRuntimeDarwinFsRequest *head;
    RRuntimeDarwinFsRequest *tail;
    uint64_t next_submission_sequence;
    uint64_t next_entry_sequence;
    uint64_t accepted_signal_count;
    uint64_t stopping_sequence;
    _Bool stopping;
    _Bool joined;
    _Bool testing_pause_dequeue;
    _Bool testing_pause_before_native;
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    uint64_t next_native_sequence;
    _Bool testing_pause_after_native;
#endif
};

RRuntimeDarwinFsNativeResult r_runtime_darwin_fs_internal_execute(RRuntimeDarwinFsRequest *request);
void r_runtime_darwin_fs_internal_release_entered_resources(RRuntimeDarwinFsRequest *request);
void r_runtime_darwin_fs_internal_request_retain(RRuntimeDarwinFsRequest *request);
_Bool r_runtime_darwin_fs_internal_prepare_deadline(RRuntimeDarwinFsRequest *request,
                                                    uint64_t timeout_nanoseconds);
void r_runtime_darwin_fs_internal_activate_deadline(RRuntimeDarwinFsRequest *request);
void r_runtime_darwin_fs_internal_disarm_deadline(RRuntimeDarwinFsRequest *request);
void r_runtime_darwin_fs_internal_schedule_completion(RRuntimeDarwinFsRequest *request);
_Bool r_runtime_darwin_fs_internal_has_pending_event(RRuntimeDarwinFsRequest *request);
void r_runtime_darwin_fs_internal_cleanup_staged_file(int directory_fd, const char *name);

#endif
