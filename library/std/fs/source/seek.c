#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsTaskStartResult r_std_fs_seek(const RStdFsFile *file,
                                    RStdFsSeekOrigin origin,
                                    int64_t offset,
                                    RStdFsDeadline deadline) {
    return r_library_internal_fs_seek(file, origin, offset, deadline);
}
