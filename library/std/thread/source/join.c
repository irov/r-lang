#include "r_std_thread.h"

#include "r_library_thread_internal.h"

RStdThreadJoinResult r_std_thread_join(RStdThreadJoinHandle *handle) {
    return r_library_internal_thread_join(handle);
}
