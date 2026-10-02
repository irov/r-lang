#include "r_std_io.h"

#include "r_library_io_internal.h"

RStdIoTaskStartResult r_std_io_close_output(RStdIoOutput *stream, RStdIoDeadline deadline) {
    return r_library_internal_io_close_output(stream, deadline);
}
