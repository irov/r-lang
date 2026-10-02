#include "r_runtime_darwin_fs_lane.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

static pthread_mutex_t r_runtime_darwin_fs_service_mutex = PTHREAD_MUTEX_INITIALIZER;
static RRuntimeDarwinFsLane *r_runtime_darwin_fs_service_lane;
static int r_runtime_darwin_fs_service_current_directory = -1;

static RRuntimeDarwinFsPrepareResult service_prepare_failure(void) {
    RRuntimeDarwinFsPrepareResult result;

    result.prepared = NULL;
    result.status = R_RUNTIME_DARWIN_FS_START_RUNTIME_STOPPING;
    result.native_error = 0;
    return result;
}

RRuntimeDarwinFsServiceStartResult r_runtime_darwin_fs_service_start(RRuntimeAllocator *allocator,
                                                                     size_t capacity) {
    RRuntimeDarwinFsServiceStartResult result;
    RRuntimeDarwinFsLaneCreateResult lane_result;
    int directory_fd;

    result.status = R_RUNTIME_DARWIN_FS_START_INVALID_ARGUMENT;
    result.native_error = 0;
    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane != NULL) {
        if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
            abort();
        }
        return result;
    }
    directory_fd = open(".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (directory_fd < 0) {
        result.status = R_RUNTIME_DARWIN_FS_START_NATIVE_RETAIN_FAILED;
        result.native_error = errno;
        if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
            abort();
        }
        return result;
    }
    lane_result = r_runtime_darwin_fs_lane_create(allocator, capacity);
    if (lane_result.status != R_RUNTIME_DARWIN_FS_START_OK) {
        (void)close(directory_fd);
        result.status = lane_result.status;
        result.native_error = lane_result.native_error;
        if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
            abort();
        }
        return result;
    }
    r_runtime_darwin_fs_service_lane = lane_result.lane;
    r_runtime_darwin_fs_service_current_directory = directory_fd;
    result.status = R_RUNTIME_DARWIN_FS_START_OK;
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

void r_runtime_darwin_fs_service_stop(void) {
    RRuntimeDarwinFsLane *lane;
    int directory_fd;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    lane = r_runtime_darwin_fs_service_lane;
    directory_fd = r_runtime_darwin_fs_service_current_directory;
    r_runtime_darwin_fs_service_lane = NULL;
    r_runtime_darwin_fs_service_current_directory = -1;
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (lane != NULL) {
        r_runtime_darwin_fs_lane_destroy(lane);
    }
    if (directory_fd >= 0) {
        (void)close(directory_fd);
    }
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_open(const char *path,
                                         int flags,
                                         mode_t mode,
                                         _Bool follow_final_symlink,
                                         uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_open_at(r_runtime_darwin_fs_service_lane,
                                                     r_runtime_darwin_fs_service_current_directory,
                                                     path,
                                                     flags,
                                                     mode,
                                                     follow_final_symlink,
                                                     0,
                                                     timeout_nanoseconds);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_open_beneath(int directory_fd,
                                                 const char *relative_path,
                                                 int flags,
                                                 mode_t mode,
                                                 uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_open_at(r_runtime_darwin_fs_service_lane,
                                                     directory_fd,
                                                     relative_path,
                                                     flags,
                                                     mode,
                                                     0,
                                                     1,
                                                     timeout_nanoseconds);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_metadata(const char *path, uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_fstat_at(r_runtime_darwin_fs_service_lane,
                                                      r_runtime_darwin_fs_service_current_directory,
                                                      path,
                                                      0,
                                                      timeout_nanoseconds);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_file_metadata(int descriptor, uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_fstat(
            r_runtime_darwin_fs_service_lane, descriptor, timeout_nanoseconds);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_service_prepare_metadata_beneath(
    int directory_fd, const char *relative_path, uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_fstat_at(
            r_runtime_darwin_fs_service_lane, directory_fd, relative_path, 1, timeout_nanoseconds);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_service_prepare_create_directory(
    const char *path, mode_t mode, _Bool recursive, uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_create_directory_at(
            r_runtime_darwin_fs_service_lane,
            r_runtime_darwin_fs_service_current_directory,
            path,
            mode,
            recursive,
            0,
            timeout_nanoseconds);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_create_directory_beneath(int directory_fd,
                                                             const char *relative_path,
                                                             mode_t mode,
                                                             _Bool recursive,
                                                             uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_create_directory_at(r_runtime_darwin_fs_service_lane,
                                                                 directory_fd,
                                                                 relative_path,
                                                                 mode,
                                                                 recursive,
                                                                 1,
                                                                 timeout_nanoseconds);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_remove_beneath(int directory_fd,
                                                   const char *relative_path,
                                                   _Bool remove_directory,
                                                   uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_unlink_at(r_runtime_darwin_fs_service_lane,
                                                       directory_fd,
                                                       relative_path,
                                                       remove_directory,
                                                       1,
                                                       timeout_nanoseconds);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_rename_beneath(int source_directory_fd,
                                                   const char *source_path,
                                                   int destination_directory_fd,
                                                   const char *destination_path,
                                                   uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_rename_at_no_replace(r_runtime_darwin_fs_service_lane,
                                                                  source_directory_fd,
                                                                  source_path,
                                                                  destination_directory_fd,
                                                                  destination_path,
                                                                  1,
                                                                  timeout_nanoseconds);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_close(uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_close(r_runtime_darwin_fs_service_lane,
                                                   timeout_nanoseconds);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_service_prepare_seek(
    int descriptor, off_t offset, int whence, uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_seek(
            r_runtime_darwin_fs_service_lane, descriptor, offset, whence, timeout_nanoseconds);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_enumerate(int directory_fd,
                                              const struct attrlist *attributes,
                                              size_t buffer_size,
                                              uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_enumerate(r_runtime_darwin_fs_service_lane,
                                                       directory_fd,
                                                       attributes,
                                                       buffer_size,
                                                       timeout_nanoseconds);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_barrier_fsync(int descriptor, uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_barrier_fsync(
            r_runtime_darwin_fs_service_lane, descriptor, timeout_nanoseconds);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_service_prepare_ofd_lock(
    int descriptor, short type, off_t start, off_t length, uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_ofd_lock(
            r_runtime_darwin_fs_service_lane, descriptor, type, start, length, timeout_nanoseconds);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_service_prepare_fsync(
    int descriptor, _Bool full_durability, uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_fsync(
            r_runtime_darwin_fs_service_lane, descriptor, full_durability, timeout_nanoseconds);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_file_stage(int directory_fd, uint64_t timeout_nanoseconds) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_file_stage(
            r_runtime_darwin_fs_service_lane, directory_fd, timeout_nanoseconds);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_file_stage_cleanup(int directory_fd, const char *staging_name) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_file_stage_cleanup(
            r_runtime_darwin_fs_service_lane, directory_fd, staging_name);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_full_fsync_directory_beneath(int directory_fd,
                                                                 const char *relative_path) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_full_fsync_directory_at(
            r_runtime_darwin_fs_service_lane, directory_fd, relative_path, 1);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult r_runtime_darwin_fs_service_prepare_file_stage_late_bound(void) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result =
            r_runtime_darwin_fs_prepare_file_stage_late_bound(r_runtime_darwin_fs_service_lane);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_fsync_late_bound(_Bool full_durability) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_fsync_late_bound(r_runtime_darwin_fs_service_lane,
                                                              full_durability);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_staging_rename_late_bound(const char *destination_path,
                                                              _Bool beneath) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_staging_rename_late_bound(
            r_runtime_darwin_fs_service_lane, destination_path, beneath);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

RRuntimeDarwinFsPrepareResult
r_runtime_darwin_fs_service_prepare_full_fsync_directory_at_late_bound(const char *path,
                                                                       _Bool beneath) {
    RRuntimeDarwinFsPrepareResult result;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    if (r_runtime_darwin_fs_service_lane == NULL) {
        result = service_prepare_failure();
    } else {
        result = r_runtime_darwin_fs_prepare_full_fsync_directory_at_late_bound(
            r_runtime_darwin_fs_service_lane, path, beneath);
    }
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return result;
}

#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
void r_runtime_darwin_fs_service_testing_pause_dequeue(_Bool paused) {
    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    r_runtime_darwin_fs_lane_testing_pause_dequeue(r_runtime_darwin_fs_service_lane, paused);
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_fs_service_testing_pause_before_native(_Bool paused) {
    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    r_runtime_darwin_fs_lane_testing_pause_before_native(r_runtime_darwin_fs_service_lane, paused);
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
}

void r_runtime_darwin_fs_service_testing_pause_after_native(_Bool paused) {
    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    r_runtime_darwin_fs_lane_testing_pause_after_native(r_runtime_darwin_fs_service_lane, paused);
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
}

uint64_t r_runtime_darwin_fs_service_testing_entry_sequence(void) {
    uint64_t sequence;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    sequence = r_runtime_darwin_fs_lane_testing_entry_sequence(r_runtime_darwin_fs_service_lane);
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return sequence;
}

uint64_t r_runtime_darwin_fs_service_testing_native_sequence(void) {
    uint64_t sequence;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    sequence = r_runtime_darwin_fs_lane_testing_native_sequence(r_runtime_darwin_fs_service_lane);
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return sequence;
}

uint64_t r_runtime_darwin_fs_service_testing_signal_count(void) {
    uint64_t count;

    if (pthread_mutex_lock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    count = r_runtime_darwin_fs_lane_testing_signal_count(r_runtime_darwin_fs_service_lane);
    if (pthread_mutex_unlock(&r_runtime_darwin_fs_service_mutex) != 0) {
        abort();
    }
    return count;
}
#endif
