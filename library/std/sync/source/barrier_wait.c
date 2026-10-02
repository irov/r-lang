#include "r_std_sync.h"

#include "r_library_sync_internal.h"

#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>

RStdSyncBarrierWaitResult r_std_sync_barrier_wait(const RStdSyncBarrier *source) {
    RStdSyncBarrier *barrier = (RStdSyncBarrier *)source;
    size_t generation;
    size_t arrival;

    generation = atomic_load_explicit(&barrier->generation, memory_order_acquire);
    arrival = atomic_fetch_add_explicit(&barrier->arrived, 1U, memory_order_acq_rel);
    if (arrival >= barrier->participants) {
        r_library_internal_sync_contract_violation();
    }
    if ((arrival + 1U) == barrier->participants) {
        atomic_store_explicit(&barrier->arrived, 0U, memory_order_relaxed);
        atomic_store_explicit(&barrier->generation, generation + 1U, memory_order_release);
        return R_STD_SYNC_BARRIER_WAIT_LEADER;
    }
    while (atomic_load_explicit(&barrier->generation, memory_order_acquire) == generation) {
        (void)sched_yield();
    }
    return R_STD_SYNC_BARRIER_WAIT_FOLLOWER;
}
