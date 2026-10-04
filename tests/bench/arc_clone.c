#include <stdatomic.h>
#include <stdint.h>
#include <stdlib.h>

/* The C mirror of bench/arc_clone.r: an atomically counted block, cloned and released. */
typedef struct {
    _Atomic uint64_t refs;
    uint64_t value;
} counter;
static void release(counter *c) {
    if (atomic_fetch_sub_explicit(&c->refs, 1, memory_order_release) == 1) {
        atomic_thread_fence(memory_order_acquire);
        free(c);
    }
}
static uint64_t read_twice(counter *c) {
    uint64_t v = c->value * 2;
    release(c);
    return v;
}
int main(void) {
    counter *shared = malloc(sizeof(counter));
    atomic_init(&shared->refs, 1);
    shared->value = 21;
    uint64_t sum = 0;
    for (uint32_t i = 0; i < 50000000u; i += 1) {
        atomic_fetch_add_explicit(&shared->refs, 1, memory_order_relaxed);
        sum += read_twice(shared) + i;
    }
    release(shared);
    return (int)(sum % 109u);
}
