#include "r_runtime_arc.h"

#include <stdatomic.h>
#include <stddef.h>
#include <string.h>

struct RRuntimeArcControl {
    RRuntimeAllocator *allocator;
    RRuntimeTypeInfo type;
    _Atomic size_t strong_count;
    _Atomic size_t weak_count;
    _Atomic unsigned int implicit_weak_state;
    size_t value_offset;
    size_t allocation_alignment;
};

enum {
    R_RUNTIME_ARC_IMPLICIT_WEAK_LIVE = 0U,
    R_RUNTIME_ARC_IMPLICIT_WEAK_RELEASING = 1U,
    R_RUNTIME_ARC_IMPLICIT_WEAK_RELEASED = 2U
};

static _Bool r_runtime_arc_align_up(size_t value, size_t alignment, size_t *result) {
    size_t mask = alignment - 1U;

    if (value > (SIZE_MAX - mask)) {
        return 0;
    }
    *result = (value + mask) & ~mask;
    return 1;
}

static void *r_runtime_arc_value(RRuntimeArcControl *control) {
    return (unsigned char *)control + control->value_offset;
}

static const void *r_runtime_arc_value_const(const RRuntimeArcControl *control) {
    return (const unsigned char *)control + control->value_offset;
}

static void r_runtime_arc_move(RRuntimeTypeInfo type, void *destination, void *source) {
    if (type.move_initialize != NULL) {
        type.move_initialize(destination, source);
    } else {
        (void)memcpy(destination, source, type.size);
    }
}

static RRuntimeArcStatus r_runtime_arc_increment(_Atomic size_t *count) {
    size_t current = atomic_load_explicit(count, memory_order_relaxed);
    const size_t maximum = SIZE_MAX / 2U;

    for (;;) {
        if (current >= maximum) {
            return R_RUNTIME_ARC_COUNT_OVERFLOW;
        }
        if (atomic_compare_exchange_weak_explicit(
                count, &current, current + 1U, memory_order_relaxed, memory_order_relaxed)) {
            return R_RUNTIME_ARC_OK;
        }
    }
}

static RRuntimeArcStatus r_runtime_arc_increment_weak(RRuntimeArcControl *control) {
    size_t current = atomic_load_explicit(&control->weak_count, memory_order_relaxed);
    const size_t maximum_explicit = SIZE_MAX / 2U;
    const size_t locked = SIZE_MAX;

    for (;;) {
        if (current == locked) {
            current = atomic_load_explicit(&control->weak_count, memory_order_relaxed);
            continue;
        }
        if (current > maximum_explicit) {
            return R_RUNTIME_ARC_COUNT_OVERFLOW;
        }
        if (current == maximum_explicit) {
            unsigned int implicit_weak_state =
                atomic_load_explicit(&control->implicit_weak_state, memory_order_acquire);

            if (implicit_weak_state == R_RUNTIME_ARC_IMPLICIT_WEAK_RELEASING) {
                current = atomic_load_explicit(&control->weak_count, memory_order_relaxed);
                continue;
            }
            if (implicit_weak_state == R_RUNTIME_ARC_IMPLICIT_WEAK_RELEASED) {
                size_t confirmed = atomic_load_explicit(&control->weak_count, memory_order_relaxed);

                if (confirmed != current) {
                    current = confirmed;
                    continue;
                }
                return R_RUNTIME_ARC_COUNT_OVERFLOW;
            }
        }
        if (atomic_compare_exchange_weak_explicit(&control->weak_count,
                                                  &current,
                                                  current + 1U,
                                                  memory_order_relaxed,
                                                  memory_order_relaxed)) {
            return R_RUNTIME_ARC_OK;
        }
    }
}

static void r_runtime_arc_free_control(RRuntimeArcControl *control) {
    size_t alignment;

    alignment = control->allocation_alignment;
    r_runtime_allocator_deallocate(control, alignment);
}

static void r_runtime_arc_release_implicit_weak(RRuntimeArcControl *control) {
    size_t previous;

    atomic_store_explicit(
        &control->implicit_weak_state, R_RUNTIME_ARC_IMPLICIT_WEAK_RELEASING, memory_order_release);
    previous = atomic_fetch_sub_explicit(&control->weak_count, 1U, memory_order_release);

    if (previous == 1U) {
        atomic_thread_fence(memory_order_acquire);
        r_runtime_arc_free_control(control);
        return;
    }
    atomic_store_explicit(
        &control->implicit_weak_state, R_RUNTIME_ARC_IMPLICIT_WEAK_RELEASED, memory_order_release);
}

RRuntimeArcStatus r_runtime_arc_create(RRuntimeAllocator *allocator,
                                       RRuntimeTypeInfo type,
                                       void *value,
                                       RRuntimeArc *result) {
    size_t prefix_size;
    size_t value_offset;
    size_t allocation_size;
    size_t alignment;
    void *allocation = NULL;
    RRuntimeAllocationStatus allocation_status;
    RRuntimeArcControl *control;

    result->control = NULL;
    if (sizeof(RRuntimeArcControl) > (SIZE_MAX - sizeof(RRuntimeArcControl *))) {
        return R_RUNTIME_ARC_SIZE_OVERFLOW;
    }
    prefix_size = sizeof(RRuntimeArcControl) + sizeof(RRuntimeArcControl *);
    if (!r_runtime_arc_align_up(prefix_size, type.alignment, &value_offset) ||
        (type.size > (SIZE_MAX - value_offset))) {
        return R_RUNTIME_ARC_SIZE_OVERFLOW;
    }
    allocation_size = value_offset + type.size;
    alignment = type.alignment > _Alignof(RRuntimeArcControl) ? type.alignment
                                                              : _Alignof(RRuntimeArcControl);
    allocation_status =
        r_runtime_allocator_allocate(allocator, allocation_size, alignment, &allocation);
    if (allocation_status == R_RUNTIME_ALLOCATION_EXHAUSTED) {
        return R_RUNTIME_ARC_ALLOCATION_FAILED;
    }
    if (allocation_status == R_RUNTIME_ALLOCATION_SIZE_OVERFLOW) {
        return R_RUNTIME_ARC_SIZE_OVERFLOW;
    }
    if (allocation_status == R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT) {
        return R_RUNTIME_ARC_UNSUPPORTED_ALIGNMENT;
    }
    control = allocation;
    control->allocator = allocator;
    control->type = type;
    atomic_init(&control->strong_count, 1U);
    atomic_init(&control->weak_count, 1U);
    atomic_init(&control->implicit_weak_state, R_RUNTIME_ARC_IMPLICIT_WEAK_LIVE);
    control->value_offset = value_offset;
    control->allocation_alignment = alignment;
    *(RRuntimeArcControl **)((unsigned char *)control + value_offset - sizeof(control)) = control;
    r_runtime_arc_move(type, r_runtime_arc_value(control), value);
    result->control = control;
    return R_RUNTIME_ARC_OK;
}

RRuntimeArcStatus r_runtime_arc_clone(const RRuntimeArc *source, RRuntimeArc *result) {
    RRuntimeArcStatus status;

    result->control = NULL;
    status = r_runtime_arc_increment(&source->control->strong_count);
    if (status == R_RUNTIME_ARC_OK) {
        result->control = source->control;
    }
    return status;
}

RRuntimeArcStatus r_runtime_arc_downgrade(const RRuntimeArc *source, RRuntimeWeakArc *result) {
    RRuntimeArcStatus status;

    result->control = NULL;
    status = r_runtime_arc_increment_weak(source->control);
    if (status == R_RUNTIME_ARC_OK) {
        result->control = source->control;
    }
    return status;
}

RRuntimeArcStatus r_runtime_weak_arc_clone(const RRuntimeWeakArc *source, RRuntimeWeakArc *result) {
    RRuntimeArcStatus status;

    result->control = NULL;
    status = r_runtime_arc_increment_weak(source->control);
    if (status == R_RUNTIME_ARC_OK) {
        result->control = source->control;
    }
    return status;
}

RRuntimeArcStatus r_runtime_weak_arc_upgrade(const RRuntimeWeakArc *source, RRuntimeArc *result) {
    size_t current;
    const size_t maximum = SIZE_MAX / 2U;

    result->control = NULL;
    current = atomic_load_explicit(&source->control->strong_count, memory_order_relaxed);
    for (;;) {
        if (current == 0U) {
            return R_RUNTIME_ARC_EXPIRED;
        }
        if (current >= maximum) {
            return R_RUNTIME_ARC_COUNT_OVERFLOW;
        }
        if (atomic_compare_exchange_weak_explicit(&source->control->strong_count,
                                                  &current,
                                                  current + 1U,
                                                  memory_order_acquire,
                                                  memory_order_relaxed)) {
            result->control = source->control;
            return R_RUNTIME_ARC_OK;
        }
    }
}

const void *r_runtime_arc_get(const RRuntimeArc *owner) {
    return r_runtime_arc_value_const(owner->control);
}

void *r_runtime_arc_get_mut(RRuntimeArc *owner) {
    const size_t locked = SIZE_MAX;
    size_t expected = 1U;
    size_t strong;

    if (!atomic_compare_exchange_strong_explicit(&owner->control->weak_count,
                                                 &expected,
                                                 locked,
                                                 memory_order_acquire,
                                                 memory_order_relaxed)) {
        return NULL;
    }
    strong = atomic_load_explicit(&owner->control->strong_count, memory_order_acquire);
    atomic_store_explicit(&owner->control->weak_count, 1U, memory_order_release);
    if (strong != 1U) {
        return NULL;
    }
    return r_runtime_arc_value(owner->control);
}

size_t r_runtime_arc_strong_count(const RRuntimeArc *owner) {
    return atomic_load_explicit(&owner->control->strong_count, memory_order_relaxed);
}

size_t r_runtime_arc_weak_count(const RRuntimeArc *owner) {
    size_t weak;
    unsigned int before;
    unsigned int after;
    const size_t locked = SIZE_MAX;

    for (;;) {
        before = atomic_load_explicit(&owner->control->implicit_weak_state, memory_order_acquire);
        if (before == R_RUNTIME_ARC_IMPLICIT_WEAK_RELEASING) {
            continue;
        }
        weak = atomic_load_explicit(&owner->control->weak_count, memory_order_relaxed);
        after = atomic_load_explicit(&owner->control->implicit_weak_state, memory_order_acquire);
        if ((weak != locked) && (before == after)) {
            break;
        }
    }
    return weak - (before == R_RUNTIME_ARC_IMPLICIT_WEAK_LIVE ? 1U : 0U);
}

_Bool r_runtime_arc_ptr_eq(const RRuntimeArc *left, const RRuntimeArc *right) {
    return left->control == right->control;
}

RRuntimeArcStatus r_runtime_arc_try_unwrap(RRuntimeArc *owner, void *result) {
    size_t expected = 1U;
    RRuntimeArcControl *control;

    control = owner->control;
    if (!atomic_compare_exchange_strong_explicit(
            &control->strong_count, &expected, 0U, memory_order_acq_rel, memory_order_relaxed)) {
        return R_RUNTIME_ARC_NOT_UNIQUE;
    }
    r_runtime_arc_move(control->type, result, r_runtime_arc_value(control));
    owner->control = NULL;
    r_runtime_arc_release_implicit_weak(control);
    return R_RUNTIME_ARC_OK;
}

const void *r_runtime_arc_into_raw(RRuntimeArc *owner) {
    const void *value;

    value = r_runtime_arc_value_const(owner->control);
    owner->control = NULL;
    return value;
}

RRuntimeArcStatus r_runtime_arc_from_raw(const void *pointer, RRuntimeArc *result) {
    RRuntimeArcControl *control;

    control = *(RRuntimeArcControl *const *)((const unsigned char *)pointer - sizeof(control));
    result->control = control;
    return R_RUNTIME_ARC_OK;
}

void r_runtime_arc_release(RRuntimeArc *owner) {
    RRuntimeArcControl *control;
    size_t previous;

    if (owner->control == NULL) {
        return;
    }
    control = owner->control;
    owner->control = NULL;
    previous = atomic_fetch_sub_explicit(&control->strong_count, 1U, memory_order_release);
    if (previous == 1U) {
        atomic_thread_fence(memory_order_acquire);
        if (control->type.drop != NULL) {
            control->type.drop(r_runtime_arc_value(control));
        }
        r_runtime_arc_release_implicit_weak(control);
    }
}

void r_runtime_weak_arc_release(RRuntimeWeakArc *owner) {
    RRuntimeArcControl *control;
    size_t previous;

    if (owner->control == NULL) {
        return;
    }
    control = owner->control;
    owner->control = NULL;
    previous = atomic_fetch_sub_explicit(&control->weak_count, 1U, memory_order_release);
    if (previous == 1U) {
        atomic_thread_fence(memory_order_acquire);
        r_runtime_arc_free_control(control);
    }
}

void *r_runtime_arc_destroy_begin(RRuntimeArc *owner) {
    RRuntimeArcControl *control = owner->control;
    size_t previous;

    if (control == NULL) {
        return NULL;
    }
    previous = atomic_fetch_sub_explicit(&control->strong_count, 1U, memory_order_release);
    if (previous != 1U) {
        owner->control = NULL;
        return NULL;
    }
    atomic_thread_fence(memory_order_acquire);
    return r_runtime_arc_value(control);
}

void *r_runtime_arc_destroy_value(RRuntimeArc *owner) {
    return owner->control == NULL ? NULL : r_runtime_arc_value(owner->control);
}

RRuntimeTypeInfo *r_runtime_arc_destroy_scratch(RRuntimeArc *owner) {
    return &owner->control->type;
}

void r_runtime_arc_destroy_finish(RRuntimeArc *owner) {
    RRuntimeArcControl *control = owner->control;

    owner->control = NULL;
    r_runtime_arc_release_implicit_weak(control);
}
