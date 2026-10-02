#include "r_library_fs_internal.h"

RStdFsTaskStartResult
r_std_fs_read_file(const RStdFsPath *path, size_t limit, RStdFsDeadline deadline) {
    return r_library_internal_fs_read_file(path, limit, deadline);
}
