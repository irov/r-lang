#ifndef C_ATOMICS_H
#define C_ATOMICS_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/* R-FFI-0017 (B12): a struct with _Atomic members of C integers and of a pointer, which R and C
   update in place. */
typedef struct atomic_counters {
    _Atomic uint64_t hits;
    _Atomic(int) level;
    void *_Atomic slot;
    _Atomic size_t total;
    unsigned char tag;
} atomic_counters;

void atomic_counters_initialize(atomic_counters *counters, unsigned char tag);
uint64_t atomic_counters_bump(atomic_counters *counters, uint64_t amount);
int atomic_counters_level(const atomic_counters *counters);
void *atomic_counters_slot(const atomic_counters *counters);

#endif
