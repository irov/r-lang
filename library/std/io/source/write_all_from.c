#include "r_std_io.h"

#include "r_library_io_internal.h"

RStdIoTaskStartResult r_std_io_write_all_from(const RStdIoOutput *stream,
                                              RStdIoConstBytes source,
                                              RStdIoDeadline deadline) {
    return r_library_internal_io_write_all_from(stream, source, deadline);
}
