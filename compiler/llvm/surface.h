#ifndef R_LLVM_SURFACE_H
#define R_LLVM_SURFACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The runtime surface: the types, functions and enumerators of the runtime and library headers
   generated code uses, as the pinned clang lays them out and passes them for the manifest target
   (tools/generate_runtime_surface.py, compiler/llvm/runtime_surface.generated.inc). */

typedef enum RLlvmSurfaceKind {
    R_LLVM_SURFACE_VOID = 0,
    R_LLVM_SURFACE_BOOL,
    R_LLVM_SURFACE_INTEGER,
    R_LLVM_SURFACE_FLOAT,
    R_LLVM_SURFACE_POINTER,
    R_LLVM_SURFACE_FUNCTION,
    R_LLVM_SURFACE_ARRAY,
    R_LLVM_SURFACE_STRUCT,
    R_LLVM_SURFACE_UNION,
    R_LLVM_SURFACE_ENUM,
    R_LLVM_SURFACE_OPAQUE
} RLlvmSurfaceKind;

#define R_LLVM_SURFACE_SIGNED 1U
#define R_LLVM_SURFACE_CONST 2U
#define R_LLVM_SURFACE_VARIADIC 4U
#define R_LLVM_SURFACE_ATOMIC 8U

typedef struct RLlvmSurfaceType {
    RLlvmSurfaceKind kind;
    unsigned flags;
    uint32_t size;
    uint32_t align;
    /* Pointer: pointee; array: element; enumeration: compatible integer; function: result. */
    uint32_t target;
    /* Array: length; struct and union: member count; function: parameter count. */
    uint32_t count;
    /* Struct and union: first member; function: first parameter. */
    uint32_t first;
    /* The typedef or tag of a record or enumeration, the spelling of a scalar. */
    const char *name;
} RLlvmSurfaceType;

typedef struct RLlvmSurfaceField {
    const char *name;
    uint32_t offset;
    uint32_t type;
} RLlvmSurfaceField;

typedef struct RLlvmSurfaceFunction {
    const char *name;
    uint32_t type;
    /* The declaration clang lowers the function to: "RESULT (PARAMETERS)". */
    const char *lowered;
} RLlvmSurfaceFunction;

typedef struct RLlvmSurfaceName {
    const char *name;
    uint32_t type;
} RLlvmSurfaceName;

typedef struct RLlvmSurfaceConstant {
    const char *name;
    int64_t value;
    uint32_t type;
} RLlvmSurfaceConstant;

size_t r_llvm_surface_type_count(void);
/* NULL for an index outside the table. */
const RLlvmSurfaceType *r_llvm_surface_type(uint32_t index);
const RLlvmSurfaceField *r_llvm_surface_field(uint32_t index);
bool r_llvm_surface_parameter(uint32_t index, uint32_t *type);

size_t r_llvm_surface_function_count(void);
const RLlvmSurfaceFunction *r_llvm_surface_function_at(size_t index);
/* The function, typedef or enumerator of that name; NULL or false when the surface has none. */
const RLlvmSurfaceFunction *r_llvm_surface_function(const char *name);
const RLlvmSurfaceName *r_llvm_surface_typedef(const char *name);
const RLlvmSurfaceConstant *r_llvm_surface_constant(const char *name);

#ifdef __cplusplus
}
#endif

#endif
