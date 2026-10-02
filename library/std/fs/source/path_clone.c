#include "r_std_fs.h"

#include "r_library_fs_internal.h"

RStdFsPathAllocResult r_std_fs_path_clone(const RStdFsPath *source) {
    RStdFsPathAllocResult result = {0};

    result.status = r_library_internal_fs_path_create(source->storage->allocator,
                                                      source->storage->bytes,
                                                      source->storage->length,
                                                      &result.value,
                                                      &result.error);
    return result;
}
