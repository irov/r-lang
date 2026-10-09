#include "emit_internal.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

/* Static objects of the LLVM emitter: a module object or a static local is a global of the
   module, initialized by the translation-time value of its initializer as the C17 emitter's
   r_c17_emit_static_initializer writes it. The value is laid out as bytes of its type, with the
   addresses of program strings as relocations, and becomes a packed constant structure. */

typedef struct RLlvmStaticImage {
    uint8_t *bytes;
    uint32_t size;
    /* Addresses in the image: a global (NULL for the image's own) plus a byte offset. */
    uint32_t *relocation_offsets;
    LLVMValueRef *relocation_targets;
    uint64_t *relocation_addends;
    uint32_t relocation_count;
    uint32_t relocation_capacity;
} RLlvmStaticImage;

static bool r_llvm_image_create(RLlvmEmitter *emitter, RLlvmStaticImage *image, uint32_t size) {
    (void)memset(image, 0, sizeof(*image));
    image->size = size;
    image->bytes = r_llvm_allocate(emitter, size == 0U ? 1U : size);
    return image->bytes != NULL;
}

static void r_llvm_image_free(RLlvmEmitter *emitter, RLlvmStaticImage *image) {
    r_llvm_free(emitter, image->bytes);
    r_llvm_free(emitter, image->relocation_offsets);
    r_llvm_free(emitter, image->relocation_targets);
    r_llvm_free(emitter, image->relocation_addends);
    (void)memset(image, 0, sizeof(*image));
}

static const RHirNode *r_llvm_hir(const RLlvmEmitter *emitter, RHirNodeId id) {
    return ((id == R_HIR_NODE_ID_INVALID) || ((size_t)id > emitter->frontend->hir_node_count))
               ? NULL
               : &emitter->frontend->hir_nodes[(size_t)id - 1U];
}

static RHirNodeId
r_llvm_hir_child(const RLlvmEmitter *emitter, const RHirNode *node, uint32_t index) {
    return (index >= node->child_count)
               ? R_HIR_NODE_ID_INVALID
               : emitter->frontend->hir_children[(size_t)node->first_child + index];
}

static bool r_llvm_image_integer(RLlvmEmitter *emitter,
                                 RLlvmStaticImage *image,
                                 uint32_t offset,
                                 uint32_t size,
                                 uint64_t value) {
    uint32_t index;

    if ((size > 8U) || (offset > image->size) || (size > image->size - offset)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    for (index = 0U; index < size; ++index) {
        image->bytes[offset + index] = (uint8_t)(value >> (8U * index));
    }
    return true;
}

/* An address at `offset`: `target` (the image's own global when NULL) plus `addend` bytes. */
static bool r_llvm_image_address(RLlvmEmitter *emitter,
                                 RLlvmStaticImage *image,
                                 uint32_t offset,
                                 LLVMValueRef target,
                                 uint64_t addend) {
    if ((offset > image->size) || (image->size - offset < 8U)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (image->relocation_count == image->relocation_capacity) {
        const uint32_t capacity =
            image->relocation_capacity == 0U ? 16U : image->relocation_capacity * 2U;
        uint32_t *offsets = r_llvm_allocate(emitter, capacity * sizeof(*offsets));
        LLVMValueRef *targets = r_llvm_allocate(emitter, capacity * sizeof(*targets));
        uint64_t *addends = r_llvm_allocate(emitter, capacity * sizeof(*addends));
        if ((offsets == NULL) || (targets == NULL) || (addends == NULL)) {
            r_llvm_free(emitter, offsets);
            r_llvm_free(emitter, targets);
            r_llvm_free(emitter, addends);
            return false;
        }
        if (image->relocation_count != 0U) {
            (void)memcpy(
                offsets, image->relocation_offsets, image->relocation_count * sizeof(*offsets));
            (void)memcpy(
                targets, image->relocation_targets, image->relocation_count * sizeof(*targets));
            (void)memcpy(
                addends, image->relocation_addends, image->relocation_count * sizeof(*addends));
        }
        r_llvm_free(emitter, image->relocation_offsets);
        r_llvm_free(emitter, image->relocation_targets);
        r_llvm_free(emitter, image->relocation_addends);
        image->relocation_offsets = offsets;
        image->relocation_targets = targets;
        image->relocation_addends = addends;
        image->relocation_capacity = capacity;
    }
    image->relocation_offsets[image->relocation_count] = offset;
    image->relocation_targets[image->relocation_count] = target;
    image->relocation_addends[image->relocation_count] = addend;
    image->relocation_count += 1U;
    return true;
}

static bool r_llvm_image_pointer(RLlvmEmitter *emitter,
                                 RLlvmStaticImage *image,
                                 uint32_t offset,
                                 LLVMValueRef target) {
    return (target != NULL) && r_llvm_image_address(emitter, image, offset, target, 0U);
}

/* The image as a packed constant structure of a new internal global: bytes between the
   addresses, in offset order. */
static LLVMValueRef r_llvm_image_global(RLlvmEmitter *emitter,
                                        RLlvmStaticImage *image,
                                        uint32_t align,
                                        const char *name) {
    const size_t capacity = (size_t)image->relocation_count * 2U + 1U;
    LLVMTypeRef *members = r_llvm_allocate(emitter, capacity * sizeof(*members));
    LLVMValueRef *values = r_llvm_allocate(emitter, capacity * sizeof(*values));
    LLVMValueRef global = NULL;
    uint32_t position = 0U;
    unsigned count = 0U;
    uint32_t index;

    if ((members == NULL) || (values == NULL)) {
        goto cleanup;
    }
    for (index = 1U; index < image->relocation_count; ++index) {
        uint32_t at = index;
        while ((at != 0U) && (image->relocation_offsets[at - 1U] > image->relocation_offsets[at])) {
            const uint32_t offset = image->relocation_offsets[at];
            LLVMValueRef target = image->relocation_targets[at];
            const uint64_t addend = image->relocation_addends[at];
            image->relocation_offsets[at] = image->relocation_offsets[at - 1U];
            image->relocation_targets[at] = image->relocation_targets[at - 1U];
            image->relocation_addends[at] = image->relocation_addends[at - 1U];
            image->relocation_offsets[at - 1U] = offset;
            image->relocation_targets[at - 1U] = target;
            image->relocation_addends[at - 1U] = addend;
            at -= 1U;
        }
    }
    for (index = 0U; index <= image->relocation_count; ++index) {
        const uint32_t end =
            index == image->relocation_count ? image->size : image->relocation_offsets[index];
        if (end > position) {
            members[count++] = LLVMArrayType2(r_llvm_int(emitter, 8U), end - position);
        }
        if (index < image->relocation_count) {
            members[count++] = r_llvm_pointer(emitter);
            position = end + 8U;
        }
    }
    global = LLVMAddGlobal(
        emitter->module, LLVMStructTypeInContext(emitter->context, members, count, 1), name);
    position = 0U;
    count = 0U;
    for (index = 0U; index <= image->relocation_count; ++index) {
        const uint32_t end =
            index == image->relocation_count ? image->size : image->relocation_offsets[index];
        if (end > position) {
            values[count++] = LLVMConstStringInContext2(
                emitter->context, (const char *)image->bytes + position, end - position, 1);
        }
        if (index < image->relocation_count) {
            LLVMValueRef target = image->relocation_targets[index] == NULL
                                      ? global
                                      : image->relocation_targets[index];
            if (image->relocation_addends[index] != 0U) {
                LLVMValueRef addend = r_llvm_u64(emitter, image->relocation_addends[index]);
                target = LLVMConstGEP2(r_llvm_int(emitter, 8U), target, &addend, 1U);
            }
            values[count++] = target;
            position = end + 8U;
        }
    }
    LLVMSetInitializer(global, LLVMConstStructInContext(emitter->context, values, count, 1));
    LLVMSetLinkage(global, LLVMInternalLinkage);
    LLVMSetAlignment(global, align == 0U ? 1U : align);

cleanup:
    r_llvm_free(emitter, members);
    r_llvm_free(emitter, values);
    return global;
}

static bool r_llvm_static_fill(
    RLlvmEmitter *emitter, RLlvmStaticImage *image, uint32_t offset, RHirNodeId id, uint32_t depth);

/* A program string as { data, length } at the offset; the data of an empty string is not null. */
static bool r_llvm_image_string(RLlvmEmitter *emitter,
                                RLlvmStaticImage *image,
                                uint32_t offset,
                                const RHirNode *node) {
    LLVMValueRef data = r_llvm_program_string(emitter, node->aggregate_member);
    return (data != NULL) && r_llvm_image_pointer(emitter, image, offset, data) &&
           r_llvm_image_integer(emitter, image, offset + 8U, 8U, node->integer_value);
}

/* The class of a scalar of a static initializer: an integer of its width and signedness (enums,
   chars and C integers by their representation; a bool is one bit), a float of 32 or 64 bits, or
   an address. */
typedef enum RLlvmStaticClass {
    R_LLVM_STATIC_NONE = 0,
    R_LLVM_STATIC_INTEGER,
    R_LLVM_STATIC_FLOAT,
    R_LLVM_STATIC_ADDRESS
} RLlvmStaticClass;

static RLlvmStaticClass
r_llvm_static_class(RLlvmEmitter *emitter, RTypeId type, unsigned *bits, bool *is_signed) {
    LLVMTypeRef scalar;

    if (r_llvm_value_kind(emitter, type) == R_SEMANTIC_TYPE_ATOMIC) {
        /* An atomic object starts as a value of its base type (C _Atomic). */
        const RSemanticType *atomic = r_llvm_type(emitter, r_llvm_value_type(emitter, type));
        type = atomic == NULL ? R_TYPE_ID_INVALID : atomic->base;
    }
    scalar = r_llvm_scalar_type(emitter, type);
    *bits = 0U;
    *is_signed = false;
    if (scalar == NULL) {
        return R_LLVM_STATIC_NONE;
    }
    switch (LLVMGetTypeKind(scalar)) {
    case LLVMIntegerTypeKind:
        if (!r_llvm_kind_integer(r_llvm_value_kind(emitter, type), bits, is_signed) &&
            !r_llvm_enum_integer(emitter, type, bits, is_signed)) {
            *bits = LLVMGetIntTypeWidth(scalar);
        }
        return R_LLVM_STATIC_INTEGER;
    case LLVMFloatTypeKind:
        *bits = 32U;
        return R_LLVM_STATIC_FLOAT;
    case LLVMDoubleTypeKind:
        *bits = 64U;
        return R_LLVM_STATIC_FLOAT;
    case LLVMPointerTypeKind:
        *bits = 64U;
        return R_LLVM_STATIC_ADDRESS;
    default:
        return R_LLVM_STATIC_NONE;
    }
}

static uint64_t r_llvm_static_mask(unsigned bits, uint64_t value) {
    return bits >= 64U ? value : (value & ((UINT64_C(1) << bits) - 1U));
}

static bool
r_llvm_static_scalar(RLlvmEmitter *emitter, RHirNodeId id, uint64_t *value, uint32_t depth);

/* A scalar of a static initializer read as a number of its class. */
typedef struct RLlvmStaticNumber {
    RLlvmStaticClass class_;
    unsigned bits;
    bool is_signed;
    uint64_t integer; /* sign-extended when signed */
    double real;
} RLlvmStaticNumber;

static bool r_llvm_static_number(RLlvmEmitter *emitter,
                                 RHirNodeId id,
                                 RLlvmStaticNumber *number,
                                 uint32_t depth) {
    const RHirNode *node = r_llvm_hir(emitter, id);
    uint64_t raw = 0U;

    if ((node == NULL) || !r_llvm_static_scalar(emitter, id, &raw, depth)) {
        return node == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
    }
    number->class_ = r_llvm_static_class(emitter, node->type, &number->bits, &number->is_signed);
    number->integer = 0U;
    number->real = 0.0;
    if (number->class_ == R_LLVM_STATIC_FLOAT) {
        if (number->bits == 32U) {
            float narrow;
            const uint32_t bits = (uint32_t)raw;
            (void)memcpy(&narrow, &bits, sizeof(narrow));
            number->real = (double)narrow;
        } else {
            (void)memcpy(&number->real, &raw, sizeof(number->real));
        }
        return true;
    }
    if (number->class_ != R_LLVM_STATIC_INTEGER) {
        return r_llvm_unsupported(emitter, "an operator on this value in a static initializer");
    }
    raw = r_llvm_static_mask(number->bits, raw);
    if (number->is_signed && (number->bits < 64U) && ((raw >> (number->bits - 1U)) & 1U)) {
        raw |= ~((UINT64_C(1) << number->bits) - 1U);
    }
    number->integer = raw;
    return true;
}

/* The bits of a number in the class of the result type `type`. */
static bool r_llvm_static_result(RLlvmEmitter *emitter,
                                 RTypeId type,
                                 bool real,
                                 uint64_t integer,
                                 double value,
                                 uint64_t *bits_out) {
    unsigned bits = 0U;
    bool is_signed = false;
    const RLlvmStaticClass class_ = r_llvm_static_class(emitter, type, &bits, &is_signed);

    if (class_ == R_LLVM_STATIC_FLOAT) {
        if (!real) {
            value = is_signed ? (double)(int64_t)integer : (double)integer;
        }
        if (bits == 32U) {
            const float narrow = (float)value;
            uint32_t raw = 0U;
            (void)memcpy(&raw, &narrow, sizeof(raw));
            *bits_out = raw;
        } else {
            (void)memcpy(bits_out, &value, sizeof(value));
        }
        return true;
    }
    if (class_ != R_LLVM_STATIC_INTEGER) {
        return r_llvm_unsupported(emitter, "an operator on this value in a static initializer");
    }
    if (real) {
        integer = is_signed ? (uint64_t)(int64_t)value : (uint64_t)value;
    }
    *bits_out = bits == 1U ? (integer != 0U ? 1U : 0U) : r_llvm_static_mask(bits, integer);
    return true;
}

/* An operator of a static initializer (r_c17_emit_static_initializer writes it as a C constant
   expression, which the C compiler folds): evaluated with the C rules for the operand types. */
static bool r_llvm_static_operator(RLlvmEmitter *emitter,
                                   const RHirNode *node,
                                   uint64_t *value,
                                   uint32_t depth) {
    RLlvmStaticNumber left;
    RLlvmStaticNumber right;
    bool real;
    uint64_t integer = 0U;
    double result = 0.0;

    if (node->kind == R_HIR_CONDITIONAL) {
        RLlvmStaticNumber condition;
        if (!r_llvm_static_number(
                emitter, r_llvm_hir_child(emitter, node, 0U), &condition, depth + 1U)) {
            return false;
        }
        return r_llvm_static_scalar(
            emitter,
            r_llvm_hir_child(emitter,
                             node,
                             ((condition.class_ == R_LLVM_STATIC_FLOAT) ? (condition.real != 0.0)
                                                                        : (condition.integer != 0U))
                                 ? 1U
                                 : 2U),
            value,
            depth + 1U);
    }
    if (!r_llvm_static_number(emitter, r_llvm_hir_child(emitter, node, 0U), &left, depth + 1U)) {
        return false;
    }
    real = left.class_ == R_LLVM_STATIC_FLOAT;
    if (node->kind == R_HIR_UNARY) {
        switch (node->operation) {
        case R_TOKEN_MINUS:
            integer = 0U - left.integer;
            result = -left.real;
            break;
        case R_TOKEN_PLUS:
            integer = left.integer;
            result = left.real;
            break;
        case R_TOKEN_TILDE:
            if (real) {
                return r_llvm_unsupported(emitter, "this operator in a static initializer");
            }
            integer = ~left.integer;
            break;
        case R_TOKEN_BANG:
            integer = (real ? (left.real == 0.0) : (left.integer == 0U)) ? 1U : 0U;
            real = false;
            break;
        default:
            return r_llvm_unsupported(emitter, "this operator in a static initializer");
        }
        return r_llvm_static_result(emitter, node->type, real, integer, result, value);
    }
    if (!r_llvm_static_number(emitter, r_llvm_hir_child(emitter, node, 1U), &right, depth + 1U)) {
        return false;
    }
    if (real != (right.class_ == R_LLVM_STATIC_FLOAT)) {
        return r_llvm_unsupported(emitter, "this operator in a static initializer");
    }
    switch (node->operation) {
    case R_TOKEN_PLUS:
        integer = left.integer + right.integer;
        result = left.real + right.real;
        break;
    case R_TOKEN_MINUS:
        integer = left.integer - right.integer;
        result = left.real - right.real;
        break;
    case R_TOKEN_STAR:
        integer = left.integer * right.integer;
        result = left.real * right.real;
        break;
    case R_TOKEN_SLASH:
    case R_TOKEN_PERCENT:
        if (real) {
            if (node->operation == R_TOKEN_PERCENT) {
                return r_llvm_unsupported(emitter, "this operator in a static initializer");
            }
            result = left.real / right.real;
            break;
        }
        if ((right.integer == 0U) || (left.is_signed && (left.integer == (UINT64_C(1) << 63U)) &&
                                      (right.integer == UINT64_MAX))) {
            return r_llvm_unsupported(emitter, "a division by zero in a static initializer");
        }
        if (left.is_signed) {
            const int64_t a = (int64_t)left.integer;
            const int64_t b = (int64_t)right.integer;
            integer = (uint64_t)(node->operation == R_TOKEN_SLASH ? a / b : a % b);
        } else {
            integer = node->operation == R_TOKEN_SLASH ? left.integer / right.integer
                                                       : left.integer % right.integer;
        }
        break;
    case R_TOKEN_AMP:
    case R_TOKEN_PIPE:
    case R_TOKEN_CARET:
    case R_TOKEN_LESS_LESS:
    case R_TOKEN_GREATER_GREATER:
        if (real) {
            return r_llvm_unsupported(emitter, "this operator in a static initializer");
        }
        if (node->operation == R_TOKEN_AMP) {
            integer = left.integer & right.integer;
        } else if (node->operation == R_TOKEN_PIPE) {
            integer = left.integer | right.integer;
        } else if (node->operation == R_TOKEN_CARET) {
            integer = left.integer ^ right.integer;
        } else if (right.integer >= 64U) {
            return r_llvm_unsupported(emitter, "this shift in a static initializer");
        } else if (node->operation == R_TOKEN_LESS_LESS) {
            integer = left.integer << right.integer;
        } else {
            integer = left.is_signed
                          ? (uint64_t)((int64_t)left.integer >> right.integer)
                          : (r_llvm_static_mask(left.bits, left.integer) >> right.integer);
        }
        break;
    case R_TOKEN_EQUAL_EQUAL:
    case R_TOKEN_BANG_EQUAL:
    case R_TOKEN_LESS:
    case R_TOKEN_LESS_EQUAL:
    case R_TOKEN_GREATER:
    case R_TOKEN_GREATER_EQUAL: {
        int order;
        if (real) {
            if ((left.real != left.real) || (right.real != right.real)) {
                integer = node->operation == R_TOKEN_BANG_EQUAL ? 1U : 0U;
                real = false;
                break;
            }
            order = left.real < right.real ? -1 : (left.real > right.real ? 1 : 0);
        } else if (left.is_signed) {
            order = (int64_t)left.integer < (int64_t)right.integer
                        ? -1
                        : ((int64_t)left.integer > (int64_t)right.integer ? 1 : 0);
        } else {
            order = left.integer < right.integer ? -1 : (left.integer > right.integer ? 1 : 0);
        }
        integer = ((node->operation == R_TOKEN_EQUAL_EQUAL) && (order == 0)) ||
                          ((node->operation == R_TOKEN_BANG_EQUAL) && (order != 0)) ||
                          ((node->operation == R_TOKEN_LESS) && (order < 0)) ||
                          ((node->operation == R_TOKEN_LESS_EQUAL) && (order <= 0)) ||
                          ((node->operation == R_TOKEN_GREATER) && (order > 0)) ||
                          ((node->operation == R_TOKEN_GREATER_EQUAL) && (order >= 0))
                      ? 1U
                      : 0U;
        real = false;
        break;
    }
    case R_TOKEN_AMP_AMP:
    case R_TOKEN_PIPE_PIPE: {
        const bool a = real ? (left.real != 0.0) : (left.integer != 0U);
        const bool b = real ? (right.real != 0.0) : (right.integer != 0U);
        integer = (node->operation == R_TOKEN_AMP_AMP ? (a && b) : (a || b)) ? 1U : 0U;
        real = false;
        break;
    }
    default:
        return r_llvm_unsupported(emitter, "this operator in a static initializer");
    }
    return r_llvm_static_result(emitter, node->type, real, integer, result, value);
}

/* The bits of a scalar constant of a static initializer in its own type: a literal, an
   enumerator, a type query or a cast of one, converted as a C constant expression is
   (r_c17_emit_static_initializer writes `(T)value`). */
static bool
r_llvm_static_scalar(RLlvmEmitter *emitter, RHirNodeId id, uint64_t *value, uint32_t depth) {
    const RHirNode *node = r_llvm_hir(emitter, id);
    const RHirNode *operand;
    RLlvmStaticClass from;
    RLlvmStaticClass to;
    unsigned from_bits = 0U;
    unsigned to_bits = 0U;
    bool from_signed = false;
    bool to_signed = false;
    uint64_t source = 0U;

    if ((node == NULL) || (depth > 64U)) {
        return r_llvm_unsupported(emitter, "a converting cast in a static initializer");
    }
    switch (node->kind) {
    case R_HIR_LITERAL:
        *value = node->operation == R_TOKEN_KW_NULL ? 0U : node->integer_value;
        return true;
    case R_HIR_ENUM_CONSTANT:
        *value = node->integer_value;
        return true;
    case R_HIR_TYPE_QUERY: {
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (!r_llvm_layout(emitter, node->auxiliary_type, &size, &align)) {
            return false;
        }
        *value = node->operation == R_TOKEN_KW_SIZEOF ? size : align;
        return true;
    }
    case R_HIR_CAST:
        break;
    case R_HIR_UNARY:
    case R_HIR_BINARY:
    case R_HIR_CONDITIONAL:
        return r_llvm_static_operator(emitter, node, value, depth);
    default:
        return r_llvm_unsupported(emitter, "a converting cast in a static initializer");
    }
    operand = r_llvm_hir(emitter, r_llvm_hir_child(emitter, node, 0U));
    if ((operand == NULL) ||
        !r_llvm_static_scalar(emitter, r_llvm_hir_child(emitter, node, 0U), &source, depth + 1U)) {
        return false;
    }
    from = r_llvm_static_class(emitter, operand->type, &from_bits, &from_signed);
    to = r_llvm_static_class(emitter, node->type, &to_bits, &to_signed);
    if ((from == R_LLVM_STATIC_NONE) || (to == R_LLVM_STATIC_NONE)) {
        return r_llvm_unsupported(emitter, "a converting cast in a static initializer");
    }
    if (from == R_LLVM_STATIC_FLOAT) {
        double real;
        if (from_bits == 32U) {
            float narrow;
            const uint32_t raw = (uint32_t)source;
            (void)memcpy(&narrow, &raw, sizeof(narrow));
            real = (double)narrow;
        } else {
            (void)memcpy(&real, &source, sizeof(real));
        }
        if (to == R_LLVM_STATIC_FLOAT) {
            if (to_bits == 32U) {
                const float narrow = (float)real;
                uint32_t raw = 0U;
                (void)memcpy(&raw, &narrow, sizeof(raw));
                *value = raw;
            } else {
                (void)memcpy(value, &real, sizeof(real));
            }
            return true;
        }
        if (to != R_LLVM_STATIC_INTEGER) {
            return r_llvm_unsupported(emitter, "a converting cast in a static initializer");
        }
        /* The truncation toward zero of C; R rejects an out-of-range constant before this. */
        *value = r_llvm_static_mask(to_bits, to_signed ? (uint64_t)(int64_t)real : (uint64_t)real);
        return true;
    }
    /* An integer or an address, extended from its width. */
    if ((from == R_LLVM_STATIC_INTEGER) && from_signed && (from_bits < 64U) &&
        ((source >> (from_bits - 1U)) & 1U)) {
        source |= ~((UINT64_C(1) << from_bits) - 1U);
    } else {
        source = r_llvm_static_mask(from_bits, source);
    }
    if (to == R_LLVM_STATIC_FLOAT) {
        const double real = (from == R_LLVM_STATIC_INTEGER) && from_signed ? (double)(int64_t)source
                                                                           : (double)source;
        if (to_bits == 32U) {
            const float narrow = (float)real;
            uint32_t raw = 0U;
            (void)memcpy(&raw, &narrow, sizeof(raw));
            *value = raw;
        } else {
            (void)memcpy(value, &real, sizeof(real));
        }
        return true;
    }
    if ((to == R_LLVM_STATIC_INTEGER) && (to_bits == 1U)) {
        *value = source != 0U ? 1U : 0U;
        return true;
    }
    *value = r_llvm_static_mask(to_bits, source);
    return true;
}

/* ---- L22.4: owners frozen into the program image (the C17 emitter's r_c17_emit_frozen_data)

   An array initializer of an owner type holds an owner computed during translation: an array,
   a string, a list or a dictionary. Its contents become globals of their own, and the owner is a
   descriptor over them without an allocator; the static object that holds one is never
   destroyed. */

typedef enum RLlvmFrozenKind {
    R_LLVM_FROZEN_NONE = 0,
    R_LLVM_FROZEN_ARRAY,
    R_LLVM_FROZEN_LIST,
    R_LLVM_FROZEN_DICT,
    R_LLVM_FROZEN_STRING
} RLlvmFrozenKind;

static RLlvmFrozenKind r_llvm_frozen_kind(RLlvmEmitter *emitter, const RHirNode *node) {
    const RSemanticType *type;

    if ((node == NULL) || (node->kind != R_HIR_ARRAY_INIT)) {
        return R_LLVM_FROZEN_NONE;
    }
    type = r_llvm_type(emitter, r_llvm_value_type(emitter, node->type));
    if (type == NULL) {
        return R_LLVM_FROZEN_NONE;
    }
    switch (type->kind) {
    case R_SEMANTIC_TYPE_ARRAY:
        return R_LLVM_FROZEN_ARRAY;
    case R_SEMANTIC_TYPE_LIST:
        return R_LLVM_FROZEN_LIST;
    case R_SEMANTIC_TYPE_DICT:
        return R_LLVM_FROZEN_DICT;
    default:
        return r_llvm_standard_named(emitter, node->type, "std.string::string")
                   ? R_LLVM_FROZEN_STRING
                   : R_LLVM_FROZEN_NONE;
    }
}

static bool r_llvm_contains_frozen(RLlvmEmitter *emitter, RHirNodeId id, uint32_t depth) {
    const RHirNode *node = r_llvm_hir(emitter, id);
    uint32_t index;

    if ((node == NULL) || (depth > 256U)) {
        return false;
    }
    if (r_llvm_frozen_kind(emitter, node) != R_LLVM_FROZEN_NONE) {
        return true;
    }
    for (index = 0U; index < node->child_count; ++index) {
        if (r_llvm_contains_frozen(emitter, r_llvm_hir_child(emitter, node, index), depth + 1U)) {
            return true;
        }
    }
    return false;
}

/* A field of a runtime structure at `base` in the image. */
static bool r_llvm_frozen_field(RLlvmEmitter *emitter,
                                const char *type_name,
                                const char *field,
                                uint32_t base,
                                uint32_t *offset) {
    uint32_t at = 0U;

    if (!r_llvm_runtime_field(emitter, type_name, field, &at, NULL)) {
        return false;
    }
    *offset = base + at;
    return true;
}

/* RRuntimeTypeInfo of a frozen element: its size and alignment, never moved or dropped. */
static bool r_llvm_frozen_type_info(RLlvmEmitter *emitter,
                                    RLlvmStaticImage *image,
                                    uint32_t offset,
                                    RTypeId type) {
    uint32_t size = 0U;
    uint32_t align = 0U;
    uint32_t size_at = 0U;
    uint32_t align_at = 0U;

    return r_llvm_layout(emitter, type, &size, &align) &&
           r_llvm_frozen_field(emitter, "RRuntimeTypeInfo", "size", offset, &size_at) &&
           r_llvm_frozen_field(emitter, "RRuntimeTypeInfo", "alignment", offset, &align_at) &&
           r_llvm_image_integer(emitter, image, size_at, 8U, size) &&
           r_llvm_image_integer(emitter, image, align_at, 8U, align);
}

/* Fills an image of `count` slots of `stride` bytes from the children of `node`, starting at
   child `first` and taking every `step`-th, at `offset` within each slot. */
static LLVMValueRef r_llvm_frozen_global(RLlvmEmitter *emitter,
                                         RLlvmStaticImage *data,
                                         uint32_t align,
                                         const char *prefix,
                                         RHirNodeId id) {
    /* Named by the place of the value in its source, which no order of the sources changes. */
    const RSourceSpan span =
        ((id != R_HIR_NODE_ID_INVALID) && ((size_t)id <= emitter->frontend->hir_node_count))
            ? emitter->frontend->hir_nodes[(size_t)id - 1U].span
            : (RSourceSpan){0U, 0U, 0U};
    char name[80];
    (void)snprintf(name,
                   sizeof(name),
                   "r_frozen_%s.%" PRIu32 ".%" PRIu32,
                   prefix,
                   r_llvm_source_key(emitter, span.source),
                   span.start);
    return r_llvm_image_global(emitter, data, align, name);
}

static uint32_t r_llvm_align_to(uint32_t value, uint32_t align) {
    return align <= 1U ? value : ((value + align - 1U) / align) * align;
}

/* The key hash of R-LIB-0020 as the key functions compute it (r_c17_frozen_key_hash). */
static bool r_llvm_frozen_key_hash(RLlvmEmitter *emitter, const RHirNode *key, uint64_t *hash) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, key->type));
    const RHirNode *literal = key;
    RSemanticTypeKind integer;
    bool is_signed = false;
    uint32_t width = 64U;

    if (type == NULL) {
        return false;
    }
    if ((literal->kind == R_HIR_CAST) && (literal->child_count == 1U)) {
        literal = r_llvm_hir(emitter, r_llvm_hir_child(emitter, literal, 0U));
    }
    if ((literal != NULL) && (literal->kind == R_HIR_STRING_LITERAL) &&
        ((type->kind == R_SEMANTIC_TYPE_STR) || (type->kind == R_SEMANTIC_TYPE_CONSTEXPR_STR))) {
        const RInternEntry *entry =
            literal->integer_value == 0U
                ? NULL
                : &emitter->frontend->intern_entries[(size_t)literal->aggregate_member - 1U];
        uint64_t value = UINT64_C(14695981039346656037);
        uint64_t index;
        for (index = 0U; index < literal->integer_value; ++index) {
            value = (value ^ (uint8_t)entry->bytes[index]) * UINT64_C(1099511628211);
        }
        *hash = value;
        return true;
    }
    if ((literal == NULL) ||
        ((literal->kind != R_HIR_LITERAL) && (literal->kind != R_HIR_ENUM_CONSTANT))) {
        return false;
    }
    integer = r_semantic_integer_representation(
        emitter->frontend,
        type->kind == R_SEMANTIC_TYPE_ENUM
            ? emitter->frontend->semantic_aggregates[type->base - 1U].enum_underlying_type
            : r_llvm_value_type(emitter, key->type));
    *hash = literal->integer_value;
    if ((type->kind != R_SEMANTIC_TYPE_BOOL) && (type->kind != R_SEMANTIC_TYPE_CHAR) &&
        r_semantic_integer_properties_public(integer, &is_signed, &width) && is_signed &&
        (width < 64U) && ((*hash & (UINT64_C(1) << (width - 1U))) != 0U)) {
        *hash |= ~((UINT64_C(1) << width) - 1U);
    }
    return true;
}

/* The runtime's mix of a key hash with the seed of a dictionary (r_c17_frozen_mix). */
static uint64_t r_llvm_frozen_mix(uint64_t hash, uint64_t seed) {
    uint64_t mixed = hash ^ seed ^ UINT64_C(0x9e3779b97f4a7c15);

    mixed ^= mixed >> 30U;
    mixed *= UINT64_C(0xbf58476d1ce4e5b9);
    mixed ^= mixed >> 27U;
    mixed *= UINT64_C(0x94d049bb133111eb);
    mixed ^= mixed >> 31U;
    return mixed;
}

/* R-LIB-0021: a power-of-two slot count of at least eight holding at most two thirds live. */
static uint64_t r_llvm_frozen_dict_capacity(uint64_t length) {
    uint64_t capacity = 8U;

    if (length == 0U) {
        return 0U;
    }
    while (length > ((capacity / 3U) * 2U) + (((capacity % 3U) * 2U) / 3U)) {
        capacity *= 2U;
    }
    return capacity;
}

static bool r_llvm_frozen_array(RLlvmEmitter *emitter,
                                RLlvmStaticImage *image,
                                uint32_t offset,
                                RHirNodeId id,
                                const RHirNode *node,
                                uint32_t depth) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, node->type));
    uint32_t element_size = 0U;
    uint32_t element_align = 0U;
    uint32_t at = 0U;
    uint32_t index;

    if ((type == NULL) || !r_llvm_layout(emitter, type->base, &element_size, &element_align) ||
        !r_llvm_frozen_field(emitter, "RRuntimeArray", "element", offset, &at) ||
        !r_llvm_frozen_type_info(emitter, image, at, type->base)) {
        return false;
    }
    if (node->child_count != 0U) {
        RLlvmStaticImage data;
        LLVMValueRef global;
        if (!r_llvm_image_create(emitter, &data, node->child_count * element_size)) {
            return false;
        }
        for (index = 0U; index < node->child_count; ++index) {
            if (!r_llvm_static_fill(emitter,
                                    &data,
                                    index * element_size,
                                    r_llvm_hir_child(emitter, node, index),
                                    depth + 1U)) {
                r_llvm_image_free(emitter, &data);
                return false;
            }
        }
        global = r_llvm_frozen_global(emitter, &data, element_align, "data", id);
        r_llvm_image_free(emitter, &data);
        if ((global == NULL) ||
            !r_llvm_frozen_field(emitter, "RRuntimeArray", "data", offset, &at) ||
            !r_llvm_image_pointer(emitter, image, at, global)) {
            return false;
        }
    }
    return r_llvm_frozen_field(emitter, "RRuntimeArray", "length", offset, &at) &&
           r_llvm_image_integer(emitter, image, at, 8U, node->child_count) &&
           r_llvm_frozen_field(emitter, "RRuntimeArray", "capacity", offset, &at) &&
           r_llvm_image_integer(emitter, image, at, 8U, node->child_count);
}

static bool r_llvm_frozen_string(RLlvmEmitter *emitter,
                                 RLlvmStaticImage *image,
                                 uint32_t offset,
                                 RHirNodeId id,
                                 const RHirNode *node) {
    const RHirNode *literal = r_llvm_hir(emitter, r_llvm_hir_child(emitter, node, 0U));
    uint64_t length = 0U;
    uint32_t bytes = 0U;
    uint32_t at = 0U;

    if (node->child_count != 0U) {
        if ((literal == NULL) || (literal->kind != R_HIR_STRING_LITERAL)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        length = literal->integer_value;
    }
    /* RRuntimeString {RRuntimeArray bytes} of u8 elements. */
    if (!r_llvm_frozen_field(emitter, "RRuntimeString", "bytes", offset, &bytes) ||
        !r_llvm_frozen_field(emitter, "RRuntimeArray", "element", bytes, &at) ||
        !r_llvm_frozen_field(emitter, "RRuntimeTypeInfo", "size", at, &at) ||
        !r_llvm_image_integer(emitter, image, at, 8U, 1U) ||
        !r_llvm_frozen_field(emitter, "RRuntimeArray", "element", bytes, &at) ||
        !r_llvm_frozen_field(emitter, "RRuntimeTypeInfo", "alignment", at, &at) ||
        !r_llvm_image_integer(emitter, image, at, 8U, 1U)) {
        return false;
    }
    if (length != 0U) {
        const RInternEntry *entry =
            &emitter->frontend->intern_entries[(size_t)literal->aggregate_member - 1U];
        RLlvmStaticImage data;
        LLVMValueRef global;
        if ((length > UINT32_MAX) || !r_llvm_image_create(emitter, &data, (uint32_t)length)) {
            return length > UINT32_MAX ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
        }
        (void)memcpy(data.bytes, entry->bytes, (size_t)length);
        global = r_llvm_frozen_global(emitter, &data, 1U, "data", id);
        r_llvm_image_free(emitter, &data);
        if ((global == NULL) ||
            !r_llvm_frozen_field(emitter, "RRuntimeArray", "data", bytes, &at) ||
            !r_llvm_image_pointer(emitter, image, at, global)) {
            return false;
        }
    }
    return r_llvm_frozen_field(emitter, "RRuntimeArray", "length", bytes, &at) &&
           r_llvm_image_integer(emitter, image, at, 8U, length) &&
           r_llvm_frozen_field(emitter, "RRuntimeArray", "capacity", bytes, &at) &&
           r_llvm_image_integer(emitter, image, at, 8U, length);
}

/* The nodes {RRuntimeListNode node; T value} linked in order in one global. */
static bool r_llvm_frozen_list(RLlvmEmitter *emitter,
                               RLlvmStaticImage *image,
                               uint32_t offset,
                               RHirNodeId id,
                               const RHirNode *node,
                               uint32_t depth) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, node->type));
    uint32_t element_size = 0U;
    uint32_t element_align = 0U;
    uint32_t link_size = 0U;
    uint32_t link_align = 0U;
    uint32_t previous = 0U;
    uint32_t next = 0U;
    uint32_t value_offset;
    uint32_t node_align;
    uint32_t node_size;
    uint32_t at = 0U;
    uint32_t index;
    RLlvmStaticImage data;
    LLVMValueRef global;

    if ((type == NULL) || !r_llvm_layout(emitter, type->base, &element_size, &element_align) ||
        !r_llvm_runtime_layout(emitter, "RRuntimeListNode", &link_size, &link_align) ||
        !r_llvm_runtime_field(emitter, "RRuntimeListNode", "previous", &previous, NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeListNode", "next", &next, NULL) ||
        !r_llvm_frozen_field(emitter, "RRuntimeList", "element", offset, &at) ||
        !r_llvm_frozen_type_info(emitter, image, at, type->base)) {
        return false;
    }
    if (node->child_count == 0U) {
        return true;
    }
    value_offset = r_llvm_align_to(link_size, element_align);
    node_align = element_align > link_align ? element_align : link_align;
    node_size = r_llvm_align_to(value_offset + element_size, node_align);
    if (!r_llvm_image_create(emitter, &data, node->child_count * node_size)) {
        return false;
    }
    for (index = 0U; index < node->child_count; ++index) {
        const uint32_t base = index * node_size;
        if (((index != 0U) &&
             !r_llvm_image_address(emitter, &data, base + previous, NULL, base - node_size)) ||
            ((index + 1U != node->child_count) &&
             !r_llvm_image_address(emitter, &data, base + next, NULL, base + node_size)) ||
            !r_llvm_static_fill(emitter,
                                &data,
                                base + value_offset,
                                r_llvm_hir_child(emitter, node, index),
                                depth + 1U)) {
            r_llvm_image_free(emitter, &data);
            return false;
        }
    }
    global = r_llvm_frozen_global(emitter, &data, node_align, "nodes", id);
    r_llvm_image_free(emitter, &data);
    return (global != NULL) && r_llvm_frozen_field(emitter, "RRuntimeList", "first", offset, &at) &&
           r_llvm_image_address(emitter, image, at, global, 0U) &&
           r_llvm_frozen_field(emitter, "RRuntimeList", "last", offset, &at) &&
           r_llvm_image_address(
               emitter, image, at, global, (uint64_t)(node->child_count - 1U) * node_size) &&
           r_llvm_frozen_field(emitter, "RRuntimeList", "length", offset, &at) &&
           r_llvm_image_integer(emitter, image, at, 8U, node->child_count) &&
           r_llvm_frozen_field(emitter, "RRuntimeList", "value_offset", offset, &at) &&
           r_llvm_image_integer(emitter, image, at, 8U, value_offset) &&
           r_llvm_frozen_field(emitter, "RRuntimeList", "node_alignment", offset, &at) &&
           r_llvm_image_integer(emitter, image, at, 8U, node_align);
}

/* Dense entries {RRuntimeDictEntryHeader header; K key; V value} in insertion order and the
   open-addressed slots of R-LIB-0021 probed linearly from the mixed hash. */
static bool r_llvm_frozen_dict(RLlvmEmitter *emitter,
                               RLlvmStaticImage *image,
                               uint32_t offset,
                               RHirNodeId id,
                               const RHirNode *node,
                               uint32_t depth) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, node->type));
    const uint32_t length = node->child_count / 2U;
    const uint64_t capacity = r_llvm_frozen_dict_capacity(length);
    uint32_t key_size = 0U;
    uint32_t key_align = 0U;
    uint32_t value_size = 0U;
    uint32_t value_align = 0U;
    uint32_t header_size = 0U;
    uint32_t header_align = 0U;
    uint32_t slot_size = 0U;
    uint32_t slot_align = 0U;
    uint32_t slot_index = 0U;
    uint32_t slot_state = 0U;
    uint32_t key = 0U;
    uint32_t at = 0U;
    uint32_t key_offset;
    uint32_t value_offset;
    uint32_t entry_align;
    uint32_t entry_size;
    int64_t live = 0;
    LLVMValueRef hash_function;
    LLVMValueRef equal_function;
    RLlvmStaticImage entries;
    RLlvmStaticImage slots;
    LLVMValueRef entries_global;
    LLVMValueRef slots_global;
    uint64_t slot;
    uint32_t index;
    bool success = false;

    if ((type == NULL) || ((node->child_count % 2U) != 0U) || (capacity > UINT32_MAX)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    hash_function = r_llvm_key_function(emitter, type->base, true);
    equal_function = r_llvm_key_function(emitter, type->base, false);
    if ((hash_function == NULL) || (equal_function == NULL) ||
        !r_llvm_frozen_field(emitter, "RRuntimeDict", "key", offset, &key) ||
        !r_llvm_frozen_field(emitter, "RRuntimeDictKeyInfo", "type", key, &at) ||
        !r_llvm_frozen_type_info(emitter, image, at, type->base) ||
        !r_llvm_frozen_field(emitter, "RRuntimeDictKeyInfo", "hash", key, &at) ||
        !r_llvm_image_pointer(emitter, image, at, hash_function) ||
        !r_llvm_frozen_field(emitter, "RRuntimeDictKeyInfo", "equal", key, &at) ||
        !r_llvm_image_pointer(emitter, image, at, equal_function) ||
        !r_llvm_frozen_field(emitter, "RRuntimeDict", "value", offset, &at) ||
        !r_llvm_frozen_type_info(emitter, image, at, type->second)) {
        return false;
    }
    if (length == 0U) {
        return r_llvm_frozen_field(emitter, "RRuntimeDict", "first_order", offset, &at) &&
               r_llvm_image_integer(emitter, image, at, 8U, UINT64_MAX) &&
               r_llvm_frozen_field(emitter, "RRuntimeDict", "last_order", offset, &at) &&
               r_llvm_image_integer(emitter, image, at, 8U, UINT64_MAX);
    }
    if (!r_llvm_layout(emitter, type->base, &key_size, &key_align) ||
        !r_llvm_layout(emitter, type->second, &value_size, &value_align) ||
        !r_llvm_runtime_layout(emitter, "RRuntimeDictEntryHeader", &header_size, &header_align) ||
        !r_llvm_runtime_layout(emitter, "RRuntimeDictIndexSlot", &slot_size, &slot_align) ||
        !r_llvm_runtime_field(emitter, "RRuntimeDictIndexSlot", "entry_index", &slot_index, NULL) ||
        !r_llvm_runtime_field(emitter, "RRuntimeDictIndexSlot", "state", &slot_state, NULL) ||
        !r_llvm_runtime_constant(emitter, "R_RUNTIME_DICT_SLOT_LIVE", &live)) {
        return false;
    }
    key_offset = r_llvm_align_to(header_size, key_align);
    value_offset = r_llvm_align_to(key_offset + key_size, value_align);
    entry_align = header_align;
    entry_align = key_align > entry_align ? key_align : entry_align;
    entry_align = value_align > entry_align ? value_align : entry_align;
    entry_size = r_llvm_align_to(value_offset + value_size, entry_align);
    if (!r_llvm_image_create(emitter, &entries, length * entry_size)) {
        return false;
    }
    if (!r_llvm_image_create(emitter, &slots, (uint32_t)capacity * slot_size)) {
        r_llvm_image_free(emitter, &entries);
        return false;
    }
    for (slot = 0U; slot < capacity; ++slot) {
        /* An empty slot: R_RUNTIME_DICT_SLOT_EMPTY is zero. */
        if (!r_llvm_image_integer(
                emitter, &slots, (uint32_t)slot * slot_size + slot_index, 8U, UINT64_MAX)) {
            goto cleanup;
        }
    }
    for (index = 0U; index < length; ++index) {
        const RHirNode *key_node = r_llvm_hir(emitter, r_llvm_hir_child(emitter, node, 2U * index));
        uint64_t hash = 0U;
        if ((key_node == NULL) || !r_llvm_frozen_key_hash(emitter, key_node, &hash)) {
            (void)r_llvm_unsupported(emitter, "a key of a frozen dictionary");
            goto cleanup;
        }
        hash = r_llvm_frozen_mix(hash, 0U);
        slot = hash & (capacity - 1U);
        while (slots.bytes[(uint32_t)slot * slot_size + slot_state] != 0U) {
            slot = (slot + 1U) & (capacity - 1U);
        }
        if (!r_llvm_image_integer(
                emitter, &slots, (uint32_t)slot * slot_size + slot_index, 8U, index) ||
            !r_llvm_image_integer(
                emitter, &slots, (uint32_t)slot * slot_size + slot_state, 4U, (uint64_t)live) ||
            !r_llvm_image_integer(emitter, &entries, index * entry_size, 8U, hash) ||
            !r_llvm_static_fill(emitter,
                                &entries,
                                index * entry_size + key_offset,
                                r_llvm_hir_child(emitter, node, 2U * index),
                                depth + 1U) ||
            !r_llvm_static_fill(emitter,
                                &entries,
                                index * entry_size + value_offset,
                                r_llvm_hir_child(emitter, node, 2U * index + 1U),
                                depth + 1U)) {
            goto cleanup;
        }
    }
    /* The entry index of an empty slot is never read; keep it zero as C17 does. */
    for (slot = 0U; slot < capacity; ++slot) {
        if ((slots.bytes[(uint32_t)slot * slot_size + slot_state] == 0U) &&
            !r_llvm_image_integer(
                emitter, &slots, (uint32_t)slot * slot_size + slot_index, 8U, 0U)) {
            goto cleanup;
        }
    }
    entries_global = r_llvm_frozen_global(emitter, &entries, entry_align, "entries", id);
    slots_global = r_llvm_frozen_global(
        emitter, &slots, entry_align > slot_align ? entry_align : slot_align, "slots", id);
    success = (entries_global != NULL) && (slots_global != NULL) &&
              r_llvm_frozen_field(emitter, "RRuntimeDict", "slots", offset, &at) &&
              r_llvm_image_pointer(emitter, image, at, slots_global) &&
              r_llvm_frozen_field(emitter, "RRuntimeDict", "entries", offset, &at) &&
              r_llvm_image_pointer(emitter, image, at, entries_global) &&
              r_llvm_frozen_field(emitter, "RRuntimeDict", "length", offset, &at) &&
              r_llvm_image_integer(emitter, image, at, 8U, length) &&
              r_llvm_frozen_field(emitter, "RRuntimeDict", "capacity", offset, &at) &&
              r_llvm_image_integer(emitter, image, at, 8U, capacity) &&
              r_llvm_frozen_field(emitter, "RRuntimeDict", "entry_capacity", offset, &at) &&
              r_llvm_image_integer(emitter, image, at, 8U, length) &&
              r_llvm_frozen_field(emitter, "RRuntimeDict", "last_order", offset, &at) &&
              r_llvm_image_integer(emitter, image, at, 8U, length - 1U) &&
              r_llvm_frozen_field(emitter, "RRuntimeDict", "key_offset", offset, &at) &&
              r_llvm_image_integer(emitter, image, at, 8U, key_offset) &&
              r_llvm_frozen_field(emitter, "RRuntimeDict", "value_offset", offset, &at) &&
              r_llvm_image_integer(emitter, image, at, 8U, value_offset) &&
              r_llvm_frozen_field(emitter, "RRuntimeDict", "slot_size", offset, &at) &&
              r_llvm_image_integer(emitter, image, at, 8U, slot_size) &&
              r_llvm_frozen_field(emitter, "RRuntimeDict", "slot_alignment", offset, &at) &&
              r_llvm_image_integer(
                  emitter, image, at, 8U, entry_align > slot_align ? entry_align : slot_align) &&
              r_llvm_frozen_field(emitter, "RRuntimeDict", "entry_size", offset, &at) &&
              r_llvm_image_integer(emitter, image, at, 8U, entry_size) &&
              r_llvm_frozen_field(emitter, "RRuntimeDict", "entry_alignment", offset, &at) &&
              r_llvm_image_integer(emitter, image, at, 8U, entry_align);

cleanup:
    r_llvm_image_free(emitter, &entries);
    r_llvm_image_free(emitter, &slots);
    return success;
}

static bool r_llvm_static_frozen(RLlvmEmitter *emitter,
                                 RLlvmStaticImage *image,
                                 uint32_t offset,
                                 RHirNodeId id,
                                 const RHirNode *node,
                                 uint32_t depth) {
    switch (r_llvm_frozen_kind(emitter, node)) {
    case R_LLVM_FROZEN_ARRAY:
        return r_llvm_frozen_array(emitter, image, offset, id, node, depth);
    case R_LLVM_FROZEN_STRING:
        return r_llvm_frozen_string(emitter, image, offset, id, node);
    case R_LLVM_FROZEN_LIST:
        return r_llvm_frozen_list(emitter, image, offset, id, node, depth);
    case R_LLVM_FROZEN_DICT:
        return r_llvm_frozen_dict(emitter, image, offset, id, node, depth);
    default:
        return r_llvm_unsupported(emitter, "a frozen owner of this type in a static initializer");
    }
}

static bool r_llvm_static_fill(RLlvmEmitter *emitter,
                               RLlvmStaticImage *image,
                               uint32_t offset,
                               RHirNodeId id,
                               uint32_t depth) {
    const RHirNode *node = r_llvm_hir(emitter, id);
    uint32_t size = 0U;
    uint32_t align = 0U;
    uint32_t index;

    if ((node == NULL) || (depth > 256U) || !r_llvm_layout(emitter, node->type, &size, &align)) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    switch (node->kind) {
    case R_HIR_LITERAL: {
        const RSemanticTypeKind kind = r_llvm_value_kind(emitter, node->type);
        if (node->operation == R_TOKEN_KW_NULL) {
            return true;
        }
        if ((kind == R_SEMANTIC_TYPE_F32) || (kind == R_SEMANTIC_TYPE_F64) ||
            (r_llvm_scalar_type(emitter, node->type) != NULL)) {
            return r_llvm_image_integer(emitter, image, offset, size, node->integer_value);
        }
        return r_llvm_unsupported(emitter, "a static literal of this type");
    }
    case R_HIR_ENUM_CONSTANT:
        return r_llvm_image_integer(emitter, image, offset, size, node->integer_value);
    case R_HIR_FUNCTION_ADDRESS:
        /* R-TYPE-0054 (L28): a function value is the number of its target. */
        return r_llvm_image_integer(emitter, image, offset, 4U, node->symbol);
    case R_HIR_DEFAULT_VALUE:
        return true;
    case R_HIR_TYPE_QUERY: {
        uint32_t queried_size = 0U;
        uint32_t queried_align = 0U;
        return r_llvm_layout(emitter, node->auxiliary_type, &queried_size, &queried_align) &&
               r_llvm_image_integer(emitter,
                                    image,
                                    offset,
                                    size,
                                    node->operation == R_TOKEN_KW_SIZEOF ? queried_size
                                                                         : queried_align);
    }
    case R_HIR_STRING_LITERAL:
        return r_llvm_image_string(emitter, image, offset, node);
    case R_HIR_FIELD_INIT:
        return r_llvm_static_fill(
            emitter, image, offset, r_llvm_hir_child(emitter, node, 0U), depth + 1U);
    case R_HIR_AGGREGATE_INIT:
        for (index = 0U; index < node->child_count; ++index) {
            const RHirNodeId child = r_llvm_hir_child(emitter, node, index);
            const RHirNode *field = r_llvm_hir(emitter, child);
            uint32_t field_offset = 0U;
            if ((field == NULL) ||
                !r_llvm_field_offset(emitter, field->aggregate_member, &field_offset) ||
                !r_llvm_static_fill(emitter, image, offset + field_offset, child, depth + 1U)) {
                return false;
            }
        }
        return true;
    case R_HIR_ARRAY_INIT: {
        const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, node->type));
        uint32_t element_size = 0U;
        uint32_t element_align = 0U;
        if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_FIXED_ARRAY)) {
            return r_llvm_static_frozen(emitter, image, offset, id, node, depth);
        }
        if (!r_llvm_layout(emitter, type->base, &element_size, &element_align)) {
            return false;
        }
        for (index = 0U; index < node->child_count; ++index) {
            if (!r_llvm_static_fill(emitter,
                                    image,
                                    offset + index * element_size,
                                    r_llvm_hir_child(emitter, node, index),
                                    depth + 1U)) {
                return false;
            }
        }
        return true;
    }
    case R_HIR_CAST: {
        const RHirNode *operand = r_llvm_hir(emitter, r_llvm_hir_child(emitter, node, 0U));
        if (operand == NULL) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        /* A string literal as a str view, or a cast that keeps the representation. */
        if ((operand->kind == R_HIR_STRING_LITERAL) &&
            (r_llvm_value_kind(emitter, node->type) == R_SEMANTIC_TYPE_STR)) {
            return r_llvm_image_string(emitter, image, offset, operand);
        }
        if (r_llvm_value_type(emitter, operand->type) == r_llvm_value_type(emitter, node->type)) {
            return r_llvm_static_fill(
                emitter, image, offset, r_llvm_hir_child(emitter, node, 0U), depth + 1U);
        }
        {
            uint64_t value = 0U;
            return r_llvm_static_scalar(emitter, id, &value, depth) &&
                   r_llvm_image_integer(emitter, image, offset, size, value);
        }
    }
    case R_HIR_UNARY:
    case R_HIR_BINARY: {
        uint64_t value = 0U;
        return r_llvm_static_scalar(emitter, id, &value, depth) &&
               r_llvm_image_integer(emitter, image, offset, size, value);
    }
    case R_HIR_CONDITIONAL: {
        RLlvmStaticNumber condition;
        if (r_llvm_scalar_type(emitter, node->type) != NULL) {
            uint64_t value = 0U;
            return r_llvm_static_scalar(emitter, id, &value, depth) &&
                   r_llvm_image_integer(emitter, image, offset, size, value);
        }
        if (!r_llvm_static_number(
                emitter, r_llvm_hir_child(emitter, node, 0U), &condition, depth + 1U)) {
            return false;
        }
        return r_llvm_static_fill(
            emitter,
            image,
            offset,
            r_llvm_hir_child(emitter,
                             node,
                             ((condition.class_ == R_LLVM_STATIC_FLOAT) ? (condition.real != 0.0)
                                                                        : (condition.integer != 0U))
                                 ? 1U
                                 : 2U),
            depth + 1U);
    }
    case R_HIR_VARIANT: {
        uint32_t payload = 0U;
        if ((node->child_count > 1U) || !r_llvm_payload_offset(emitter, node->type, &payload) ||
            !r_llvm_image_integer(emitter, image, offset, 4U, node->integer_value)) {
            return (node->child_count > 1U) ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR)
                                            : false;
        }
        return (node->child_count == 0U) || r_llvm_static_fill(emitter,
                                                               image,
                                                               offset + payload,
                                                               r_llvm_hir_child(emitter, node, 0U),
                                                               depth + 1U);
    }
    default: {
        const char *kind = r_hir_kind_name(node->kind);
        return r_llvm_unsupported_detail(emitter, "this static initializer", kind, strlen(kind));
    }
    }
}

/* The initializer of a static: a module object's own node or integer value, a static local's
   first child in its LOCAL node. */
static bool
r_llvm_static_initializer(RLlvmEmitter *emitter, RSymbolId id, RHirNodeId *node, bool *integer) {
    const RSemanticSymbol *symbol = &emitter->frontend->semantic_symbols[(size_t)id - 1U];
    size_t index;

    *node = R_HIR_NODE_ID_INVALID;
    *integer = false;
    if (symbol->kind == R_SEMANTIC_SYMBOL_MODULE_OBJECT) {
        *node = symbol->hir_node;
        *integer = symbol->hir_node == R_HIR_NODE_ID_INVALID;
        return true;
    }
    for (index = 0U; index < emitter->frontend->hir_node_count; ++index) {
        const RHirNode *candidate = &emitter->frontend->hir_nodes[index];
        if ((candidate->kind == R_HIR_LOCAL) && (candidate->symbol == id)) {
            *node = r_llvm_hir_child(emitter, candidate, 0U);
            return *node != R_HIR_NODE_ID_INVALID;
        }
    }
    return false;
}

LLVMValueRef r_llvm_static_global(RLlvmEmitter *emitter, RSymbolId id) {
    const RSemanticSymbol *symbol =
        (id == R_SYMBOL_ID_INVALID) || ((size_t)id > emitter->frontend->semantic_symbol_count)
            ? NULL
            : &emitter->frontend->semantic_symbols[(size_t)id - 1U];
    RLlvmStaticImage image;
    RHirNodeId initializer = R_HIR_NODE_ID_INVALID;
    bool integer = false;
    uint32_t size = 0U;
    uint32_t align = 0U;
    LLVMValueRef global;
    char name[64];

    if (symbol == NULL) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    if (emitter->statics[id] != NULL) {
        return emitter->statics[id];
    }
    if (symbol->is_import || symbol->poisoned) {
        (void)r_llvm_unsupported(emitter, "an imported C object");
        return NULL;
    }
    if (!r_llvm_layout(emitter, symbol->type, &size, &align) ||
        !r_llvm_static_initializer(emitter, id, &initializer, &integer)) {
        if (emitter->status == R_FRONTEND_OK) {
            (void)r_llvm_unsupported(emitter, "a static object without an initializer");
        }
        return NULL;
    }
    if (!r_llvm_image_create(emitter, &image, size)) {
        return NULL;
    }
    if (integer ? !r_llvm_image_integer(
                      emitter, &image, 0U, image.size > 8U ? 8U : image.size, symbol->integer_value)
                : !r_llvm_static_fill(emitter, &image, 0U, initializer, 0U)) {
        r_llvm_image_free(emitter, &image);
        return NULL;
    }
    (void)snprintf(name, sizeof(name), "r_static.%" PRIu32, r_llvm_symbol_key(emitter, id));
    global = r_llvm_image_global(emitter, &image, align, name);
    r_llvm_image_free(emitter, &image);
    if (global == NULL) {
        return NULL;
    }
    if (symbol->is_thread_local) {
        LLVMSetThreadLocal(global, 1);
    }
    emitter->statics[id] = global;
    return global;
}

/* A static object holding a frozen owner is never destroyed: its storage is the image. */
static bool r_llvm_static_frozen_owner(RLlvmEmitter *emitter, RSymbolId id) {
    RHirNodeId initializer = R_HIR_NODE_ID_INVALID;
    bool integer = false;

    return r_llvm_static_initializer(emitter, id, &initializer, &integer) && !integer &&
           r_llvm_contains_frozen(emitter, initializer, 0U);
}

/* r_c17_compare_static_objects: module name, source name, position, symbol. */
static int
r_llvm_compare_statics(const RLlvmEmitter *emitter, RSymbolId left_id, RSymbolId right_id) {
    const RSemanticSymbol *left = &emitter->frontend->semantic_symbols[(size_t)left_id - 1U];
    const RSemanticSymbol *right = &emitter->frontend->semantic_symbols[(size_t)right_id - 1U];
    const RSource *left_module = r_get_source_const(emitter->frontend, left->module_source);
    const RSource *right_module = r_get_source_const(emitter->frontend, right->module_source);
    const RSource *left_source;
    const RSource *right_source;
    int comparison;

    if ((left_module == NULL) || (right_module == NULL) || (left_module->module_name == NULL) ||
        (right_module->module_name == NULL)) {
        return left_id < right_id ? -1 : (left_id != right_id ? 1 : 0);
    }
    comparison = strcmp(left_module->module_name, right_module->module_name);
    if (comparison != 0) {
        return comparison;
    }
    left_source = r_get_source_const(emitter->frontend, left->name_span.source);
    right_source = r_get_source_const(emitter->frontend, right->name_span.source);
    if ((left_source != NULL) && (right_source != NULL) && (left_source->display_name != NULL) &&
        (right_source->display_name != NULL)) {
        comparison = strcmp(left_source->display_name, right_source->display_name);
        if (comparison != 0) {
            return comparison;
        }
    } else if (left->name_span.source != right->name_span.source) {
        return left->name_span.source < right->name_span.source ? -1 : 1;
    }
    if (left->name_span.start != right->name_span.start) {
        return left->name_span.start < right->name_span.start ? -1 : 1;
    }
    return left_id < right_id ? -1 : (left_id != right_id ? 1 : 0);
}

/* r_static_drop_all of the C17 emitter: the static objects the program uses that own resources,
   in the reverse of their order, each followed by r_runtime_hosted_after_object_drop. */
bool r_llvm_emit_static_drops(RLlvmEmitter *emitter) {
    RSymbolId previous = R_SYMBOL_ID_INVALID;

    /* The thread-local objects of the main thread go first (r_c17_emit_hosted_static_cleanup). */
    if ((emitter->thread_local_drop_all != NULL) &&
        (LLVMBuildCall2(emitter->builder,
                        LLVMGlobalGetValueType(emitter->thread_local_drop_all),
                        emitter->thread_local_drop_all,
                        NULL,
                        0U,
                        "") == NULL)) {
        return false;
    }

    for (;;) {
        RSymbolId next = R_SYMBOL_ID_INVALID;
        size_t index;
        for (index = 1U; index <= emitter->frontend->semantic_symbol_count; ++index) {
            const RSymbolId candidate = (RSymbolId)index;
            const RSemanticSymbol *symbol = &emitter->frontend->semantic_symbols[index - 1U];
            if ((emitter->statics[candidate] == NULL) || symbol->is_thread_local ||
                symbol->is_import || !r_llvm_type_requires_drop(emitter, symbol->type) ||
                r_llvm_static_frozen_owner(emitter, candidate) ||
                ((previous != R_SYMBOL_ID_INVALID) &&
                 (r_llvm_compare_statics(emitter, candidate, previous) >= 0)) ||
                ((next != R_SYMBOL_ID_INVALID) &&
                 (r_llvm_compare_statics(emitter, candidate, next) <= 0))) {
                continue;
            }
            next = candidate;
        }
        if (next == R_SYMBOL_ID_INVALID) {
            return true;
        }
        if (!r_llvm_call_drop(emitter,
                              emitter->frontend->semantic_symbols[(size_t)next - 1U].type,
                              emitter->statics[next]) ||
            ((emitter->frontend->profile != R_FRONTEND_PROFILE_FREESTANDING) &&
             (r_llvm_call_runtime(emitter, "r_runtime_hosted_after_object_drop", NULL, 0U, NULL) ==
              NULL))) {
            return false;
        }
        previous = next;
    }
}

/* ---- Thread-local objects that own resources (r_c17_emit_thread_local_object_support) ----

   Each such object has a state byte per thread (0 unused, 1 live, 2 dropping, 3 dropped), a
   drop and an accessor that every use of the object calls: at the first use on a thread it
   pushes the drop on the thread's drop stack, which r_thread_local_drop_all empties at the exit
   of the thread (r_runtime_thread_local_cleanup_install) and of the program; a use outside the
   live state is R_RUNTIME_PANIC_THREAD_LOCAL_LIFETIME. */

/* A `bytes` value (array<u8>) inside a static object: its `{}` image has no allocator or element
   type yet, which the program sets before first use (r_c17_emit_static_bytes_initializers). */
static bool r_llvm_static_bytes_inside(RLlvmEmitter *emitter, RTypeId id, uint32_t depth) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    const RSemanticAggregate *aggregate;
    uint32_t index;

    if ((type == NULL) || (depth > emitter->frontend->options.limits.max_nesting)) {
        return false;
    }
    if ((type->kind == R_SEMANTIC_TYPE_ARRAY) && (type->flags == R_SEMANTIC_TYPE_FLAG_NONE) &&
        (type->second == R_TYPE_ID_INVALID) &&
        (r_llvm_value_kind(emitter, type->base) == R_SEMANTIC_TYPE_U8)) {
        return true;
    }
    if (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) {
        return r_llvm_static_bytes_inside(emitter, type->base, depth + 1U);
    }
    aggregate = type->kind == R_SEMANTIC_TYPE_STRUCT
                    ? r_llvm_aggregate(emitter, r_llvm_value_type(emitter, id))
                    : NULL;
    for (index = 0U; (aggregate != NULL) && (index < aggregate->field_count); ++index) {
        if (r_llvm_static_bytes_inside(
                emitter,
                emitter->frontend->semantic_fields[(size_t)aggregate->first_field + index].type,
                depth + 1U)) {
            return true;
        }
    }
    return false;
}

/* r_runtime_array_initialize of every `bytes` value inside the object at `address`. */
static bool r_llvm_static_bytes_initialize(RLlvmEmitter *emitter,
                                           RTypeId id,
                                           LLVMValueRef address,
                                           uint32_t depth) {
    const RSemanticType *type = r_llvm_type(emitter, r_llvm_value_type(emitter, id));
    uint32_t index;

    if (!r_llvm_static_bytes_inside(emitter, id, depth)) {
        return true;
    }
    if (type->kind == R_SEMANTIC_TYPE_ARRAY) {
        LLVMValueRef arguments[3];
        arguments[0] = address;
        arguments[1] = r_llvm_call_runtime(emitter, "r_runtime_hosted_allocator", NULL, 0U, NULL);
        arguments[2] = r_llvm_type_info(emitter, type->base);
        return (arguments[1] != NULL) && (arguments[2] != NULL) &&
               (r_llvm_call_runtime(emitter, "r_runtime_array_initialize", arguments, 3U, NULL) !=
                NULL);
    }
    if (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) {
        uint32_t size = 0U;
        uint32_t align = 0U;
        if (!r_llvm_layout(emitter, type->base, &size, &align)) {
            return false;
        }
        for (index = 0U; index < (uint32_t)type->length; ++index) {
            if (!r_llvm_static_bytes_initialize(
                    emitter,
                    type->base,
                    r_llvm_byte_offset(emitter, address, (uint64_t)index * size),
                    depth + 1U)) {
                return false;
            }
        }
        return true;
    }
    {
        const RSemanticAggregate *aggregate =
            r_llvm_aggregate(emitter, r_llvm_value_type(emitter, id));
        for (index = 0U; (aggregate != NULL) && (index < aggregate->field_count); ++index) {
            const RSemanticField *field =
                &emitter->frontend->semantic_fields[(size_t)aggregate->first_field + index];
            uint32_t offset = 0U;
            if (!r_llvm_static_bytes_inside(emitter, field->type, depth + 1U)) {
                continue;
            }
            if (!r_llvm_field_offset(emitter, aggregate->first_field + index + 1U, &offset) ||
                !r_llvm_static_bytes_initialize(emitter,
                                                field->type,
                                                r_llvm_byte_offset(emitter, address, offset),
                                                depth + 1U)) {
                return false;
            }
        }
    }
    return true;
}

/* The `bytes` values inside the static objects the program uses, before its entry runs; a
   thread-local object initializes its own on first use. */
bool r_llvm_initialize_static_bytes(RLlvmEmitter *emitter) {
    size_t id;

    for (id = 1U; id <= emitter->frontend->semantic_symbol_count; ++id) {
        const RSemanticSymbol *symbol = &emitter->frontend->semantic_symbols[id - 1U];
        if ((emitter->statics[id] == NULL) || symbol->is_thread_local || symbol->is_import) {
            continue;
        }
        if (!r_llvm_static_bytes_initialize(emitter, symbol->type, emitter->statics[id], 0U)) {
            return false;
        }
    }
    return true;
}

static bool r_llvm_thread_local_owning(RLlvmEmitter *emitter, const RSemanticSymbol *symbol) {
    return symbol->is_thread_local && !symbol->is_import &&
           r_llvm_type_requires_drop(emitter, symbol->type);
}

static LLVMValueRef
r_llvm_thread_local_global(RLlvmEmitter *emitter, const char *name, LLVMTypeRef type) {
    LLVMValueRef global = LLVMAddGlobal(emitter->module, type, name);
    LLVMSetInitializer(global, LLVMConstNull(type));
    LLVMSetLinkage(global, LLVMInternalLinkage);
    LLVMSetThreadLocal(global, 1);
    return global;
}

/* The place of a static object: its global, or for an owning thread-local object the result of
   its accessor. */
LLVMValueRef r_llvm_static_place(RLlvmEmitter *emitter, RSymbolId id) {
    const RSemanticSymbol *symbol = &emitter->frontend->semantic_symbols[(size_t)id - 1U];
    LLVMValueRef global = r_llvm_static_global(emitter, id);

    if ((global == NULL) || !r_llvm_thread_local_owning(emitter, symbol)) {
        return global;
    }
    if (emitter->thread_local_access[id] == NULL) {
        char name[64];
        if (emitter->thread_local_stack == NULL) {
            size_t count = 0U;
            size_t index;
            for (index = 0U; index < emitter->frontend->semantic_symbol_count; ++index) {
                count +=
                    r_llvm_thread_local_owning(emitter, &emitter->frontend->semantic_symbols[index])
                        ? 1U
                        : 0U;
            }
            emitter->thread_local_capacity = (uint32_t)count;
            emitter->thread_local_stack = r_llvm_thread_local_global(
                emitter,
                "r_thread_local_drop_stack",
                LLVMArrayType2(r_llvm_pointer(emitter), count == 0U ? 1U : count));
            emitter->thread_local_count = r_llvm_thread_local_global(
                emitter, "r_thread_local_drop_count", r_llvm_int(emitter, 64U));
        }
        (void)snprintf(
            name, sizeof(name), "r_thread_local_state.%" PRIu32, r_llvm_symbol_key(emitter, id));
        emitter->thread_local_state[id] =
            r_llvm_thread_local_global(emitter, name, r_llvm_int(emitter, 8U));
        (void)snprintf(
            name, sizeof(name), "r_thread_local_drop.%" PRIu32, r_llvm_symbol_key(emitter, id));
        emitter->thread_local_drop[id] =
            LLVMAddFunction(emitter->module,
                            name,
                            LLVMFunctionType(LLVMVoidTypeInContext(emitter->context), NULL, 0U, 0));
        LLVMSetLinkage(emitter->thread_local_drop[id], LLVMInternalLinkage);
        (void)snprintf(
            name, sizeof(name), "r_thread_local_access.%" PRIu32, r_llvm_symbol_key(emitter, id));
        emitter->thread_local_access[id] = LLVMAddFunction(
            emitter->module, name, LLVMFunctionType(r_llvm_pointer(emitter), NULL, 0U, 0));
        LLVMSetLinkage(emitter->thread_local_access[id], LLVMInternalLinkage);
    }
    /* The accessor itself: a place calls it where it is used, since the first use of an
       owning thread-local object fixes the order its drop runs in (r_llvm_place_root). */
    return emitter->thread_local_access[id];
}

/* r_runtime_panic(R_RUNTIME_PANIC_THREAD_LOCAL_LIFETIME) unless the state is live. */
static bool r_llvm_thread_local_live(RLlvmEmitter *emitter, LLVMValueRef state) {
    LLVMBasicBlockRef dead = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMBasicBlockRef live = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    LLVMValueRef arguments[2];
    int64_t category = 0;

    if (!r_llvm_runtime_constant(emitter, "R_RUNTIME_PANIC_THREAD_LOCAL_LIFETIME", &category)) {
        return false;
    }
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder,
                      LLVMIntEQ,
                      LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), state, ""),
                      LLVMConstInt(r_llvm_int(emitter, 8U), 1U, 0),
                      ""),
        live,
        dead);
    LLVMPositionBuilderAtEnd(emitter->builder, dead);
    arguments[0] = r_llvm_u32(emitter, (uint64_t)category);
    arguments[1] = r_llvm_span(emitter, (RSourceSpan){0});
    if ((arguments[1] == NULL) ||
        (r_llvm_call_runtime(emitter, "r_runtime_panic", arguments, 2U, NULL) == NULL)) {
        return false;
    }
    (void)LLVMBuildUnreachable(emitter->builder);
    LLVMPositionBuilderAtEnd(emitter->builder, live);
    return true;
}

static void r_llvm_thread_local_begin(RLlvmEmitter *emitter, LLVMValueRef function) {
    emitter->function = function;
    emitter->mir = NULL;
    emitter->frame = NULL;
    LLVMPositionBuilderAtEnd(emitter->builder,
                             LLVMAppendBasicBlockInContext(emitter->context, function, "entry"));
}

/* The accessors and drops of the owning thread-local objects the program used, and
   r_thread_local_drop_all. */
bool r_llvm_emit_thread_locals(RLlvmEmitter *emitter) {
    size_t id;
    LLVMValueRef count;
    LLVMBasicBlockRef test;
    LLVMBasicBlockRef body;
    LLVMBasicBlockRef done;

    if (emitter->thread_local_stack == NULL) {
        return true;
    }
    for (id = 1U; id <= emitter->frontend->semantic_symbol_count; ++id) {
        const RSemanticSymbol *symbol = &emitter->frontend->semantic_symbols[id - 1U];
        LLVMValueRef state = emitter->thread_local_state[id];
        LLVMBasicBlockRef fresh;
        LLVMBasicBlockRef used;
        if (emitter->thread_local_access[id] == NULL) {
            continue;
        }
        /* The drop: live → dropping → dropped. */
        r_llvm_thread_local_begin(emitter, emitter->thread_local_drop[id]);
        if (!r_llvm_thread_local_live(emitter, state)) {
            return false;
        }
        (void)LLVMBuildStore(emitter->builder, LLVMConstInt(r_llvm_int(emitter, 8U), 2U, 0), state);
        if (!r_llvm_call_drop(emitter, symbol->type, emitter->statics[id])) {
            return false;
        }
        (void)LLVMBuildStore(emitter->builder, LLVMConstInt(r_llvm_int(emitter, 8U), 3U, 0), state);
        (void)LLVMBuildRetVoid(emitter->builder);
        /* The accessor: the first use makes the object live and pushes its drop. */
        r_llvm_thread_local_begin(emitter, emitter->thread_local_access[id]);
        fresh = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        used = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(emitter->builder,
                          LLVMIntEQ,
                          LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 8U), state, ""),
                          LLVMConstInt(r_llvm_int(emitter, 8U), 0U, 0),
                          ""),
            fresh,
            used);
        LLVMPositionBuilderAtEnd(emitter->builder, fresh);
        if (!r_llvm_static_bytes_initialize(emitter,
                                            emitter->frontend->semantic_symbols[id - 1U].type,
                                            emitter->statics[id],
                                            0U)) {
            return false;
        }
        (void)LLVMBuildStore(emitter->builder, LLVMConstInt(r_llvm_int(emitter, 8U), 1U, 0), state);
        count = LLVMBuildLoad2(
            emitter->builder, r_llvm_int(emitter, 64U), emitter->thread_local_count, "");
        (void)LLVMBuildStore(emitter->builder,
                             emitter->thread_local_drop[id],
                             LLVMBuildGEP2(emitter->builder,
                                           r_llvm_pointer(emitter),
                                           emitter->thread_local_stack,
                                           &count,
                                           1U,
                                           ""));
        (void)LLVMBuildStore(emitter->builder,
                             LLVMBuildAdd(emitter->builder, count, r_llvm_u64(emitter, 1U), ""),
                             emitter->thread_local_count);
        (void)LLVMBuildRet(emitter->builder, emitter->statics[id]);
        LLVMPositionBuilderAtEnd(emitter->builder, used);
        if (!r_llvm_thread_local_live(emitter, state)) {
            return false;
        }
        (void)LLVMBuildRet(emitter->builder, emitter->statics[id]);
    }
    /* r_thread_local_drop_all: the drops of this thread, last pushed first. */
    emitter->thread_local_drop_all =
        LLVMAddFunction(emitter->module,
                        "r_thread_local_drop_all",
                        LLVMFunctionType(LLVMVoidTypeInContext(emitter->context), NULL, 0U, 0));
    LLVMSetLinkage(emitter->thread_local_drop_all, LLVMInternalLinkage);
    r_llvm_thread_local_begin(emitter, emitter->thread_local_drop_all);
    test = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    body = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    (void)LLVMBuildBr(emitter->builder, test);
    LLVMPositionBuilderAtEnd(emitter->builder, test);
    count =
        LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), emitter->thread_local_count, "");
    (void)LLVMBuildCondBr(
        emitter->builder,
        LLVMBuildICmp(emitter->builder, LLVMIntEQ, count, r_llvm_u64(emitter, 0U), ""),
        done,
        body);
    LLVMPositionBuilderAtEnd(emitter->builder, body);
    {
        LLVMValueRef index = LLVMBuildSub(emitter->builder, count, r_llvm_u64(emitter, 1U), "");
        LLVMValueRef slot = LLVMBuildGEP2(
            emitter->builder, r_llvm_pointer(emitter), emitter->thread_local_stack, &index, 1U, "");
        LLVMValueRef drop = LLVMBuildLoad2(emitter->builder, r_llvm_pointer(emitter), slot, "");
        (void)LLVMBuildStore(emitter->builder, index, emitter->thread_local_count);
        (void)LLVMBuildStore(emitter->builder, LLVMConstNull(r_llvm_pointer(emitter)), slot);
        (void)LLVMBuildCall2(emitter->builder,
                             LLVMFunctionType(LLVMVoidTypeInContext(emitter->context), NULL, 0U, 0),
                             drop,
                             NULL,
                             0U,
                             "");
        if ((emitter->frontend->profile != R_FRONTEND_PROFILE_FREESTANDING) &&
            (r_llvm_call_runtime(emitter, "r_runtime_hosted_after_object_drop", NULL, 0U, NULL) ==
             NULL)) {
            return false;
        }
    }
    (void)LLVMBuildBr(emitter->builder, test);
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    (void)LLVMBuildRetVoid(emitter->builder);
    return true;
}
