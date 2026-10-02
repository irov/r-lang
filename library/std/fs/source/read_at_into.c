#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsTaskStartResult r_std_fs_read_at_into(const RStdFsFile *file,
                                            uint64_t offset,
                                            RStdFsMutableBytes target,
                                            RStdFsDeadline deadline) {
    return r_library_internal_fs_read_at_into(file, offset, target, deadline);
}
