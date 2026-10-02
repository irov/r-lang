#include "r_std_process.h"

#include "r_runtime_0_1.h"

#include <stdlib.h>

_Noreturn void r_std_process_exit(int32_t status) {
    exit(r_runtime_hosted_exit(status));
}
