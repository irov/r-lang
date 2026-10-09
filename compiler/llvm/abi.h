#ifndef R_LLVM_ABI_H
#define R_LLVM_ABI_H

#include "surface.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The C ABI of the LLVM backend: how a C function passes each argument and its result on the
   AAPCS64 targets of the transition (arm64-apple-darwin, aarch64-unknown-linux-gnu). Types are
   rows of a type table in the runtime surface format, so one classifier serves the runtime
   surface (B2) and the aggregates of a program (B3). */

typedef struct RLlvmTypeTable {
    const RLlvmSurfaceType *(*type)(const void *context, uint32_t index);
    const RLlvmSurfaceField *(*field)(const void *context, uint32_t index);
    bool (*parameter)(const void *context, uint32_t index, uint32_t *type);
    const void *context;
} RLlvmTypeTable;

/* The type table of the runtime surface. */
RLlvmTypeTable r_llvm_surface_table(void);

typedef enum RLlvmAbiClass {
    R_LLVM_ABI_IGNORE = 0, /* a void result */
    R_LLVM_ABI_DIRECT,     /* a scalar in its own type, a small integer extended */
    R_LLVM_ABI_COERCE,     /* an aggregate in registers: `count` elements of `element` */
    R_LLVM_ABI_INDIRECT    /* an argument: pointer to a copy; a result: memory the caller gives */
} RLlvmAbiClass;

typedef enum RLlvmAbiElement {
    R_LLVM_ABI_ELEMENT_NONE = 0,
    R_LLVM_ABI_ELEMENT_I1,
    R_LLVM_ABI_ELEMENT_I8,
    R_LLVM_ABI_ELEMENT_I16,
    R_LLVM_ABI_ELEMENT_I32,
    R_LLVM_ABI_ELEMENT_I64,
    R_LLVM_ABI_ELEMENT_I128,
    R_LLVM_ABI_ELEMENT_INTEGER, /* an integer of `bits` bits */
    R_LLVM_ABI_ELEMENT_FLOAT,
    R_LLVM_ABI_ELEMENT_DOUBLE,
    R_LLVM_ABI_ELEMENT_POINTER
} RLlvmAbiElement;

typedef enum RLlvmAbiExtend {
    R_LLVM_ABI_EXTEND_NONE = 0,
    R_LLVM_ABI_EXTEND_ZERO,
    R_LLVM_ABI_EXTEND_SIGN
} RLlvmAbiExtend;

typedef struct RLlvmAbiValue {
    RLlvmAbiClass abi_class;
    RLlvmAbiElement element;
    uint32_t count;
    RLlvmAbiExtend extend;
    uint32_t bits;
    /* A homogeneous floating-point aggregate: in registers as an argument, returned as a
       structure of `count` elements. */
    bool homogeneous;
} RLlvmAbiValue;

/* Classify the argument or the result of the given type; false for a type the C ABI cannot pass
   (an opaque or function type by value). */
bool r_llvm_abi_classify_argument(const RLlvmTypeTable *table, uint32_t type, RLlvmAbiValue *value);
bool r_llvm_abi_classify_result(const RLlvmTypeTable *table, uint32_t type, RLlvmAbiValue *value);

/* The lowered signature of a function type as text, "RESULT (PARAMETERS)", in the normalized form
   tests compare with clang: `sret` and `indirect` mark memory, `zeroext`/`signext` extension,
   `hfa` a homogeneous result. False when it does not fit or a type cannot be passed. */
bool r_llvm_abi_render_function(const RLlvmTypeTable *table,
                                uint32_t function_type,
                                char *buffer,
                                size_t capacity);

#ifdef __cplusplus
}
#endif

#endif
