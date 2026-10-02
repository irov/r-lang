#ifndef R_STD_FS_NATIVE_H
#define R_STD_FS_NATIVE_H

/* The native provider of std.fs::map_file and std.fs::mapping (Library R-SLIB-FS-0017): shared
   memory mappings of a range of a file. */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The class of a failure, returned instead of 0; native_error keeps errno. */
#define R_STD_FS_NATIVE_FAILED_OTHER 1
#define R_STD_FS_NATIVE_FAILED_INVALID 2
#define R_STD_FS_NATIVE_FAILED_NOT_FOUND 3
#define R_STD_FS_NATIVE_FAILED_PERMISSION 4
#define R_STD_FS_NATIVE_FAILED_RESOURCES 5
#define R_STD_FS_NATIVE_FAILED_IS_DIRECTORY 6
#define R_STD_FS_NATIVE_FAILED_READ_ONLY 7
#define R_STD_FS_NATIVE_FAILED_NAME_TOO_LONG 8

/* Maps length bytes of the file at path from offset, shared with the file, readable and also
   writable when writable is nonzero. The range shall be nonempty and lie within a regular file.
   On success returns 0 and stores the page-aligned base and size of the mapping and the address
   of the first byte of the range. On failure returns the class of the failure, stores errno in
   native_error and maps nothing. */
int32_t r_std_fs_native_map(const uint8_t *path,
                            size_t path_length,
                            uint64_t offset,
                            uint64_t length,
                            int32_t writable,
                            void **base,
                            size_t *total,
                            uint8_t **start,
                            int32_t *native_error);

/* Unmaps a mapping of r_std_fs_native_map; a null base does nothing. */
void r_std_fs_native_unmap(void *base, size_t total);

/* msync(MS_SYNC) of a mapping of r_std_fs_native_map: writes its changed pages to the file and
   waits for the write. Returns 0, or the class of the failure with errno in native_error. */
int32_t r_std_fs_native_sync(void *base, size_t total, int32_t *native_error);

#ifdef __cplusplus
}
#endif

#endif
