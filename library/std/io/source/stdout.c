#include "r_std_io.h"

#include "r_library_io_internal.h"

RStdIoOutput r_std_io_stdout(void) {
    return r_library_internal_io_stdout();
}
