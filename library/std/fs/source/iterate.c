#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsTaskStartResult r_std_fs_iterate(const RStdFsDirectory *directory, RStdFsDeadline deadline) {
    return r_library_internal_fs_iterate(directory, deadline);
}
