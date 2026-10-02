#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsTaskStartResult
r_std_fs_unlock(const RStdFsFile *file, uint64_t start, uint64_t length, RStdFsDeadline deadline) {
    return r_library_internal_fs_unlock(file, start, length, deadline);
}
