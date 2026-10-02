#include "r_runtime_0_1.h"

#include <pthread.h>
#include <stddef.h>
#include <stdint.h>

typedef struct RRuntimeStackBounds {
    uintptr_t protected_low;
    uintptr_t high;
    _Bool initialized;
} RRuntimeStackBounds;

static _Thread_local RRuntimeStackBounds r_runtime_stack_bounds;

static _Noreturn void r_runtime_stack_exhausted(RRuntimeSourceSpan span) {
    r_runtime_panic(R_RUNTIME_PANIC_STACK_EXHAUSTION, span);
}

_Bool r_runtime_stack_initialize_current_thread(void) {
    unsigned char current_marker;
    void *stack_address;
    size_t stack_size_bytes;
    uintptr_t stack_size;
    uintptr_t high;
    uintptr_t low;
    uintptr_t protected_low;
    uintptr_t current;

    r_runtime_stack_bounds.initialized = 0;
    stack_address = pthread_get_stackaddr_np(pthread_self());
    stack_size_bytes = pthread_get_stacksize_np(pthread_self());
    if (stack_address == NULL || stack_size_bytes == 0U) {
        return 0;
    }

    stack_size = (uintptr_t)stack_size_bytes;
    if ((size_t)stack_size != stack_size_bytes) {
        return 0;
    }
    high = (uintptr_t)stack_address;
    if (stack_size > high) {
        return 0;
    }
    low = high - stack_size;
    if (low > UINTPTR_MAX - (uintptr_t)R_RUNTIME_STACK_PROTECTED_LOW_BYTES) {
        return 0;
    }
    protected_low = low + (uintptr_t)R_RUNTIME_STACK_PROTECTED_LOW_BYTES;
    if (protected_low >= high) {
        return 0;
    }

    current = (uintptr_t)&current_marker;
    if (current <= protected_low || current > high) {
        return 0;
    }
    r_runtime_stack_bounds.protected_low = protected_low;
    r_runtime_stack_bounds.high = high;
    r_runtime_stack_bounds.initialized = 1;
    return 1;
}

void r_runtime_stack_require(size_t frame_bytes, RRuntimeSourceSpan span) {
    unsigned char current_marker;
    uintptr_t frame_size;
    uintptr_t required;
    uintptr_t current;

    if (!r_runtime_stack_bounds.initialized) {
        r_runtime_stack_exhausted(span);
    }
    frame_size = (uintptr_t)frame_bytes;
    if ((size_t)frame_size != frame_bytes ||
        frame_size > UINTPTR_MAX - (uintptr_t)R_RUNTIME_STACK_CALL_TRANSITION_BYTES) {
        r_runtime_stack_exhausted(span);
    }
    required = frame_size + (uintptr_t)R_RUNTIME_STACK_CALL_TRANSITION_BYTES;
    current = (uintptr_t)&current_marker;
    if (current > r_runtime_stack_bounds.high || current <= r_runtime_stack_bounds.protected_low ||
        required > current - r_runtime_stack_bounds.protected_low) {
        r_runtime_stack_exhausted(span);
    }
}

_Bool r_runtime_stack_can_require(size_t frame_bytes) {
    unsigned char current_marker;
    uintptr_t frame_size;
    uintptr_t required;
    uintptr_t current;

    if (!r_runtime_stack_bounds.initialized) {
        return 0;
    }
    frame_size = (uintptr_t)frame_bytes;
    if ((size_t)frame_size != frame_bytes ||
        frame_size > UINTPTR_MAX - (uintptr_t)R_RUNTIME_STACK_CALL_TRANSITION_BYTES) {
        return 0;
    }
    required = frame_size + (uintptr_t)R_RUNTIME_STACK_CALL_TRANSITION_BYTES;
    current = (uintptr_t)&current_marker;
    return current <= r_runtime_stack_bounds.high &&
           current > r_runtime_stack_bounds.protected_low &&
           required <= current - r_runtime_stack_bounds.protected_low;
}
