#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsTaskStartResult
r_std_fs_read_into(const RStdFsFile *file, RStdFsMutableBytes target, RStdFsDeadline deadline) {
    return r_library_internal_fs_read_into(file, target, deadline);
}
