#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsTaskStartResult
r_std_fs_write_all_from(const RStdFsFile *file, RStdFsConstBytes source, RStdFsDeadline deadline) {
    return r_library_internal_fs_write_all_from(file, source, deadline);
}
