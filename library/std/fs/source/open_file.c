#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsTaskStartResult
r_std_fs_open_file(const RStdFsPath *path, RStdFsOpenFileOptions options, RStdFsDeadline deadline) {
    return r_library_internal_fs_open_file(path, options, deadline);
}
