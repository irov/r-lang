#include "r_std_thread.h"

#include "r_library_thread_internal.h"

void r_std_thread_unpark(const RStdThread *target) {
    r_library_internal_thread_unpark(target);
}
