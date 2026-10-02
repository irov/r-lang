#include "r_std_process.h"

#include <stdlib.h>

_Noreturn void r_std_process_abort(void) {
    abort();
}
