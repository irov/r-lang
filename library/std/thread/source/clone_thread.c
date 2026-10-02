#include "r_std_thread.h"

#include "r_library_thread_internal.h"

RStdThread r_std_thread_clone_thread(const RStdThread *source) {
    return r_library_internal_thread_clone(source);
}
