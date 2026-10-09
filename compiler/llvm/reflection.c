#include "standard_internal.h"

#include <string.h>

/* Reflection of program types (R-REFL, L42): the C17 emitter calls one helper per selection and
   type (r_reflection_<selection>_a<aggregate>); here the same `switch` is built where the
   operation is used. A selection reads the enumerator, the declaration index or the active tag
   and produces a program string, an ordinal, an `o::some` of an enumerator or of the arguments of
   an attribute; any other operand leaves the empty string, zero or none. */

/* The aggregate of a type of the program, or the protected declaration a named standard type
   has in the library (r_c17_aggregate_for_type). */
static const RSemanticAggregate *r_llvm_reflection_aggregate(RLlvmEmitter *emitter, RTypeId id) {
    const RTypeId value = r_llvm_value_type(emitter, id);
    const RSemanticType *type = r_llvm_type(emitter, value);
    size_t index;

    if ((type == NULL) || (type->kind != R_SEMANTIC_TYPE_STANDARD)) {
        return r_llvm_aggregate(emitter, id);
    }
    for (index = 0U; index < emitter->frontend->semantic_aggregate_count; ++index) {
        const RSemanticAggregate *candidate = &emitter->frontend->semantic_aggregates[index];
        if ((candidate->type == value) && (candidate->module_source == R_SOURCE_ID_INVALID) &&
            candidate->is_protected && candidate->complete && !candidate->poisoned) {
            return candidate;
        }
    }
    return NULL;
}

static const RSemanticAggregate *r_llvm_reflection_subject(RLlvmEmitter *emitter,
                                                           RStandardCallOperation operation,
                                                           RTypeId subject) {
    const RSemanticAggregate *aggregate = r_llvm_reflection_aggregate(emitter, subject);

    if (aggregate == NULL) {
        return NULL;
    }
    switch (operation) {
    case R_STANDARD_CALL_CORE_TYPE_ATTRIBUTE:
        return aggregate;
    case R_STANDARD_CALL_CORE_FIELD_ATTRIBUTE:
    case R_STANDARD_CALL_CORE_FIELD_NAME_AT:
        return aggregate->kind == R_SEMANTIC_AGGREGATE_STRUCT ? aggregate : NULL;
    case R_STANDARD_CALL_CORE_VARIANT_ATTRIBUTE:
        return (aggregate->kind == R_SEMANTIC_AGGREGATE_ENUM) && !aggregate->is_tagged ? aggregate
                                                                                       : NULL;
    default:
        break;
    }
    if ((aggregate->kind != R_SEMANTIC_AGGREGATE_ENUM) || (aggregate->variant_count == 0U) ||
        ((operation == R_STANDARD_CALL_CORE_VARIANT_NAME) != aggregate->is_tagged)) {
        return NULL;
    }
    return aggregate;
}

static const RSemanticVariant *r_llvm_reflection_variant(const RLlvmEmitter *emitter,
                                                         const RSemanticAggregate *aggregate,
                                                         uint32_t index) {
    const size_t global = (size_t)aggregate->first_variant + (size_t)index;

    return global < emitter->frontend->semantic_variant_count
               ? &emitter->frontend->semantic_variants[global]
               : NULL;
}

/* A str {data, length} of a program string in memory; intern 0 is the empty string. */
static bool r_llvm_reflection_text(RLlvmEmitter *emitter,
                                   LLVMValueRef target,
                                   uint32_t intern_id,
                                   size_t length) {
    LLVMValueRef data;

    if (length == 0U) {
        /* The empty string still has an address that is not null. */
        data = LLVMBuildGlobalStringPtr(emitter->builder, "", "r_empty");
    } else {
        data = r_llvm_program_string(emitter, intern_id);
        if (data == NULL) {
            return false;
        }
    }
    (void)LLVMBuildStore(emitter->builder, data, target);
    (void)LLVMBuildStore(
        emitter->builder, r_llvm_u64(emitter, length), r_llvm_byte_offset(emitter, target, 8U));
    return true;
}

static size_t r_llvm_reflection_length(const RLlvmEmitter *emitter, uint32_t intern_id) {
    return (intern_id == 0U) || ((size_t)intern_id > emitter->frontend->intern_count)
               ? 0U
               : emitter->frontend->intern_entries[(size_t)intern_id - 1U].length;
}

/* An enumerator of the subject as a constant of its representation. */
static LLVMValueRef
r_llvm_reflection_enumerator(RLlvmEmitter *emitter, RTypeId subject, uint64_t value) {
    LLVMTypeRef type = r_llvm_scalar_type(emitter, subject);

    if (type == NULL) {
        (void)r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        return NULL;
    }
    return LLVMConstInt(type, value, 0);
}

/* An attribute argument (R-AGG-0013) stored into its field of the payload. */
static bool r_llvm_reflection_argument(RLlvmEmitter *emitter,
                                       const RSemanticAttributeValue *value,
                                       LLVMValueRef target) {
    const RSemanticTypeKind kind = r_llvm_value_kind(emitter, value->type);
    LLVMTypeRef type;
    LLVMValueRef constant;

    if (kind == R_SEMANTIC_TYPE_STR) {
        return r_llvm_reflection_text(emitter, target, value->text_intern_id, value->text_length);
    }
    type = r_llvm_scalar_type(emitter, value->type);
    if (type == NULL) {
        return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    if (kind == R_SEMANTIC_TYPE_F32) {
        constant = LLVMConstBitCast(LLVMConstInt(r_llvm_int(emitter, 32U), value->bits, 0), type);
    } else if (kind == R_SEMANTIC_TYPE_F64) {
        constant = LLVMConstBitCast(LLVMConstInt(r_llvm_int(emitter, 64U), value->bits, 0), type);
    } else if (kind == R_SEMANTIC_TYPE_BOOL) {
        constant = LLVMConstInt(type, value->bits != 0U ? 1U : 0U, 0);
    } else {
        /* Integers, chars, C integers and enumerators keep the low bits of their width. */
        constant = LLVMConstInt(type, value->bits, 0);
    }
    r_llvm_store_scalar(emitter, value->type, constant, target);
    return true;
}

/* `o::some` of the arguments of one attribute use. */
static bool r_llvm_reflection_some_attribute(RLlvmEmitter *emitter,
                                             LLVMValueRef result,
                                             uint32_t payload,
                                             const RSemanticAggregate *attribute,
                                             const RSemanticAttributeUse *use) {
    uint32_t index;

    (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
    for (index = 0U; index < attribute->field_count; ++index) {
        const RSemanticAttributeValue *value =
            &emitter->frontend->attribute_values[(size_t)use->first_value + index];
        uint32_t offset = 0U;
        if (!r_llvm_field_offset(emitter, attribute->first_field + index + 1U, &offset) ||
            !r_llvm_reflection_argument(
                emitter, value, r_llvm_byte_offset(emitter, result, payload + offset))) {
            return false;
        }
    }
    return true;
}

static const RSemanticAttributeUse *r_llvm_reflection_use(RLlvmEmitter *emitter,
                                                          RStandardCallOperation operation,
                                                          RTypeId subject,
                                                          const RSemanticAggregate *attribute,
                                                          uint32_t member) {
    const uint32_t target =
        operation == R_STANDARD_CALL_CORE_TYPE_ATTRIBUTE    ? R_ATTRIBUTE_TARGET_TYPE
        : operation == R_STANDARD_CALL_CORE_FIELD_ATTRIBUTE ? R_ATTRIBUTE_TARGET_FIELD
                                                            : R_ATTRIBUTE_TARGET_VARIANT;

    return r_semantic_attribute_use(emitter->frontend, attribute->type, target, subject, member);
}

/* The index of the variant named by a str, or the count when none is (enum_from_name). */
static LLVMValueRef r_llvm_reflection_find(RLlvmEmitter *emitter,
                                           const RSemanticAggregate *aggregate,
                                           LLVMValueRef name) {
    LLVMTypeRef memcmp_parameters[3];
    LLVMTypeRef memcmp_type;
    LLVMValueRef memcmp_function = LLVMGetNamedFunction(emitter->module, "memcmp");
    LLVMValueRef data = LLVMBuildLoad2(emitter->builder, r_llvm_pointer(emitter), name, "");
    LLVMValueRef length = LLVMBuildLoad2(
        emitter->builder, r_llvm_int(emitter, 64U), r_llvm_byte_offset(emitter, name, 8U), "");
    LLVMValueRef found = r_llvm_entry_alloca(emitter, 8U, 8U, "found");
    LLVMBasicBlockRef done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    uint32_t index;

    memcmp_parameters[0] = r_llvm_pointer(emitter);
    memcmp_parameters[1] = r_llvm_pointer(emitter);
    memcmp_parameters[2] = r_llvm_int(emitter, 64U);
    memcmp_type = LLVMFunctionType(r_llvm_int(emitter, 32U), memcmp_parameters, 3U, 0);
    if (memcmp_function == NULL) {
        memcmp_function = LLVMAddFunction(emitter->module, "memcmp", memcmp_type);
    }
    (void)LLVMBuildStore(emitter->builder, r_llvm_u64(emitter, aggregate->variant_count), found);
    for (index = 0U; index < aggregate->variant_count; ++index) {
        const RSemanticVariant *variant = r_llvm_reflection_variant(emitter, aggregate, index);
        const size_t expected =
            variant == NULL ? 0U : r_llvm_reflection_length(emitter, variant->name_intern_id);
        LLVMBasicBlockRef compare;
        LLVMBasicBlockRef matched;
        LLVMBasicBlockRef next;
        LLVMValueRef arguments[3];
        if (variant == NULL) {
            return NULL;
        }
        compare = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        matched = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        next = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        (void)LLVMBuildCondBr(
            emitter->builder,
            LLVMBuildICmp(emitter->builder, LLVMIntEQ, length, r_llvm_u64(emitter, expected), ""),
            compare,
            next);
        LLVMPositionBuilderAtEnd(emitter->builder, compare);
        if (expected == 0U) {
            (void)LLVMBuildBr(emitter->builder, matched);
        } else {
            arguments[0] = data;
            arguments[1] = r_llvm_program_string(emitter, variant->name_intern_id);
            arguments[2] = r_llvm_u64(emitter, expected);
            if (arguments[1] == NULL) {
                return NULL;
            }
            (void)LLVMBuildCondBr(
                emitter->builder,
                LLVMBuildICmp(
                    emitter->builder,
                    LLVMIntEQ,
                    LLVMBuildCall2(
                        emitter->builder, memcmp_type, memcmp_function, arguments, 3U, ""),
                    r_llvm_u32(emitter, 0U),
                    ""),
                matched,
                next);
        }
        LLVMPositionBuilderAtEnd(emitter->builder, matched);
        (void)LLVMBuildStore(emitter->builder, r_llvm_u64(emitter, index), found);
        (void)LLVMBuildBr(emitter->builder, done);
        LLVMPositionBuilderAtEnd(emitter->builder, next);
    }
    (void)LLVMBuildBr(emitter->builder, done);
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    return LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), found, "");
}

bool r_llvm_std_reflection(RLlvmEmitter *emitter, const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;
    const RTypeId subject = instruction->auxiliary_type;
    const RSemanticAggregate *aggregate = r_llvm_reflection_subject(emitter, operation, subject);
    const bool attribute_form = (operation == R_STANDARD_CALL_CORE_TYPE_ATTRIBUTE) ||
                                (operation == R_STANDARD_CALL_CORE_FIELD_ATTRIBUTE) ||
                                (operation == R_STANDARD_CALL_CORE_VARIANT_ATTRIBUTE);
    const RSemanticType *option =
        r_llvm_type(emitter, r_llvm_value_type(emitter, instruction->type));
    const RSemanticAggregate *attribute = NULL;
    LLVMValueRef operand = r_llvm_value(emitter, r_llvm_std_operand(emitter, instruction, 0U));
    LLVMValueRef result;
    LLVMValueRef selector;
    LLVMValueRef choice;
    LLVMBasicBlockRef done;
    uint32_t payload = 0U;
    uint32_t members;
    uint32_t member;

    if ((aggregate == NULL) || (option == NULL) || (operand == NULL)) {
        return aggregate == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
    }
    if (attribute_form) {
        attribute =
            option->kind == R_SEMANTIC_TYPE_OPTION ? r_llvm_aggregate(emitter, option->base) : NULL;
        if ((attribute == NULL) || (attribute->attribute_targets == 0U)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
    }
    if ((operation == R_STANDARD_CALL_CORE_ENUM_ORDINAL) ||
        (r_llvm_scalar_type(emitter, instruction->type) != NULL)) {
        result = r_llvm_entry_alloca(emitter, 8U, 8U, "ordinal");
        (void)LLVMBuildStore(emitter->builder, r_llvm_u64(emitter, 0U), result);
    } else {
        result = r_llvm_std_result(emitter, instruction);
        if (result == NULL) {
            return false;
        }
    }
    if ((option->kind == R_SEMANTIC_TYPE_OPTION) &&
        !r_llvm_payload_offset(emitter, instruction->type, &payload)) {
        return false;
    }
    if ((operation == R_STANDARD_CALL_CORE_ENUM_NAME) ||
        (operation == R_STANDARD_CALL_CORE_VARIANT_NAME) ||
        (operation == R_STANDARD_CALL_CORE_FIELD_NAME_AT)) {
        if (!r_llvm_reflection_text(emitter, result, 0U, 0U)) {
            return false;
        }
    }
    /* The selector: the enumerator, the active tag, the index or the found name. */
    switch (operation) {
    case R_STANDARD_CALL_CORE_VARIANT_NAME:
        selector = LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 32U), operand, "");
        break;
    case R_STANDARD_CALL_CORE_ENUM_FROM_NAME:
        selector = r_llvm_reflection_find(emitter, aggregate, operand);
        break;
    case R_STANDARD_CALL_CORE_ENUM_AT:
    case R_STANDARD_CALL_CORE_FIELD_NAME_AT:
    case R_STANDARD_CALL_CORE_TYPE_ATTRIBUTE:
    case R_STANDARD_CALL_CORE_FIELD_ATTRIBUTE:
        selector = LLVMBuildIntCast2(emitter->builder, operand, r_llvm_int(emitter, 64U), 0, "");
        break;
    default:
        selector = operand;
        break;
    }
    if (selector == NULL) {
        return false;
    }
    if (operation == R_STANDARD_CALL_CORE_TYPE_ATTRIBUTE) {
        const RSemanticAttributeUse *use =
            r_llvm_reflection_use(emitter, operation, subject, attribute, 0U);
        if ((use != NULL) &&
            !r_llvm_reflection_some_attribute(emitter, result, payload, attribute, use)) {
            return false;
        }
        r_llvm_std_initialized(emitter, instruction);
        return true;
    }
    members = (operation == R_STANDARD_CALL_CORE_FIELD_ATTRIBUTE) ||
                      (operation == R_STANDARD_CALL_CORE_FIELD_NAME_AT)
                  ? aggregate->field_count
                  : aggregate->variant_count;
    done = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
    choice = LLVMBuildSwitch(emitter->builder, selector, done, members);
    for (member = 0U; member < members; ++member) {
        const RSemanticVariant *variant =
            (operation == R_STANDARD_CALL_CORE_FIELD_ATTRIBUTE) ||
                    (operation == R_STANDARD_CALL_CORE_FIELD_NAME_AT)
                ? NULL
                : r_llvm_reflection_variant(emitter, aggregate, member);
        const RSemanticAttributeUse *use =
            attribute == NULL
                ? NULL
                : r_llvm_reflection_use(emitter, operation, subject, attribute, member);
        LLVMBasicBlockRef selected;
        LLVMValueRef label;
        if ((variant == NULL) && (operation != R_STANDARD_CALL_CORE_FIELD_ATTRIBUTE) &&
            (operation != R_STANDARD_CALL_CORE_FIELD_NAME_AT)) {
            return r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        if ((attribute != NULL) && (use == NULL)) {
            continue;
        }
        switch (operation) {
        case R_STANDARD_CALL_CORE_ENUM_AT:
        case R_STANDARD_CALL_CORE_ENUM_FROM_NAME:
        case R_STANDARD_CALL_CORE_FIELD_NAME_AT:
        case R_STANDARD_CALL_CORE_FIELD_ATTRIBUTE:
            label = r_llvm_u64(emitter, member);
            break;
        case R_STANDARD_CALL_CORE_VARIANT_NAME:
            label = r_llvm_u32(emitter, variant->value);
            break;
        default:
            label = r_llvm_reflection_enumerator(emitter, subject, variant->value);
            break;
        }
        if (label == NULL) {
            return false;
        }
        selected = LLVMAppendBasicBlockInContext(emitter->context, emitter->function, "");
        LLVMAddCase(choice, label, selected);
        LLVMPositionBuilderAtEnd(emitter->builder, selected);
        switch (operation) {
        case R_STANDARD_CALL_CORE_ENUM_NAME:
        case R_STANDARD_CALL_CORE_VARIANT_NAME:
            if (!r_llvm_reflection_text(
                    emitter,
                    result,
                    variant->name_intern_id,
                    r_llvm_reflection_length(emitter, variant->name_intern_id))) {
                return false;
            }
            break;
        case R_STANDARD_CALL_CORE_FIELD_NAME_AT: {
            const RSemanticField *field =
                r_llvm_field(emitter, aggregate->first_field + member + 1U);
            if ((field == NULL) ||
                !r_llvm_reflection_text(emitter,
                                        result,
                                        field->name_intern_id,
                                        r_llvm_reflection_length(emitter, field->name_intern_id))) {
                return field == NULL ? r_llvm_fail(emitter, R_FRONTEND_INTERNAL_ERROR) : false;
            }
            break;
        }
        case R_STANDARD_CALL_CORE_ENUM_ORDINAL:
            (void)LLVMBuildStore(emitter->builder, r_llvm_u64(emitter, member), result);
            break;
        case R_STANDARD_CALL_CORE_ENUM_AT:
        case R_STANDARD_CALL_CORE_ENUM_FROM_NAME: {
            LLVMValueRef value = r_llvm_reflection_enumerator(emitter, subject, variant->value);
            if (value == NULL) {
                return false;
            }
            (void)LLVMBuildStore(emitter->builder, r_llvm_u32(emitter, 1U), result);
            r_llvm_store_scalar(
                emitter, subject, value, r_llvm_byte_offset(emitter, result, payload));
            break;
        }
        default:
            if (!r_llvm_reflection_some_attribute(emitter, result, payload, attribute, use)) {
                return false;
            }
            break;
        }
        (void)LLVMBuildBr(emitter->builder, done);
    }
    LLVMPositionBuilderAtEnd(emitter->builder, done);
    if (r_llvm_scalar_type(emitter, instruction->type) != NULL) {
        return r_llvm_set_value(
            emitter,
            instruction->result,
            LLVMBuildLoad2(emitter->builder, r_llvm_int(emitter, 64U), result, ""));
    }
    r_llvm_std_initialized(emitter, instruction);
    return true;
}
