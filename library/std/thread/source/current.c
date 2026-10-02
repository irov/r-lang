#include "r_std_thread.h"

#include "r_library_thread_internal.h"

RStdThread r_std_thread_current(void) {
    return r_library_internal_thread_current();
}
