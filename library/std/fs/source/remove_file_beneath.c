#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsTaskStartResult r_std_fs_remove_file_beneath(const RStdFsDirectory *root,
                                                   const RStdFsPath *relative,
                                                   RStdFsDeadline deadline) {
    return r_library_internal_fs_remove_file_beneath(root, relative, deadline);
}
