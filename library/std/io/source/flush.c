#include "r_std_io.h"

#include "r_library_io_internal.h"

RStdIoTaskStartResult r_std_io_flush(const RStdIoOutput *stream, RStdIoDeadline deadline) {
    return r_library_internal_io_flush(stream, deadline);
}
