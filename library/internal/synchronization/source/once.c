#include "r_library_sync_internal.h"

#include "r_runtime_0_1.h"

#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>

enum {
    R_LIBRARY_SYNC_ONCE_NEW = 0U,
    R_LIBRARY_SYNC_ONCE_RUNNING = 1U,
    R_LIBRARY_SYNC_ONCE_COMPLETED = 2U,
    R_LIBRARY_SYNC_ONCE_POISONED = 3U
};

void r_library_internal_sync_once_initialize(RStdSyncOnce *result) {
    atomic_init(&result->state, R_LIBRARY_SYNC_ONCE_NEW);
    atomic_init(&result->owner_token, (uintptr_t)0U);
    result->initialized = 1;
}

_Bool r_library_internal_sync_once_call(const RStdSyncOnce *source,
                                        RStdSyncOnceInitializer initializer,
                                        void *context,
                                        _Bool force) {
    RStdSyncOnce *once = (RStdSyncOnce *)source;
    const uintptr_t token = r_library_internal_sync_current_thread_token();

    for (;;) {
        unsigned int state = atomic_load_explicit(&once->state, memory_order_acquire);
        unsigned int expected;

        if (state == R_LIBRARY_SYNC_ONCE_COMPLETED) {
            return 1;
        }
        if ((state == R_LIBRARY_SYNC_ONCE_POISONED) && !force) {
            r_runtime_panic(R_RUNTIME_PANIC_ONCE_POISONED,
                            (RRuntimeSourceSpan){UINT32_C(0), UINT32_C(0), UINT32_C(0)});
        }
        if (state == R_LIBRARY_SYNC_ONCE_RUNNING) {
            if (atomic_load_explicit(&once->owner_token, memory_order_acquire) == token) {
                r_library_internal_sync_contract_violation();
            }
            (void)sched_yield();
            continue;
        }
        expected = state;
        if (!atomic_compare_exchange_weak_explicit(&once->state,
                                                   &expected,
                                                   R_LIBRARY_SYNC_ONCE_RUNNING,
                                                   memory_order_acq_rel,
                                                   memory_order_acquire)) {
            continue;
        }
        atomic_store_explicit(&once->owner_token, token, memory_order_release);
        if (!initializer(context)) {
            atomic_store_explicit(&once->owner_token, (uintptr_t)0U, memory_order_relaxed);
            atomic_store_explicit(&once->state, state, memory_order_release);
            return 0;
        }
        atomic_store_explicit(&once->owner_token, (uintptr_t)0U, memory_order_relaxed);
        atomic_store_explicit(&once->state, R_LIBRARY_SYNC_ONCE_COMPLETED, memory_order_release);
        return 1;
    }
}

void r_library_internal_sync_once_move(RStdSyncOnce *destination, RStdSyncOnce *source) {
    unsigned int state;

    state = atomic_load_explicit(&source->state, memory_order_acquire);
    atomic_init(&destination->state, state);
    atomic_init(&destination->owner_token, (uintptr_t)0U);
    destination->initialized = 1;
    source->initialized = 0;
}

void r_library_internal_sync_once_destroy(RStdSyncOnce *once) {
    if (!once->initialized) {
        return;
    }
    once->initialized = 0;
}
