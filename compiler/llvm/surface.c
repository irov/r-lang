#include "surface.h"

#include <string.h>

#include "runtime_surface.generated.inc"

#define R_LLVM_SURFACE_LENGTH(table) (sizeof(table) / sizeof((table)[0]))

size_t r_llvm_surface_type_count(void) {
    return R_LLVM_SURFACE_LENGTH(r_llvm_surface_types);
}

const RLlvmSurfaceType *r_llvm_surface_type(uint32_t index) {
    return (size_t)index < R_LLVM_SURFACE_LENGTH(r_llvm_surface_types)
               ? &r_llvm_surface_types[index]
               : NULL;
}

const RLlvmSurfaceField *r_llvm_surface_field(uint32_t index) {
    return (size_t)index < R_LLVM_SURFACE_LENGTH(r_llvm_surface_fields)
               ? &r_llvm_surface_fields[index]
               : NULL;
}

bool r_llvm_surface_parameter(uint32_t index, uint32_t *type) {
    if ((type == NULL) || ((size_t)index >= R_LLVM_SURFACE_LENGTH(r_llvm_surface_parameters))) {
        return false;
    }
    *type = r_llvm_surface_parameters[index];
    return true;
}

size_t r_llvm_surface_function_count(void) {
    return R_LLVM_SURFACE_LENGTH(r_llvm_surface_functions);
}

const RLlvmSurfaceFunction *r_llvm_surface_function_at(size_t index) {
    return index < R_LLVM_SURFACE_LENGTH(r_llvm_surface_functions)
               ? &r_llvm_surface_functions[index]
               : NULL;
}

/* The tables are sorted by name in byte order and each row begins with its name; returns the
   index of `name` or `count`. */
static size_t
r_llvm_surface_search(const void *table, size_t count, size_t stride, const char *name) {
    const unsigned char *rows = table;
    size_t low = 0U;
    size_t high = count;

    while (low < high) {
        const size_t middle = low + ((high - low) / 2U);
        const char *candidate;
        int order;

        (void)memcpy(&candidate, rows + (middle * stride), sizeof(candidate));
        order = strcmp(name, candidate);
        if (order == 0) {
            return middle;
        }
        if (order < 0) {
            high = middle;
        } else {
            low = middle + 1U;
        }
    }
    return count;
}

const RLlvmSurfaceFunction *r_llvm_surface_function(const char *name) {
    const size_t count = R_LLVM_SURFACE_LENGTH(r_llvm_surface_functions);
    const size_t index =
        name == NULL
            ? count
            : r_llvm_surface_search(
                  r_llvm_surface_functions, count, sizeof(r_llvm_surface_functions[0]), name);
    return index < count ? &r_llvm_surface_functions[index] : NULL;
}

const RLlvmSurfaceName *r_llvm_surface_typedef(const char *name) {
    const size_t count = R_LLVM_SURFACE_LENGTH(r_llvm_surface_typedefs);
    const size_t index =
        name == NULL
            ? count
            : r_llvm_surface_search(
                  r_llvm_surface_typedefs, count, sizeof(r_llvm_surface_typedefs[0]), name);
    return index < count ? &r_llvm_surface_typedefs[index] : NULL;
}

const RLlvmSurfaceConstant *r_llvm_surface_constant(const char *name) {
    const size_t count = R_LLVM_SURFACE_LENGTH(r_llvm_surface_constants);
    const size_t index =
        name == NULL
            ? count
            : r_llvm_surface_search(
                  r_llvm_surface_constants, count, sizeof(r_llvm_surface_constants[0]), name);
    return index < count ? &r_llvm_surface_constants[index] : NULL;
}
