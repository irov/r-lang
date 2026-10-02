#include "r_std_thread.h"

#include <errno.h>
#include <stdint.h>
#include <time.h>

void r_std_thread_sleep_nanoseconds(uint64_t nanoseconds) {
    struct timespec remaining;
    struct timespec requested;

    requested.tv_sec = (time_t)(nanoseconds / UINT64_C(1000000000));
    requested.tv_nsec = (long)(nanoseconds % UINT64_C(1000000000));
    while (nanosleep(&requested, &remaining) != 0) {
        if (errno != EINTR) {
            return;
        }
        requested = remaining;
    }
}
