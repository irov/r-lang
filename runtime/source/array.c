#include "r_runtime_array.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

static void *r_runtime_array_element(RRuntimeArray *array, size_t index) {
    return (unsigned char *)array->data + (index * array->element.size);
}

static const void *r_runtime_array_element_const(const RRuntimeArray *array, size_t index) {
    return (const unsigned char *)array->data + (index * array->element.size);
}

static void r_runtime_array_move(const RRuntimeArray *array, void *destination, void *source) {
    if (array->element.move_initialize != NULL) {
        array->element.move_initialize(destination, source);
    } else {
        (void)memcpy(destination, source, array->element.size);
    }
}

static RRuntimeArrayStatus
r_runtime_array_allocate(RRuntimeArray *array, size_t capacity, void **result) {
    size_t byte_size;
    RRuntimeAllocationStatus status;

    if ((capacity != 0U) && (array->element.size > (SIZE_MAX / capacity))) {
        return R_RUNTIME_ARRAY_SIZE_OVERFLOW;
    }
    byte_size = capacity * array->element.size;
    status =
        r_runtime_allocator_allocate(array->allocator, byte_size, array->element.alignment, result);
    if (status == R_RUNTIME_ALLOCATION_OK) {
        return R_RUNTIME_ARRAY_OK;
    }
    if (status == R_RUNTIME_ALLOCATION_EXHAUSTED) {
        return R_RUNTIME_ARRAY_ALLOCATION_FAILED;
    }
    if (status == R_RUNTIME_ALLOCATION_SIZE_OVERFLOW) {
        return R_RUNTIME_ARRAY_SIZE_OVERFLOW;
    }
    if (status == R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT) {
        return R_RUNTIME_ARRAY_UNSUPPORTED_ALIGNMENT;
    }
    return R_RUNTIME_ARRAY_INVALID;
}

static RRuntimeArrayStatus r_runtime_array_map_allocation(RRuntimeAllocationStatus status) {
    if (status == R_RUNTIME_ALLOCATION_OK) {
        return R_RUNTIME_ARRAY_OK;
    }
    if (status == R_RUNTIME_ALLOCATION_EXHAUSTED) {
        return R_RUNTIME_ARRAY_ALLOCATION_FAILED;
    }
    if (status == R_RUNTIME_ALLOCATION_SIZE_OVERFLOW) {
        return R_RUNTIME_ARRAY_SIZE_OVERFLOW;
    }
    if (status == R_RUNTIME_ALLOCATION_UNSUPPORTED_ALIGNMENT) {
        return R_RUNTIME_ARRAY_UNSUPPORTED_ALIGNMENT;
    }
    return R_RUNTIME_ARRAY_INVALID;
}

static RRuntimeArrayStatus r_runtime_array_reallocate_trivial(RRuntimeArray *array,
                                                               size_t capacity) {
    void *replacement = NULL;
    RRuntimeArrayStatus status;

    if ((capacity != 0U) && (array->element.size > (SIZE_MAX / capacity))) {
        return R_RUNTIME_ARRAY_SIZE_OVERFLOW;
    }
    status = r_runtime_array_map_allocation(
        r_runtime_allocator_reallocate(array->allocator,
                                       array->data,
                                       array->capacity * array->element.size,
                                       capacity * array->element.size,
                                       array->element.alignment,
                                       &replacement));
    if (status != R_RUNTIME_ARRAY_OK) {
        return status;
    }
    array->data = replacement;
    array->capacity = capacity;
    return R_RUNTIME_ARRAY_OK;
}

static RRuntimeArrayStatus r_runtime_array_reserve_valid(RRuntimeArray *array, size_t additional) {
    size_t required;
    size_t capacity;
    void *replacement = NULL;
    size_t index;
    RRuntimeArrayStatus status;

    if (additional > (SIZE_MAX - array->length)) {
        return R_RUNTIME_ARRAY_SIZE_OVERFLOW;
    }
    required = array->length + additional;
    if (required <= array->capacity) {
        return R_RUNTIME_ARRAY_OK;
    }
    capacity = array->capacity == 0U ? 4U : array->capacity;
    while (capacity < required) {
        if (capacity > (SIZE_MAX / 2U)) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }
    if (array->element.move_initialize == NULL) {
        /* Trivially movable elements: one reallocation moves the block as a whole. */
        return r_runtime_array_reallocate_trivial(array, capacity);
    }
    status = r_runtime_array_allocate(array, capacity, &replacement);
    if (status != R_RUNTIME_ARRAY_OK) {
        return status;
    }
    for (index = 0U; index < array->length; ++index) {
        void *destination = (unsigned char *)replacement + (index * array->element.size);
        r_runtime_array_move(array, destination, r_runtime_array_element(array, index));
    }
    r_runtime_allocator_deallocate(array->data, array->element.alignment);
    array->data = replacement;
    array->capacity = capacity;
    return R_RUNTIME_ARRAY_OK;
}

static void r_runtime_array_clear_valid(RRuntimeArray *array) {
    while (array->length != 0U) {
        array->length -= 1U;
        if (array->element.drop != NULL) {
            array->element.drop(r_runtime_array_element(array, array->length));
        }
    }
}

void r_runtime_array_initialize(RRuntimeArray *array,
                                RRuntimeAllocator *allocator,
                                RRuntimeTypeInfo element) {
    array->allocator = allocator;
    array->element = element;
    array->data = NULL;
    array->length = 0U;
    array->capacity = 0U;
}

RRuntimeArrayStatus r_runtime_array_with_capacity(RRuntimeArray *array,
                                                  RRuntimeAllocator *allocator,
                                                  RRuntimeTypeInfo element,
                                                  size_t capacity) {
    RRuntimeArrayStatus status;

    r_runtime_array_initialize(array, allocator, element);
    if (capacity == 0U) {
        return R_RUNTIME_ARRAY_OK;
    }
    status = r_runtime_array_allocate(array, capacity, &array->data);
    if (status != R_RUNTIME_ARRAY_OK) {
        r_runtime_array_initialize(array, allocator, element);
        return status;
    }
    array->capacity = capacity;
    return R_RUNTIME_ARRAY_OK;
}

RRuntimeArrayStatus r_runtime_array_reserve(RRuntimeArray *array, size_t additional) {
    return r_runtime_array_reserve_valid(array, additional);
}

RRuntimeArrayStatus r_runtime_array_push(RRuntimeArray *array, void *value) {
    RRuntimeArrayStatus status;

    status = r_runtime_array_reserve_valid(array, 1U);
    if (status != R_RUNTIME_ARRAY_OK) {
        return status;
    }
    r_runtime_array_move(array, r_runtime_array_element(array, array->length), value);
    array->length += 1U;
    return R_RUNTIME_ARRAY_OK;
}

_Bool r_runtime_array_pop(RRuntimeArray *array, void *result) {
    if (array->length == 0U) {
        return 0;
    }
    array->length -= 1U;
    r_runtime_array_move(array, result, r_runtime_array_element(array, array->length));
    return 1;
}

_Bool r_runtime_array_remove(RRuntimeArray *array, size_t index, void *result) {
    size_t source_index;

    if (index >= array->length) {
        return 0;
    }
    r_runtime_array_move(array, result, r_runtime_array_element(array, index));
    for (source_index = index + 1U; source_index < array->length; ++source_index) {
        r_runtime_array_move(array,
                             r_runtime_array_element(array, source_index - 1U),
                             r_runtime_array_element(array, source_index));
    }
    array->length -= 1U;
    return 1;
}

const void *r_runtime_array_get(const RRuntimeArray *array, size_t index) {
    if (index >= array->length) {
        return NULL;
    }
    return r_runtime_array_element_const(array, index);
}

void *r_runtime_array_get_mut(RRuntimeArray *array, size_t index) {
    if (index >= array->length) {
        return NULL;
    }
    return r_runtime_array_element(array, index);
}

void r_runtime_array_clear(RRuntimeArray *array) {
    if (array->allocator == NULL) {
        return;
    }
    r_runtime_array_clear_valid(array);
}

void r_runtime_array_destroy(RRuntimeArray *array) {
    if (array->allocator == NULL) {
        return;
    }
    r_runtime_array_clear_valid(array);
    r_runtime_allocator_deallocate(array->data, array->element.alignment);
    array->data = NULL;
    array->capacity = 0U;
}

size_t r_runtime_array_destroy_count(const RRuntimeArray *array) {
    return array->allocator == NULL ? 0U : array->length;
}

void *r_runtime_array_destroy_element(RRuntimeArray *array, size_t index, size_t stride) {
    return (unsigned char *)array->data + (index * stride);
}

_Bool r_runtime_array_destroy_locate(const RRuntimeArray *array,
                                     const void *element,
                                     size_t stride,
                                     size_t *index) {
    const uintptr_t base = (uintptr_t)array->data;
    const uintptr_t address = (uintptr_t)element;
    uintptr_t offset;

    if ((array->data == NULL) || (stride == 0U) || (address < base)) {
        return 0;
    }
    offset = address - base;
    if ((offset % stride != 0U) || ((offset / stride) >= array->length)) {
        return 0;
    }
    *index = (size_t)(offset / stride);
    return 1;
}

RRuntimeTypeInfo *r_runtime_array_destroy_scratch(RRuntimeArray *array) {
    return &array->element;
}

void r_runtime_array_destroy_finish(RRuntimeArray *array, size_t alignment) {
    if (array->allocator == NULL) {
        return;
    }
    r_runtime_allocator_deallocate(array->data, alignment);
    array->data = NULL;
    array->length = 0U;
    array->capacity = 0U;
}
