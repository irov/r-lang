#include "r_runtime_darwin_event.h"

#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

static _Atomic uint64_t r_runtime_darwin_event_sequence;

uint64_t r_runtime_darwin_event_sequence_next(void) {
    uint64_t current = atomic_load_explicit(&r_runtime_darwin_event_sequence, memory_order_relaxed);

    for (;;) {
        if (current == UINT64_MAX) {
            abort();
        }
        if (atomic_compare_exchange_weak_explicit(&r_runtime_darwin_event_sequence,
                                                  &current,
                                                  current + UINT64_C(1),
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
            return current + UINT64_C(1);
        }
    }
}

#if defined(R_RUNTIME_DARWIN_EVENT_TESTING)
void r_runtime_darwin_event_testing_set_sequence(uint64_t sequence) {
    atomic_store_explicit(&r_runtime_darwin_event_sequence, sequence, memory_order_relaxed);
}
#endif
