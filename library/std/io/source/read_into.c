#include "r_std_io.h"

#include "r_library_io_internal.h"

RStdIoTaskStartResult
r_std_io_read_into(const RStdIoInput *stream, RStdIoMutableBytes target, RStdIoDeadline deadline) {
    return r_library_internal_io_read_into(stream, target, deadline);
}
