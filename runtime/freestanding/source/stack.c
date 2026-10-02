#include "r_runtime_freestanding.h"

#include "r_runtime_target_abi.h"

#include <stddef.h>
#include <stdint.h>

typedef struct RRuntimeStackBounds {
    uintptr_t protected_low;
    uintptr_t high;
    _Bool adopted;
} RRuntimeStackBounds;

static _Thread_local RRuntimeStackBounds r_runtime_stack_bounds;

static _Noreturn void r_runtime_stack_exhausted(RRuntimeSourceSpan span) {
    r_runtime_panic(R_RUNTIME_PANIC_STACK_EXHAUSTION, span);
}

_Bool r_runtime_freestanding_stack_adopt(uintptr_t low, uintptr_t high) {
    unsigned char current_marker;
    const uintptr_t current = (uintptr_t)&current_marker;
    uintptr_t protected_low;

    r_runtime_stack_bounds.adopted = 0;
    if ((low >= high) || (low > UINTPTR_MAX - (uintptr_t)R_RUNTIME_STACK_PROTECTED_LOW_BYTES)) {
        return 0;
    }
    protected_low = low + (uintptr_t)R_RUNTIME_STACK_PROTECTED_LOW_BYTES;
    if ((protected_low >= high) || (current <= protected_low) || (current > high)) {
        return 0;
    }
    r_runtime_stack_bounds.protected_low = protected_low;
    r_runtime_stack_bounds.high = high;
    r_runtime_stack_bounds.adopted = 1;
    return 1;
}

void r_runtime_freestanding_stack_release(void) {
    r_runtime_stack_bounds.adopted = 0;
}

_Bool r_runtime_stack_initialize_current_thread(void) {
    return r_runtime_stack_bounds.adopted;
}

void r_runtime_stack_require(size_t frame_bytes, RRuntimeSourceSpan span) {
    unsigned char current_marker;
    uintptr_t frame_size;
    uintptr_t required;
    uintptr_t current;

    if (!r_runtime_stack_bounds.adopted) {
        r_runtime_stack_exhausted(span);
    }
    frame_size = (uintptr_t)frame_bytes;
    if (((size_t)frame_size != frame_bytes) ||
        (frame_size > UINTPTR_MAX - (uintptr_t)R_RUNTIME_STACK_CALL_TRANSITION_BYTES)) {
        r_runtime_stack_exhausted(span);
    }
    required = frame_size + (uintptr_t)R_RUNTIME_STACK_CALL_TRANSITION_BYTES;
    current = (uintptr_t)&current_marker;
    if ((current > r_runtime_stack_bounds.high) ||
        (current <= r_runtime_stack_bounds.protected_low) ||
        (required > current - r_runtime_stack_bounds.protected_low)) {
        r_runtime_stack_exhausted(span);
    }
}
