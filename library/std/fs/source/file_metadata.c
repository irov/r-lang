#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsTaskStartResult r_std_fs_file_metadata(const RStdFsFile *file, RStdFsDeadline deadline) {
    return r_library_internal_fs_file_metadata(file, deadline);
}
