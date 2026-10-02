#include "r_std_io.h"

#include "r_library_io_internal.h"

RStdIoTaskStartResult r_std_io_write_shared(const RStdIoOutput *stream,
                                            RRuntimeArc *buffer,
                                            size_t offset,
                                            size_t length,
                                            RStdIoDeadline deadline) {
    return r_library_internal_io_write_shared(stream, buffer, offset, length, deadline);
}
