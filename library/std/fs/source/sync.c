#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsTaskStartResult
r_std_fs_sync(const RStdFsFile *file, RStdFsSyncLevel level, RStdFsDeadline deadline) {
    return r_library_internal_fs_sync(file, level, deadline);
}
