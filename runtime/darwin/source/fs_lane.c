#include "r_runtime_darwin_fs_lane.h"

#include "fs_lane_internal.h"
#include "r_runtime_darwin_event.h"

#include <dispatch/dispatch.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/attr.h>
#include <sys/stat.h>
#include <unistd.h>

_Static_assert(R_RUNTIME_DARWIN_FS_WORKER_COUNT == 4,
               "the Darwin filesystem adapter lane has exactly four workers");

#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
static _Atomic uint64_t testing_open_result_cleanup_count;
static _Atomic int testing_last_open_result_cleanup_fd = -1;

static void testing_report_open_result_cleanup(int descriptor) {
    atomic_store_explicit(&testing_last_open_result_cleanup_fd, descriptor, memory_order_relaxed);
    (void)atomic_fetch_add_explicit(
        &testing_open_result_cleanup_count, UINT64_C(1), memory_order_release);
}
#endif

static RRuntimeDarwinFsSubmitResult submit_failure(RRuntimeDarwinFsStartStatus status,
                                                   int native_error) {
    RRuntimeDarwinFsSubmitResult result;

    result.request = NULL;
    result.status = status;
    result.native_error = native_error;
    return result;
}

static RRuntimeDarwinFsSubmitResult submit_success(RRuntimeDarwinFsRequest *request) {
    RRuntimeDarwinFsSubmitResult result;

    result.request = request;
    result.status = R_RUNTIME_DARWIN_FS_START_OK;
    result.native_error = 0;
    return result;
}

static RRuntimeDarwinFsPrepareResult prepare_failure(RRuntimeDarwinFsStartStatus status,
                                                     int native_error) {
    RRuntimeDarwinFsPrepareResult result;

    result.prepared = NULL;
    result.status = status;
    result.native_error = native_error;
    return result;
}

static RRuntimeDarwinFsPrepareResult prepare_success(RRuntimeDarwinFsRequest *request) {
    RRuntimeDarwinFsPrepareResult result;

    result.prepared = request;
    result.status = R_RUNTIME_DARWIN_FS_START_OK;
    result.native_error = 0;
    return result;
}

static void close_descriptor(int *descriptor) {
    if (*descriptor >= 0) {
        (void)close(*descriptor);
        *descriptor = -1;
    }
}

static void lane_retain(RRuntimeDarwinFsLane *lane) {
    int lock_result = pthread_mutex_lock(&lane->mutex);

    if (lock_result != 0) {
        abort();
    }
    lane->references += 1U;
    if (pthread_mutex_unlock(&lane->mutex) != 0) {
        abort();
    }
}

static void lane_release(RRuntimeDarwinFsLane *lane) {
    _Bool destroy = 0;

    if (pthread_mutex_lock(&lane->mutex) != 0) {
        abort();
    }
    if (lane->references == 0U) {
        abort();
    }
    lane->references -= 1U;
    destroy = lane->references == 0U;
    if (pthread_mutex_unlock(&lane->mutex) != 0) {
        abort();
    }
    if (destroy) {
        if (!lane->joined) {
            abort();
        }
        dispatch_release(lane->completion_queue);
        (void)pthread_cond_destroy(&lane->queue_condition);
        (void)pthread_mutex_destroy(&lane->mutex);
        r_runtime_allocator_deallocate(lane, _Alignof(RRuntimeDarwinFsLane));
    }
}

static void initialize_request_descriptors(RRuntimeDarwinFsRequest *request) {
    request->opened_fd = -1;
    switch (request->operation) {
    case R_RUNTIME_DARWIN_FS_OPEN_AT:
        request->parameters.open_at.directory_fd = -1;
        break;
    case R_RUNTIME_DARWIN_FS_CLOSE:
        request->parameters.close.descriptor = -1;
        break;
    case R_RUNTIME_DARWIN_FS_FSTAT:
        request->parameters.fstat.descriptor = -1;
        break;
    case R_RUNTIME_DARWIN_FS_FSTAT_AT:
        request->parameters.fstat_at.directory_fd = -1;
        break;
    case R_RUNTIME_DARWIN_FS_SEEK:
        request->parameters.seek.descriptor = -1;
        break;
    case R_RUNTIME_DARWIN_FS_ENUMERATE:
        request->parameters.enumerate.directory_fd = -1;
        break;
    case R_RUNTIME_DARWIN_FS_MKDIR_AT:
        request->parameters.mkdir_at.directory_fd = -1;
        break;
    case R_RUNTIME_DARWIN_FS_UNLINK_AT:
        request->parameters.unlink_at.directory_fd = -1;
        break;
    case R_RUNTIME_DARWIN_FS_RENAME_AT_NO_REPLACE:
        request->parameters.rename_at.source_directory_fd = -1;
        request->parameters.rename_at.destination_directory_fd = -1;
        break;
    case R_RUNTIME_DARWIN_FS_FSYNC:
    case R_RUNTIME_DARWIN_FS_FULL_FSYNC:
    case R_RUNTIME_DARWIN_FS_BARRIER_FSYNC:
        request->parameters.sync.descriptor = -1;
        break;
    case R_RUNTIME_DARWIN_FS_OFD_LOCK:
        request->parameters.lock.descriptor = -1;
        break;
    case R_RUNTIME_DARWIN_FS_FILE_STAGE_CREATE:
        request->parameters.file_stage.directory_fd = -1;
        request->parameters.file_stage.cleanup_prepared = NULL;
        break;
    case R_RUNTIME_DARWIN_FS_FILE_STAGE_CLEANUP:
        request->parameters.file_stage_cleanup.directory_fd = -1;
        break;
    case R_RUNTIME_DARWIN_FS_FULL_FSYNC_DIRECTORY_AT:
        request->parameters.sync_directory_at.directory_fd = -1;
        break;
    }
}

static RRuntimeDarwinFsRequest *
request_create(RRuntimeDarwinFsLane *lane, RRuntimeDarwinFsOperation operation, int *native_error) {
    RRuntimeDarwinFsRequest *request = NULL;
    RRuntimeAllocationStatus allocation_status;
    int mutex_result;
    int condition_result;

    allocation_status = r_runtime_allocator_allocate(
        lane->allocator, sizeof(*request), _Alignof(RRuntimeDarwinFsRequest), (void **)&request);
    if (allocation_status != R_RUNTIME_ALLOCATION_OK) {
        *native_error = ENOMEM;
        return NULL;
    }
    (void)memset(request, 0, sizeof(*request));
    mutex_result = pthread_mutex_init(&request->mutex, NULL);
    if (mutex_result != 0) {
        r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinFsRequest));
        *native_error = mutex_result;
        return NULL;
    }
    condition_result = pthread_cond_init(&request->condition, NULL);
    if (condition_result != 0) {
        (void)pthread_mutex_destroy(&request->mutex);
        r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinFsRequest));
        *native_error = condition_result;
        return NULL;
    }
    lane_retain(lane);
    request->references = 1U;
    request->lane = lane;
    request->operation = operation;
    request->state = R_RUNTIME_DARWIN_FS_REQUEST_PREPARED;
    initialize_request_descriptors(request);
    *native_error = 0;
    return request;
}

static void request_cleanup(RRuntimeDarwinFsRequest *request) {
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    const int testing_opened_fd = request->opened_fd;
#endif

    close_descriptor(&request->opened_fd);
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    if (testing_opened_fd >= 0) {
        testing_report_open_result_cleanup(testing_opened_fd);
    }
#endif
    switch (request->operation) {
    case R_RUNTIME_DARWIN_FS_OPEN_AT:
        close_descriptor(&request->parameters.open_at.directory_fd);
        r_runtime_allocator_deallocate(request->parameters.open_at.path, _Alignof(char));
        break;
    case R_RUNTIME_DARWIN_FS_CLOSE:
        if (request->parameters.close.owned) {
            close_descriptor(&request->parameters.close.descriptor);
        }
        break;
    case R_RUNTIME_DARWIN_FS_FSTAT:
        close_descriptor(&request->parameters.fstat.descriptor);
        break;
    case R_RUNTIME_DARWIN_FS_FSTAT_AT:
        close_descriptor(&request->parameters.fstat_at.directory_fd);
        r_runtime_allocator_deallocate(request->parameters.fstat_at.path, _Alignof(char));
        break;
    case R_RUNTIME_DARWIN_FS_SEEK:
        close_descriptor(&request->parameters.seek.descriptor);
        break;
    case R_RUNTIME_DARWIN_FS_ENUMERATE:
        close_descriptor(&request->parameters.enumerate.directory_fd);
        r_runtime_allocator_deallocate(request->parameters.enumerate.buffer,
                                       _Alignof(unsigned char));
        break;
    case R_RUNTIME_DARWIN_FS_MKDIR_AT:
        close_descriptor(&request->parameters.mkdir_at.directory_fd);
        r_runtime_allocator_deallocate(request->parameters.mkdir_at.path, _Alignof(char));
        break;
    case R_RUNTIME_DARWIN_FS_UNLINK_AT:
        close_descriptor(&request->parameters.unlink_at.directory_fd);
        r_runtime_allocator_deallocate(request->parameters.unlink_at.path, _Alignof(char));
        break;
    case R_RUNTIME_DARWIN_FS_RENAME_AT_NO_REPLACE:
        if (request->parameters.rename_at.source_directory_owned) {
            close_descriptor(&request->parameters.rename_at.source_directory_fd);
        }
        if (request->parameters.rename_at.destination_directory_owned) {
            close_descriptor(&request->parameters.rename_at.destination_directory_fd);
        }
        r_runtime_allocator_deallocate(request->parameters.rename_at.source_path, _Alignof(char));
        r_runtime_allocator_deallocate(request->parameters.rename_at.destination_path,
                                       _Alignof(char));
        break;
    case R_RUNTIME_DARWIN_FS_FSYNC:
    case R_RUNTIME_DARWIN_FS_FULL_FSYNC:
    case R_RUNTIME_DARWIN_FS_BARRIER_FSYNC:
        if (request->parameters.sync.descriptor_owned) {
            close_descriptor(&request->parameters.sync.descriptor);
        }
        break;
    case R_RUNTIME_DARWIN_FS_OFD_LOCK:
        if (request->parameters.lock.descriptor_owned) {
            close_descriptor(&request->parameters.lock.descriptor);
        }
        break;
    case R_RUNTIME_DARWIN_FS_FILE_STAGE_CREATE:
        if (request->parameters.file_stage.owned) {
            abort();
        }
        r_runtime_darwin_fs_prepared_abort(&request->parameters.file_stage.cleanup_prepared);
        if (request->parameters.file_stage.directory_owned) {
            close_descriptor(&request->parameters.file_stage.directory_fd);
        }
        break;
    case R_RUNTIME_DARWIN_FS_FILE_STAGE_CLEANUP:
        if (request->parameters.file_stage_cleanup.directory_owned) {
            close_descriptor(&request->parameters.file_stage_cleanup.directory_fd);
        }
        break;
    case R_RUNTIME_DARWIN_FS_FULL_FSYNC_DIRECTORY_AT:
        if (request->parameters.sync_directory_at.directory_owned) {
            close_descriptor(&request->parameters.sync_directory_at.directory_fd);
        }
        r_runtime_allocator_deallocate(request->parameters.sync_directory_at.path, _Alignof(char));
        break;
    }
}

void r_runtime_darwin_fs_request_release(RRuntimeDarwinFsRequest *request) {
    _Bool destroy = 0;
    RRuntimeDarwinFsLane *lane;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->references == 0U) {
        abort();
    }
    request->references -= 1U;
    destroy = request->references == 0U;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (!destroy) {
        return;
    }
    if (request->deadline_timer != NULL) {
        abort();
    }
    lane = request->lane;
    request_cleanup(request);
    (void)pthread_cond_destroy(&request->condition);
    (void)pthread_mutex_destroy(&request->mutex);
    r_runtime_allocator_deallocate(request, _Alignof(RRuntimeDarwinFsRequest));
    lane_release(lane);
}

static int retain_descriptor(int descriptor, int *native_error) {
    int retained;

    if (descriptor < 0) {
        *native_error = EINVAL;
        return -1;
    }
    retained = fcntl(descriptor, F_DUPFD_CLOEXEC, 0);
    if (retained < 0) {
        *native_error = errno;
        return -1;
    }
    *native_error = 0;
    return retained;
}

static char *
copy_path(RRuntimeDarwinFsLane *lane, const char *path, RRuntimeDarwinFsStartStatus *status) {
    size_t length = 0U;
    char *copy = NULL;
    RRuntimeAllocationStatus allocation_status;

    if (path == NULL || path[0] == '\0') {
        *status = R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT;
        return NULL;
    }
    while (length < R_RUNTIME_DARWIN_FS_PATH_SCAN_LIMIT && path[length] != '\0') {
        length += 1U;
    }
    if (length == R_RUNTIME_DARWIN_FS_PATH_SCAN_LIMIT) {
        *status = R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT;
        return NULL;
    }
    allocation_status =
        r_runtime_allocator_allocate(lane->allocator, length + 1U, _Alignof(char), (void **)&copy);
    if (allocation_status != R_RUNTIME_ALLOCATION_OK) {
        *status = R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED;
        return NULL;
    }
    memcpy(copy, path, length + 1U);
    *status = R_RUNTIME_DARWIN_FS_START_OK;
    return copy;
}

static _Bool valid_staging_name(const char *name) {
    static const char prefix[] = R_RUNTIME_DARWIN_FS_STAGING_PREFIX;
    size_t index;

    if (name == NULL) {
        return 0;
    }
    for (index = 0U; index < R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY; ++index) {
        const char value = name[index];

        if (value == '\0') {
            return index + 1U == R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY;
        }
        if (index < sizeof(prefix) - 1U) {
            if (value != prefix[index]) {
                return 0;
            }
        } else if (!((value >= '0' && value <= '9') || (value >= 'a' && value <= 'f'))) {
            return 0;
        }
    }
    return 0;
}

static _Bool valid_staging_source(const char *source) {
    char staging_name[R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY];
    const size_t name_length = R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY - 1U;
    const size_t payload_size = sizeof(R_RUNTIME_DARWIN_FS_STAGING_PAYLOAD_NAME);
    size_t index;

    if (source == NULL) {
        return 0;
    }
    for (index = 0U; index < R_RUNTIME_DARWIN_FS_STAGING_SOURCE_CAPACITY; ++index) {
        if (source[index] == '\0') {
            if (index + 1U != R_RUNTIME_DARWIN_FS_STAGING_SOURCE_CAPACITY) {
                return 0;
            }
            break;
        }
    }
    if (index == R_RUNTIME_DARWIN_FS_STAGING_SOURCE_CAPACITY || source[name_length] != '/') {
        return 0;
    }
    memcpy(staging_name, source, name_length);
    staging_name[name_length] = '\0';
    return valid_staging_name(staging_name) && memcmp(source + name_length + 1U,
                                                      R_RUNTIME_DARWIN_FS_STAGING_PAYLOAD_NAME,
                                                      payload_size) == 0;
}

static uint64_t lane_stopping_sequence(RRuntimeDarwinFsLane *lane) {
    uint64_t sequence;

    if (pthread_mutex_lock(&lane->mutex) != 0) {
        abort();
    }
    sequence = lane->stopping_sequence;
    if (pthread_mutex_unlock(&lane->mutex) != 0) {
        abort();
    }
    return sequence;
}

static void finalize_native_request(RRuntimeDarwinFsRequest *request,
                                    RRuntimeDarwinFsNativeResult native_result,
                                    uint64_t stopping_sequence) {
    RRuntimeDarwinFsTerminalEvent event = R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->pending_event == 0 && stopping_sequence != UINT64_C(0)) {
        request->pending_event = R_RUNTIME_DARWIN_FS_TERMINAL_STOPPING;
        request->pending_event_sequence = stopping_sequence;
    }
    if (request->pending_event != 0 &&
        (native_result.event_sequence == UINT64_C(0) ||
         request->pending_event_sequence < native_result.event_sequence) &&
        !(native_result.native_error == 0 && native_result.committed)) {
        event = request->pending_event;
    }
    if (event != R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE &&
        request->operation == R_RUNTIME_DARWIN_FS_FILE_STAGE_CREATE &&
        request->parameters.file_stage.owned) {
        if (native_result.opened_fd < 0) {
            (void)pthread_mutex_unlock(&request->mutex);
            abort();
        }
        (void)close(native_result.opened_fd);
        native_result.opened_fd = -1;
        r_runtime_darwin_fs_internal_cleanup_staged_file(
            request->parameters.file_stage.directory_fd, request->parameters.file_stage.name);
        request->parameters.file_stage.owned = 0;
    }
    if (request->operation == R_RUNTIME_DARWIN_FS_ENUMERATE && native_result.native_error == 0 &&
        native_result.return_value >= 0) {
        request->enumeration_cache_valid = 1;
        request->enumeration_entry_count = native_result.return_value;
    }
    request->operation_native_error = native_result.native_error;
    request->native_event_sequence = native_result.event_sequence;
    if (event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE) {
        request->native_error = native_result.native_error;
        request->native_return_value = native_result.return_value;
        request->committed = native_result.committed;
        request->metadata = native_result.metadata;
        request->opened_fd = native_result.opened_fd;
        request->terminal_event_sequence = native_result.event_sequence;
    } else {
        request->native_error = 0;
        request->native_return_value = 0;
        request->committed = 0;
        request->terminal_event_sequence = request->pending_event_sequence;
        if (native_result.opened_fd >= 0) {
            (void)close(native_result.opened_fd);
        }
    }
    request->terminal_event = event;
    request->state = R_RUNTIME_DARWIN_FS_REQUEST_TERMINAL;
    (void)pthread_cond_broadcast(&request->condition);
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    r_runtime_darwin_fs_internal_disarm_deadline(request);
    r_runtime_darwin_fs_internal_schedule_completion(request);
}

static void *filesystem_worker(void *opaque_lane) {
    RRuntimeDarwinFsLane *lane = opaque_lane;

    if (pthread_setname_np("r.fs.adapter") != 0) {
        abort();
    }
    for (;;) {
        RRuntimeDarwinFsRequest *request;
        RRuntimeDarwinFsNativeResult native_result;
        _Bool stop_before_native;

        if (pthread_mutex_lock(&lane->mutex) != 0) {
            abort();
        }
        while ((lane->head == NULL || lane->testing_pause_dequeue) && !lane->stopping) {
            (void)pthread_cond_wait(&lane->queue_condition, &lane->mutex);
        }
        if (lane->head == NULL && lane->stopping) {
            if (pthread_mutex_unlock(&lane->mutex) != 0) {
                abort();
            }
            return NULL;
        }
        request = lane->head;
        lane->head = request->next;
        if (lane->head == NULL) {
            lane->tail = NULL;
        }
        lane->queued_count -= 1U;
        lane->next_entry_sequence += 1U;
        if (pthread_mutex_lock(&request->mutex) != 0) {
            abort();
        }
        request->state = R_RUNTIME_DARWIN_FS_REQUEST_ENTERED;
        request->entry_sequence = lane->next_entry_sequence;
        (void)pthread_cond_broadcast(&request->condition);
        if (pthread_mutex_unlock(&request->mutex) != 0) {
            abort();
        }
        while (lane->testing_pause_before_native && !lane->stopping) {
            (void)pthread_cond_wait(&lane->queue_condition, &lane->mutex);
        }
        stop_before_native = lane->stopping;
        if (pthread_mutex_unlock(&lane->mutex) != 0) {
            abort();
        }

        if (stop_before_native) {
            memset(&native_result, 0, sizeof(native_result));
            native_result.return_value = -1;
            native_result.opened_fd = -1;
        } else {
            if (pthread_mutex_lock(&request->mutex) != 0) {
                abort();
            }
            request->native_entered = 1;
            if (pthread_mutex_unlock(&request->mutex) != 0) {
                abort();
            }
            native_result = r_runtime_darwin_fs_internal_execute(request);
            native_result.event_sequence = r_runtime_darwin_event_sequence_next();
            r_runtime_darwin_fs_internal_release_entered_resources(request);
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
            if (pthread_mutex_lock(&lane->mutex) != 0) {
                abort();
            }
            lane->next_native_sequence += 1U;
            (void)pthread_cond_broadcast(&lane->queue_condition);
            while (lane->testing_pause_after_native && !lane->stopping) {
                (void)pthread_cond_wait(&lane->queue_condition, &lane->mutex);
            }
            if (pthread_mutex_unlock(&lane->mutex) != 0) {
                abort();
            }
#endif
        }
        finalize_native_request(request, native_result, lane_stopping_sequence(lane));
        r_runtime_darwin_fs_request_release(request);
    }
}

RRuntimeDarwinFsLaneCreateResult r_runtime_darwin_fs_lane_create(RRuntimeAllocator *allocator,
                                                                 size_t capacity) {
    RRuntimeDarwinFsLaneCreateResult result;
    RRuntimeDarwinFsLane *lane = NULL;
    RRuntimeAllocationStatus allocation_status;
    int native_error = 0;
    size_t index;

    result.lane = NULL;
    result.status = R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT;
    result.native_error = 0;
    if (capacity == 0U || capacity > R_RUNTIME_DARWIN_FS_MAX_PENDING_REQUESTS) {
        return result;
    }
    allocation_status = r_runtime_allocator_allocate(
        allocator, sizeof(*lane), _Alignof(RRuntimeDarwinFsLane), (void **)&lane);
    if (allocation_status != R_RUNTIME_ALLOCATION_OK) {
        result.status = R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED;
        result.native_error = ENOMEM;
        return result;
    }
    (void)memset(lane, 0, sizeof(*lane));
    lane->allocator = allocator;
    native_error = pthread_mutex_init(&lane->mutex, NULL);
    if (native_error != 0) {
        r_runtime_allocator_deallocate(lane, _Alignof(RRuntimeDarwinFsLane));
        result.status = R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED;
        result.native_error = native_error;
        return result;
    }
    native_error = pthread_cond_init(&lane->queue_condition, NULL);
    if (native_error != 0) {
        (void)pthread_mutex_destroy(&lane->mutex);
        r_runtime_allocator_deallocate(lane, _Alignof(RRuntimeDarwinFsLane));
        result.status = R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED;
        result.native_error = native_error;
        return result;
    }
    lane->completion_queue =
        dispatch_queue_create("r.runtime.fs.completion", DISPATCH_QUEUE_SERIAL);
    if (lane->completion_queue == NULL) {
        (void)pthread_cond_destroy(&lane->queue_condition);
        (void)pthread_mutex_destroy(&lane->mutex);
        r_runtime_allocator_deallocate(lane, _Alignof(RRuntimeDarwinFsLane));
        result.status = R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED;
        result.native_error = ENOMEM;
        return result;
    }
    lane->references = 1U;
    lane->capacity = capacity;
    for (index = 0U; index < R_RUNTIME_DARWIN_FS_WORKER_COUNT; ++index) {
        native_error = pthread_create(&lane->workers[index], NULL, filesystem_worker, lane);
        if (native_error != 0) {
            break;
        }
        lane->created_workers += 1U;
    }
    if (lane->created_workers != R_RUNTIME_DARWIN_FS_WORKER_COUNT) {
        if (pthread_mutex_lock(&lane->mutex) != 0) {
            abort();
        }
        lane->stopping_sequence = r_runtime_darwin_event_sequence_next();
        lane->stopping = 1;
        (void)pthread_cond_broadcast(&lane->queue_condition);
        if (pthread_mutex_unlock(&lane->mutex) != 0) {
            abort();
        }
        for (index = 0U; index < lane->created_workers; ++index) {
            if (pthread_join(lane->workers[index], NULL) != 0) {
                abort();
            }
        }
        dispatch_release(lane->completion_queue);
        (void)pthread_cond_destroy(&lane->queue_condition);
        (void)pthread_mutex_destroy(&lane->mutex);
        r_runtime_allocator_deallocate(lane, _Alignof(RRuntimeDarwinFsLane));
        result.status = R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED;
        result.native_error = native_error;
        return result;
    }
    result.lane = lane;
    result.status = R_RUNTIME_DARWIN_FS_START_OK;
    return result;
}

static RRuntimeDarwinFsRequest *detach_queued_requests(RRuntimeDarwinFsLane *lane) {
    RRuntimeDarwinFsRequest *request = lane->head;
    RRuntimeDarwinFsRequest *cursor;

    lane->head = NULL;
    lane->tail = NULL;
    lane->queued_count = 0U;
    for (cursor = request; cursor != NULL; cursor = cursor->next) {
        if (pthread_mutex_lock(&cursor->mutex) != 0) {
            abort();
        }
        cursor->pending_event = R_RUNTIME_DARWIN_FS_TERMINAL_STOPPING;
        cursor->pending_event_sequence = lane->stopping_sequence;
        cursor->terminal_event = R_RUNTIME_DARWIN_FS_TERMINAL_STOPPING;
        cursor->terminal_event_sequence = lane->stopping_sequence;
        cursor->state = R_RUNTIME_DARWIN_FS_REQUEST_TERMINAL;
        cursor->native_error = 0;
        cursor->native_return_value = 0;
        (void)pthread_cond_broadcast(&cursor->condition);
        if (pthread_mutex_unlock(&cursor->mutex) != 0) {
            abort();
        }
    }
    return request;
}

void r_runtime_darwin_fs_lane_destroy(RRuntimeDarwinFsLane *lane) {
    RRuntimeDarwinFsRequest *queued;
    size_t index;

    if (pthread_mutex_lock(&lane->mutex) != 0) {
        abort();
    }
    lane->stopping_sequence = r_runtime_darwin_event_sequence_next();
    lane->stopping = 1;
    lane->testing_pause_dequeue = 0;
    lane->testing_pause_before_native = 0;
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    lane->testing_pause_after_native = 0;
#endif
    queued = detach_queued_requests(lane);
    (void)pthread_cond_broadcast(&lane->queue_condition);
    if (pthread_mutex_unlock(&lane->mutex) != 0) {
        abort();
    }
    while (queued != NULL) {
        RRuntimeDarwinFsRequest *next = queued->next;
        queued->next = NULL;
        r_runtime_darwin_fs_internal_disarm_deadline(queued);
        r_runtime_darwin_fs_internal_schedule_completion(queued);
        r_runtime_darwin_fs_request_release(queued);
        queued = next;
    }
    for (index = 0U; index < lane->created_workers; ++index) {
        if (pthread_join(lane->workers[index], NULL) != 0) {
            abort();
        }
    }
    if (pthread_mutex_lock(&lane->mutex) != 0) {
        abort();
    }
    lane->joined = 1;
    if (pthread_mutex_unlock(&lane->mutex) != 0) {
        abort();
    }
    lane_release(lane);
}

size_t r_runtime_darwin_fs_lane_worker_count(const RRuntimeDarwinFsLane *lane) {
    (void)lane;
    return R_RUNTIME_DARWIN_FS_WORKER_COUNT;
}

size_t r_runtime_darwin_fs_lane_capacity(const RRuntimeDarwinFsLane *lane) {
    return lane->capacity;
}

static RRuntimeDarwinFsStartStatus reserve_prepared_request(RRuntimeDarwinFsRequest *request) {
    RRuntimeDarwinFsLane *lane = request->lane;
    RRuntimeDarwinFsStartStatus status = R_RUNTIME_DARWIN_FS_START_OK;

    if (pthread_mutex_lock(&lane->mutex) != 0) {
        abort();
    }
    if (lane->stopping) {
        status = R_RUNTIME_DARWIN_FS_START_RUNTIME_STOPPING;
    } else if (lane->queued_count + lane->reserved_count >= lane->capacity) {
        status = R_RUNTIME_DARWIN_FS_START_QUEUE_FULL;
    } else {
        lane->reserved_count += 1U;
    }
    if (pthread_mutex_unlock(&lane->mutex) != 0) {
        abort();
    }
    return status;
}

static RRuntimeDarwinFsPrepareResult finish_prepared_request(RRuntimeDarwinFsRequest *request,
                                                             uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsStartStatus reserve_status;

    if (!r_runtime_darwin_fs_internal_prepare_deadline(request, timeout_nanoseconds)) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, ENOMEM);
    }
    reserve_status = reserve_prepared_request(request);
    if (reserve_status != R_RUNTIME_DARWIN_FS_START_OK) {
        r_runtime_darwin_fs_internal_disarm_deadline(request);
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(reserve_status, 0);
    }
    return prepare_success(request);
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_open_at(RRuntimeDarwinFsLane *lane,
                                                                  int directory_fd,
                                                                  const char *path,
                                                                  int flags,
                                                                  mode_t mode,
                                                                  _Bool follow_final_symlink,
                                                                  _Bool beneath,
                                                                  uint64_t timeout_nanoseconds) {
    const int allowed_flags = O_ACCMODE | O_APPEND | O_CREAT | O_EXCL | O_TRUNC | O_DIRECTORY;
    RRuntimeDarwinFsRequest *request;
    RRuntimeDarwinFsStartStatus path_status;
    int native_error;

    if (directory_fd < 0 || (flags & ~allowed_flags) != 0 ||
        ((flags & O_EXCL) != 0 && (flags & O_CREAT) == 0) ||
        ((flags & O_TRUNC) != 0 && (flags & O_ACCMODE) == O_RDONLY) ||
        (beneath && follow_final_symlink) || timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT, 0);
    }
    request = request_create(lane, R_RUNTIME_DARWIN_FS_OPEN_AT, &native_error);
    if (request == NULL) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    request->parameters.open_at.path = copy_path(lane, path, &path_status);
    if (request->parameters.open_at.path == NULL) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(
            path_status, path_status == R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED ? ENOMEM : 0);
    }
    request->parameters.open_at.directory_fd = retain_descriptor(directory_fd, &native_error);
    if (request->parameters.open_at.directory_fd < 0) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED, native_error);
    }
    request->parameters.open_at.flags = flags;
    request->parameters.open_at.mode = mode;
    request->parameters.open_at.follow_final_symlink = follow_final_symlink;
    request->parameters.open_at.beneath = beneath;
    return finish_prepared_request(request, timeout_nanoseconds);
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_fstat(RRuntimeDarwinFsLane *lane,
                                                                int descriptor,
                                                                uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsRequest *request;
    int native_error;

    if (descriptor < 0 || timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT, 0);
    }
    request = request_create(lane, R_RUNTIME_DARWIN_FS_FSTAT, &native_error);
    if (request == NULL) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    request->parameters.fstat.descriptor = retain_descriptor(descriptor, &native_error);
    if (request->parameters.fstat.descriptor < 0) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED, native_error);
    }
    return finish_prepared_request(request, timeout_nanoseconds);
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_fstat_at(RRuntimeDarwinFsLane *lane,
                                                                   int directory_fd,
                                                                   const char *path,
                                                                   _Bool beneath,
                                                                   uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsRequest *request;
    RRuntimeDarwinFsStartStatus path_status;
    int native_error;

    if (directory_fd < 0 || timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT, 0);
    }
    request = request_create(lane, R_RUNTIME_DARWIN_FS_FSTAT_AT, &native_error);
    if (request == NULL) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    request->parameters.fstat_at.path = copy_path(lane, path, &path_status);
    if (request->parameters.fstat_at.path == NULL) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(
            path_status, path_status == R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED ? ENOMEM : 0);
    }
    request->parameters.fstat_at.directory_fd = retain_descriptor(directory_fd, &native_error);
    if (request->parameters.fstat_at.directory_fd < 0) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED, native_error);
    }
    request->parameters.fstat_at.flags = AT_SYMLINK_NOFOLLOW;
    if (beneath) {
        request->parameters.fstat_at.flags |= AT_SYMLINK_NOFOLLOW_ANY | AT_RESOLVE_BENEATH;
    }
    return finish_prepared_request(request, timeout_nanoseconds);
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_prepare_create_directory_at(RRuntimeDarwinFsLane *lane,
                                                int directory_fd,
                                                const char *path,
                                                mode_t mode,
                                                _Bool recursive,
                                                _Bool beneath,
                                                uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsRequest *request;
    RRuntimeDarwinFsStartStatus path_status;
    int native_error;

    if (directory_fd < 0 || (mode & ~(mode_t)ACCESSPERMS) != 0U || (beneath && mode != S_IRWXU) ||
        timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT, 0);
    }
    request = request_create(lane, R_RUNTIME_DARWIN_FS_MKDIR_AT, &native_error);
    if (request == NULL) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    request->parameters.mkdir_at.path = copy_path(lane, path, &path_status);
    if (request->parameters.mkdir_at.path == NULL) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(
            path_status, path_status == R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED ? ENOMEM : 0);
    }
    request->parameters.mkdir_at.directory_fd = retain_descriptor(directory_fd, &native_error);
    if (request->parameters.mkdir_at.directory_fd < 0) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED, native_error);
    }
    request->parameters.mkdir_at.mode = mode;
    request->parameters.mkdir_at.recursive = recursive;
    request->parameters.mkdir_at.beneath = beneath;
    return finish_prepared_request(request, timeout_nanoseconds);
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_unlink_at(RRuntimeDarwinFsLane *lane,
                                                                    int directory_fd,
                                                                    const char *path,
                                                                    _Bool remove_directory,
                                                                    _Bool beneath,
                                                                    uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsRequest *request;
    RRuntimeDarwinFsStartStatus path_status;
    int native_error;

    if (directory_fd < 0 || timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT, 0);
    }
    request = request_create(lane, R_RUNTIME_DARWIN_FS_UNLINK_AT, &native_error);
    if (request == NULL) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    request->parameters.unlink_at.path = copy_path(lane, path, &path_status);
    if (request->parameters.unlink_at.path == NULL) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(
            path_status, path_status == R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED ? ENOMEM : 0);
    }
    request->parameters.unlink_at.directory_fd = retain_descriptor(directory_fd, &native_error);
    if (request->parameters.unlink_at.directory_fd < 0) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED, native_error);
    }
    request->parameters.unlink_at.flags = remove_directory ? AT_REMOVEDIR : 0;
    if (beneath) {
        request->parameters.unlink_at.flags |= AT_SYMLINK_NOFOLLOW_ANY | AT_RESOLVE_BENEATH;
    }
    return finish_prepared_request(request, timeout_nanoseconds);
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_prepare_rename_at_no_replace(RRuntimeDarwinFsLane *lane,
                                                 int source_directory_fd,
                                                 const char *source_path,
                                                 int destination_directory_fd,
                                                 const char *destination_path,
                                                 _Bool beneath,
                                                 uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsRequest *request;
    RRuntimeDarwinFsStartStatus path_status;
    int native_error;

    if (source_directory_fd < 0 || destination_directory_fd < 0 ||
        timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT, 0);
    }
    request = request_create(lane, R_RUNTIME_DARWIN_FS_RENAME_AT_NO_REPLACE, &native_error);
    if (request == NULL) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    request->parameters.rename_at.source_path = copy_path(lane, source_path, &path_status);
    if (request->parameters.rename_at.source_path == NULL) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(
            path_status, path_status == R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED ? ENOMEM : 0);
    }
    request->parameters.rename_at.destination_path =
        copy_path(lane, destination_path, &path_status);
    if (request->parameters.rename_at.destination_path == NULL) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(
            path_status, path_status == R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED ? ENOMEM : 0);
    }
    request->parameters.rename_at.source_directory_fd =
        retain_descriptor(source_directory_fd, &native_error);
    if (request->parameters.rename_at.source_directory_fd < 0) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED, native_error);
    }
    request->parameters.rename_at.source_directory_owned = 1;
    request->parameters.rename_at.destination_directory_fd =
        retain_descriptor(destination_directory_fd, &native_error);
    if (request->parameters.rename_at.destination_directory_fd < 0) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED, native_error);
    }
    request->parameters.rename_at.destination_directory_owned = 1;
    request->parameters.rename_at.flags = RENAME_EXCL;
    if (beneath) {
        request->parameters.rename_at.flags |= RENAME_NOFOLLOW_ANY | RENAME_RESOLVE_BENEATH;
    }
    request->parameters.rename_at.configured = 1;
    return finish_prepared_request(request, timeout_nanoseconds);
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_close(RRuntimeDarwinFsLane *lane,
                                                                uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsRequest *request;
    int native_error;

    if (timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT, 0);
    }
    request = request_create(lane, R_RUNTIME_DARWIN_FS_CLOSE, &native_error);
    if (request == NULL) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    return finish_prepared_request(request, timeout_nanoseconds);
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_seek(RRuntimeDarwinFsLane *lane,
                                                               int descriptor,
                                                               off_t offset,
                                                               int whence,
                                                               uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsRequest *request;
    int native_error;

    if (descriptor < 0 || (whence != SEEK_SET && whence != SEEK_CUR && whence != SEEK_END) ||
        timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT, 0);
    }
    request = request_create(lane, R_RUNTIME_DARWIN_FS_SEEK, &native_error);
    if (request == NULL) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    request->parameters.seek.descriptor = retain_descriptor(descriptor, &native_error);
    if (request->parameters.seek.descriptor < 0) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED, native_error);
    }
    request->parameters.seek.offset = offset;
    request->parameters.seek.whence = whence;
    return finish_prepared_request(request, timeout_nanoseconds);
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_prepare_enumerate(RRuntimeDarwinFsLane *lane,
                                      int directory_fd,
                                      const struct attrlist *attributes,
                                      size_t buffer_size,
                                      uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsRequest *request;
    int native_error;

    if (directory_fd < 0 || buffer_size == 0U || attributes->bitmapcount != ATTR_BIT_MAP_COUNT ||
        attributes->reserved != 0U || timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT, 0);
    }
    request = request_create(lane, R_RUNTIME_DARWIN_FS_ENUMERATE, &native_error);
    if (request == NULL) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    if (r_runtime_allocator_allocate(lane->allocator,
                                     buffer_size,
                                     _Alignof(unsigned char),
                                     &request->parameters.enumerate.buffer) !=
        R_RUNTIME_ALLOCATION_OK) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, ENOMEM);
    }
    request->parameters.enumerate.directory_fd = retain_descriptor(directory_fd, &native_error);
    if (request->parameters.enumerate.directory_fd < 0) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED, native_error);
    }
    request->parameters.enumerate.attributes = *attributes;
    request->parameters.enumerate.buffer_size = buffer_size;
    return finish_prepared_request(request, timeout_nanoseconds);
}

/* A durability request of one of the fsync operations over a retained duplicate of descriptor. */
static RRuntimeDarwinFsPrepareResult prepare_sync_operation(RRuntimeDarwinFsLane *lane,
                                                            int descriptor,
                                                            RRuntimeDarwinFsOperation operation,
                                                            uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsRequest *request;
    int native_error;

    if (descriptor < 0 || timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT, 0);
    }
    request = request_create(lane, operation, &native_error);
    if (request == NULL) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    request->parameters.sync.descriptor = retain_descriptor(descriptor, &native_error);
    if (request->parameters.sync.descriptor < 0) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED, native_error);
    }
    request->parameters.sync.descriptor_owned = 1;
    request->parameters.sync.configured = 1;
    return finish_prepared_request(request, timeout_nanoseconds);
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_fsync(RRuntimeDarwinFsLane *lane,
                                                                int descriptor,
                                                                _Bool full_durability,
                                                                uint64_t timeout_nanoseconds) {
    return prepare_sync_operation(lane,
                                  descriptor,
                                  full_durability ? R_RUNTIME_DARWIN_FS_FULL_FSYNC
                                                  : R_RUNTIME_DARWIN_FS_FSYNC,
                                  timeout_nanoseconds);
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_barrier_fsync(
    RRuntimeDarwinFsLane *lane, int descriptor, uint64_t timeout_nanoseconds) {
    return prepare_sync_operation(
        lane, descriptor, R_RUNTIME_DARWIN_FS_BARRIER_FSYNC, timeout_nanoseconds);
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_ofd_lock(RRuntimeDarwinFsLane *lane,
                                                                   int descriptor,
                                                                   short type,
                                                                   off_t start,
                                                                   off_t length,
                                                                   uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsRequest *request;
    int native_error;

    if (descriptor < 0 || timeout_nanoseconds > (uint64_t)INT64_MAX || start < 0 || length < 0 ||
        (type != F_RDLCK && type != F_WRLCK && type != F_UNLCK)) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT, 0);
    }
    request = request_create(lane, R_RUNTIME_DARWIN_FS_OFD_LOCK, &native_error);
    if (request == NULL) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    request->parameters.lock.descriptor = retain_descriptor(descriptor, &native_error);
    if (request->parameters.lock.descriptor < 0) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED, native_error);
    }
    request->parameters.lock.descriptor_owned = 1;
    request->parameters.lock.type = type;
    request->parameters.lock.start = start;
    request->parameters.lock.length = length;
    return finish_prepared_request(request, timeout_nanoseconds);
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_file_stage(RRuntimeDarwinFsLane *lane,
                                                                     int directory_fd,
                                                                     uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsRequest *request;
    RRuntimeDarwinFsRequest *cleanup;
    RRuntimeDarwinFsPrepareResult cleanup_preparation;
    RRuntimeDarwinFsPrepareResult preparation;
    int native_error;

    if (directory_fd < 0 || timeout_nanoseconds > (uint64_t)INT64_MAX) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT, 0);
    }
    request = request_create(lane, R_RUNTIME_DARWIN_FS_FILE_STAGE_CREATE, &native_error);
    if (request == NULL) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    request->parameters.file_stage.directory_fd = retain_descriptor(directory_fd, &native_error);
    if (request->parameters.file_stage.directory_fd < 0) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED, native_error);
    }
    request->parameters.file_stage.directory_owned = 1;
    cleanup = request_create(lane, R_RUNTIME_DARWIN_FS_FILE_STAGE_CLEANUP, &native_error);
    if (cleanup == NULL) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    cleanup->parameters.file_stage_cleanup.directory_fd =
        retain_descriptor(directory_fd, &native_error);
    if (cleanup->parameters.file_stage_cleanup.directory_fd < 0) {
        r_runtime_darwin_fs_request_release(cleanup);
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED, native_error);
    }
    cleanup->parameters.file_stage_cleanup.directory_owned = 1;
    cleanup->parameters.file_stage_cleanup.configured = 1;
    cleanup_preparation = finish_prepared_request(cleanup, UINT64_C(0));
    if (cleanup_preparation.prepared == NULL) {
        r_runtime_darwin_fs_request_release(request);
        return cleanup_preparation;
    }
    request->parameters.file_stage.cleanup_prepared = cleanup_preparation.prepared;
    request->parameters.file_stage.configured = 1;
    preparation = finish_prepared_request(request, timeout_nanoseconds);
    return preparation;
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_file_stage_cleanup(
    RRuntimeDarwinFsLane *lane, int directory_fd, const char *staging_name) {
    RRuntimeDarwinFsRequest *request;
    int native_error;

    if (directory_fd < 0 || !valid_staging_name(staging_name)) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT, 0);
    }
    request = request_create(lane, R_RUNTIME_DARWIN_FS_FILE_STAGE_CLEANUP, &native_error);
    if (request == NULL) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    request->parameters.file_stage_cleanup.directory_fd =
        retain_descriptor(directory_fd, &native_error);
    if (request->parameters.file_stage_cleanup.directory_fd < 0) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED, native_error);
    }
    request->parameters.file_stage_cleanup.directory_owned = 1;
    memcpy(request->parameters.file_stage_cleanup.name,
           staging_name,
           R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY);
    request->parameters.file_stage_cleanup.configured = 1;
    return finish_prepared_request(request, UINT64_C(0));
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_full_fsync_directory_at(
    RRuntimeDarwinFsLane *lane, int directory_fd, const char *path, _Bool beneath) {
    RRuntimeDarwinFsRequest *request;
    RRuntimeDarwinFsStartStatus path_status;
    int native_error;

    if (directory_fd < 0) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT, 0);
    }
    request = request_create(lane, R_RUNTIME_DARWIN_FS_FULL_FSYNC_DIRECTORY_AT, &native_error);
    if (request == NULL) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    request->parameters.sync_directory_at.path = copy_path(lane, path, &path_status);
    if (request->parameters.sync_directory_at.path == NULL) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(
            path_status, path_status == R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED ? ENOMEM : 0);
    }
    request->parameters.sync_directory_at.directory_fd =
        retain_descriptor(directory_fd, &native_error);
    if (request->parameters.sync_directory_at.directory_fd < 0) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED, native_error);
    }
    request->parameters.sync_directory_at.directory_owned = 1;
    request->parameters.sync_directory_at.beneath = beneath;
    request->parameters.sync_directory_at.configured = 1;
    return finish_prepared_request(request, UINT64_C(0));
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_prepare_file_stage_late_bound(RRuntimeDarwinFsLane *lane) {
    RRuntimeDarwinFsRequest *request;
    RRuntimeDarwinFsRequest *cleanup;
    RRuntimeDarwinFsPrepareResult cleanup_preparation;
    RRuntimeDarwinFsPrepareResult preparation;
    int native_error;

    request = request_create(lane, R_RUNTIME_DARWIN_FS_FILE_STAGE_CREATE, &native_error);
    if (request == NULL) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    cleanup = request_create(lane, R_RUNTIME_DARWIN_FS_FILE_STAGE_CLEANUP, &native_error);
    if (cleanup == NULL) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    cleanup_preparation = finish_prepared_request(cleanup, UINT64_C(0));
    if (cleanup_preparation.prepared == NULL) {
        r_runtime_darwin_fs_request_release(request);
        return cleanup_preparation;
    }
    request->parameters.file_stage.cleanup_prepared = cleanup_preparation.prepared;
    preparation = finish_prepared_request(request, UINT64_C(0));
    return preparation;
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_prepare_fsync_late_bound(RRuntimeDarwinFsLane *lane, _Bool full_durability) {
    RRuntimeDarwinFsRequest *request;
    int native_error;

    request =
        request_create(lane,
                       full_durability ? R_RUNTIME_DARWIN_FS_FULL_FSYNC : R_RUNTIME_DARWIN_FS_FSYNC,
                       &native_error);
    if (request == NULL) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    return finish_prepared_request(request, UINT64_C(0));
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_staging_rename_late_bound(
    RRuntimeDarwinFsLane *lane, const char *destination_path, _Bool beneath) {
    RRuntimeDarwinFsRequest *request;
    RRuntimeDarwinFsStartStatus path_status;
    int native_error;

    request = request_create(lane, R_RUNTIME_DARWIN_FS_RENAME_AT_NO_REPLACE, &native_error);
    if (request == NULL) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    if (r_runtime_allocator_allocate(lane->allocator,
                                     R_RUNTIME_DARWIN_FS_STAGING_SOURCE_CAPACITY,
                                     _Alignof(char),
                                     (void **)&request->parameters.rename_at.source_path) !=
        R_RUNTIME_ALLOCATION_OK) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, ENOMEM);
    }
    (void)memset(
        request->parameters.rename_at.source_path, 0, R_RUNTIME_DARWIN_FS_STAGING_SOURCE_CAPACITY);
    request->parameters.rename_at.destination_path =
        copy_path(lane, destination_path, &path_status);
    if (request->parameters.rename_at.destination_path == NULL) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(
            path_status, path_status == R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED ? ENOMEM : 0);
    }
    request->parameters.rename_at.flags = RENAME_EXCL;
    if (beneath) {
        request->parameters.rename_at.flags |= RENAME_NOFOLLOW_ANY | RENAME_RESOLVE_BENEATH;
    }
    return finish_prepared_request(request, UINT64_C(0));
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_prepare_full_fsync_directory_at_late_bound(
    RRuntimeDarwinFsLane *lane, const char *path, _Bool beneath) {
    RRuntimeDarwinFsRequest *request;
    RRuntimeDarwinFsStartStatus path_status;
    int native_error;

    request = request_create(lane, R_RUNTIME_DARWIN_FS_FULL_FSYNC_DIRECTORY_AT, &native_error);
    if (request == NULL) {
        return prepare_failure(R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED, native_error);
    }
    request->parameters.sync_directory_at.path = copy_path(lane, path, &path_status);
    if (request->parameters.sync_directory_at.path == NULL) {
        r_runtime_darwin_fs_request_release(request);
        return prepare_failure(
            path_status, path_status == R_RUNTIME_DARWIN_FS_START_ALLOCATION_FAILED ? ENOMEM : 0);
    }
    request->parameters.sync_directory_at.beneath = beneath;
    return finish_prepared_request(request, UINT64_C(0));
}

_Bool r_runtime_darwin_fs_prepared_bind_file_stage(RRuntimeDarwinFsPreparedRequest *prepared,
                                                   int directory_fd) {
    RRuntimeDarwinFsPreparedRequest *cleanup;
    _Bool bound = 0;

    if (directory_fd < 0) {
        return 0;
    }
    if (pthread_mutex_lock(&prepared->mutex) != 0) {
        abort();
    }
    cleanup = prepared->parameters.file_stage.cleanup_prepared;
    if (prepared->operation == R_RUNTIME_DARWIN_FS_FILE_STAGE_CREATE &&
        prepared->state == R_RUNTIME_DARWIN_FS_REQUEST_PREPARED &&
        !prepared->parameters.file_stage.configured && cleanup != NULL) {
        if (pthread_mutex_lock(&cleanup->mutex) != 0) {
            (void)pthread_mutex_unlock(&prepared->mutex);
            abort();
        }
        if (cleanup->operation == R_RUNTIME_DARWIN_FS_FILE_STAGE_CLEANUP &&
            cleanup->state == R_RUNTIME_DARWIN_FS_REQUEST_PREPARED &&
            !cleanup->parameters.file_stage_cleanup.configured) {
            prepared->parameters.file_stage.directory_fd = directory_fd;
            prepared->parameters.file_stage.configured = 1;
            cleanup->parameters.file_stage_cleanup.directory_fd = directory_fd;
            cleanup->parameters.file_stage_cleanup.configured = 1;
            bound = 1;
        }
        if (pthread_mutex_unlock(&cleanup->mutex) != 0) {
            (void)pthread_mutex_unlock(&prepared->mutex);
            abort();
        }
    }
    if (pthread_mutex_unlock(&prepared->mutex) != 0) {
        abort();
    }
    return bound;
}

_Bool r_runtime_darwin_fs_prepared_bind_fsync(RRuntimeDarwinFsPreparedRequest *prepared,
                                              int descriptor) {
    _Bool bound = 0;

    if (descriptor < 0 || pthread_mutex_lock(&prepared->mutex) != 0) {
        return 0;
    }
    if ((prepared->operation == R_RUNTIME_DARWIN_FS_FSYNC ||
         prepared->operation == R_RUNTIME_DARWIN_FS_FULL_FSYNC) &&
        prepared->state == R_RUNTIME_DARWIN_FS_REQUEST_PREPARED &&
        !prepared->parameters.sync.configured) {
        prepared->parameters.sync.descriptor = descriptor;
        prepared->parameters.sync.configured = 1;
        bound = 1;
    }
    if (pthread_mutex_unlock(&prepared->mutex) != 0) {
        abort();
    }
    return bound;
}

_Bool r_runtime_darwin_fs_prepared_bind_staging_rename(RRuntimeDarwinFsPreparedRequest *prepared,
                                                       int directory_fd,
                                                       const char *staging_source) {
    _Bool bound = 0;

    if (directory_fd < 0 || !valid_staging_source(staging_source) ||
        pthread_mutex_lock(&prepared->mutex) != 0) {
        return 0;
    }
    if (prepared->operation == R_RUNTIME_DARWIN_FS_RENAME_AT_NO_REPLACE &&
        prepared->state == R_RUNTIME_DARWIN_FS_REQUEST_PREPARED &&
        !prepared->parameters.rename_at.configured) {
        memcpy(prepared->parameters.rename_at.source_path,
               staging_source,
               R_RUNTIME_DARWIN_FS_STAGING_SOURCE_CAPACITY);
        prepared->parameters.rename_at.source_directory_fd = directory_fd;
        prepared->parameters.rename_at.destination_directory_fd = directory_fd;
        prepared->parameters.rename_at.configured = 1;
        bound = 1;
    }
    if (pthread_mutex_unlock(&prepared->mutex) != 0) {
        abort();
    }
    return bound;
}

_Bool r_runtime_darwin_fs_prepared_bind_full_fsync_directory_at(
    RRuntimeDarwinFsPreparedRequest *prepared, int directory_fd) {
    _Bool bound = 0;

    if (directory_fd < 0 || pthread_mutex_lock(&prepared->mutex) != 0) {
        return 0;
    }
    if (prepared->operation == R_RUNTIME_DARWIN_FS_FULL_FSYNC_DIRECTORY_AT &&
        prepared->state == R_RUNTIME_DARWIN_FS_REQUEST_PREPARED &&
        !prepared->parameters.sync_directory_at.configured) {
        prepared->parameters.sync_directory_at.directory_fd = directory_fd;
        prepared->parameters.sync_directory_at.configured = 1;
        bound = 1;
    }
    if (pthread_mutex_unlock(&prepared->mutex) != 0) {
        abort();
    }
    return bound;
}

RRuntimeDarwinFsSubmitResult
r_runtime_darwin_fs_prepared_activate(RRuntimeDarwinFsPreparedRequest **prepared_slot) {
    RRuntimeDarwinFsRequest *request;
    RRuntimeDarwinFsLane *lane;
    RRuntimeDarwinFsStartStatus status = R_RUNTIME_DARWIN_FS_START_OK;

    request = *prepared_slot;
    lane = request->lane;
    if (pthread_mutex_lock(&lane->mutex) != 0) {
        abort();
    }
    if (pthread_mutex_lock(&request->mutex) != 0) {
        (void)pthread_mutex_unlock(&lane->mutex);
        abort();
    }
    if (lane->stopping) {
        status = R_RUNTIME_DARWIN_FS_START_RUNTIME_STOPPING;
    } else {
        if (request->references == SIZE_MAX) {
            (void)pthread_mutex_unlock(&request->mutex);
            (void)pthread_mutex_unlock(&lane->mutex);
            abort();
        }
        lane->reserved_count -= 1U;
        request->references += 1U;
        lane->next_submission_sequence += 1U;
        request->submission_sequence = lane->next_submission_sequence;
        request->state = R_RUNTIME_DARWIN_FS_REQUEST_QUEUED;
        request->next = NULL;
        if (lane->tail == NULL) {
            lane->head = request;
        } else {
            lane->tail->next = request;
        }
        lane->tail = request;
        lane->queued_count += 1U;
        *prepared_slot = NULL;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        (void)pthread_mutex_unlock(&lane->mutex);
        abort();
    }
    if (status == R_RUNTIME_DARWIN_FS_START_OK) {
        (void)pthread_cond_broadcast(&lane->queue_condition);
    }
    if (pthread_mutex_unlock(&lane->mutex) != 0) {
        abort();
    }
    if (status != R_RUNTIME_DARWIN_FS_START_OK) {
        return submit_failure(status, 0);
    }
    r_runtime_darwin_fs_internal_activate_deadline(request);
    return submit_success(request);
}

_Bool r_runtime_darwin_fs_prepared_set_seek(RRuntimeDarwinFsPreparedRequest *prepared,
                                            off_t offset,
                                            int whence) {
    _Bool changed = 0;

    if ((whence != SEEK_SET && whence != SEEK_END) || pthread_mutex_lock(&prepared->mutex) != 0) {
        return 0;
    }
    if (prepared->operation == R_RUNTIME_DARWIN_FS_SEEK &&
        prepared->state == R_RUNTIME_DARWIN_FS_REQUEST_PREPARED) {
        prepared->parameters.seek.offset = offset;
        prepared->parameters.seek.whence = whence;
        changed = 1;
    }
    if (pthread_mutex_unlock(&prepared->mutex) != 0) {
        abort();
    }
    return changed;
}

RRuntimeDarwinFsSubmitResult
r_runtime_darwin_fs_prepared_activate_close(RRuntimeDarwinFsPreparedRequest **prepared_slot,
                                            int *descriptor) {
    RRuntimeDarwinFsRequest *request;
    RRuntimeDarwinFsSubmitResult result;

    if (*descriptor < 0) {
        return submit_failure(R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT, 0);
    }
    request = *prepared_slot;
    request->parameters.close.descriptor = *descriptor;
    request->parameters.close.owned = 1;
    result = r_runtime_darwin_fs_prepared_activate(prepared_slot);
    if (result.status != R_RUNTIME_DARWIN_FS_START_OK) {
        request->parameters.close.descriptor = -1;
        request->parameters.close.owned = 0;
        return result;
    }
    *descriptor = -1;
    return result;
}

void r_runtime_darwin_fs_prepared_abort(RRuntimeDarwinFsPreparedRequest **prepared_slot) {
    RRuntimeDarwinFsRequest *request;
    RRuntimeDarwinFsLane *lane;

    if (prepared_slot == NULL || *prepared_slot == NULL) {
        return;
    }
    request = *prepared_slot;
    lane = request->lane;
    if (pthread_mutex_lock(&lane->mutex) != 0) {
        abort();
    }
    if (pthread_mutex_lock(&request->mutex) != 0) {
        (void)pthread_mutex_unlock(&lane->mutex);
        abort();
    }
    if (request->state != R_RUNTIME_DARWIN_FS_REQUEST_PREPARED || lane->reserved_count == 0U) {
        (void)pthread_mutex_unlock(&request->mutex);
        (void)pthread_mutex_unlock(&lane->mutex);
        abort();
    }
    if (request->operation == R_RUNTIME_DARWIN_FS_FILE_STAGE_CLEANUP &&
        request->parameters.file_stage_cleanup.name[0] != '\0') {
        (void)pthread_mutex_unlock(&request->mutex);
        (void)pthread_mutex_unlock(&lane->mutex);
        abort();
    }
    lane->reserved_count -= 1U;
    request->state = R_RUNTIME_DARWIN_FS_REQUEST_TERMINAL;
    request->terminal_event = R_RUNTIME_DARWIN_FS_TERMINAL_STOPPING;
    *prepared_slot = NULL;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        (void)pthread_mutex_unlock(&lane->mutex);
        abort();
    }
    if (pthread_mutex_unlock(&lane->mutex) != 0) {
        abort();
    }
    r_runtime_darwin_fs_internal_disarm_deadline(request);
    r_runtime_darwin_fs_request_release(request);
}

RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_open_at(
    RRuntimeDarwinFsLane *lane, int directory_fd, const char *path, int flags, mode_t mode) {
    RRuntimeDarwinFsPrepareResult preparation = r_runtime_darwin_fs_prepare_open_at(
        lane, directory_fd, path, flags, mode, 0, 0, UINT64_C(0));
    RRuntimeDarwinFsSubmitResult result;

    if (preparation.prepared == NULL) {
        return submit_failure(preparation.status, preparation.native_error);
    }
    result = r_runtime_darwin_fs_prepared_activate(&preparation.prepared);
    if (result.status != R_RUNTIME_DARWIN_FS_START_OK) {
        r_runtime_darwin_fs_prepared_abort(&preparation.prepared);
    }
    return result;
}

RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_close(RRuntimeDarwinFsLane *lane,
                                                              int descriptor) {
    RRuntimeDarwinFsPrepareResult preparation =
        r_runtime_darwin_fs_prepare_close(lane, UINT64_C(0));
    RRuntimeDarwinFsSubmitResult result;

    if (descriptor < 0) {
        if (preparation.prepared != NULL) {
            r_runtime_darwin_fs_prepared_abort(&preparation.prepared);
        }
        return submit_failure(R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT, 0);
    }
    if (preparation.prepared == NULL) {
        return submit_failure(preparation.status, preparation.native_error);
    }
    result = r_runtime_darwin_fs_prepared_activate_close(&preparation.prepared, &descriptor);
    if (result.status != R_RUNTIME_DARWIN_FS_START_OK) {
        r_runtime_darwin_fs_prepared_abort(&preparation.prepared);
    }
    return result;
}

RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_fstat(RRuntimeDarwinFsLane *lane,
                                                              int descriptor) {
    RRuntimeDarwinFsPrepareResult preparation =
        r_runtime_darwin_fs_prepare_fstat(lane, descriptor, UINT64_C(0));
    RRuntimeDarwinFsSubmitResult result;

    if (preparation.prepared == NULL) {
        return submit_failure(preparation.status, preparation.native_error);
    }
    result = r_runtime_darwin_fs_prepared_activate(&preparation.prepared);
    if (result.status != R_RUNTIME_DARWIN_FS_START_OK) {
        r_runtime_darwin_fs_prepared_abort(&preparation.prepared);
    }
    return result;
}

RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_fstat_at(RRuntimeDarwinFsLane *lane,
                                                                 int directory_fd,
                                                                 const char *path) {
    RRuntimeDarwinFsPrepareResult preparation =
        r_runtime_darwin_fs_prepare_fstat_at(lane, directory_fd, path, 0, UINT64_C(0));
    RRuntimeDarwinFsSubmitResult result;

    if (preparation.prepared == NULL) {
        return submit_failure(preparation.status, preparation.native_error);
    }
    result = r_runtime_darwin_fs_prepared_activate(&preparation.prepared);
    if (result.status != R_RUNTIME_DARWIN_FS_START_OK) {
        r_runtime_darwin_fs_prepared_abort(&preparation.prepared);
    }
    return result;
}

RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_seek(RRuntimeDarwinFsLane *lane,
                                                             int descriptor,
                                                             off_t offset,
                                                             int whence) {
    RRuntimeDarwinFsPrepareResult preparation =
        r_runtime_darwin_fs_prepare_seek(lane, descriptor, offset, whence, UINT64_C(0));
    RRuntimeDarwinFsSubmitResult result;

    if (preparation.prepared == NULL) {
        return submit_failure(preparation.status, preparation.native_error);
    }
    result = r_runtime_darwin_fs_prepared_activate(&preparation.prepared);
    if (result.status != R_RUNTIME_DARWIN_FS_START_OK) {
        r_runtime_darwin_fs_prepared_abort(&preparation.prepared);
    }
    return result;
}

RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_enumerate(RRuntimeDarwinFsLane *lane,
                                                                  int directory_fd,
                                                                  const struct attrlist *attributes,
                                                                  size_t buffer_size) {
    RRuntimeDarwinFsPrepareResult preparation = r_runtime_darwin_fs_prepare_enumerate(
        lane, directory_fd, attributes, buffer_size, UINT64_C(0));
    RRuntimeDarwinFsSubmitResult result;

    if (preparation.prepared == NULL) {
        return submit_failure(preparation.status, preparation.native_error);
    }
    result = r_runtime_darwin_fs_prepared_activate(&preparation.prepared);
    if (result.status != R_RUNTIME_DARWIN_FS_START_OK) {
        r_runtime_darwin_fs_prepared_abort(&preparation.prepared);
    }
    return result;
}

RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_mkdir_at(RRuntimeDarwinFsLane *lane,
                                                                 int directory_fd,
                                                                 const char *path,
                                                                 mode_t mode) {
    RRuntimeDarwinFsPrepareResult preparation = r_runtime_darwin_fs_prepare_create_directory_at(
        lane, directory_fd, path, mode, 0, 0, UINT64_C(0));
    RRuntimeDarwinFsSubmitResult result;

    if (preparation.prepared == NULL) {
        return submit_failure(preparation.status, preparation.native_error);
    }
    result = r_runtime_darwin_fs_prepared_activate(&preparation.prepared);
    if (result.status != R_RUNTIME_DARWIN_FS_START_OK) {
        r_runtime_darwin_fs_prepared_abort(&preparation.prepared);
    }
    return result;
}

RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_unlink_at(RRuntimeDarwinFsLane *lane,
                                                                  int directory_fd,
                                                                  const char *path,
                                                                  _Bool remove_directory) {
    RRuntimeDarwinFsPrepareResult preparation = r_runtime_darwin_fs_prepare_unlink_at(
        lane, directory_fd, path, remove_directory, 0, UINT64_C(0));
    RRuntimeDarwinFsSubmitResult result;

    if (preparation.prepared == NULL) {
        return submit_failure(preparation.status, preparation.native_error);
    }
    result = r_runtime_darwin_fs_prepared_activate(&preparation.prepared);
    if (result.status != R_RUNTIME_DARWIN_FS_START_OK) {
        r_runtime_darwin_fs_prepared_abort(&preparation.prepared);
    }
    return result;
}

RRuntimeDarwinFsSubmitResult
r_runtime_darwin_fs_submit_rename_at_no_replace(RRuntimeDarwinFsLane *lane,
                                                int source_directory_fd,
                                                const char *source_path,
                                                int destination_directory_fd,
                                                const char *destination_path) {
    RRuntimeDarwinFsPrepareResult preparation =
        r_runtime_darwin_fs_prepare_rename_at_no_replace(lane,
                                                         source_directory_fd,
                                                         source_path,
                                                         destination_directory_fd,
                                                         destination_path,
                                                         0,
                                                         UINT64_C(0));
    RRuntimeDarwinFsSubmitResult result;

    if (preparation.prepared == NULL) {
        return submit_failure(preparation.status, preparation.native_error);
    }
    result = r_runtime_darwin_fs_prepared_activate(&preparation.prepared);
    if (result.status != R_RUNTIME_DARWIN_FS_START_OK) {
        r_runtime_darwin_fs_prepared_abort(&preparation.prepared);
    }
    return result;
}

static RRuntimeDarwinFsSubmitResult
submit_sync(RRuntimeDarwinFsLane *lane, int descriptor, _Bool full_durability) {
    RRuntimeDarwinFsPrepareResult preparation =
        r_runtime_darwin_fs_prepare_fsync(lane, descriptor, full_durability, UINT64_C(0));
    RRuntimeDarwinFsSubmitResult result;

    if (preparation.prepared == NULL) {
        return submit_failure(preparation.status, preparation.native_error);
    }
    result = r_runtime_darwin_fs_prepared_activate(&preparation.prepared);
    if (result.status != R_RUNTIME_DARWIN_FS_START_OK) {
        r_runtime_darwin_fs_prepared_abort(&preparation.prepared);
    }
    return result;
}

RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_fsync(RRuntimeDarwinFsLane *lane,
                                                              int descriptor) {
    return submit_sync(lane, descriptor, 0);
}

RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_full_fsync(RRuntimeDarwinFsLane *lane,
                                                                   int descriptor) {
    return submit_sync(lane, descriptor, 1);
}

RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_ofd_lock(
    RRuntimeDarwinFsLane *lane, int descriptor, short type, off_t start, off_t length) {
    RRuntimeDarwinFsPrepareResult preparation =
        r_runtime_darwin_fs_prepare_ofd_lock(lane, descriptor, type, start, length, UINT64_C(0));
    RRuntimeDarwinFsSubmitResult result;

    if (preparation.prepared == NULL) {
        return submit_failure(preparation.status, preparation.native_error);
    }
    result = r_runtime_darwin_fs_prepared_activate(&preparation.prepared);
    if (result.status != R_RUNTIME_DARWIN_FS_START_OK) {
        r_runtime_darwin_fs_prepared_abort(&preparation.prepared);
    }
    return result;
}

RRuntimeDarwinFsSubmitResult r_runtime_darwin_fs_submit_barrier_fsync(RRuntimeDarwinFsLane *lane,
                                                                      int descriptor) {
    RRuntimeDarwinFsPrepareResult preparation =
        r_runtime_darwin_fs_prepare_barrier_fsync(lane, descriptor, UINT64_C(0));
    RRuntimeDarwinFsSubmitResult result;

    if (preparation.prepared == NULL) {
        return submit_failure(preparation.status, preparation.native_error);
    }
    result = r_runtime_darwin_fs_prepared_activate(&preparation.prepared);
    if (result.status != R_RUNTIME_DARWIN_FS_START_OK) {
        r_runtime_darwin_fs_prepared_abort(&preparation.prepared);
    }
    return result;
}

static _Bool remove_queued_request(RRuntimeDarwinFsLane *lane, RRuntimeDarwinFsRequest *request) {
    RRuntimeDarwinFsRequest *previous = NULL;
    RRuntimeDarwinFsRequest *cursor = lane->head;

    while (cursor != NULL && cursor != request) {
        previous = cursor;
        cursor = cursor->next;
    }
    if (cursor == NULL) {
        return 0;
    }
    if (previous == NULL) {
        lane->head = cursor->next;
    } else {
        previous->next = cursor->next;
    }
    if (lane->tail == cursor) {
        lane->tail = previous;
    }
    cursor->next = NULL;
    lane->queued_count -= 1U;
    return 1;
}

static _Bool request_signal(RRuntimeDarwinFsRequest *request, RRuntimeDarwinFsTerminalEvent event) {
    RRuntimeDarwinFsLane *lane;
    uint64_t event_sequence;
    _Bool accepted = 0;
    _Bool removed = 0;

    if (request->operation == R_RUNTIME_DARWIN_FS_FILE_STAGE_CLEANUP) {
        return 0;
    }
    event_sequence = r_runtime_darwin_event_sequence_next();
    lane = request->lane;
    if (pthread_mutex_lock(&lane->mutex) != 0) {
        abort();
    }
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->state != R_RUNTIME_DARWIN_FS_REQUEST_TERMINAL && request->pending_event == 0) {
        request->pending_event = event;
        request->pending_event_sequence = event_sequence;
        accepted = 1;
        if (lane->accepted_signal_count == UINT64_MAX) {
            abort();
        }
        lane->accepted_signal_count += 1U;
        if (request->state == R_RUNTIME_DARWIN_FS_REQUEST_QUEUED &&
            request->operation != R_RUNTIME_DARWIN_FS_CLOSE) {
            removed = remove_queued_request(lane, request);
            if (removed) {
                request->terminal_event = event;
                request->terminal_event_sequence = event_sequence;
                request->state = R_RUNTIME_DARWIN_FS_REQUEST_TERMINAL;
                request->native_error = 0;
                request->native_return_value = 0;
                (void)pthread_cond_broadcast(&request->condition);
            }
        }
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (pthread_mutex_unlock(&lane->mutex) != 0) {
        abort();
    }
    if (removed) {
        r_runtime_darwin_fs_internal_disarm_deadline(request);
        r_runtime_darwin_fs_internal_schedule_completion(request);
        r_runtime_darwin_fs_request_release(request);
    }
    return accepted;
}

_Bool r_runtime_darwin_fs_request_cancel(RRuntimeDarwinFsRequest *request) {
    return request_signal(request, R_RUNTIME_DARWIN_FS_TERMINAL_CANCELLED);
}

_Bool r_runtime_darwin_fs_request_deadline_expired(RRuntimeDarwinFsRequest *request) {
    return request_signal(request, R_RUNTIME_DARWIN_FS_TERMINAL_TIMED_OUT);
}

RRuntimeDarwinFsRequestState r_runtime_darwin_fs_request_state(RRuntimeDarwinFsRequest *request) {
    RRuntimeDarwinFsRequestState state;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    state = request->state;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return state;
}

RRuntimeDarwinFsResult r_runtime_darwin_fs_request_wait(RRuntimeDarwinFsRequest *request) {
    RRuntimeDarwinFsResult result;

    memset(&result, 0, sizeof(result));
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    while (request->state != R_RUNTIME_DARWIN_FS_REQUEST_TERMINAL) {
        (void)pthread_cond_wait(&request->condition, &request->mutex);
    }
    result.operation = request->operation;
    result.terminal_event = request->terminal_event;
    result.native_error = request->native_error;
    result.operation_native_error = request->operation_native_error;
    result.native_return_value = request->native_return_value;
    result.terminal_event_sequence = request->terminal_event_sequence;
    result.native_event_sequence = request->native_event_sequence;
    result.native_entered = request->native_entered;
    result.committed = request->committed;
    result.metadata = request->metadata;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return result;
}

uint64_t r_runtime_darwin_fs_request_submission_sequence(RRuntimeDarwinFsRequest *request) {
    uint64_t sequence;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    sequence = request->submission_sequence;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return sequence;
}

uint64_t r_runtime_darwin_fs_request_entry_sequence(RRuntimeDarwinFsRequest *request) {
    uint64_t sequence;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    sequence = request->entry_sequence;
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return sequence;
}

int r_runtime_darwin_fs_request_opened_fd(RRuntimeDarwinFsRequest *request) {
    int descriptor = -1;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->state == R_RUNTIME_DARWIN_FS_REQUEST_TERMINAL &&
        request->operation == R_RUNTIME_DARWIN_FS_OPEN_AT &&
        request->terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE &&
        request->native_error == 0) {
        descriptor = request->opened_fd;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return descriptor;
}

_Bool r_runtime_darwin_fs_request_discard_opened_fd(RRuntimeDarwinFsRequest *request) {
    int descriptor = -1;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->state == R_RUNTIME_DARWIN_FS_REQUEST_TERMINAL &&
        request->operation == R_RUNTIME_DARWIN_FS_OPEN_AT &&
        request->terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE &&
        request->native_error == 0) {
        descriptor = request->opened_fd;
        request->opened_fd = -1;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    if (descriptor < 0) {
        return 0;
    }
    (void)close(descriptor);
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    testing_report_open_result_cleanup(descriptor);
#endif
    return 1;
}

int r_runtime_darwin_fs_request_take_opened_fd(RRuntimeDarwinFsRequest *request) {
    int descriptor = -1;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->state == R_RUNTIME_DARWIN_FS_REQUEST_TERMINAL &&
        request->operation == R_RUNTIME_DARWIN_FS_OPEN_AT &&
        request->terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE &&
        request->native_error == 0) {
        descriptor = request->opened_fd;
        request->opened_fd = -1;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return descriptor;
}

_Bool r_runtime_darwin_fs_request_take_file_stage(
    RRuntimeDarwinFsRequest *request,
    int *payload_descriptor,
    char staging_name[R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY],
    RRuntimeDarwinFsPreparedRequest **cleanup_prepared) {
    RRuntimeDarwinFsPreparedRequest *cleanup;
    int descriptor;
    _Bool taken = 0;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->state == R_RUNTIME_DARWIN_FS_REQUEST_TERMINAL &&
        request->operation == R_RUNTIME_DARWIN_FS_FILE_STAGE_CREATE &&
        request->terminal_event == R_RUNTIME_DARWIN_FS_TERMINAL_NATIVE &&
        request->native_error == 0 && request->opened_fd >= 0 &&
        request->parameters.file_stage.owned &&
        request->parameters.file_stage.cleanup_prepared != NULL) {
        cleanup = request->parameters.file_stage.cleanup_prepared;
        if (pthread_mutex_lock(&cleanup->mutex) != 0) {
            (void)pthread_mutex_unlock(&request->mutex);
            abort();
        }
        if (cleanup->state != R_RUNTIME_DARWIN_FS_REQUEST_PREPARED ||
            cleanup->operation != R_RUNTIME_DARWIN_FS_FILE_STAGE_CLEANUP ||
            cleanup->parameters.file_stage_cleanup.name[0] != '\0') {
            (void)pthread_mutex_unlock(&cleanup->mutex);
            (void)pthread_mutex_unlock(&request->mutex);
            abort();
        }
        memcpy(cleanup->parameters.file_stage_cleanup.name,
               request->parameters.file_stage.name,
               R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY);
        if (pthread_mutex_unlock(&cleanup->mutex) != 0) {
            (void)pthread_mutex_unlock(&request->mutex);
            abort();
        }
        descriptor = request->opened_fd;
        request->opened_fd = -1;
        request->parameters.file_stage.owned = 0;
        request->parameters.file_stage.cleanup_prepared = NULL;
        *payload_descriptor = descriptor;
        memcpy(staging_name,
               request->parameters.file_stage.name,
               R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY);
        *cleanup_prepared = cleanup;
        taken = 1;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return taken;
}

int r_runtime_darwin_fs_request_take_unclosed_fd(RRuntimeDarwinFsRequest *request) {
    int descriptor = -1;

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->state == R_RUNTIME_DARWIN_FS_REQUEST_TERMINAL &&
        request->operation == R_RUNTIME_DARWIN_FS_CLOSE && !request->native_entered &&
        request->parameters.close.owned) {
        descriptor = request->parameters.close.descriptor;
        request->parameters.close.descriptor = -1;
        request->parameters.close.owned = 0;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return descriptor;
}

const void *r_runtime_darwin_fs_request_enumeration_data(RRuntimeDarwinFsRequest *request,
                                                         size_t *size) {
    const void *buffer = NULL;

    if (size != NULL) {
        *size = 0U;
    }
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->operation == R_RUNTIME_DARWIN_FS_ENUMERATE &&
        request->state == R_RUNTIME_DARWIN_FS_REQUEST_TERMINAL &&
        request->enumeration_cache_valid) {
        buffer = request->parameters.enumerate.buffer;
        if (size != NULL) {
            *size = request->parameters.enumerate.buffer_size;
        }
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return buffer;
}

int64_t r_runtime_darwin_fs_request_enumeration_entry_count(RRuntimeDarwinFsRequest *request) {
    int64_t entry_count = INT64_C(-1);

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->operation == R_RUNTIME_DARWIN_FS_ENUMERATE &&
        request->state == R_RUNTIME_DARWIN_FS_REQUEST_TERMINAL &&
        request->enumeration_cache_valid) {
        entry_count = request->enumeration_entry_count;
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return entry_count;
}

RRuntimeDarwinFsEnumerationBuffer
r_runtime_darwin_fs_request_take_enumeration_buffer(RRuntimeDarwinFsRequest *request) {
    RRuntimeDarwinFsEnumerationBuffer result = {0};

    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    if (request->operation == R_RUNTIME_DARWIN_FS_ENUMERATE &&
        request->state == R_RUNTIME_DARWIN_FS_REQUEST_TERMINAL &&
        request->enumeration_cache_valid && request->parameters.enumerate.buffer != NULL) {
        result.allocator = request->lane->allocator;
        result.data = request->parameters.enumerate.buffer;
        result.capacity = request->parameters.enumerate.buffer_size;
        result.entry_count = request->enumeration_entry_count;
        request->parameters.enumerate.buffer = NULL;
        request->parameters.enumerate.buffer_size = 0U;
        request->enumeration_cache_valid = 0;
        request->enumeration_entry_count = INT64_C(-1);
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
    return result;
}

void r_runtime_darwin_fs_enumeration_buffer_release(RRuntimeDarwinFsEnumerationBuffer *buffer) {
    if (buffer->data == NULL) {
        return;
    }
    if (buffer->allocator == NULL) {
        abort();
    }
    r_runtime_allocator_deallocate(buffer->data, _Alignof(unsigned char));
    (void)memset(buffer, 0, sizeof(*buffer));
}

#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
void r_runtime_darwin_fs_lane_testing_pause_dequeue(RRuntimeDarwinFsLane *lane, _Bool paused) {
    if (lane == NULL) {
        return;
    }
    if (pthread_mutex_lock(&lane->mutex) != 0) {
        abort();
    }
    lane->testing_pause_dequeue = paused;
    (void)pthread_cond_broadcast(&lane->queue_condition);
    if (pthread_mutex_unlock(&lane->mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_fs_lane_testing_pause_before_native(RRuntimeDarwinFsLane *lane,
                                                          _Bool paused) {
    if (lane == NULL) {
        return;
    }
    if (pthread_mutex_lock(&lane->mutex) != 0) {
        abort();
    }
    lane->testing_pause_before_native = paused;
    (void)pthread_cond_broadcast(&lane->queue_condition);
    if (pthread_mutex_unlock(&lane->mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_fs_lane_testing_pause_after_native(RRuntimeDarwinFsLane *lane, _Bool paused) {
    if (lane == NULL) {
        return;
    }
    if (pthread_mutex_lock(&lane->mutex) != 0) {
        abort();
    }
    lane->testing_pause_after_native = paused;
    (void)pthread_cond_broadcast(&lane->queue_condition);
    if (pthread_mutex_unlock(&lane->mutex) != 0) {
        abort();
    }
}

uint64_t r_runtime_darwin_fs_lane_testing_entry_sequence(RRuntimeDarwinFsLane *lane) {
    uint64_t sequence;

    if (lane == NULL) {
        return 0U;
    }
    if (pthread_mutex_lock(&lane->mutex) != 0) {
        abort();
    }
    sequence = lane->next_entry_sequence;
    if (pthread_mutex_unlock(&lane->mutex) != 0) {
        abort();
    }
    return sequence;
}

uint64_t r_runtime_darwin_fs_lane_testing_native_sequence(RRuntimeDarwinFsLane *lane) {
    uint64_t sequence;

    if (lane == NULL) {
        return UINT64_C(0);
    }
    if (pthread_mutex_lock(&lane->mutex) != 0) {
        abort();
    }
    sequence = lane->next_native_sequence;
    if (pthread_mutex_unlock(&lane->mutex) != 0) {
        abort();
    }
    return sequence;
}

uint64_t r_runtime_darwin_fs_lane_testing_signal_count(RRuntimeDarwinFsLane *lane) {
    uint64_t count;

    if (lane == NULL) {
        return 0U;
    }
    if (pthread_mutex_lock(&lane->mutex) != 0) {
        abort();
    }
    count = lane->accepted_signal_count;
    if (pthread_mutex_unlock(&lane->mutex) != 0) {
        abort();
    }
    return count;
}

uint64_t r_runtime_darwin_fs_testing_open_result_cleanup_count(void) {
    return atomic_load_explicit(&testing_open_result_cleanup_count, memory_order_acquire);
}

int r_runtime_darwin_fs_testing_last_open_result_cleanup_fd(void) {
    return atomic_load_explicit(&testing_last_open_result_cleanup_fd, memory_order_relaxed);
}

void r_runtime_darwin_fs_request_testing_wait_for_state(RRuntimeDarwinFsRequest *request,
                                                        RRuntimeDarwinFsRequestState state) {
    if (request == NULL) {
        return;
    }
    if (pthread_mutex_lock(&request->mutex) != 0) {
        abort();
    }
    while (request->state < state) {
        (void)pthread_cond_wait(&request->condition, &request->mutex);
    }
    if (pthread_mutex_unlock(&request->mutex) != 0) {
        abort();
    }
}
#endif
