#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsTaskStartResult r_std_fs_next(RStdFsDirectoryIter *iterator, RStdFsDeadline deadline) {
    return r_library_internal_fs_next(iterator, deadline);
}
