module runtime.rcs;
import runtime.types;
import runtime.heap;
import runtime.types::{TypeInfo, Allocator, AllocationStatus, Rc, WeakRc, RcStatus, RcControlHandle};

// Shared owners of one thread (r_runtime_rc.h): the layout of the arc owners with plain counts.
@header("r_runtime_rc.h")
extern "C" {
}

struct RcControl {
    raw Allocator*? allocator;
    TypeInfo type;
    usize strong_count;
    usize weak_count;
    usize value_offset;
    usize allocation_alignment;
};

const usize count_limit = 9223372036854775807usize;

protected raw RcControl*? control_of(raw RcControlHandle*? handle) {
    unsafe {
        return (handle as raw void*?) as raw RcControl*?;
    }
}

protected raw RcControlHandle*? handle_of(raw RcControl*? control) {
    unsafe {
        return (control as raw void*?) as raw RcControlHandle*?;
    }
}

protected raw void*? value_of(raw RcControl*? control) {
    unsafe {
        return core::offset_bytes(control, control->value_offset);
    }
}

protected RcStatus increment_weak(raw RcControl*? control) {
    unsafe {
        if ((control->weak_count > count_limit) ||
            ((control->weak_count == count_limit) && (control->strong_count == 0usize))) {
            return RcStatus::R_RUNTIME_RC_COUNT_OVERFLOW;
        }
        control->weak_count += 1usize;
    }
    return RcStatus::R_RUNTIME_RC_OK;
}

protected void free_control(raw RcControl*? control) {
    unsafe {
        runtime.heap::r_runtime_allocator_deallocate(control as raw void*?, control->allocation_alignment as c_size);
    }
}

protected void release_implicit_weak(raw RcControl*? control) {
    unsafe {
        control->weak_count -= 1usize;
        if (control->weak_count == 0usize) {
            free_control(control);
        }
    }
}

@safety("R-RUNTIME-RC-CREATE", "value addresses a T of the type, which moves into the owner; result is writable")
extern "C" RcStatus r_runtime_rc_create(raw Allocator*? allocator, TypeInfo type, raw void*? value, raw Rc* result) {
    unsafe {
        result->control = null;
    }
    usize prefix = sizeof(RcControl) + sizeof(raw RcControl*);
    usize value_offset = 0usize;
    if ((runtime.types::align_up(prefix, type.alignment as usize, &value_offset) == false) ||
        ((type.size as usize) > runtime.types::size_maximum - value_offset)) {
        return RcStatus::R_RUNTIME_RC_SIZE_OVERFLOW;
    }
    usize alignment = alignof(RcControl);
    if ((type.alignment as usize) > alignment) {
        alignment = type.alignment as usize;
    }
    unsafe {
        raw void*? allocation = null;
        AllocationStatus status = runtime.heap::r_runtime_allocator_allocate(
            allocator, (value_offset + (type.size as usize)) as c_size, alignment as c_size,
            &allocation as raw (raw void*?)*);
        switch (status) {
        case AllocationStatus::R_RUNTIME_ALLOCATION_OK:
            break;
        case AllocationStatus::R_RUNTIME_ALLOCATION_EXHAUSTED:
            return RcStatus::R_RUNTIME_RC_ALLOCATION_FAILED;
        case AllocationStatus::R_RUNTIME_ALLOCATION_SIZE_OVERFLOW:
            return RcStatus::R_RUNTIME_RC_SIZE_OVERFLOW;
        case AllocationStatus::R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT:
            return RcStatus::R_RUNTIME_RC_UNSUPPORTED_ALIGNMENT;
        default:
            return RcStatus::R_RUNTIME_RC_OK;
        }
        raw RcControl*? control = allocation as raw RcControl*?;
        *control = RcControl {.allocator = allocator, .type = type, .strong_count = 1usize,
                              .weak_count = 1usize, .value_offset = value_offset,
                              .allocation_alignment = alignment};
        raw (raw RcControl*?)*? back = core::offset_bytes(control, value_offset - sizeof(raw RcControl*)) as raw (raw RcControl*?)*?;
        *back = control;
        runtime.types::move_value(type, value_of(control), value);
        result->control = handle_of(control);
    }
    return RcStatus::R_RUNTIME_RC_OK;
}

@safety("R-RUNTIME-RC-CLONE", "source holds a value; result is writable")
extern "C" RcStatus r_runtime_rc_clone(raw const Rc* source, raw Rc* result) {
    unsafe {
        result->control = null;
        raw RcControl*? control = control_of(source->control);
        if (control->strong_count >= count_limit) {
            return RcStatus::R_RUNTIME_RC_COUNT_OVERFLOW;
        }
        control->strong_count += 1usize;
        result->control = source->control;
    }
    return RcStatus::R_RUNTIME_RC_OK;
}

@safety("R-RUNTIME-RC-DOWNGRADE", "source holds a value; result is writable")
extern "C" RcStatus r_runtime_rc_downgrade(raw const Rc* source, raw WeakRc* result) {
    unsafe {
        result->control = null;
        RcStatus status = increment_weak(control_of(source->control));
        if (status == RcStatus::R_RUNTIME_RC_OK) {
            result->control = source->control;
        }
        return status;
    }
}

@safety("R-RUNTIME-WEAK-RC-CLONE", "source holds a control block; result is writable")
extern "C" RcStatus r_runtime_weak_rc_clone(raw const WeakRc* source, raw WeakRc* result) {
    unsafe {
        result->control = null;
        RcStatus status = increment_weak(control_of(source->control));
        if (status == RcStatus::R_RUNTIME_RC_OK) {
            result->control = source->control;
        }
        return status;
    }
}

@safety("R-RUNTIME-WEAK-RC-UPGRADE", "source holds a control block; result is writable")
extern "C" RcStatus r_runtime_weak_rc_upgrade(raw const WeakRc* source, raw Rc* result) {
    unsafe {
        result->control = null;
        raw RcControl*? control = control_of(source->control);
        if (control->strong_count == 0usize) {
            return RcStatus::R_RUNTIME_RC_EXPIRED;
        }
        if (control->strong_count >= count_limit) {
            return RcStatus::R_RUNTIME_RC_COUNT_OVERFLOW;
        }
        control->strong_count += 1usize;
        result->control = source->control;
    }
    return RcStatus::R_RUNTIME_RC_OK;
}

@safety("R-RUNTIME-RC-GET", "owner holds a value")
extern "C" raw const void*? r_runtime_rc_get(raw const Rc* owner) {
    unsafe {
        return value_of(control_of(owner->control)) as raw const void*?;
    }
}

@safety("R-RUNTIME-RC-GET-MUT", "owner holds a value")
extern "C" raw void*? r_runtime_rc_get_mut(raw Rc* owner) {
    unsafe {
        raw RcControl*? control = control_of(owner->control);
        if ((control->strong_count != 1usize) || (control->weak_count != 1usize)) {
            return null;
        }
        return value_of(control);
    }
}

@safety("R-RUNTIME-RC-STRONG-COUNT", "owner holds a value")
extern "C" c_size r_runtime_rc_strong_count(raw const Rc* owner) {
    unsafe {
        raw RcControl*? control = control_of(owner->control);
        return control->strong_count as c_size;
    }
}

@safety("R-RUNTIME-RC-WEAK-COUNT", "owner holds a value")
extern "C" c_size r_runtime_rc_weak_count(raw const Rc* owner) {
    unsafe {
        raw RcControl*? control = control_of(owner->control);
        if (control->strong_count == 0usize) {
            return control->weak_count as c_size;
        }
        return (control->weak_count - 1usize) as c_size;
    }
}

@safety("R-RUNTIME-RC-PTR-EQ", "both owners are initialized")
extern "C" c_bool r_runtime_rc_ptr_eq(raw const Rc* left, raw const Rc* right) {
    unsafe {
        if (left->control == right->control) {
            return 1i32 as c_bool;
        }
    }
    return 0i32 as c_bool;
}

@safety("R-RUNTIME-RC-TRY-UNWRAP", "owner holds a value; result addresses storage for one T")
extern "C" RcStatus r_runtime_rc_try_unwrap(raw Rc* owner, raw void*? result) {
    unsafe {
        raw RcControl*? control = control_of(owner->control);
        if (control->strong_count != 1usize) {
            return RcStatus::R_RUNTIME_RC_NOT_UNIQUE;
        }
        control->strong_count = 0usize;
        runtime.types::move_value(control->type, result, value_of(control));
        owner->control = null;
        release_implicit_weak(control);
    }
    return RcStatus::R_RUNTIME_RC_OK;
}

@safety("R-RUNTIME-RC-INTO-RAW", "owner holds a value")
extern "C" raw const void*? r_runtime_rc_into_raw(raw Rc* owner) {
    unsafe {
        raw const void*? value = value_of(control_of(owner->control)) as raw const void*?;
        owner->control = null;
        return value;
    }
}

@safety("R-RUNTIME-RC-FROM-RAW", "pointer came from r_runtime_rc_into_raw; result is writable")
extern "C" RcStatus r_runtime_rc_from_raw(raw const void*? pointer, raw Rc* result) {
    unsafe {
        isize back = 0isize - (sizeof(raw RcControl*) as isize);
        raw const (raw RcControl*?)*? slot = core::offset_bytes(pointer, back) as raw const (raw RcControl*?)*?;
        result->control = handle_of(*slot);
    }
    return RcStatus::R_RUNTIME_RC_OK;
}

@safety("R-RUNTIME-RC-RELEASE", "owner is initialized")
extern "C" void r_runtime_rc_release(raw Rc* owner) {
    unsafe {
        if (owner->control == null) {
            return;
        }
        raw RcControl*? control = control_of(owner->control);
        owner->control = null;
        control->strong_count -= 1usize;
        if (control->strong_count == 0usize) {
            raw fn?(raw void*?) -> void drop_glue = control->type.drop_value;
            if (drop_glue != null) {
                drop_glue(value_of(control));
            }
            release_implicit_weak(control);
        }
    }
}

@safety("R-RUNTIME-WEAK-RC-RELEASE", "owner is initialized")
extern "C" void r_runtime_weak_rc_release(raw WeakRc* owner) {
    unsafe {
        if (owner->control == null) {
            return;
        }
        raw RcControl*? control = control_of(owner->control);
        owner->control = null;
        control->weak_count -= 1usize;
        if (control->weak_count == 0usize) {
            free_control(control);
        }
    }
}

@safety("R-RUNTIME-RC-DESTROY-BEGIN", "owner is initialized; it is consumed")
extern "C" raw void*? r_runtime_rc_destroy_begin(raw Rc*? owner) {
    unsafe {
        if (owner->control == null) {
            return null;
        }
        raw RcControl*? control = control_of(owner->control);
        control->strong_count -= 1usize;
        if (control->strong_count != 0usize) {
            owner->control = null;
            return null;
        }
        return value_of(control);
    }
}

@safety("R-RUNTIME-RC-DESTROY-VALUE", "owner is being destroyed")
extern "C" raw void*? r_runtime_rc_destroy_value(raw Rc*? owner) {
    unsafe {
        if (owner->control == null) {
            return null;
        }
        return value_of(control_of(owner->control));
    }
}

@safety("R-RUNTIME-RC-DESTROY-SCRATCH", "owner is being destroyed")
extern "C" raw TypeInfo* r_runtime_rc_destroy_scratch(raw Rc*? owner) {
    unsafe {
        raw RcControl*? control = control_of(owner->control);
        return &control->type as raw TypeInfo*;
    }
}

@safety("R-RUNTIME-RC-DESTROY-FINISH", "the value of the owner is destroyed")
extern "C" void r_runtime_rc_destroy_finish(raw Rc*? owner) {
    unsafe {
        raw RcControl*? control = control_of(owner->control);
        owner->control = null;
        release_implicit_weak(control);
    }
}
