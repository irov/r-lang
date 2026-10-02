#include "r_std_fs.h"

#include "r_library_fs_internal.h"

#include <stdint.h>

_Bool r_std_fs_path_is_absolute(const RStdFsPath *source) {
    const uint8_t *bytes;

    bytes = source->storage->bytes;
    return (source->storage->length != 0U) && (bytes[0] == UINT8_C('/'));
}
