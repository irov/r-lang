#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsTaskStartResult
r_std_fs_create_directory(const RStdFsPath *path, _Bool recursive, RStdFsDeadline deadline) {
    return r_library_internal_fs_create_directory(path, recursive, deadline);
}
