#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsTaskStartResult r_std_fs_write_all_at(const RStdFsFile *file,
                                            uint64_t offset,
                                            RRuntimeArray *buffer,
                                            RStdFsDeadline deadline) {
    return r_library_internal_fs_write_all_at(file, offset, buffer, deadline);
}
