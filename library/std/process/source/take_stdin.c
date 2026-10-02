#include "r_std_process.h"

#include "r_library_process_internal.h"

RStdProcessOutputOption r_std_process_take_stdin(RStdProcessChild *child) {
    RStdProcessOutputOption result = {0};

    result.value.handle =
        r_library_internal_process_child_take_pipe(child, R_RUNTIME_DARWIN_PROCESS_PIPE_STDIN);
    result.has_value = result.value.handle != NULL;
    return result;
}
