#include "r_library_sync_internal.h"

#include <sched.h>
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

static void condvar_lock(RStdSyncCondvar *condition) {
    while (atomic_exchange_explicit(&condition->gate, 1U, memory_order_acquire) != 0U) {
        (void)sched_yield();
    }
}

static void condvar_unlock(RStdSyncCondvar *condition) {
    atomic_store_explicit(&condition->gate, 0U, memory_order_release);
}

void r_library_internal_sync_condvar_initialize(RStdSyncCondvar *result) {
    atomic_init(&result->gate, 0U);
    atomic_init(&result->wake_through, UINT64_C(0));
    result->next_ticket = UINT64_C(0);
    result->waiter_count = 0U;
    result->initialized = 1;
}

RLibrarySyncAcquireResult r_library_internal_sync_condvar_wait(const RStdSyncCondvar *source,
                                                               RStdSyncMutexGuard *guard) {
    RStdSyncCondvar *condition = (RStdSyncCondvar *)source;
    RLibrarySyncAcquireResult acquired;
    RStdSyncMutex *mutex;
    uint64_t ticket;

    mutex = guard->mutex;

    condvar_lock(condition);
    if ((condition->next_ticket == UINT64_MAX) || (condition->waiter_count == SIZE_MAX)) {
        condvar_unlock(condition);
        r_library_internal_sync_contract_violation();
    }
    ticket = condition->next_ticket;
    condition->next_ticket += UINT64_C(1);
    condition->waiter_count += 1U;
    condvar_unlock(condition);

    r_library_internal_sync_mutex_unlock(guard, 0);
    while (atomic_load_explicit(&condition->wake_through, memory_order_acquire) <= ticket) {
        (void)sched_yield();
    }

    acquired = r_library_internal_sync_mutex_acquire(mutex, 1);

    condvar_lock(condition);
    condition->waiter_count -= 1U;
    if ((condition->waiter_count == 0U) &&
        (atomic_load_explicit(&condition->wake_through, memory_order_relaxed) ==
         condition->next_ticket)) {
        condition->next_ticket = UINT64_C(0);
        atomic_store_explicit(&condition->wake_through, UINT64_C(0), memory_order_relaxed);
    }
    condvar_unlock(condition);
    return acquired;
}

void r_library_internal_sync_condvar_notify(const RStdSyncCondvar *source, _Bool notify_all) {
    RStdSyncCondvar *condition = (RStdSyncCondvar *)source;
    uint64_t wake_through;

    condvar_lock(condition);
    wake_through = atomic_load_explicit(&condition->wake_through, memory_order_relaxed);
    if (wake_through < condition->next_ticket) {
        wake_through = notify_all ? condition->next_ticket : wake_through + UINT64_C(1);
        atomic_store_explicit(&condition->wake_through, wake_through, memory_order_release);
    }
    condvar_unlock(condition);
}

void r_library_internal_sync_condvar_move(RStdSyncCondvar *destination, RStdSyncCondvar *source) {
    condvar_lock(source);
    atomic_init(&destination->gate, 0U);
    atomic_init(&destination->wake_through, UINT64_C(0));
    destination->next_ticket = UINT64_C(0);
    destination->waiter_count = 0U;
    destination->initialized = 1;
    source->initialized = 0;
    condvar_unlock(source);
}

void r_library_internal_sync_condvar_destroy(RStdSyncCondvar *condition) {
    if (!condition->initialized) {
        return;
    }
    condvar_lock(condition);
    condition->initialized = 0;
    condvar_unlock(condition);
}
