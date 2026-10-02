#include "r_std_process.h"

#include "r_library_process_internal.h"

RStdProcessTaskStartResult r_std_process_spawn(RStdProcessCommand *command,
                                               RStdProcessDeadline deadline) {
    return r_library_internal_process_spawn(command, deadline);
}
