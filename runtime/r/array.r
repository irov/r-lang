module runtime.arrays;
import runtime.types;
import runtime.heap;
import runtime.types::{TypeInfo, Array, ArrayStatus, AllocationStatus, Allocator};

// Growable arrays of elements described by their type information (r_runtime_array.h): the
// elements lie contiguously at the size of their type, move through its move glue (or as bytes)
// and are destroyed through its drop glue.
@header("r_runtime_array.h")
extern "C" {
}

// The address of element `index` of a buffer of elements of `size` bytes.
protected raw void*? element_at(raw void*? data, usize index, usize size) {
    unsafe {
        return core::offset_bytes(data, index * size);
    }
}

protected ArrayStatus from_allocation(AllocationStatus status) {
    switch (status) {
    case AllocationStatus::R_RUNTIME_ALLOCATION_OK:
        return ArrayStatus::R_RUNTIME_ARRAY_OK;
    case AllocationStatus::R_RUNTIME_ALLOCATION_EXHAUSTED:
        return ArrayStatus::R_RUNTIME_ARRAY_ALLOCATION_FAILED;
    case AllocationStatus::R_RUNTIME_ALLOCATION_SIZE_OVERFLOW:
        return ArrayStatus::R_RUNTIME_ARRAY_SIZE_OVERFLOW;
    case AllocationStatus::R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT:
        return ArrayStatus::R_RUNTIME_ARRAY_UNSUPPORTED_ALIGNMENT;
    default:
        return ArrayStatus::R_RUNTIME_ARRAY_INVALID;
    }
}

protected ArrayStatus allocate_elements(raw Array* items, usize capacity, raw (raw void*?)* result) {
    unsafe {
        usize size = items->element.size as usize;
        if ((capacity != 0usize) && (size > runtime.types::size_maximum / capacity)) {
            return ArrayStatus::R_RUNTIME_ARRAY_SIZE_OVERFLOW;
        }
        return from_allocation(runtime.heap::r_runtime_allocator_allocate(
            items->allocator, (capacity * size) as c_size, items->element.alignment, result));
    }
}

protected ArrayStatus reallocate_trivial(raw Array* items, usize capacity) {
    unsafe {
        usize size = items->element.size as usize;
        if ((capacity != 0usize) && (size > runtime.types::size_maximum / capacity)) {
            return ArrayStatus::R_RUNTIME_ARRAY_SIZE_OVERFLOW;
        }
        raw void*? replacement = null;
        ArrayStatus status = from_allocation(runtime.heap::r_runtime_allocator_reallocate(
            items->allocator, items->data, ((items->capacity as usize) * size) as c_size,
            (capacity * size) as c_size, items->element.alignment,
            &replacement as raw (raw void*?)*));
        if (status != ArrayStatus::R_RUNTIME_ARRAY_OK) {
            return status;
        }
        items->data = replacement;
        items->capacity = capacity as c_size;
    }
    return ArrayStatus::R_RUNTIME_ARRAY_OK;
}

protected ArrayStatus reserve_valid(raw Array* items, usize additional) {
    unsafe {
        usize length = items->length as usize;
        if (additional > runtime.types::size_maximum - length) {
            return ArrayStatus::R_RUNTIME_ARRAY_SIZE_OVERFLOW;
        }
        usize required = length + additional;
        usize current = items->capacity as usize;
        if (required <= current) {
            return ArrayStatus::R_RUNTIME_ARRAY_OK;
        }
        usize capacity = current;
        if (capacity == 0usize) {
            capacity = 4usize;
        }
        while (capacity < required) {
            if (capacity > runtime.types::size_maximum / 2usize) {
                capacity = required;
                break;
            }
            capacity *= 2usize;
        }
        if (items->element.move_initialize == null) {
            // Trivially movable elements: one reallocation moves the block as a whole.
            return reallocate_trivial(items, capacity);
        }
        raw void*? replacement = null;
        ArrayStatus status = allocate_elements(items, capacity, &replacement as raw (raw void*?)*);
        if (status != ArrayStatus::R_RUNTIME_ARRAY_OK) {
            return status;
        }
        usize size = items->element.size as usize;
        for (usize index = 0usize; index < length; index += 1usize) {
            runtime.types::move_value(items->element, element_at(replacement, index, size),
                                      element_at(items->data, index, size));
        }
        runtime.heap::r_runtime_allocator_deallocate(items->data, items->element.alignment);
        items->data = replacement;
        items->capacity = capacity as c_size;
    }
    return ArrayStatus::R_RUNTIME_ARRAY_OK;
}

protected void clear_valid(raw Array* items) {
    unsafe {
        raw fn?(raw void*?) -> void drop_glue = items->element.drop_value;
        if (drop_glue == null) {
            items->length = 0usize as c_size;
            return;
        }
        usize size = items->element.size as usize;
        while ((items->length as usize) != 0usize) {
            usize last = (items->length as usize) - 1usize;
            items->length = last as c_size;
            drop_glue(element_at(items->data, last, size));
        }
    }
}

@safety("R-RUNTIME-ARRAY-INITIALIZE", "array is writable")
extern "C" void r_runtime_array_initialize(raw Array* items, raw Allocator*? allocator, TypeInfo element) {
    unsafe {
        *items = Array {.allocator = allocator, .element = element, .data = null,
                        .length = 0usize as c_size, .capacity = 0usize as c_size};
    }
}

@safety("R-RUNTIME-ARRAY-WITH-CAPACITY", "array is writable")
extern "C" ArrayStatus r_runtime_array_with_capacity(raw Array* items, raw Allocator*? allocator, TypeInfo element, c_size capacity) {
    r_runtime_array_initialize(items, allocator, element);
    if ((capacity as usize) == 0usize) {
        return ArrayStatus::R_RUNTIME_ARRAY_OK;
    }
    unsafe {
        raw void*? data = null;
        ArrayStatus status = allocate_elements(items, capacity as usize, &data as raw (raw void*?)*);
        if (status != ArrayStatus::R_RUNTIME_ARRAY_OK) {
            r_runtime_array_initialize(items, allocator, element);
            return status;
        }
        items->data = data;
        items->capacity = capacity;
    }
    return ArrayStatus::R_RUNTIME_ARRAY_OK;
}

@safety("R-RUNTIME-ARRAY-RESERVE", "array is initialized")
extern "C" ArrayStatus r_runtime_array_reserve(raw Array* items, c_size additional) {
    return reserve_valid(items, additional as usize);
}

@safety("R-RUNTIME-ARRAY-PUSH", "array is initialized; value addresses an element that moves into the array")
extern "C" ArrayStatus r_runtime_array_push(raw Array* items, raw void*? value) {
    ArrayStatus status = reserve_valid(items, 1usize);
    if (status != ArrayStatus::R_RUNTIME_ARRAY_OK) {
        return status;
    }
    unsafe {
        usize length = items->length as usize;
        runtime.types::move_value(items->element,
                                  element_at(items->data, length, items->element.size as usize),
                                  value);
        items->length = (length + 1usize) as c_size;
    }
    return ArrayStatus::R_RUNTIME_ARRAY_OK;
}

@safety("R-RUNTIME-ARRAY-POP", "array is initialized; result addresses storage for one element")
extern "C" c_bool r_runtime_array_pop(raw Array* items, raw void*? result) {
    unsafe {
        usize length = items->length as usize;
        if (length == 0usize) {
            return 0i32 as c_bool;
        }
        items->length = (length - 1usize) as c_size;
        runtime.types::move_value(
            items->element, result,
            element_at(items->data, length - 1usize, items->element.size as usize));
    }
    return 1i32 as c_bool;
}

@safety("R-RUNTIME-ARRAY-REMOVE", "array is initialized; result addresses storage for one element")
extern "C" c_bool r_runtime_array_remove(raw Array* items, c_size index, raw void*? result) {
    unsafe {
        usize length = items->length as usize;
        usize at = index as usize;
        if (at >= length) {
            return 0i32 as c_bool;
        }
        usize size = items->element.size as usize;
        runtime.types::move_value(items->element, result, element_at(items->data, at, size));
        for (usize source = at + 1usize; source < length; source += 1usize) {
            runtime.types::move_value(items->element,
                                      element_at(items->data, source - 1usize, size),
                                      element_at(items->data, source, size));
        }
        items->length = (length - 1usize) as c_size;
    }
    return 1i32 as c_bool;
}

@safety("R-RUNTIME-ARRAY-GET", "array is initialized")
extern "C" raw const void*? r_runtime_array_get(raw const Array* items, c_size index) {
    unsafe {
        if ((index as usize) >= (items->length as usize)) {
            return null;
        }
        return element_at(items->data, index as usize, items->element.size as usize) as raw const void*?;
    }
}

@safety("R-RUNTIME-ARRAY-GET-MUT", "array is initialized")
extern "C" raw void*? r_runtime_array_get_mut(raw Array* items, c_size index) {
    unsafe {
        if ((index as usize) >= (items->length as usize)) {
            return null;
        }
        return element_at(items->data, index as usize, items->element.size as usize);
    }
}

@safety("R-RUNTIME-ARRAY-CLEAR", "array is initialized or zeroed")
extern "C" void r_runtime_array_clear(raw Array* items) {
    unsafe {
        if (items->allocator == null) {
            return;
        }
    }
    clear_valid(items);
}

@safety("R-RUNTIME-ARRAY-DESTROY", "array is initialized or zeroed")
extern "C" void r_runtime_array_destroy(raw Array* items) {
    unsafe {
        if (items->allocator == null) {
            return;
        }
        clear_valid(items);
        runtime.heap::r_runtime_allocator_deallocate(items->data, items->element.alignment);
        items->data = null;
        items->capacity = 0usize as c_size;
    }
}

@safety("R-RUNTIME-ARRAY-DESTROY-COUNT", "array is initialized or zeroed")
extern "C" c_size r_runtime_array_destroy_count(raw const Array*? items) {
    unsafe {
        if (items->allocator == null) {
            return 0usize as c_size;
        }
        return items->length;
    }
}

@safety("R-RUNTIME-ARRAY-DESTROY-ELEMENT", "array is being destroyed; index is below its count")
extern "C" raw void*? r_runtime_array_destroy_element(raw Array*? items, c_size index, c_size stride) {
    unsafe {
        return element_at(items->data, index as usize, stride as usize);
    }
}

@safety("R-RUNTIME-ARRAY-DESTROY-LOCATE", "array is being destroyed; index is writable")
extern "C" c_bool r_runtime_array_destroy_locate(raw const Array*? items, raw const void*? element, c_size stride, raw c_size* index) {
    unsafe {
        usize step = stride as usize;
        if ((items->data == null) || (step == 0usize)) {
            return 0i32 as c_bool;
        }
        usize base = core::pointer_address(items->data);
        usize address = core::pointer_address(element);
        if (address < base) {
            return 0i32 as c_bool;
        }
        usize offset = address - base;
        if (((offset % step) != 0usize) || ((offset / step) >= (items->length as usize))) {
            return 0i32 as c_bool;
        }
        *index = (offset / step) as c_size;
    }
    return 1i32 as c_bool;
}

@safety("R-RUNTIME-ARRAY-DESTROY-SCRATCH", "array is being destroyed")
extern "C" raw TypeInfo* r_runtime_array_destroy_scratch(raw Array*? items) {
    unsafe {
        return &items->element as raw TypeInfo*;
    }
}

@safety("R-RUNTIME-ARRAY-DESTROY-FINISH", "every element of the array is destroyed")
extern "C" void r_runtime_array_destroy_finish(raw Array*? items, c_size alignment) {
    unsafe {
        if (items->allocator == null) {
            return;
        }
        runtime.heap::r_runtime_allocator_deallocate(items->data, alignment);
        items->data = null;
        items->length = 0usize as c_size;
        items->capacity = 0usize as c_size;
    }
}
