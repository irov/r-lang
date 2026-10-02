#ifndef R_STD_RANDOM_H
#define R_STD_RANDOM_H

#include <stddef.h>
#include <stdint.h>

/* A zero-length slice may carry a null data pointer; every nonzero slice names a valid region. */
typedef struct RStdRandomMutSlice {
    uint8_t *data;
    size_t length;
} RStdRandomMutSlice;

/*
 * Ownership: target is an exclusive call-bounded borrow that is not retained. Every byte comes
 * from the cryptographically secure generator of the operating system. The call allocates
 * nothing, cannot fail and does not wait for entropy.
 */
void r_std_random_fill(RStdRandomMutSlice target);

#endif
