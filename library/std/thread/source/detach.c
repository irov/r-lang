#include "r_std_thread.h"

#include "r_library_thread_internal.h"

void r_std_thread_detach(RStdThreadJoinHandle *handle) {
    r_library_internal_thread_detach(handle);
}
