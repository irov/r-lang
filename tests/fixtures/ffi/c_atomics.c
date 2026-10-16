#include "c_atomics.h"

struct atomic_registry {
    int tag;
};

struct atomic_registry atomic_registry_main = {42};

void atomic_counters_initialize(atomic_counters *counters, unsigned char tag) {
    atomic_init(&counters->hits, UINT64_C(0));
    atomic_init(&counters->level, 0);
    atomic_init(&counters->slot, NULL);
    atomic_init(&counters->total, (size_t)0U);
    atomic_init(&counters->ready, 0);
    counters->tag = tag;
}

uint64_t atomic_counters_bump(atomic_counters *counters, uint64_t amount) {
    (void)atomic_fetch_add_explicit(&counters->total, (size_t)amount, memory_order_relaxed);
    return atomic_fetch_add_explicit(&counters->hits, amount, memory_order_acq_rel);
}

int atomic_counters_level(const atomic_counters *counters) {
    return atomic_load_explicit(&counters->level, memory_order_acquire);
}

void *atomic_counters_slot(const atomic_counters *counters) {
    return atomic_load_explicit(&counters->slot, memory_order_acquire);
}

_Bool atomic_counters_ready(const atomic_counters *counters) {
    return atomic_load_explicit(&counters->ready, memory_order_acquire);
}

int atomic_registry_tag(const struct atomic_registry *registry) {
    return registry->tag;
}
