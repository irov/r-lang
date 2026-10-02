#include "r_std_process.h"

#include "r_library_process_internal.h"

RStdProcessTaskStartResult r_std_process_wait(RStdProcessChild *child,
                                              RStdProcessDeadline deadline) {
    return r_library_internal_process_wait(child, deadline);
}
