#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsTaskStartResult r_std_fs_open_file_beneath(const RStdFsDirectory *root,
                                                 const RStdFsPath *relative,
                                                 RStdFsOpenFileOptions options,
                                                 RStdFsDeadline deadline) {
    return r_library_internal_fs_open_file_beneath(root, relative, options, deadline);
}
