#include "r_std_fs_native.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

/* The longest path the provider accepts, with its terminating zero. */
#define R_STD_FS_NATIVE_PATH_CAPACITY 1025U

static int32_t r_std_fs_native_failure_class(int native_error) {
    switch (native_error) {
    case EINVAL:
    case ENODEV:
    case ENXIO:
        return R_STD_FS_NATIVE_FAILED_INVALID;
    case ENOENT:
    case ENOTDIR:
        return R_STD_FS_NATIVE_FAILED_NOT_FOUND;
    case EACCES:
    case EPERM:
        return R_STD_FS_NATIVE_FAILED_PERMISSION;
    case ENOMEM:
    case EMFILE:
    case ENFILE:
    case EAGAIN:
        return R_STD_FS_NATIVE_FAILED_RESOURCES;
    case EISDIR:
        return R_STD_FS_NATIVE_FAILED_IS_DIRECTORY;
    case EROFS:
        return R_STD_FS_NATIVE_FAILED_READ_ONLY;
    case ENAMETOOLONG:
        return R_STD_FS_NATIVE_FAILED_NAME_TOO_LONG;
    default:
        return R_STD_FS_NATIVE_FAILED_OTHER;
    }
}

static int32_t r_std_fs_native_fail(int native, int32_t *native_error) {
    *native_error = native;
    return r_std_fs_native_failure_class(native);
}

int32_t r_std_fs_native_map(const uint8_t *path,
                            size_t path_length,
                            uint64_t offset,
                            uint64_t length,
                            int32_t writable,
                            void **base,
                            size_t *total,
                            uint8_t **start,
                            int32_t *native_error) {
    char terminated[R_STD_FS_NATIVE_PATH_CAPACITY];
    const long page_size = sysconf(_SC_PAGESIZE);
    struct stat status;
    uint64_t aligned_offset;
    uint64_t delta;
    void *address;
    int descriptor;

    *base = NULL;
    *total = 0U;
    *start = NULL;
    *native_error = 0;
    if ((path == NULL) || (path_length == 0U) || (page_size <= 0)) {
        return r_std_fs_native_fail(EINVAL, native_error);
    }
    if (path_length >= sizeof(terminated)) {
        return r_std_fs_native_fail(ENAMETOOLONG, native_error);
    }
    if (memchr(path, 0, path_length) != NULL) {
        return r_std_fs_native_fail(EINVAL, native_error);
    }
    (void)memcpy(terminated, path, path_length);
    terminated[path_length] = '\0';
    descriptor = open(terminated, (writable != 0 ? O_RDWR : O_RDONLY) | O_CLOEXEC);
    if (descriptor < 0) {
        return r_std_fs_native_fail(errno, native_error);
    }
    if (fstat(descriptor, &status) != 0) {
        const int native = errno;

        (void)close(descriptor);
        return r_std_fs_native_fail(native, native_error);
    }
    if (S_ISDIR(status.st_mode)) {
        (void)close(descriptor);
        return r_std_fs_native_fail(EISDIR, native_error);
    }
    /* A range past the end of the file would fault when touched. */
    if (!S_ISREG(status.st_mode) || (status.st_size < 0) || (length == UINT64_C(0)) ||
        (offset > (uint64_t)status.st_size) || (length > (uint64_t)status.st_size - offset)) {
        (void)close(descriptor);
        return r_std_fs_native_fail(!S_ISREG(status.st_mode) ? ENODEV : EINVAL, native_error);
    }
    aligned_offset = offset - offset % (uint64_t)page_size;
    delta = offset - aligned_offset;
    if (length > (uint64_t)SIZE_MAX - delta) {
        (void)close(descriptor);
        return r_std_fs_native_fail(ENOMEM, native_error);
    }
    address = mmap(NULL,
                   (size_t)(length + delta),
                   PROT_READ | (writable != 0 ? PROT_WRITE : 0),
                   MAP_SHARED,
                   descriptor,
                   (off_t)aligned_offset);
    if (address == MAP_FAILED) {
        const int native = errno;

        (void)close(descriptor);
        return r_std_fs_native_fail(native, native_error);
    }
    /* The mapping keeps the file; the descriptor is not needed any more. */
    (void)close(descriptor);
    *base = address;
    *total = (size_t)(length + delta);
    *start = (uint8_t *)address + delta;
    return 0;
}

void r_std_fs_native_unmap(void *base, size_t total) {
    if (base != NULL) {
        (void)munmap(base, total);
    }
}

int32_t r_std_fs_native_sync(void *base, size_t total, int32_t *native_error) {
    *native_error = 0;
    if (base == NULL) {
        return 0;
    }
    if (msync(base, total, MS_SYNC) != 0) {
        return r_std_fs_native_fail(errno, native_error);
    }
    return 0;
}
