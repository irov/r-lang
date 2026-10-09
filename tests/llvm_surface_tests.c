#include "surface.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define R_SURFACE_CHECK(condition)                                                                 \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            (void)fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);    \
            failures += 1;                                                                         \
        }                                                                                          \
    } while (0)

/* Every index of the tables names a row, a member lies inside its record and an array is its
   element repeated (B2 of the LLVM transition). */
static void r_surface_test_tables_are_consistent(void) {
    const size_t count = r_llvm_surface_type_count();
    uint32_t index;

    R_SURFACE_CHECK(count > 0U);
    for (index = 0U; (size_t)index < count; ++index) {
        const RLlvmSurfaceType *type = r_llvm_surface_type(index);
        uint32_t member;

        R_SURFACE_CHECK((type != NULL) && (type->align != 0U));
        if (type == NULL) {
            continue;
        }
        switch (type->kind) {
        case R_LLVM_SURFACE_POINTER:
        case R_LLVM_SURFACE_ENUM:
            R_SURFACE_CHECK(r_llvm_surface_type(type->target) != NULL);
            break;
        case R_LLVM_SURFACE_ARRAY:
            R_SURFACE_CHECK((r_llvm_surface_type(type->target) != NULL) &&
                            (type->size == r_llvm_surface_type(type->target)->size * type->count));
            break;
        case R_LLVM_SURFACE_FUNCTION:
            R_SURFACE_CHECK(r_llvm_surface_type(type->target) != NULL);
            for (member = 0U; member < type->count; ++member) {
                uint32_t parameter = 0U;
                R_SURFACE_CHECK(r_llvm_surface_parameter(type->first + member, &parameter) &&
                                (r_llvm_surface_type(parameter) != NULL));
            }
            break;
        case R_LLVM_SURFACE_STRUCT:
        case R_LLVM_SURFACE_UNION:
            R_SURFACE_CHECK((type->size % type->align) == 0U);
            for (member = 0U; member < type->count; ++member) {
                const RLlvmSurfaceField *field = r_llvm_surface_field(type->first + member);
                const RLlvmSurfaceType *field_type =
                    field == NULL ? NULL : r_llvm_surface_type(field->type);
                R_SURFACE_CHECK(
                    (field_type != NULL) && (field->offset + field_type->size <= type->size) &&
                    ((field_type->align == 0U) || ((field->offset % field_type->align) == 0U)));
            }
            break;
        default:
            break;
        }
    }
}

/* Lookups by name find what generated C names. */
static void r_surface_test_lookups(void) {
    const RLlvmSurfaceName *array = r_llvm_surface_typedef("RStdArray");
    const RLlvmSurfaceFunction *validate = r_llvm_surface_function("r_core_validate_utf8");
    const RLlvmSurfaceConstant *bounds = r_llvm_surface_constant("R_RUNTIME_PANIC_BOUNDS");
    size_t index;

    R_SURFACE_CHECK((array != NULL) &&
                    (r_llvm_surface_type(array->type)->kind == R_LLVM_SURFACE_STRUCT));
    R_SURFACE_CHECK((validate != NULL) && (strstr(validate->lowered, "sret(") != NULL) &&
                    (r_llvm_surface_type(validate->type)->kind == R_LLVM_SURFACE_FUNCTION));
    R_SURFACE_CHECK(bounds != NULL);
    R_SURFACE_CHECK(r_llvm_surface_function("r_not_a_runtime_function") == NULL);
    R_SURFACE_CHECK(r_llvm_surface_typedef(NULL) == NULL);
    for (index = 0U; index < r_llvm_surface_function_count(); ++index) {
        const RLlvmSurfaceFunction *function = r_llvm_surface_function_at(index);
        R_SURFACE_CHECK((function != NULL) &&
                        (r_llvm_surface_function(function->name) == function));
    }
}

int main(void) {
    r_surface_test_tables_are_consistent();
    r_surface_test_lookups();
    if (failures != 0) {
        (void)fprintf(stderr, "%d runtime surface checks failed\n", failures);
        return 1;
    }
    return 0;
}
