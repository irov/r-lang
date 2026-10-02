#include "r_library_fs_internal.h"

RStdFsTaskStartResult r_std_fs_metadata(const RStdFsPath *path, RStdFsDeadline deadline) {
    return r_library_internal_fs_metadata(path, deadline);
}
