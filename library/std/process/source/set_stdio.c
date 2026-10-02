#include "r_std_process.h"

#include "r_library_process_internal.h"

void r_std_process_set_stdio(RStdProcessCommand *command, RStdProcessStdio policy) {
    command->storage->stdio = policy;
}
