#include "r_runtime_rc.h"

#include <stddef.h>
#include <string.h>

struct RRuntimeRcControl {
    RRuntimeAllocator *allocator;
    RRuntimeTypeInfo type;
    size_t strong_count;
    size_t weak_count;
    size_t value_offset;
    size_t allocation_alignment;
};

static _Bool r_runtime_rc_align_up(size_t value, size_t alignment, size_t *result) {
    size_t mask = alignment - 1U;

    if (value > (SIZE_MAX - mask)) {
        return 0;
    }
    *result = (value + mask) & ~mask;
    return 1;
}

static void *r_runtime_rc_value(RRuntimeRcControl *control) {
    return (unsigned char *)control + control->value_offset;
}

static const void *r_runtime_rc_value_const(const RRuntimeRcControl *control) {
    return (const unsigned char *)control + control->value_offset;
}

static void r_runtime_rc_move(RRuntimeTypeInfo type, void *destination, void *source) {
    if (type.move_initialize != NULL) {
        type.move_initialize(destination, source);
    } else {
        (void)memcpy(destination, source, type.size);
    }
}

static RRuntimeRcStatus r_runtime_rc_increment_live(size_t *count) {
    if (*count >= (SIZE_MAX / 2U)) {
        return R_RUNTIME_RC_COUNT_OVERFLOW;
    }
    *count += 1U;
    return R_RUNTIME_RC_OK;
}

static RRuntimeRcStatus r_runtime_rc_try_increment(size_t *count) {
    if (*count == 0U) {
        return R_RUNTIME_RC_EXPIRED;
    }
    if (*count >= (SIZE_MAX / 2U)) {
        return R_RUNTIME_RC_COUNT_OVERFLOW;
    }
    *count += 1U;
    return R_RUNTIME_RC_OK;
}

static RRuntimeRcStatus r_runtime_rc_increment_weak(RRuntimeRcControl *control) {
    const size_t maximum_explicit = SIZE_MAX / 2U;

    if ((control->weak_count > maximum_explicit) ||
        ((control->weak_count == maximum_explicit) && (control->strong_count == 0U))) {
        return R_RUNTIME_RC_COUNT_OVERFLOW;
    }
    control->weak_count += 1U;
    return R_RUNTIME_RC_OK;
}

static void r_runtime_rc_free_control(RRuntimeRcControl *control) {
    size_t alignment;

    alignment = control->allocation_alignment;
    r_runtime_allocator_deallocate(control, alignment);
}

static void r_runtime_rc_release_implicit_weak(RRuntimeRcControl *control) {
    control->weak_count -= 1U;
    if (control->weak_count == 0U) {
        r_runtime_rc_free_control(control);
    }
}

RRuntimeRcStatus r_runtime_rc_create(RRuntimeAllocator *allocator,
                                     RRuntimeTypeInfo type,
                                     void *value,
                                     RRuntimeRc *result) {
    size_t prefix_size;
    size_t value_offset;
    size_t allocation_size;
    size_t alignment;
    void *allocation = NULL;
    RRuntimeAllocationStatus allocation_status;
    RRuntimeRcControl *control;

    result->control = NULL;
    if (sizeof(RRuntimeRcControl) > (SIZE_MAX - sizeof(RRuntimeRcControl *))) {
        return R_RUNTIME_RC_SIZE_OVERFLOW;
    }
    prefix_size = sizeof(RRuntimeRcControl) + sizeof(RRuntimeRcControl *);
    if (!r_runtime_rc_align_up(prefix_size, type.alignment, &value_offset) ||
        (type.size > (SIZE_MAX - value_offset))) {
        return R_RUNTIME_RC_SIZE_OVERFLOW;
    }
    allocation_size = value_offset + type.size;
    alignment =
        type.alignment > _Alignof(RRuntimeRcControl) ? type.alignment : _Alignof(RRuntimeRcControl);
    allocation_status =
        r_runtime_allocator_allocate(allocator, allocation_size, alignment, &allocation);
    if (allocation_status == R_RUNTIME_ALLOCATION_EXHAUSTED) {
        return R_RUNTIME_RC_ALLOCATION_FAILED;
    }
    if (allocation_status == R_RUNTIME_ALLOCATION_SIZE_OVERFLOW) {
        return R_RUNTIME_RC_SIZE_OVERFLOW;
    }
    if (allocation_status == R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT) {
        return R_RUNTIME_RC_UNSUPPORTED_ALIGNMENT;
    }
    control = allocation;
    control->allocator = allocator;
    control->type = type;
    control->strong_count = 1U;
    control->weak_count = 1U;
    control->value_offset = value_offset;
    control->allocation_alignment = alignment;
    *(RRuntimeRcControl **)((unsigned char *)control + value_offset - sizeof(control)) = control;
    r_runtime_rc_move(type, r_runtime_rc_value(control), value);
    result->control = control;
    return R_RUNTIME_RC_OK;
}

RRuntimeRcStatus r_runtime_rc_clone(const RRuntimeRc *source, RRuntimeRc *result) {
    RRuntimeRcStatus status;

    result->control = NULL;
    status = r_runtime_rc_increment_live(&source->control->strong_count);
    if (status == R_RUNTIME_RC_OK) {
        result->control = source->control;
    }
    return status;
}

RRuntimeRcStatus r_runtime_rc_downgrade(const RRuntimeRc *source, RRuntimeWeakRc *result) {
    RRuntimeRcStatus status;

    result->control = NULL;
    status = r_runtime_rc_increment_weak(source->control);
    if (status == R_RUNTIME_RC_OK) {
        result->control = source->control;
    }
    return status;
}

RRuntimeRcStatus r_runtime_weak_rc_clone(const RRuntimeWeakRc *source, RRuntimeWeakRc *result) {
    RRuntimeRcStatus status;

    result->control = NULL;
    status = r_runtime_rc_increment_weak(source->control);
    if (status == R_RUNTIME_RC_OK) {
        result->control = source->control;
    }
    return status;
}

RRuntimeRcStatus r_runtime_weak_rc_upgrade(const RRuntimeWeakRc *source, RRuntimeRc *result) {
    RRuntimeRcStatus status;

    result->control = NULL;
    status = r_runtime_rc_try_increment(&source->control->strong_count);
    if (status == R_RUNTIME_RC_OK) {
        result->control = source->control;
    }
    return status;
}

const void *r_runtime_rc_get(const RRuntimeRc *owner) {
    return r_runtime_rc_value_const(owner->control);
}

void *r_runtime_rc_get_mut(RRuntimeRc *owner) {
    if ((owner->control->strong_count != 1U) || (owner->control->weak_count != 1U)) {
        return NULL;
    }
    return r_runtime_rc_value(owner->control);
}

size_t r_runtime_rc_strong_count(const RRuntimeRc *owner) {
    return owner->control->strong_count;
}

size_t r_runtime_rc_weak_count(const RRuntimeRc *owner) {
    return owner->control->weak_count - (owner->control->strong_count == 0U ? 0U : 1U);
}

_Bool r_runtime_rc_ptr_eq(const RRuntimeRc *left, const RRuntimeRc *right) {
    return left->control == right->control;
}

RRuntimeRcStatus r_runtime_rc_try_unwrap(RRuntimeRc *owner, void *result) {
    RRuntimeRcControl *control;

    control = owner->control;
    if (control->strong_count != 1U) {
        return R_RUNTIME_RC_NOT_UNIQUE;
    }
    control->strong_count = 0U;
    r_runtime_rc_move(control->type, result, r_runtime_rc_value(control));
    owner->control = NULL;
    r_runtime_rc_release_implicit_weak(control);
    return R_RUNTIME_RC_OK;
}

const void *r_runtime_rc_into_raw(RRuntimeRc *owner) {
    const void *value;

    value = r_runtime_rc_value_const(owner->control);
    owner->control = NULL;
    return value;
}

RRuntimeRcStatus r_runtime_rc_from_raw(const void *pointer, RRuntimeRc *result) {
    RRuntimeRcControl *control;

    control = *(RRuntimeRcControl *const *)((const unsigned char *)pointer - sizeof(control));
    result->control = control;
    return R_RUNTIME_RC_OK;
}

void r_runtime_rc_release(RRuntimeRc *owner) {
    RRuntimeRcControl *control;

    if (owner->control == NULL) {
        return;
    }
    control = owner->control;
    owner->control = NULL;
    control->strong_count -= 1U;
    if (control->strong_count == 0U) {
        if (control->type.drop != NULL) {
            control->type.drop(r_runtime_rc_value(control));
        }
        r_runtime_rc_release_implicit_weak(control);
    }
}

void r_runtime_weak_rc_release(RRuntimeWeakRc *owner) {
    RRuntimeRcControl *control;

    if (owner->control == NULL) {
        return;
    }
    control = owner->control;
    owner->control = NULL;
    control->weak_count -= 1U;
    if (control->weak_count == 0U) {
        r_runtime_rc_free_control(control);
    }
}

void *r_runtime_rc_destroy_begin(RRuntimeRc *owner) {
    RRuntimeRcControl *control = owner->control;

    if (control == NULL) {
        return NULL;
    }
    control->strong_count -= 1U;
    if (control->strong_count != 0U) {
        owner->control = NULL;
        return NULL;
    }
    return r_runtime_rc_value(control);
}

void *r_runtime_rc_destroy_value(RRuntimeRc *owner) {
    return owner->control == NULL ? NULL : r_runtime_rc_value(owner->control);
}

RRuntimeTypeInfo *r_runtime_rc_destroy_scratch(RRuntimeRc *owner) {
    return &owner->control->type;
}

void r_runtime_rc_destroy_finish(RRuntimeRc *owner) {
    RRuntimeRcControl *control = owner->control;

    owner->control = NULL;
    r_runtime_rc_release_implicit_weak(control);
}
