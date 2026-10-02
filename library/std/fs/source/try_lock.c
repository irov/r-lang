#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsTaskStartResult r_std_fs_try_lock(const RStdFsFile *file,
                                        RStdFsLockKind kind,
                                        uint64_t start,
                                        uint64_t length,
                                        RStdFsDeadline deadline) {
    return r_library_internal_fs_try_lock(file, kind, start, length, deadline);
}
