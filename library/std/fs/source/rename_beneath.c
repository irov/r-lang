#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsTaskStartResult r_std_fs_rename_beneath(const RStdFsDirectory *source_root,
                                              const RStdFsPath *source,
                                              const RStdFsDirectory *destination_root,
                                              const RStdFsPath *destination,
                                              RStdFsDeadline deadline) {
    return r_library_internal_fs_rename_beneath(
        source_root, source, destination_root, destination, deadline);
}
