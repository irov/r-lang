#include "r_std_io.h"

#include "r_library_io_internal.h"

RStdIoInput r_std_io_stdin(void) {
    return r_library_internal_io_stdin();
}
