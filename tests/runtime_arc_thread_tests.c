#include "r_runtime_allocator.h"
#include "r_runtime_arc.h"
#include "r_runtime_type.h"

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>

typedef struct RRuntimeArcThreadValue {
    uint64_t first;
    uint64_t second;
} RRuntimeArcThreadValue;

typedef struct RRuntimeArcThreadContext {
    const RRuntimeArc *owner;
    size_t iterations;
    _Atomic int failed;
} RRuntimeArcThreadContext;

static _Atomic size_t r_runtime_arc_thread_drop_count;

static void r_runtime_arc_thread_drop(void *value) {
    (void)value;
    (void)atomic_fetch_add_explicit(&r_runtime_arc_thread_drop_count, 1U, memory_order_relaxed);
}

static void *r_runtime_arc_thread_worker(void *user_data) {
    RRuntimeArcThreadContext *context = user_data;
    size_t iteration;

    for (iteration = 0U; iteration < context->iterations; ++iteration) {
        RRuntimeArc clone = {NULL};
        const RRuntimeArcThreadValue *value;

        if (r_runtime_arc_clone(context->owner, &clone) != R_RUNTIME_ARC_OK) {
            atomic_store_explicit(&context->failed, 1, memory_order_relaxed);
            return NULL;
        }
        value = r_runtime_arc_get(&clone);
        if ((value == NULL) || (value->first != UINT64_C(0x0123456789abcdef)) ||
            (value->second != UINT64_C(0xfedcba9876543210))) {
            atomic_store_explicit(&context->failed, 1, memory_order_relaxed);
        }
        r_runtime_arc_release(&clone);
    }
    return NULL;
}

int main(void) {
    enum {
        R_RUNTIME_ARC_THREAD_COUNT = 8
    };
    RRuntimeAllocator allocator;
    RRuntimeTypeInfo type = {
        sizeof(RRuntimeArcThreadValue),
        _Alignof(RRuntimeArcThreadValue),
        NULL,
        r_runtime_arc_thread_drop,
    };
    RRuntimeArcThreadValue value = {
        UINT64_C(0x0123456789abcdef),
        UINT64_C(0xfedcba9876543210),
    };
    RRuntimeArc owner = {NULL};
    RRuntimeArcThreadContext context;
    pthread_t threads[R_RUNTIME_ARC_THREAD_COUNT];
    size_t created = 0U;
    size_t index;

    r_runtime_allocator_initialize(&allocator);
    atomic_init(&r_runtime_arc_thread_drop_count, 0U);
    if (r_runtime_arc_create(&allocator, type, &value, &owner) != R_RUNTIME_ARC_OK) {
        (void)fputs("arc thread test: create failed\n", stderr);
        return 1;
    }
    context.owner = &owner;
    context.iterations = 20000U;
    atomic_init(&context.failed, 0);
    for (index = 0U; index < R_RUNTIME_ARC_THREAD_COUNT; ++index) {
        if (pthread_create(&threads[index], NULL, r_runtime_arc_thread_worker, &context) != 0) {
            atomic_store_explicit(&context.failed, 1, memory_order_relaxed);
            break;
        }
        created += 1U;
    }
    for (index = 0U; index < created; ++index) {
        if (pthread_join(threads[index], NULL) != 0) {
            atomic_store_explicit(&context.failed, 1, memory_order_relaxed);
        }
    }
    if ((atomic_load_explicit(&context.failed, memory_order_relaxed) != 0) ||
        (r_runtime_arc_strong_count(&owner) != 1U)) {
        r_runtime_arc_release(&owner);
        (void)fputs("arc thread test: concurrent clone/release failed\n", stderr);
        return 1;
    }
    r_runtime_arc_release(&owner);
    if (atomic_load_explicit(&r_runtime_arc_thread_drop_count, memory_order_relaxed) != 1U) {
        (void)fputs("arc thread test: value was not dropped exactly once\n", stderr);
        return 1;
    }
    (void)puts("runtime_arc_thread_ok");
    return 0;
}
