#include "r_std_process.h"

#include "r_library_process_internal.h"

RStdProcessTaskStartResult r_std_process_terminate(const RStdProcessChild *child,
                                                   RStdProcessDeadline deadline) {
    return r_library_internal_process_terminate(child, deadline);
}
