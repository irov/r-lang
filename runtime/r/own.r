module runtime.owned;
import runtime.types;
import runtime.heap;
import runtime.types::{TypeInfo, Own, OwnStatus, AllocationStatus, Allocator};

// Single owners of one allocated value (r_runtime_own.h, Core R-OWN-0001): the exact allocation
// of the value, its type information and the alignment it was allocated with.
@header("r_runtime_own.h")
extern "C" {
}

protected Own empty_owner() {
    return Own {.allocation = null,
                .type = TypeInfo {.size = 0usize as c_size, .alignment = 0usize as c_size,
                                  .move_initialize = null, .drop_value = null},
                .allocation_alignment = 0usize as c_size};
}

protected OwnStatus allocation_failure(AllocationStatus status) {
    switch (status) {
    case AllocationStatus::R_RUNTIME_ALLOCATION_EXHAUSTED:
        return OwnStatus::R_RUNTIME_OWN_ALLOCATION_FAILED;
    case AllocationStatus::R_RUNTIME_ALLOCATION_SIZE_OVERFLOW:
        return OwnStatus::R_RUNTIME_OWN_SIZE_OVERFLOW;
    case AllocationStatus::R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT:
        return OwnStatus::R_RUNTIME_OWN_UNSUPPORTED_ALIGNMENT;
    default:
        return OwnStatus::R_RUNTIME_OWN_OK;
    }
}

@safety("R-RUNTIME-OWN-CREATE", "value addresses a T of the type, which moves into the owner; result is writable")
extern "C" OwnStatus r_runtime_own_create(raw Allocator*? allocator, TypeInfo type, raw void*? value, raw Own* result) {
    unsafe {
        *result = empty_owner();
        raw void*? memory = null;
        AllocationStatus status = runtime.heap::r_runtime_allocator_allocate(
            allocator, type.size, type.alignment, &memory as raw (raw void*?)*);
        if (status != AllocationStatus::R_RUNTIME_ALLOCATION_OK) {
            return allocation_failure(status);
        }
        runtime.types::move_value(type, memory, value);
        *result = Own {.allocation = memory, .type = type, .allocation_alignment = type.alignment};
    }
    return OwnStatus::R_RUNTIME_OWN_OK;
}

@safety("R-RUNTIME-OWN-CREATE-INITIALIZE", "initialize writes a T of the type into its destination; result is writable")
extern "C" OwnStatus r_runtime_own_create_initialize(raw Allocator*? allocator, TypeInfo type, raw fn(raw void*?, raw const void*?) -> void initialize, raw const void*? context, raw Own* result) {
    unsafe {
        *result = empty_owner();
        raw void*? memory = null;
        AllocationStatus status = runtime.heap::r_runtime_allocator_allocate(
            allocator, type.size, type.alignment, &memory as raw (raw void*?)*);
        if (status != AllocationStatus::R_RUNTIME_ALLOCATION_OK) {
            return allocation_failure(status);
        }
        initialize(memory, context);
        *result = Own {.allocation = memory, .type = type, .allocation_alignment = type.alignment};
    }
    return OwnStatus::R_RUNTIME_OWN_OK;
}

@safety("R-RUNTIME-OWN-ADOPT", "allocation is null or the runtime allocation of one T of the type; result is writable")
extern "C" OwnStatus r_runtime_own_adopt(TypeInfo type, raw void*? allocation, raw Own* result) {
    unsafe {
        *result = empty_owner();
        if ((type.size as usize) > (runtime.types::maximum_object_size as usize)) {
            return OwnStatus::R_RUNTIME_OWN_SIZE_OVERFLOW;
        }
        if ((type.alignment as usize) > (runtime.types::maximum_alignment as usize)) {
            return OwnStatus::R_RUNTIME_OWN_UNSUPPORTED_ALIGNMENT;
        }
        *result = Own {.allocation = allocation, .type = type, .allocation_alignment = type.alignment};
    }
    return OwnStatus::R_RUNTIME_OWN_OK;
}

@safety("R-RUNTIME-OWN-GET", "owner addresses an owner")
extern "C" raw const void*? r_runtime_own_get(raw const Own* owner) {
    unsafe {
        return owner->allocation as raw const void*?;
    }
}

@safety("R-RUNTIME-OWN-GET-MUT", "owner addresses an owner")
extern "C" raw void*? r_runtime_own_get_mut(raw Own* owner) {
    unsafe {
        return owner->allocation;
    }
}

@safety("R-RUNTIME-OWN-INTO-VALUE", "owner holds a value; result addresses storage for one T of its type")
extern "C" OwnStatus r_runtime_own_into_value(raw Own* owner, raw void*? result) {
    unsafe {
        raw void*? allocation = owner->allocation;
        TypeInfo type = owner->type;
        c_size alignment = owner->allocation_alignment;
        *owner = empty_owner();
        runtime.types::move_value(type, result, allocation);
        runtime.heap::r_runtime_allocator_deallocate(allocation, alignment);
    }
    return OwnStatus::R_RUNTIME_OWN_OK;
}

@safety("R-RUNTIME-OWN-INTO-RAW", "owner addresses an owner; result is writable")
extern "C" OwnStatus r_runtime_own_into_raw(raw Own* owner, raw (raw void*?)* result) {
    unsafe {
        raw void*? allocation = owner->allocation;
        *owner = empty_owner();
        *result = allocation;
    }
    return OwnStatus::R_RUNTIME_OWN_OK;
}

@safety("R-RUNTIME-OWN-RELEASE", "owner addresses an owner")
extern "C" void r_runtime_own_release(raw Own* owner) {
    unsafe {
        raw void*? allocation = owner->allocation;
        if (allocation == null) {
            return;
        }
        raw fn?(raw void*?) -> void drop_glue = owner->type.drop_value;
        c_size alignment = owner->allocation_alignment;
        *owner = empty_owner();
        if (drop_glue != null) {
            drop_glue(allocation);
        }
        runtime.heap::r_runtime_allocator_deallocate(allocation, alignment);
    }
}
