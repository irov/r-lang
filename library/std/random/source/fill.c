#include "r_std_random.h"

#include <stdlib.h>

/* R-SLIB-RANDOM-0001: arc4random_buf reads the kernel generator, which is seeded before any
   process starts, so it neither fails nor blocks. */
void r_std_random_fill(RStdRandomMutSlice target) {
    if (target.length != 0U) {
        arc4random_buf(target.data, target.length);
    }
}
