#include "r_std_process.h"

#include "r_library_process_internal.h"

void r_std_process_clear_environment(RStdProcessCommand *command) {
    r_runtime_dict_clear(&command->storage->environment);
}
