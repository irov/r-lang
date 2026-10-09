#include "abi.h"

#include <stdio.h>
#include <string.h>

/* AAPCS64 (Procedure Call Standard for the Arm 64-bit Architecture) with the choices clang makes
   for it, which tests/llvm_abi_tests.c checks against clang's lowering of every runtime surface
   function: a scalar is passed in its own type and an integer narrower than 32 bits is extended
   by the caller; a homogeneous floating-point aggregate of one to four members goes in floating
   registers; another aggregate of at most 16 bytes goes in one or two integer registers, as an
   argument an i64 or [2 x i64] (i128 when it is 16-byte aligned), as a result of at most 8 bytes
   an integer of its exact size in the low bits of x0; a larger aggregate is passed as a pointer
   to a copy and returned in memory the caller provides. */

static const RLlvmSurfaceType *r_llvm_abi_type(const RLlvmTypeTable *table, uint32_t index) {
    return table->type(table->context, index);
}

/* The common floating-point member type and the member count of a homogeneous aggregate, or
   false. Arrays count each element; a union is homogeneous when every member is. */
static bool r_llvm_abi_homogeneous(const RLlvmTypeTable *table,
                                   uint32_t index,
                                   uint32_t depth,
                                   uint32_t *base_size,
                                   uint32_t *count) {
    const RLlvmSurfaceType *type = r_llvm_abi_type(table, index);
    uint32_t member;

    if ((type == NULL) || (depth > 32U)) {
        return false;
    }
    switch (type->kind) {
    case R_LLVM_SURFACE_FLOAT:
        if ((*base_size != 0U) && (*base_size != type->size)) {
            return false;
        }
        *base_size = type->size;
        *count += 1U;
        return true;
    case R_LLVM_SURFACE_ARRAY: {
        uint32_t element_count = 0U;
        if (!r_llvm_abi_homogeneous(table, type->target, depth + 1U, base_size, &element_count)) {
            return false;
        }
        *count += element_count * type->count;
        return true;
    }
    case R_LLVM_SURFACE_STRUCT:
        if (type->count == 0U) {
            return false;
        }
        for (member = 0U; member < type->count; ++member) {
            const RLlvmSurfaceField *field = table->field(table->context, type->first + member);
            if ((field == NULL) ||
                !r_llvm_abi_homogeneous(table, field->type, depth + 1U, base_size, count)) {
                return false;
            }
        }
        /* Padding breaks homogeneity: the members must fill the aggregate. */
        return (*base_size != 0U) && ((*count * *base_size) == type->size);
    case R_LLVM_SURFACE_UNION: {
        uint32_t widest = 0U;
        if (type->count == 0U) {
            return false;
        }
        for (member = 0U; member < type->count; ++member) {
            const RLlvmSurfaceField *field = table->field(table->context, type->first + member);
            uint32_t member_count = 0U;
            if ((field == NULL) ||
                !r_llvm_abi_homogeneous(table, field->type, depth + 1U, base_size, &member_count)) {
                return false;
            }
            widest = member_count > widest ? member_count : widest;
        }
        *count += widest;
        return (*base_size != 0U) && ((widest * *base_size) == type->size);
    }
    default:
        return false;
    }
}

static RLlvmAbiElement r_llvm_abi_integer_element(uint32_t size) {
    switch (size) {
    case 1U:
        return R_LLVM_ABI_ELEMENT_I8;
    case 2U:
        return R_LLVM_ABI_ELEMENT_I16;
    case 4U:
        return R_LLVM_ABI_ELEMENT_I32;
    case 8U:
        return R_LLVM_ABI_ELEMENT_I64;
    case 16U:
        return R_LLVM_ABI_ELEMENT_I128;
    default:
        return R_LLVM_ABI_ELEMENT_NONE;
    }
}

static bool r_llvm_abi_classify(const RLlvmTypeTable *table,
                                uint32_t index,
                                bool result,
                                RLlvmAbiValue *value) {
    const RLlvmSurfaceType *type = r_llvm_abi_type(table, index);
    uint32_t base_size = 0U;
    uint32_t count = 0U;

    (void)memset(value, 0, sizeof(*value));
    if (type == NULL) {
        return false;
    }
    switch (type->kind) {
    case R_LLVM_SURFACE_VOID:
        value->abi_class = R_LLVM_ABI_IGNORE;
        return result;
    case R_LLVM_SURFACE_BOOL:
        value->abi_class = R_LLVM_ABI_DIRECT;
        value->element = R_LLVM_ABI_ELEMENT_I1;
        value->count = 1U;
        value->extend = R_LLVM_ABI_EXTEND_ZERO;
        return true;
    case R_LLVM_SURFACE_ENUM:
        return r_llvm_abi_classify(table, type->target, result, value);
    case R_LLVM_SURFACE_INTEGER:
        value->abi_class = R_LLVM_ABI_DIRECT;
        value->element = r_llvm_abi_integer_element(type->size);
        value->count = 1U;
        if (type->size < 4U) {
            value->extend = (type->flags & R_LLVM_SURFACE_SIGNED) != 0U ? R_LLVM_ABI_EXTEND_SIGN
                                                                        : R_LLVM_ABI_EXTEND_ZERO;
        }
        return value->element != R_LLVM_ABI_ELEMENT_NONE;
    case R_LLVM_SURFACE_FLOAT:
        value->abi_class = R_LLVM_ABI_DIRECT;
        value->element = type->size == 4U ? R_LLVM_ABI_ELEMENT_FLOAT : R_LLVM_ABI_ELEMENT_DOUBLE;
        value->count = 1U;
        return (type->size == 4U) || (type->size == 8U);
    case R_LLVM_SURFACE_POINTER:
        value->abi_class = R_LLVM_ABI_DIRECT;
        value->element = R_LLVM_ABI_ELEMENT_POINTER;
        value->count = 1U;
        return true;
    case R_LLVM_SURFACE_STRUCT:
    case R_LLVM_SURFACE_UNION:
    case R_LLVM_SURFACE_ARRAY:
        if (r_llvm_abi_homogeneous(table, index, 0U, &base_size, &count) && (count >= 1U) &&
            (count <= 4U)) {
            value->abi_class = R_LLVM_ABI_COERCE;
            value->element = base_size == 4U ? R_LLVM_ABI_ELEMENT_FLOAT : R_LLVM_ABI_ELEMENT_DOUBLE;
            value->count = count;
            value->homogeneous = true;
            return (base_size == 4U) || (base_size == 8U);
        }
        if (type->size > 16U) {
            value->abi_class = R_LLVM_ABI_INDIRECT;
            return true;
        }
        value->abi_class = R_LLVM_ABI_COERCE;
        if (type->align == 16U) {
            value->element = R_LLVM_ABI_ELEMENT_I128;
            value->count = 1U;
        } else if (result && (type->size <= 8U)) {
            value->element = r_llvm_abi_integer_element(type->size);
            if (value->element == R_LLVM_ABI_ELEMENT_NONE) {
                value->element = R_LLVM_ABI_ELEMENT_INTEGER;
                value->bits = type->size * 8U;
            }
            value->count = 1U;
        } else {
            value->element = R_LLVM_ABI_ELEMENT_I64;
            value->count = type->size <= 8U ? 1U : 2U;
        }
        return type->size != 0U;
    default:
        return false;
    }
}

bool r_llvm_abi_classify_argument(const RLlvmTypeTable *table,
                                  uint32_t type,
                                  RLlvmAbiValue *value) {
    return (table != NULL) && (value != NULL) && r_llvm_abi_classify(table, type, false, value);
}

bool r_llvm_abi_classify_result(const RLlvmTypeTable *table, uint32_t type, RLlvmAbiValue *value) {
    return (table != NULL) && (value != NULL) && r_llvm_abi_classify(table, type, true, value);
}

static const char *r_llvm_abi_element_text(RLlvmAbiElement element) {
    switch (element) {
    case R_LLVM_ABI_ELEMENT_I1:
        return "i1";
    case R_LLVM_ABI_ELEMENT_I8:
        return "i8";
    case R_LLVM_ABI_ELEMENT_I16:
        return "i16";
    case R_LLVM_ABI_ELEMENT_I32:
        return "i32";
    case R_LLVM_ABI_ELEMENT_I64:
        return "i64";
    case R_LLVM_ABI_ELEMENT_I128:
        return "i128";
    case R_LLVM_ABI_ELEMENT_FLOAT:
        return "float";
    case R_LLVM_ABI_ELEMENT_DOUBLE:
        return "double";
    case R_LLVM_ABI_ELEMENT_POINTER:
        return "ptr";
    default:
        return "?";
    }
}

static const char *r_llvm_abi_extend_text(RLlvmAbiExtend extend) {
    return extend == R_LLVM_ABI_EXTEND_ZERO   ? " zeroext"
           : extend == R_LLVM_ABI_EXTEND_SIGN ? " signext"
                                              : "";
}

/* Appends to buffer at *length; false when it does not fit. */
static bool r_llvm_abi_append(char *buffer, size_t capacity, size_t *length, const char *text) {
    const size_t extra = strlen(text);
    if ((*length >= capacity) || (extra >= capacity - *length)) {
        return false;
    }
    (void)memcpy(buffer + *length, text, extra + 1U);
    *length += extra;
    return true;
}

static bool r_llvm_abi_render_value(
    const RLlvmAbiValue *value, bool result, char *buffer, size_t capacity, size_t *length) {
    char text[64];

    if (value->element == R_LLVM_ABI_ELEMENT_INTEGER) {
        (void)snprintf(text, sizeof(text), "i%u", value->bits);
        return r_llvm_abi_append(buffer, capacity, length, text);
    }

    switch (value->abi_class) {
    case R_LLVM_ABI_IGNORE:
        return r_llvm_abi_append(buffer, capacity, length, "void");
    case R_LLVM_ABI_DIRECT:
        (void)snprintf(text,
                       sizeof(text),
                       "%s%s",
                       r_llvm_abi_element_text(value->element),
                       r_llvm_abi_extend_text(value->extend));
        return r_llvm_abi_append(buffer, capacity, length, text);
    case R_LLVM_ABI_COERCE:
        if (value->homogeneous && result) {
            (void)snprintf(text,
                           sizeof(text),
                           "hfa %u x %s",
                           value->count,
                           r_llvm_abi_element_text(value->element));
        } else if (value->count == 1U) {
            (void)snprintf(text, sizeof(text), "%s", r_llvm_abi_element_text(value->element));
        } else {
            (void)snprintf(text,
                           sizeof(text),
                           "[%u x %s]",
                           value->count,
                           r_llvm_abi_element_text(value->element));
        }
        return r_llvm_abi_append(buffer, capacity, length, text);
    case R_LLVM_ABI_INDIRECT:
        return r_llvm_abi_append(buffer, capacity, length, result ? "sret" : "indirect");
    default:
        return false;
    }
}

bool r_llvm_abi_render_function(const RLlvmTypeTable *table,
                                uint32_t function_type,
                                char *buffer,
                                size_t capacity) {
    const RLlvmSurfaceType *type = table == NULL ? NULL : r_llvm_abi_type(table, function_type);
    RLlvmAbiValue value;
    size_t length = 0U;
    uint32_t parameter;
    bool first = true;

    if ((type == NULL) || (type->kind != R_LLVM_SURFACE_FUNCTION) || (buffer == NULL) ||
        (capacity == 0U)) {
        return false;
    }
    buffer[0] = '\0';
    if (!r_llvm_abi_classify_result(table, type->target, &value)) {
        return false;
    }
    if (value.abi_class == R_LLVM_ABI_INDIRECT) {
        if (!r_llvm_abi_append(buffer, capacity, &length, "void (sret")) {
            return false;
        }
        first = false;
    } else if (!r_llvm_abi_render_value(&value, true, buffer, capacity, &length) ||
               !r_llvm_abi_append(buffer, capacity, &length, " (")) {
        return false;
    }
    for (parameter = 0U; parameter < type->count; ++parameter) {
        uint32_t parameter_type = 0U;
        if (!table->parameter(table->context, type->first + parameter, &parameter_type) ||
            !r_llvm_abi_classify_argument(table, parameter_type, &value) ||
            (!first && !r_llvm_abi_append(buffer, capacity, &length, ", ")) ||
            !r_llvm_abi_render_value(&value, false, buffer, capacity, &length)) {
            return false;
        }
        first = false;
    }
    if (((type->flags & R_LLVM_SURFACE_VARIADIC) != 0U) &&
        !r_llvm_abi_append(buffer, capacity, &length, first ? "..." : ", ...")) {
        return false;
    }
    return r_llvm_abi_append(buffer, capacity, &length, ")");
}

static const RLlvmSurfaceType *r_llvm_surface_table_type(const void *context, uint32_t index) {
    (void)context;
    return r_llvm_surface_type(index);
}

static const RLlvmSurfaceField *r_llvm_surface_table_field(const void *context, uint32_t index) {
    (void)context;
    return r_llvm_surface_field(index);
}

static bool r_llvm_surface_table_parameter(const void *context, uint32_t index, uint32_t *type) {
    (void)context;
    return r_llvm_surface_parameter(index, type);
}

RLlvmTypeTable r_llvm_surface_table(void) {
    RLlvmTypeTable table;

    table.type = r_llvm_surface_table_type;
    table.field = r_llvm_surface_table_field;
    table.parameter = r_llvm_surface_table_parameter;
    table.context = NULL;
    return table;
}
