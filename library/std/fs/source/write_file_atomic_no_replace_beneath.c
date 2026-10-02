#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsTaskStartResult r_std_fs_write_file_atomic_no_replace_beneath(const RStdFsDirectory *root,
                                                                    const RStdFsPath *relative,
                                                                    RRuntimeArray *data,
                                                                    RStdFsDeadline deadline) {
    return r_library_internal_fs_write_file_atomic_no_replace_beneath(
        root, relative, data, deadline);
}
