#include "r_library_sync_internal.h"

#include <stdatomic.h>
#include <stddef.h>

void r_library_internal_sync_barrier_move(RStdSyncBarrier *destination, RStdSyncBarrier *source) {
    size_t generation;

    generation = atomic_load_explicit(&source->generation, memory_order_relaxed);
    atomic_init(&destination->arrived, 0U);
    atomic_init(&destination->generation, generation);
    destination->participants = source->participants;
    destination->initialized = 1;
    source->initialized = 0;
    source->participants = 0U;
}

void r_library_internal_sync_barrier_destroy(RStdSyncBarrier *barrier) {
    if (!barrier->initialized) {
        return;
    }
    barrier->initialized = 0;
    barrier->participants = 0U;
}
