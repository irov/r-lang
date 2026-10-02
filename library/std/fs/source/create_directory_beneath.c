#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsTaskStartResult r_std_fs_create_directory_beneath(const RStdFsDirectory *root,
                                                        const RStdFsPath *relative,
                                                        _Bool recursive,
                                                        RStdFsDeadline deadline) {
    return r_library_internal_fs_create_directory_beneath(root, relative, recursive, deadline);
}
