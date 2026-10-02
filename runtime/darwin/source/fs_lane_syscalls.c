#include "fs_lane_internal.h"

#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/attr.h>
#include <sys/stat.h>
#include <unistd.h>

#if !defined(O_RESOLVE_BENEATH) || !defined(O_NOFOLLOW_ANY) ||                                     \
    !defined(AT_SYMLINK_NOFOLLOW_ANY) || !defined(AT_RESOLVE_BENEATH) ||                           \
    !defined(RENAME_NOFOLLOW_ANY) || !defined(RENAME_RESOLVE_BENEATH)
#error "the arm64-apple-darwin filesystem adapter requires atomic beneath resolution flags"
#endif

#define R_RUNTIME_DARWIN_FS_OPEN_OR_CREATE_RETRY_LIMIT 16U

static _Atomic uint64_t r_runtime_darwin_fs_stage_sequence;

_Static_assert(sizeof(R_RUNTIME_DARWIN_FS_STAGING_PREFIX) + 16U ==
                   R_RUNTIME_DARWIN_FS_STAGING_NAME_CAPACITY,
               "staging name capacity must include prefix, hex token and NUL");

#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
static _Atomic _Bool r_runtime_darwin_fs_force_open_or_create_race;
static _Atomic unsigned int r_runtime_darwin_fs_forced_staging_collisions;
static _Atomic _Bool r_runtime_darwin_fs_pause_after_staging;
static _Atomic _Bool r_runtime_darwin_fs_staging_reached;
static _Atomic uint64_t r_runtime_darwin_fs_staging_cleanup_count;

void r_runtime_darwin_fs_testing_force_open_or_create_race(_Bool enabled) {
    atomic_store_explicit(
        &r_runtime_darwin_fs_force_open_or_create_race, enabled, memory_order_relaxed);
}

void r_runtime_darwin_fs_testing_force_staging_collisions(unsigned int count) {
    atomic_store_explicit(
        &r_runtime_darwin_fs_forced_staging_collisions, count, memory_order_relaxed);
}

void r_runtime_darwin_fs_testing_pause_after_staging(_Bool paused) {
    atomic_store_explicit(&r_runtime_darwin_fs_pause_after_staging, paused, memory_order_release);
}

_Bool r_runtime_darwin_fs_testing_staging_reached(void) {
    return atomic_load_explicit(&r_runtime_darwin_fs_staging_reached, memory_order_acquire);
}

uint64_t r_runtime_darwin_fs_testing_staging_cleanup_count(void) {
    return atomic_load_explicit(&r_runtime_darwin_fs_staging_cleanup_count, memory_order_relaxed);
}

static _Bool force_open_or_create_race(void) {
    return atomic_load_explicit(&r_runtime_darwin_fs_force_open_or_create_race,
                                memory_order_relaxed);
}

static _Bool force_staging_collision(void) {
    unsigned int remaining =
        atomic_load_explicit(&r_runtime_darwin_fs_forced_staging_collisions, memory_order_relaxed);

    while (remaining != 0U) {
        if (atomic_compare_exchange_weak_explicit(&r_runtime_darwin_fs_forced_staging_collisions,
                                                  &remaining,
                                                  remaining - 1U,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
            return 1;
        }
    }
    return 0;
}

static void pause_after_staging(void) {
    atomic_store_explicit(&r_runtime_darwin_fs_staging_reached, 1, memory_order_release);
    while (atomic_load_explicit(&r_runtime_darwin_fs_pause_after_staging, memory_order_acquire)) {
        (void)sched_yield();
    }
    atomic_store_explicit(&r_runtime_darwin_fs_staging_reached, 0, memory_order_release);
}
#else
static _Bool force_open_or_create_race(void) {
    return 0;
}

static _Bool force_staging_collision(void) {
    return 0;
}

static void pause_after_staging(void) {
}
#endif

static RRuntimeDarwinFsNativeResult native_result(void) {
    RRuntimeDarwinFsNativeResult result;

    memset(&result, 0, sizeof(result));
    result.return_value = -1;
    result.opened_fd = -1;
    return result;
}

static void capture_int_result(RRuntimeDarwinFsNativeResult *result, int value) {
    result->return_value = (int64_t)value;
    if (value < 0) {
        result->native_error = errno;
    }
}

static void close_retained_descriptor(int *descriptor) {
    if (*descriptor >= 0) {
        (void)close(*descriptor);
        *descriptor = -1;
    }
}

static void write_hex_u64(char *destination, uint64_t value) {
    static const char digits[] = "0123456789abcdef";
    size_t index;

    for (index = 0U; index < 16U; ++index) {
        const unsigned int shift = (unsigned int)((15U - index) * 4U);

        destination[index] = digits[(value >> shift) & UINT64_C(0x0f)];
    }
}

static void staging_name(char *name) {
    static const char prefix[] = R_RUNTIME_DARWIN_FS_STAGING_PREFIX;
    const uint64_t sequence = atomic_fetch_add_explicit(&r_runtime_darwin_fs_stage_sequence,
                                                        UINT64_C(1),
                                                        memory_order_relaxed) +
                              UINT64_C(1);

    memcpy(name, prefix, sizeof(prefix) - 1U);
    write_hex_u64(name + sizeof(prefix) - 1U, sequence);
    name[sizeof(prefix) - 1U + 16U] = '\0';
}

static void cleanup_staging_directory(int directory_fd, const char *name) {
    int value;

    do {
        value = unlinkat(
            directory_fd, name, AT_REMOVEDIR | AT_SYMLINK_NOFOLLOW_ANY | AT_RESOLVE_BENEATH);
    } while (value != 0 && errno == EINTR);
    if (value != 0) {
        abort();
    }
#if defined(R_RUNTIME_DARWIN_FS_LANE_TESTING)
    (void)atomic_fetch_add_explicit(
        &r_runtime_darwin_fs_staging_cleanup_count, UINT64_C(1), memory_order_relaxed);
#endif
}

void r_runtime_darwin_fs_internal_cleanup_staged_file(int directory_fd, const char *name) {
    int staging_fd;
    int value;

    staging_fd = openat(directory_fd,
                        name,
                        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW_ANY |
                            O_RESOLVE_BENEATH);
    if (staging_fd < 0) {
        abort();
    }
    do {
        value = unlinkat(staging_fd,
                         R_RUNTIME_DARWIN_FS_STAGING_PAYLOAD_NAME,
                         AT_SYMLINK_NOFOLLOW_ANY | AT_RESOLVE_BENEATH);
    } while (value != 0 && errno == EINTR);
    if (value != 0 && errno != ENOENT) {
        (void)close(staging_fd);
        abort();
    }
    if (close(staging_fd) != 0) {
        abort();
    }
    cleanup_staging_directory(directory_fd, name);
}

static int create_staging_directory(int directory_fd, mode_t mode, char *name) {
    unsigned int attempt;

    for (attempt = 0U; attempt < R_RUNTIME_DARWIN_FS_CREATE_RETRY_LIMIT; ++attempt) {
        staging_name(name);
        if (force_staging_collision()) {
            continue;
        }
        if (mkdirat(directory_fd, name, mode) == 0) {
            return 0;
        }
        if (errno != EEXIST) {
            return -1;
        }
    }
    errno = EAGAIN;
    return -1;
}

static int execute_create_file_stage(RRuntimeDarwinFsRequest *request) {
    int directory_fd = request->parameters.file_stage.directory_fd;
    char *name = request->parameters.file_stage.name;
    int staging_fd;
    int payload_fd;
    int native_error;

    if (r_runtime_darwin_fs_internal_has_pending_event(request)) {
        errno = ECANCELED;
        return -1;
    }
    if (create_staging_directory(directory_fd, S_IRWXU, name) != 0) {
        return -1;
    }
    pause_after_staging();
    if (r_runtime_darwin_fs_internal_has_pending_event(request)) {
        cleanup_staging_directory(directory_fd, name);
        errno = ECANCELED;
        return -1;
    }
    staging_fd = openat(directory_fd,
                        name,
                        O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW_ANY |
                            O_RESOLVE_BENEATH);
    if (staging_fd < 0) {
        native_error = errno;
        cleanup_staging_directory(directory_fd, name);
        errno = native_error;
        return -1;
    }
    payload_fd = openat(staging_fd,
                        R_RUNTIME_DARWIN_FS_STAGING_PAYLOAD_NAME,
                        O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW_ANY |
                            O_RESOLVE_BENEATH,
                        S_IRUSR | S_IWUSR);
    native_error = errno;
    if (close(staging_fd) != 0 && payload_fd >= 0) {
        native_error = errno;
        (void)close(payload_fd);
        payload_fd = -1;
    }
    if (payload_fd < 0) {
        r_runtime_darwin_fs_internal_cleanup_staged_file(directory_fd, name);
        errno = native_error;
        return -1;
    }
    if (r_runtime_darwin_fs_internal_has_pending_event(request)) {
        (void)close(payload_fd);
        r_runtime_darwin_fs_internal_cleanup_staged_file(directory_fd, name);
        errno = ECANCELED;
        return -1;
    }
    request->parameters.file_stage.owned = 1;
    return payload_fd;
}

static int publish_staged_directory(RRuntimeDarwinFsRequest *request,
                                    const char *destination,
                                    _Bool *published) {
    char name[sizeof(R_RUNTIME_DARWIN_FS_STAGING_PREFIX) + 16U];
    int directory_fd = request->parameters.mkdir_at.directory_fd;
    int native_error;

    *published = 0;
    if (r_runtime_darwin_fs_internal_has_pending_event(request)) {
        errno = ECANCELED;
        return -1;
    }
    if (create_staging_directory(directory_fd, S_IRWXU, name) != 0) {
        return -1;
    }
    pause_after_staging();
    if (r_runtime_darwin_fs_internal_has_pending_event(request)) {
        cleanup_staging_directory(directory_fd, name);
        errno = ECANCELED;
        return -1;
    }
    if (renameatx_np(directory_fd,
                     name,
                     directory_fd,
                     destination,
                     RENAME_EXCL | RENAME_NOFOLLOW_ANY | RENAME_RESOLVE_BENEATH) == 0) {
        *published = 1;
        return 0;
    }
    native_error = errno;
    cleanup_staging_directory(directory_fd, name);
    errno = native_error;
    return -1;
}

static _Bool only_separators_follow(const char *path) {
    while (*path != '\0') {
        if (*path != '/') {
            return 0;
        }
        path += 1;
    }
    return 1;
}

static int verify_ordinary_directory(int directory_fd, const char *path) {
    struct stat metadata;

    if (fstatat(directory_fd, path, &metadata, 0) != 0) {
        return -1;
    }
    if (!S_ISDIR(metadata.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    return 0;
}

static int execute_recursive_ordinary_create(RRuntimeDarwinFsRequest *request, _Bool *committed) {
    char *path = request->parameters.mkdir_at.path;
    size_t index;

    *committed = 0;
    for (index = 0U;; ++index) {
        char saved;
        _Bool final_component;
        _Bool created;
        int value;
        int native_error;

        if (path[index] != '/' && path[index] != '\0') {
            continue;
        }
        if (index == 0U || (path[index] == '\0' && path[index - 1U] == '/')) {
            if (path[index] == '\0') {
                return verify_ordinary_directory(request->parameters.mkdir_at.directory_fd, path);
            }
            continue;
        }
        saved = path[index];
        final_component = only_separators_follow(path + index);
        path[index] = '\0';
        if (r_runtime_darwin_fs_internal_has_pending_event(request)) {
            path[index] = saved;
            errno = ECANCELED;
            return -1;
        }
        value = mkdirat(
            request->parameters.mkdir_at.directory_fd, path, request->parameters.mkdir_at.mode);
        created = value == 0;
        native_error = errno;
        if (value != 0 && native_error == EEXIST) {
            value = verify_ordinary_directory(request->parameters.mkdir_at.directory_fd, path);
            native_error = errno;
        }
        path[index] = saved;
        if (value != 0) {
            errno = native_error;
            return -1;
        }
        if (final_component) {
            *committed = created;
            return 0;
        }
        if (r_runtime_darwin_fs_internal_has_pending_event(request)) {
            errno = ECANCELED;
            return -1;
        }
    }
}

static int verify_beneath_directory(int directory_fd, const char *path, _Bool final_component) {
    int descriptor = openat(directory_fd,
                            path,
                            O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NONBLOCK | O_NOFOLLOW_ANY |
                                O_RESOLVE_BENEATH);
    int native_error;

    if (descriptor >= 0) {
        (void)close(descriptor);
        return 0;
    }
    native_error = errno;
    if (final_component && native_error == ELOOP) {
        struct stat metadata;
        int stat_value;

        stat_value = fstatat(directory_fd,
                             path,
                             &metadata,
                             AT_SYMLINK_NOFOLLOW | AT_SYMLINK_NOFOLLOW_ANY | AT_RESOLVE_BENEATH);
        if (stat_value == 0 && S_ISLNK(metadata.st_mode)) {
            errno = ENOTDIR;
            return -1;
        }
        if (stat_value != 0) {
            native_error = errno;
        }
    }
    errno = native_error;
    return -1;
}

static int execute_recursive_beneath_create(RRuntimeDarwinFsRequest *request, _Bool *committed) {
    char *path = request->parameters.mkdir_at.path;
    size_t index;

    *committed = 0;
    for (index = 0U;; ++index) {
        char saved;
        _Bool final_component;
        unsigned int attempt;

        if (path[index] != '/' && path[index] != '\0') {
            continue;
        }
        saved = path[index];
        final_component = saved == '\0';
        path[index] = '\0';
        for (attempt = 0U; attempt < R_RUNTIME_DARWIN_FS_CREATE_RETRY_LIMIT; ++attempt) {
            _Bool published = 0;
            int native_error;

            if (r_runtime_darwin_fs_internal_has_pending_event(request)) {
                path[index] = saved;
                errno = ECANCELED;
                return -1;
            }
            if (verify_beneath_directory(
                    request->parameters.mkdir_at.directory_fd, path, final_component) == 0) {
                path[index] = saved;
                if (final_component) {
                    return 0;
                }
                break;
            }
            native_error = errno;
            if (native_error != ENOENT) {
                path[index] = saved;
                errno = native_error;
                return -1;
            }
            if (publish_staged_directory(request, path, &published) == 0) {
                path[index] = saved;
                if (final_component) {
                    *committed = published;
                    return 0;
                }
                if (r_runtime_darwin_fs_internal_has_pending_event(request)) {
                    errno = ECANCELED;
                    return -1;
                }
                break;
            }
            native_error = errno;
            if (native_error != EEXIST) {
                path[index] = saved;
                errno = native_error;
                return -1;
            }
        }
        if (attempt == R_RUNTIME_DARWIN_FS_CREATE_RETRY_LIMIT) {
            path[index] = saved;
            errno = EAGAIN;
            return -1;
        }
        path[index] = saved;
        if (final_component) {
            return 0;
        }
    }
}

static int execute_create_directory(RRuntimeDarwinFsRequest *request, _Bool *committed) {
    int value;

    *committed = 0;
    if (r_runtime_darwin_fs_internal_has_pending_event(request)) {
        errno = ECANCELED;
        return -1;
    }
    if (!request->parameters.mkdir_at.recursive) {
        if (request->parameters.mkdir_at.beneath) {
            return publish_staged_directory(request, request->parameters.mkdir_at.path, committed);
        }
        value = mkdirat(request->parameters.mkdir_at.directory_fd,
                        request->parameters.mkdir_at.path,
                        request->parameters.mkdir_at.mode);
        *committed = value == 0;
        return value;
    }
    if (request->parameters.mkdir_at.beneath) {
        return execute_recursive_beneath_create(request, committed);
    }
    return execute_recursive_ordinary_create(request, committed);
}

static int validate_opened_kind(int descriptor, int flags) {
    struct stat metadata;

    if (fstat(descriptor, &metadata) != 0) {
        return errno;
    }
    if ((flags & O_DIRECTORY) != 0) {
        return S_ISDIR(metadata.st_mode) ? 0 : ENOTDIR;
    }
    if (S_ISDIR(metadata.st_mode)) {
        return EISDIR;
    }
    return S_ISREG(metadata.st_mode) ? 0 : EINVAL;
}

static int open_final_component(int directory_fd,
                                const char *path,
                                int flags,
                                mode_t mode,
                                _Bool follow_final_symlink,
                                int policy_flags,
                                _Bool *created) {
    int native_flags = flags | O_CLOEXEC | O_NONBLOCK;
    int descriptor;
    int validation_error;
    unsigned int attempt;

    if (!follow_final_symlink && (policy_flags & O_NOFOLLOW_ANY) == 0) {
        native_flags |= O_NOFOLLOW;
    }
    native_flags |= policy_flags;
    *created = 0;
    if ((native_flags & O_CREAT) != 0 && (native_flags & O_EXCL) == 0) {
        for (attempt = 0U; attempt < R_RUNTIME_DARWIN_FS_OPEN_OR_CREATE_RETRY_LIMIT; ++attempt) {
            descriptor = openat(directory_fd, path, native_flags | O_EXCL, mode);
            if (descriptor >= 0) {
                *created = 1;
                break;
            }
            if (errno != EEXIST) {
                return -1;
            }
            if (force_open_or_create_race()) {
                descriptor = -1;
                errno = ENOENT;
            } else {
                descriptor = openat(directory_fd, path, native_flags & ~O_CREAT, mode);
            }
            if (descriptor >= 0 || errno != ENOENT) {
                break;
            }
        }
        if (attempt == R_RUNTIME_DARWIN_FS_OPEN_OR_CREATE_RETRY_LIMIT) {
            errno = EAGAIN;
            return -1;
        }
    } else {
        descriptor = openat(directory_fd, path, native_flags, mode);
        *created = descriptor >= 0 && (native_flags & (O_CREAT | O_EXCL)) == (O_CREAT | O_EXCL);
    }
    if (descriptor < 0) {
        return -1;
    }
    validation_error = validate_opened_kind(descriptor, flags);
    if (validation_error != 0) {
        (void)close(descriptor);
        errno = validation_error;
        return -1;
    }
    return descriptor;
}

static int open_beneath(RRuntimeDarwinFsRequest *request, _Bool *created) {
    return open_final_component(request->parameters.open_at.directory_fd,
                                request->parameters.open_at.path,
                                request->parameters.open_at.flags,
                                request->parameters.open_at.mode,
                                0,
                                O_RESOLVE_BENEATH | O_NOFOLLOW_ANY,
                                created);
}

static int execute_open_at(RRuntimeDarwinFsRequest *request, _Bool *created) {
    if (request->parameters.open_at.beneath) {
        return open_beneath(request, created);
    }
    return open_final_component(request->parameters.open_at.directory_fd,
                                request->parameters.open_at.path,
                                request->parameters.open_at.flags,
                                request->parameters.open_at.mode,
                                request->parameters.open_at.follow_final_symlink,
                                0,
                                created);
}

RRuntimeDarwinFsNativeResult
r_runtime_darwin_fs_internal_execute(RRuntimeDarwinFsRequest *request) {
    RRuntimeDarwinFsNativeResult result = native_result();

    switch (request->operation) {
    case R_RUNTIME_DARWIN_FS_OPEN_AT: {
        _Bool created = 0;
        int descriptor = execute_open_at(request, &created);
        capture_int_result(&result, descriptor);
        if (descriptor >= 0) {
            result.opened_fd = descriptor;
            result.committed = created || (request->parameters.open_at.flags & O_TRUNC) != 0;
        }
        break;
    }
    case R_RUNTIME_DARWIN_FS_CLOSE: {
        int descriptor = request->parameters.close.descriptor;
        int value = close(descriptor);
        capture_int_result(&result, value);
        request->parameters.close.descriptor = -1;
        request->parameters.close.owned = 0;
        result.committed = value == 0;
        break;
    }
    case R_RUNTIME_DARWIN_FS_FSTAT: {
        struct stat metadata;
        int value = fstat(request->parameters.fstat.descriptor, &metadata);
        capture_int_result(&result, value);
        if (value == 0) {
            result.metadata = metadata;
        }
        break;
    }
    case R_RUNTIME_DARWIN_FS_FSTAT_AT: {
        struct stat metadata;
        int value = fstatat(request->parameters.fstat_at.directory_fd,
                            request->parameters.fstat_at.path,
                            &metadata,
                            request->parameters.fstat_at.flags);
        capture_int_result(&result, value);
        if (value == 0) {
            result.metadata = metadata;
        }
        break;
    }
    case R_RUNTIME_DARWIN_FS_SEEK: {
        off_t value = lseek(request->parameters.seek.descriptor,
                            request->parameters.seek.offset,
                            request->parameters.seek.whence);
        result.return_value = (int64_t)value;
        if (value == (off_t)-1) {
            result.native_error = errno;
        } else {
            result.committed = 1;
        }
        break;
    }
    case R_RUNTIME_DARWIN_FS_ENUMERATE: {
        int value = getattrlistbulk(request->parameters.enumerate.directory_fd,
                                    &request->parameters.enumerate.attributes,
                                    request->parameters.enumerate.buffer,
                                    request->parameters.enumerate.buffer_size,
                                    FSOPT_PACK_INVAL_ATTRS);
        capture_int_result(&result, value);
        break;
    }
    case R_RUNTIME_DARWIN_FS_MKDIR_AT: {
        _Bool committed = 0;
        int value = execute_create_directory(request, &committed);
        capture_int_result(&result, value);
        result.committed = value == 0 && committed;
        break;
    }
    case R_RUNTIME_DARWIN_FS_UNLINK_AT: {
        int value = unlinkat(request->parameters.unlink_at.directory_fd,
                             request->parameters.unlink_at.path,
                             request->parameters.unlink_at.flags);
        capture_int_result(&result, value);
        result.committed = value == 0;
        break;
    }
    case R_RUNTIME_DARWIN_FS_RENAME_AT_NO_REPLACE: {
        int value = renameatx_np(request->parameters.rename_at.source_directory_fd,
                                 request->parameters.rename_at.source_path,
                                 request->parameters.rename_at.destination_directory_fd,
                                 request->parameters.rename_at.destination_path,
                                 request->parameters.rename_at.flags);
        capture_int_result(&result, value);
        result.committed = value == 0;
        break;
    }
    case R_RUNTIME_DARWIN_FS_FSYNC: {
        int value = fsync(request->parameters.sync.descriptor);
        capture_int_result(&result, value);
        result.committed = value == 0;
        break;
    }
    case R_RUNTIME_DARWIN_FS_FULL_FSYNC: {
        int value = fcntl(request->parameters.sync.descriptor, F_FULLFSYNC);
        capture_int_result(&result, value);
        result.committed = value == 0;
        break;
    }
    case R_RUNTIME_DARWIN_FS_BARRIER_FSYNC: {
        int value = fcntl(request->parameters.sync.descriptor, F_BARRIERFSYNC);
        capture_int_result(&result, value);
        result.committed = value == 0;
        break;
    }
    case R_RUNTIME_DARWIN_FS_OFD_LOCK: {
        struct flock range;
        int value;

        (void)memset(&range, 0, sizeof(range));
        range.l_type = request->parameters.lock.type;
        range.l_whence = SEEK_SET;
        range.l_start = request->parameters.lock.start;
        range.l_len = request->parameters.lock.length;
        value = fcntl(request->parameters.lock.descriptor, F_OFD_SETLK, &range);
        capture_int_result(&result, value);
        result.committed = value == 0;
        break;
    }
    case R_RUNTIME_DARWIN_FS_FILE_STAGE_CREATE: {
        int descriptor = execute_create_file_stage(request);
        capture_int_result(&result, descriptor);
        if (descriptor >= 0) {
            result.opened_fd = descriptor;
        }
        break;
    }
    case R_RUNTIME_DARWIN_FS_FILE_STAGE_CLEANUP:
        r_runtime_darwin_fs_internal_cleanup_staged_file(
            request->parameters.file_stage_cleanup.directory_fd,
            request->parameters.file_stage_cleanup.name);
        capture_int_result(&result, 0);
        result.committed = 1;
        break;
    case R_RUNTIME_DARWIN_FS_FULL_FSYNC_DIRECTORY_AT: {
        const int policy_flags =
            request->parameters.sync_directory_at.beneath ? O_RESOLVE_BENEATH | O_NOFOLLOW_ANY : 0;
        int descriptor = open_final_component(request->parameters.sync_directory_at.directory_fd,
                                              request->parameters.sync_directory_at.path,
                                              O_RDONLY | O_DIRECTORY,
                                              (mode_t)0,
                                              !request->parameters.sync_directory_at.beneath,
                                              policy_flags,
                                              &result.committed);
        int value;

        result.committed = 0;
        if (descriptor < 0) {
            capture_int_result(&result, -1);
            break;
        }
        value = fcntl(descriptor, F_FULLFSYNC);
        capture_int_result(&result, value);
        if (close(descriptor) != 0 && value == 0) {
            capture_int_result(&result, -1);
        }
        result.committed = value == 0 && result.native_error == 0;
        break;
    }
    }
    return result;
}

void r_runtime_darwin_fs_internal_release_entered_resources(RRuntimeDarwinFsRequest *request) {
    switch (request->operation) {
    case R_RUNTIME_DARWIN_FS_OPEN_AT:
        close_retained_descriptor(&request->parameters.open_at.directory_fd);
        break;
    case R_RUNTIME_DARWIN_FS_CLOSE:
        break;
    case R_RUNTIME_DARWIN_FS_FSTAT:
        close_retained_descriptor(&request->parameters.fstat.descriptor);
        break;
    case R_RUNTIME_DARWIN_FS_FSTAT_AT:
        close_retained_descriptor(&request->parameters.fstat_at.directory_fd);
        break;
    case R_RUNTIME_DARWIN_FS_SEEK:
        close_retained_descriptor(&request->parameters.seek.descriptor);
        break;
    case R_RUNTIME_DARWIN_FS_ENUMERATE:
        close_retained_descriptor(&request->parameters.enumerate.directory_fd);
        break;
    case R_RUNTIME_DARWIN_FS_MKDIR_AT:
        close_retained_descriptor(&request->parameters.mkdir_at.directory_fd);
        break;
    case R_RUNTIME_DARWIN_FS_UNLINK_AT:
        close_retained_descriptor(&request->parameters.unlink_at.directory_fd);
        break;
    case R_RUNTIME_DARWIN_FS_RENAME_AT_NO_REPLACE:
        if (request->parameters.rename_at.source_directory_owned) {
            close_retained_descriptor(&request->parameters.rename_at.source_directory_fd);
        }
        if (request->parameters.rename_at.destination_directory_owned) {
            close_retained_descriptor(&request->parameters.rename_at.destination_directory_fd);
        }
        break;
    case R_RUNTIME_DARWIN_FS_FSYNC:
    case R_RUNTIME_DARWIN_FS_FULL_FSYNC:
    case R_RUNTIME_DARWIN_FS_BARRIER_FSYNC:
        if (request->parameters.sync.descriptor_owned) {
            close_retained_descriptor(&request->parameters.sync.descriptor);
        }
        break;
    case R_RUNTIME_DARWIN_FS_OFD_LOCK:
        if (request->parameters.lock.descriptor_owned) {
            close_retained_descriptor(&request->parameters.lock.descriptor);
        }
        break;
    case R_RUNTIME_DARWIN_FS_FILE_STAGE_CREATE:
        break;
    case R_RUNTIME_DARWIN_FS_FILE_STAGE_CLEANUP:
        if (request->parameters.file_stage_cleanup.directory_owned) {
            close_retained_descriptor(&request->parameters.file_stage_cleanup.directory_fd);
        }
        break;
    case R_RUNTIME_DARWIN_FS_FULL_FSYNC_DIRECTORY_AT:
        if (request->parameters.sync_directory_at.directory_owned) {
            close_retained_descriptor(&request->parameters.sync_directory_at.directory_fd);
        }
        break;
    }
}
