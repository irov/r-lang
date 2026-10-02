#include "r_std_thread.h"

#include <sched.h>

void r_std_thread_yield_now(void) {
    (void)sched_yield();
}
