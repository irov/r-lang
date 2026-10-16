module runtime.arcs;
import runtime.types;
import runtime.heap;
import runtime.types::{TypeInfo, Allocator, AllocationStatus, Arc, WeakArc, ArcStatus, ArcControlHandle};

// Shared owners with atomic counts (r_runtime_arc.h). One allocation holds the control block,
// a pointer back to it and, at the value's alignment, the value. The strong owners together hold
// one implicit weak reference, released after the value is dropped.
@header("r_runtime_arc.h")
extern "C" {
}

struct ArcControl {
    raw Allocator*? allocator;
    TypeInfo type;
    atomic usize strong_count;
    atomic usize weak_count;
    atomic u32 implicit_weak_state;
    usize value_offset;
    usize allocation_alignment;
};

const u32 implicit_weak_live = 0u32;
const u32 implicit_weak_releasing = 1u32;
const u32 implicit_weak_released = 2u32;
const usize count_limit = 9223372036854775807usize;

protected raw ArcControl*? control_of(raw ArcControlHandle*? handle) {
    unsafe {
        return (handle as raw void*?) as raw ArcControl*?;
    }
}

protected raw ArcControlHandle*? handle_of(raw ArcControl*? control) {
    unsafe {
        return (control as raw void*?) as raw ArcControlHandle*?;
    }
}

protected raw void*? value_of(raw ArcControl*? control) {
    unsafe {
        return core::offset_bytes(control, control->value_offset);
    }
}

protected ArcStatus increment(const (atomic usize)* count) {
    usize current = core::atomic_load(count, core::memory_order::relaxed);
    while (true) {
        if (current >= count_limit) {
            return ArcStatus::R_RUNTIME_ARC_COUNT_OVERFLOW;
        }
        if (runtime.types::exchanged(core::atomic_compare_exchange(count, current, current + 1usize, core::memory_order::relaxed, core::memory_order::relaxed), &current) == true) {
            return ArcStatus::R_RUNTIME_ARC_OK;
        }
    }
}

protected ArcStatus increment_weak(raw ArcControl*? control) {
    unsafe {
        usize current = core::atomic_load(&control->weak_count, core::memory_order::relaxed);
        while (true) {
            if (current == runtime.types::size_maximum) {
                // A get_mut holds the weak count locked; it restores it at once.
                current = core::atomic_load(&control->weak_count, core::memory_order::relaxed);
                continue;
            }
            if (current > count_limit) {
                return ArcStatus::R_RUNTIME_ARC_COUNT_OVERFLOW;
            }
            if (current == count_limit) {
                u32 state = core::atomic_load(&control->implicit_weak_state, core::memory_order::acquire);
                if (state == implicit_weak_releasing) {
                    current = core::atomic_load(&control->weak_count, core::memory_order::relaxed);
                    continue;
                }
                if (state == implicit_weak_released) {
                    usize confirmed = core::atomic_load(&control->weak_count, core::memory_order::relaxed);
                    if (confirmed != current) {
                        current = confirmed;
                        continue;
                    }
                    return ArcStatus::R_RUNTIME_ARC_COUNT_OVERFLOW;
                }
            }
            if (runtime.types::exchanged(core::atomic_compare_exchange(&control->weak_count, current, current + 1usize, core::memory_order::relaxed, core::memory_order::relaxed), &current) == true) {
                return ArcStatus::R_RUNTIME_ARC_OK;
            }
        }
    }
}

protected void free_control(raw ArcControl*? control) {
    unsafe {
        runtime.heap::r_runtime_allocator_deallocate(control as raw void*?, control->allocation_alignment as c_size);
    }
}

protected void release_implicit_weak(raw ArcControl*? control) {
    unsafe {
        core::atomic_store(&control->implicit_weak_state, implicit_weak_releasing, core::memory_order::release);
        // acq_rel rather than a release decrement and an acquire fence: the same order, and one
        // that ThreadSanitizer, which models no standalone fence, sees (M44-1).
        usize previous = core::atomic_fetch_sub(&control->weak_count, 1usize, core::memory_order::acq_rel);
        if (previous == 1usize) {
            free_control(control);
            return;
        }
        core::atomic_store(&control->implicit_weak_state, implicit_weak_released, core::memory_order::release);
    }
}

protected ArcStatus from_allocation(AllocationStatus status) {
    switch (status) {
    case AllocationStatus::R_RUNTIME_ALLOCATION_EXHAUSTED:
        return ArcStatus::R_RUNTIME_ARC_ALLOCATION_FAILED;
    case AllocationStatus::R_RUNTIME_ALLOCATION_SIZE_OVERFLOW:
        return ArcStatus::R_RUNTIME_ARC_SIZE_OVERFLOW;
    case AllocationStatus::R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT:
        return ArcStatus::R_RUNTIME_ARC_UNSUPPORTED_ALIGNMENT;
    default:
        return ArcStatus::R_RUNTIME_ARC_OK;
    }
}

@safety("R-RUNTIME-ARC-CREATE", "value addresses a T of the type, which moves into the owner; result is writable")
extern "C" ArcStatus r_runtime_arc_create(raw Allocator*? allocator, TypeInfo type, raw void*? value, raw Arc* result) {
    unsafe {
        result->control = null;
    }
    usize prefix = sizeof(ArcControl) + sizeof(raw ArcControl*);
    usize value_offset = 0usize;
    if ((runtime.types::align_up(prefix, type.alignment as usize, &value_offset) == false) ||
        ((type.size as usize) > runtime.types::size_maximum - value_offset)) {
        return ArcStatus::R_RUNTIME_ARC_SIZE_OVERFLOW;
    }
    usize alignment = alignof(ArcControl);
    if ((type.alignment as usize) > alignment) {
        alignment = type.alignment as usize;
    }
    unsafe {
        raw void*? allocation = null;
        AllocationStatus status = runtime.heap::r_runtime_allocator_allocate(
            allocator, (value_offset + (type.size as usize)) as c_size, alignment as c_size,
            &allocation as raw (raw void*?)*);
        if (status != AllocationStatus::R_RUNTIME_ALLOCATION_OK) {
            return from_allocation(status);
        }
        raw ArcControl*? control = allocation as raw ArcControl*?;
        *control = ArcControl {.allocator = allocator, .type = type, .strong_count = 1usize,
                               .weak_count = 1usize, .implicit_weak_state = implicit_weak_live,
                               .value_offset = value_offset, .allocation_alignment = alignment};
        raw (raw ArcControl*?)*? back = core::offset_bytes(control, value_offset - sizeof(raw ArcControl*)) as raw (raw ArcControl*?)*?;
        *back = control;
        runtime.types::move_value(type, value_of(control), value);
        result->control = handle_of(control);
    }
    return ArcStatus::R_RUNTIME_ARC_OK;
}

@safety("R-RUNTIME-ARC-CLONE", "source holds a value; result is writable")
extern "C" ArcStatus r_runtime_arc_clone(raw const Arc* source, raw Arc* result) {
    unsafe {
        result->control = null;
        raw ArcControl*? control = control_of(source->control);
        ArcStatus status = increment(&control->strong_count);
        if (status == ArcStatus::R_RUNTIME_ARC_OK) {
            result->control = source->control;
        }
        return status;
    }
}

@safety("R-RUNTIME-ARC-DOWNGRADE", "source holds a value; result is writable")
extern "C" ArcStatus r_runtime_arc_downgrade(raw const Arc* source, raw WeakArc* result) {
    unsafe {
        result->control = null;
        ArcStatus status = increment_weak(control_of(source->control));
        if (status == ArcStatus::R_RUNTIME_ARC_OK) {
            result->control = source->control;
        }
        return status;
    }
}

@safety("R-RUNTIME-WEAK-ARC-CLONE", "source holds a control block; result is writable")
extern "C" ArcStatus r_runtime_weak_arc_clone(raw const WeakArc* source, raw WeakArc* result) {
    unsafe {
        result->control = null;
        ArcStatus status = increment_weak(control_of(source->control));
        if (status == ArcStatus::R_RUNTIME_ARC_OK) {
            result->control = source->control;
        }
        return status;
    }
}

@safety("R-RUNTIME-WEAK-ARC-UPGRADE", "source holds a control block; result is writable")
extern "C" ArcStatus r_runtime_weak_arc_upgrade(raw const WeakArc* source, raw Arc* result) {
    unsafe {
        result->control = null;
        raw ArcControl*? control = control_of(source->control);
        usize current = core::atomic_load(&control->strong_count, core::memory_order::relaxed);
        while (true) {
            if (current == 0usize) {
                return ArcStatus::R_RUNTIME_ARC_EXPIRED;
            }
            if (current >= count_limit) {
                return ArcStatus::R_RUNTIME_ARC_COUNT_OVERFLOW;
            }
            if (runtime.types::exchanged(core::atomic_compare_exchange(&control->strong_count, current, current + 1usize, core::memory_order::acquire, core::memory_order::relaxed), &current) == true) {
                result->control = source->control;
                return ArcStatus::R_RUNTIME_ARC_OK;
            }
        }
    }
}

@safety("R-RUNTIME-ARC-GET", "owner holds a value")
extern "C" raw const void*? r_runtime_arc_get(raw const Arc* owner) {
    unsafe {
        return value_of(control_of(owner->control)) as raw const void*?;
    }
}

@safety("R-RUNTIME-ARC-GET-MUT", "owner holds a value")
extern "C" raw void*? r_runtime_arc_get_mut(raw Arc* owner) {
    unsafe {
        raw ArcControl*? control = control_of(owner->control);
        usize observed = 0usize;
        // Locking the weak count keeps a downgrade from racing the uniqueness test.
        if (runtime.types::exchanged(core::atomic_compare_exchange(&control->weak_count, 1usize, runtime.types::size_maximum, core::memory_order::acquire, core::memory_order::relaxed), &observed) == false) {
            return null;
        }
        usize strong = core::atomic_load(&control->strong_count, core::memory_order::acquire);
        core::atomic_store(&control->weak_count, 1usize, core::memory_order::release);
        if (strong != 1usize) {
            return null;
        }
        return value_of(control);
    }
}

@safety("R-RUNTIME-ARC-STRONG-COUNT", "owner holds a value")
extern "C" c_size r_runtime_arc_strong_count(raw const Arc* owner) {
    unsafe {
        raw ArcControl*? control = control_of(owner->control);
        return core::atomic_load(&control->strong_count, core::memory_order::relaxed) as c_size;
    }
}

@safety("R-RUNTIME-ARC-WEAK-COUNT", "owner holds a value")
extern "C" c_size r_runtime_arc_weak_count(raw const Arc* owner) {
    unsafe {
        raw ArcControl*? control = control_of(owner->control);
        while (true) {
            u32 before = core::atomic_load(&control->implicit_weak_state, core::memory_order::acquire);
            if (before == implicit_weak_releasing) {
                continue;
            }
            usize weak_value = core::atomic_load(&control->weak_count, core::memory_order::relaxed);
            u32 after = core::atomic_load(&control->implicit_weak_state, core::memory_order::acquire);
            bool stable = after == before;
            bool unlocked = weak_value != runtime.types::size_maximum;
            if ((stable == true) && (unlocked == true)) {
                if (before == implicit_weak_live) {
                    return (weak_value - 1usize) as c_size;
                }
                return weak_value as c_size;
            }
        }
    }
}

@safety("R-RUNTIME-ARC-PTR-EQ", "both owners are initialized")
extern "C" c_bool r_runtime_arc_ptr_eq(raw const Arc* left, raw const Arc* right) {
    unsafe {
        if (left->control == right->control) {
            return 1i32 as c_bool;
        }
    }
    return 0i32 as c_bool;
}

@safety("R-RUNTIME-ARC-TRY-UNWRAP", "owner holds a value; result addresses storage for one T")
extern "C" ArcStatus r_runtime_arc_try_unwrap(raw Arc* owner, raw void*? result) {
    unsafe {
        raw ArcControl*? control = control_of(owner->control);
        usize observed = 0usize;
        if (runtime.types::exchanged(core::atomic_compare_exchange(&control->strong_count, 1usize, 0usize, core::memory_order::acq_rel, core::memory_order::relaxed), &observed) == false) {
            return ArcStatus::R_RUNTIME_ARC_NOT_UNIQUE;
        }
        runtime.types::move_value(control->type, result, value_of(control));
        owner->control = null;
        release_implicit_weak(control);
    }
    return ArcStatus::R_RUNTIME_ARC_OK;
}

@safety("R-RUNTIME-ARC-INTO-RAW", "owner holds a value")
extern "C" raw const void*? r_runtime_arc_into_raw(raw Arc* owner) {
    unsafe {
        raw const void*? value = value_of(control_of(owner->control)) as raw const void*?;
        owner->control = null;
        return value;
    }
}

@safety("R-RUNTIME-ARC-FROM-RAW", "pointer came from r_runtime_arc_into_raw; result is writable")
extern "C" ArcStatus r_runtime_arc_from_raw(raw const void*? pointer, raw Arc* result) {
    unsafe {
        isize back = 0isize - (sizeof(raw ArcControl*) as isize);
        raw const (raw ArcControl*?)*? slot = core::offset_bytes(pointer, back) as raw const (raw ArcControl*?)*?;
        result->control = handle_of(*slot);
    }
    return ArcStatus::R_RUNTIME_ARC_OK;
}

@safety("R-RUNTIME-ARC-RELEASE", "owner is initialized")
extern "C" void r_runtime_arc_release(raw Arc* owner) {
    unsafe {
        if (owner->control == null) {
            return;
        }
        raw ArcControl*? control = control_of(owner->control);
        owner->control = null;
        // The last owner sees every access of the others before it drops the value (M44-1).
        usize previous = core::atomic_fetch_sub(&control->strong_count, 1usize, core::memory_order::acq_rel);
        if (previous == 1usize) {
            raw fn?(raw void*?) -> void drop_glue = control->type.drop_value;
            if (drop_glue != null) {
                drop_glue(value_of(control));
            }
            release_implicit_weak(control);
        }
    }
}

@safety("R-RUNTIME-WEAK-ARC-RELEASE", "owner is initialized")
extern "C" void r_runtime_weak_arc_release(raw WeakArc* owner) {
    unsafe {
        if (owner->control == null) {
            return;
        }
        raw ArcControl*? control = control_of(owner->control);
        owner->control = null;
        usize previous = core::atomic_fetch_sub(&control->weak_count, 1usize, core::memory_order::acq_rel);
        if (previous == 1usize) {
            free_control(control);
        }
    }
}

@safety("R-RUNTIME-ARC-DESTROY-BEGIN", "owner is initialized; it is consumed")
extern "C" raw void*? r_runtime_arc_destroy_begin(raw Arc*? owner) {
    unsafe {
        if (owner->control == null) {
            return null;
        }
        raw ArcControl*? control = control_of(owner->control);
        usize previous = core::atomic_fetch_sub(&control->strong_count, 1usize, core::memory_order::acq_rel);
        if (previous != 1usize) {
            owner->control = null;
            return null;
        }
        return value_of(control);
    }
}

@safety("R-RUNTIME-ARC-DESTROY-VALUE", "owner is being destroyed")
extern "C" raw void*? r_runtime_arc_destroy_value(raw Arc*? owner) {
    unsafe {
        if (owner->control == null) {
            return null;
        }
        return value_of(control_of(owner->control));
    }
}

@safety("R-RUNTIME-ARC-DESTROY-SCRATCH", "owner is being destroyed")
extern "C" raw TypeInfo* r_runtime_arc_destroy_scratch(raw Arc*? owner) {
    unsafe {
        raw ArcControl*? control = control_of(owner->control);
        return &control->type as raw TypeInfo*;
    }
}

@safety("R-RUNTIME-ARC-DESTROY-FINISH", "the value of the owner is destroyed")
extern "C" void r_runtime_arc_destroy_finish(raw Arc*? owner) {
    unsafe {
        raw ArcControl*? control = control_of(owner->control);
        owner->control = null;
        release_implicit_weak(control);
    }
}
