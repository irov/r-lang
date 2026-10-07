/* Library R-LIB-0027 (L45): the bit and wide integer operations of core. Each (operation, integer
   type) pair used by the program becomes one static helper `r_core_<operation>_<suffix>`. The
   helpers are portable C17 idioms that the pinned compiler reduces to single instructions where
   the target has them: the shift loop of leading_zeros to clz, the population count of
   Hacker's Delight to cnt, the trailing mask of trailing_zeros to rbit and clz, the shifts and
   masks of swap_bytes to rev, the masked shifts of a rotation to ror, and a chain of carrying
   additions to adds and adc. A 64-bit widening multiplication is four 32-bit products, which
   the compiler does not fuse into umulh (compiler/README.md, *Bit and wide integer operations*).
   A wide operation returns its first part and writes its second through the last parameter; a
   narrowing division is reached only after the caller has checked its divisor. */

static bool r_c17_core_bits_operand_type_ok(const RC17Emitter *emitter,
                                            RStandardCallOperation operation,
                                            uint32_t index,
                                            RTypeId integer_type,
                                            RTypeId operand_type) {
    const RSemanticTypeKind kind = r_c17_value_kind(emitter, operand_type);

    if ((index == UINT32_C(1)) && ((operation == R_STANDARD_CALL_CORE_ROTATE_LEFT) ||
                                   (operation == R_STANDARD_CALL_CORE_ROTATE_RIGHT))) {
        return kind == R_SEMANTIC_TYPE_U32;
    }
    if ((index == UINT32_C(2)) && ((operation == R_STANDARD_CALL_CORE_CARRYING_ADD) ||
                                   (operation == R_STANDARD_CALL_CORE_BORROWING_SUB))) {
        return kind == R_SEMANTIC_TYPE_BOOL;
    }
    return r_c17_value_type(emitter, operand_type) == r_c17_value_type(emitter, integer_type);
}

/* The result: a u32 count, the integer type, or a tuple of two parts whose first is the integer
   type and whose second is the integer type or bool. */
static bool r_c17_core_bits_result_ok(RC17Emitter *emitter,
                                      RStandardCallOperation operation,
                                      RTypeId integer_type,
                                      RTypeId result_type) {
    const RSemanticAggregate *aggregate;

    if (operation <= R_STANDARD_CALL_CORE_COUNT_ONES) {
        return r_c17_value_kind(emitter, result_type) == R_SEMANTIC_TYPE_U32;
    }
    if (operation < R_STANDARD_CALL_CORE_WIDENING_MUL) {
        return r_c17_value_type(emitter, result_type) == r_c17_value_type(emitter, integer_type);
    }
    aggregate = r_c17_aggregate_for_type(emitter, r_c17_value_type(emitter, result_type), NULL);
    if ((aggregate == NULL) || !aggregate->is_tuple || (aggregate->field_count != UINT32_C(2))) {
        return false;
    }
    const RSemanticField *fields = &emitter->frontend->semantic_fields[aggregate->first_field];
    const bool flag = (operation == R_STANDARD_CALL_CORE_CARRYING_ADD) ||
                      (operation == R_STANDARD_CALL_CORE_BORROWING_SUB);
    return (r_c17_value_type(emitter, fields[0].type) == r_c17_value_type(emitter, integer_type)) &&
           (flag ? (r_c17_value_kind(emitter, fields[1].type) == R_SEMANTIC_TYPE_BOOL)
                 : (r_c17_value_type(emitter, fields[1].type) ==
                    r_c17_value_type(emitter, integer_type)));
}

static bool r_c17_note_core_bits_helper(RC17Emitter *emitter,
                                        RTypeId integer_type,
                                        RStandardCallOperation operation) {
    const RSemanticTypeKind kind = r_c17_value_kind(emitter, integer_type);
    bool is_signed;
    uint32_t width;

    if (!r_standard_core_bits_operation(operation) ||
        !r_c17_integer_properties(kind, &is_signed, &width) ||
        (is_signed && (operation >= R_STANDARD_CALL_CORE_WIDENING_MUL))) {
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    }
    emitter
        ->core_bits_helpers[(uint32_t)operation - (uint32_t)R_STANDARD_CALL_CORE_LEADING_ZEROS] |=
        (uint16_t)(UINT16_C(1) << ((uint32_t)kind - (uint32_t)R_SEMANTIC_TYPE_I8));
    if (is_signed && ((operation == R_STANDARD_CALL_CORE_SWAP_BYTES) ||
                      (operation == R_STANDARD_CALL_CORE_ROTATE_LEFT) ||
                      (operation == R_STANDARD_CALL_CORE_ROTATE_RIGHT))) {
        emitter->uses_memory = true;
    }
    return true;
}

static bool r_c17_emit_core_bits_helper_name(RC17Emitter *emitter,
                                             RTypeId integer_type,
                                             RStandardCallOperation operation) {
    const char *suffix = r_c17_core_integer_suffix(r_c17_value_kind(emitter, integer_type));

    if (!r_standard_core_bits_operation(operation) || (suffix == NULL)) {
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    return r_c17_format(emitter, "r_core_%s_%s", r_standard_core_bits_name(operation), suffix);
}

/* The C name of part `index` of the tuple result of a wide operation. */
static bool r_c17_emit_core_bits_part(RC17Emitter *emitter, RTypeId result_type, uint32_t index) {
    const RSemanticAggregate *aggregate =
        r_c17_aggregate_for_type(emitter, r_c17_value_type(emitter, result_type), NULL);

    if ((aggregate == NULL) || (index >= aggregate->field_count)) {
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    return r_c17_emit_field_name(
        emitter, &emitter->frontend->semantic_fields[(size_t)aggregate->first_field + index]);
}

static bool r_c17_preflight_core_bits_call(RC17Emitter *emitter,
                                           const RHirNode *node,
                                           RSymbolId function_symbol,
                                           size_t depth) {
    const RStandardCallOperation operation = node->standard_operation;

    if (!r_standard_core_bits_operation(operation) ||
        (node->child_count != r_standard_core_bits_operand_count(operation))) {
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    }
    for (uint32_t index = 0U; index < node->child_count; ++index) {
        const RHirNodeId child = r_c17_child(emitter, node, index);
        const RHirNode *operand = r_c17_node(emitter, child);

        if (!r_c17_preflight_expression(emitter, child, function_symbol, depth + 1U) ||
            (operand == NULL) ||
            !r_c17_core_bits_operand_type_ok(
                emitter, operation, index, node->auxiliary_type, operand->type)) {
            return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
        }
    }
    if (!r_c17_core_bits_result_ok(emitter, operation, node->auxiliary_type, node->type) ||
        !r_c17_preflight_type_dependency(emitter, node->type, 0U)) {
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    }
    return r_c17_note_core_bits_helper(emitter, node->auxiliary_type, operation);
}

static bool r_c17_preflight_async_core_bits(RC17Emitter *emitter,
                                            const RMirFunction *mir,
                                            const RMirInstruction *instruction) {
    const RStandardCallOperation operation = instruction->standard_operation;

    if ((instruction->operand_count != r_standard_core_bits_operand_count(operation)) ||
        (instruction->result == R_MIR_VALUE_ID_INVALID)) {
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    }
    for (uint32_t index = 0U; index < instruction->operand_count; ++index) {
        const RMirInstruction *operand = r_c17_mir_value_definition(
            emitter, mir, r_c17_mir_operand(emitter, instruction, index));

        if ((operand == NULL) ||
            !r_c17_core_bits_operand_type_ok(
                emitter, operation, index, instruction->auxiliary_type, operand->type)) {
            return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
        }
    }
    if (!r_c17_core_bits_result_ok(
            emitter, operation, instruction->auxiliary_type, instruction->type) ||
        !r_c17_preflight_type_dependency(emitter, instruction->type, 0U)) {
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    }
    return r_c17_note_core_bits_helper(emitter, instruction->auxiliary_type, operation);
}

/* An operand of a call: a temporary of the synchronous body, or a frame value when mir is set. */
typedef struct RC17CoreBitsOperands {
    const RMirFunction *mir;
    const RSemanticSymbol *function;
    uint32_t values[3];
    uint32_t count;
} RC17CoreBitsOperands;

static bool r_c17_emit_core_bits_operand(RC17Emitter *emitter,
                                         const RC17CoreBitsOperands *operands,
                                         uint32_t index) {
    return operands->mir == NULL
               ? r_c17_emit_temporary_name(emitter, operands->values[index])
               : r_c17_emit_async_storage_reference(
                     emitter, operands->mir, operands->function, false, operands->values[index]);
}

/* Library R-LIB-0027: a narrowing division panics with division_by_zero for a zero divisor and
   with integer_overflow when the high half is not below it, since the quotient would not fit. */
static bool r_c17_emit_core_bits_division_checks(RC17Emitter *emitter,
                                                 const RC17CoreBitsOperands *operands,
                                                 RSourceSpan span,
                                                 uint32_t depth) {
    return r_c17_indent(emitter, depth) && r_c17_write(emitter, "if (") &&
           r_c17_emit_core_bits_operand(emitter, operands, UINT32_C(2)) &&
           r_c17_write(emitter, " == 0) {\n") &&
           r_c17_emit_panic_call(
               emitter, "R_RUNTIME_PANIC_DIVISION_BY_ZERO", span, depth + UINT32_C(1)) &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "}\n") &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "if (") &&
           r_c17_emit_core_bits_operand(emitter, operands, UINT32_C(0)) &&
           r_c17_write(emitter, " >= ") &&
           r_c17_emit_core_bits_operand(emitter, operands, UINT32_C(2)) &&
           r_c17_write(emitter, ") {\n") &&
           r_c17_emit_panic_call(
               emitter, "R_RUNTIME_PANIC_INTEGER_OVERFLOW", span, depth + UINT32_C(1)) &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "}\n");
}

/* `helper(operands` without the closing parenthesis. */
static bool r_c17_emit_core_bits_call_head(RC17Emitter *emitter,
                                           RTypeId integer_type,
                                           RStandardCallOperation operation,
                                           const RC17CoreBitsOperands *operands) {
    if (!r_c17_emit_core_bits_helper_name(emitter, integer_type, operation) ||
        !r_c17_write(emitter, "(")) {
        return false;
    }
    for (uint32_t index = 0U; index < operands->count; ++index) {
        if (((index != 0U) && !r_c17_write(emitter, ", ")) ||
            !r_c17_emit_core_bits_operand(emitter, operands, index)) {
            return false;
        }
    }
    return true;
}

static bool r_c17_emit_core_bits(RC17Emitter *emitter,
                                 const RHirNode *node,
                                 uint32_t depth,
                                 RC17Value *result) {
    const RStandardCallOperation operation = node->standard_operation;
    RC17CoreBitsOperands operands = {0};
    uint32_t temporary = UINT32_C(0);

    if (!r_standard_core_bits_operation(operation) ||
        (node->child_count != r_standard_core_bits_operand_count(operation))) {
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    operands.count = node->child_count;
    for (uint32_t index = 0U; index < node->child_count; ++index) {
        RC17Value operand = {0};
        if (!r_c17_emit_expression(emitter, r_c17_child(emitter, node, index), depth, &operand) ||
            !operand.has_value) {
            return false;
        }
        operands.values[index] = operand.temporary;
    }
    if ((operation == R_STANDARD_CALL_CORE_NARROWING_DIV) &&
        !r_c17_emit_core_bits_division_checks(emitter, &operands, node->span, depth)) {
        return false;
    }
    if (!r_c17_new_temporary(emitter, node->type, depth, &temporary)) {
        return false;
    }
    if (operation < R_STANDARD_CALL_CORE_WIDENING_MUL) {
        if (!r_c17_write(emitter, " = ") ||
            !r_c17_emit_core_bits_call_head(emitter, node->auxiliary_type, operation, &operands) ||
            !r_c17_write(emitter, ");\n")) {
            return false;
        }
    } else if (!r_c17_write(emitter, ";\n") || !r_c17_indent(emitter, depth) ||
               !r_c17_emit_temporary_name(emitter, temporary) || !r_c17_write(emitter, ".") ||
               !r_c17_emit_core_bits_part(emitter, node->type, UINT32_C(0)) ||
               !r_c17_write(emitter, " = ") ||
               !r_c17_emit_core_bits_call_head(
                   emitter, node->auxiliary_type, operation, &operands) ||
               !r_c17_write(emitter, ", &") || !r_c17_emit_temporary_name(emitter, temporary) ||
               !r_c17_write(emitter, ".") ||
               !r_c17_emit_core_bits_part(emitter, node->type, UINT32_C(1)) ||
               !r_c17_write(emitter, ");\n")) {
        return false;
    }
    result->type = r_c17_value_type(emitter, node->type);
    result->temporary = temporary;
    result->has_value = true;
    return true;
}

static bool r_c17_emit_async_core_bits(RC17Emitter *emitter,
                                       const RMirFunction *mir,
                                       const RSemanticSymbol *function,
                                       const RMirInstruction *instruction,
                                       uint32_t depth) {
    const RStandardCallOperation operation = instruction->standard_operation;
    RC17CoreBitsOperands operands = {mir, function, {0U, 0U, 0U}, instruction->operand_count};

    if ((instruction->operand_count > UINT32_C(3)) ||
        (instruction->operand_count != r_standard_core_bits_operand_count(operation))) {
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    for (uint32_t index = 0U; index < instruction->operand_count; ++index) {
        operands.values[index] = r_c17_mir_operand(emitter, instruction, index);
    }
    if ((operation == R_STANDARD_CALL_CORE_NARROWING_DIV) &&
        !r_c17_emit_core_bits_division_checks(emitter, &operands, instruction->span, depth)) {
        return false;
    }
    if (!r_c17_indent(emitter, depth) ||
        !r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result)) {
        return false;
    }
    if (operation < R_STANDARD_CALL_CORE_WIDENING_MUL) {
        return r_c17_write(emitter, " = ") &&
               r_c17_emit_core_bits_call_head(
                   emitter, instruction->auxiliary_type, operation, &operands) &&
               r_c17_write(emitter, ");\n");
    }
    return r_c17_write(emitter, ".") &&
           r_c17_emit_core_bits_part(emitter, instruction->type, UINT32_C(0)) &&
           r_c17_write(emitter, " = ") &&
           r_c17_emit_core_bits_call_head(
               emitter, instruction->auxiliary_type, operation, &operands) &&
           r_c17_write(emitter, ", &") &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) &&
           r_c17_write(emitter, ".") &&
           r_c17_emit_core_bits_part(emitter, instruction->type, UINT32_C(1)) &&
           r_c17_write(emitter, ");\n");
}

/* The text of one helper. `type` is the C type of the R integer type, `bits` the unsigned type of
   its width, `work` the unsigned type the idioms compute in and `constant` its constant macro. */
static bool r_c17_emit_core_bits_helper(RC17Emitter *emitter,
                                        RSemanticTypeKind kind,
                                        RStandardCallOperation operation) {
    const char *type = r_c17_integer_c_type(kind);
    const char *bits = r_c17_core_integer_unsigned_type(kind);
    const char *suffix = r_c17_core_integer_suffix(kind);
    const char *name = r_standard_core_bits_name(operation);
    bool is_signed;
    uint32_t width;

    if ((type == NULL) || (bits == NULL) || (suffix == NULL) || (name == NULL) ||
        !r_c17_integer_properties(kind, &is_signed, &width)) {
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    const bool wide = width == UINT32_C(64);
    const char *work = wide ? "uint64_t" : "uint32_t";
    const char *constant = wide ? "UINT64_C" : "UINT32_C";
    const uint32_t top = width - UINT32_C(1);
    switch (operation) {
    case R_STANDARD_CALL_CORE_LEADING_ZEROS:
        if (width <= UINT32_C(16)) {
            /* The pinned compiler keeps the shift loop of a narrow value a loop, so the bits below
               the highest set one are filled and counted. */
            return r_c17_format(emitter,
                                "static uint32_t r_core_%s_%s(%s value) {\n"
                                "    uint32_t ones = (%s)value;\n"
                                "    ones |= ones >> 1;\n"
                                "    ones |= ones >> 2;\n"
                                "    ones |= ones >> 4;\n"
                                "%s"
                                "    ones = ones - ((ones >> 1) & UINT32_C(0x55555555));\n"
                                "    ones = (ones & UINT32_C(0x33333333)) + ((ones >> 2) & "
                                "UINT32_C(0x33333333));\n"
                                "    ones = (ones + (ones >> 4)) & UINT32_C(0x0f0f0f0f);\n"
                                "    return UINT32_C(%u) - ((ones * UINT32_C(0x01010101)) >> 24);\n"
                                "}\n\n",
                                name,
                                suffix,
                                type,
                                bits,
                                width == UINT32_C(16) ? "    ones |= ones >> 8;\n" : "",
                                width);
        }
        return r_c17_format(emitter,
                            "static uint32_t r_core_%s_%s(%s value) {\n"
                            "    uint32_t count = UINT32_C(%u);\n"
                            "    for (%s rest = (%s)value; rest != %s(0); rest >>= 1) {\n"
                            "        --count;\n"
                            "    }\n"
                            "    return count;\n"
                            "}\n\n",
                            name,
                            suffix,
                            type,
                            width,
                            work,
                            bits,
                            constant);
    case R_STANDARD_CALL_CORE_TRAILING_ZEROS:
    case R_STANDARD_CALL_CORE_COUNT_ONES:
        /* trailing_zeros counts the ones of the mask below the lowest set bit, all ones for
           zero. */
        if (!r_c17_format(
                emitter, "static uint32_t r_core_%s_%s(%s value) {\n", name, suffix, type) ||
            !(operation == R_STANDARD_CALL_CORE_COUNT_ONES
                  ? r_c17_format(emitter, "    %s ones = (%s)value;\n", work, bits)
                  : r_c17_format(emitter,
                                 "    %s lowest = (%s)value;\n"
                                 "    %s ones = (%s)((lowest & (%s(0) - lowest)) - %s(1));\n",
                                 work,
                                 bits,
                                 work,
                                 bits,
                                 constant,
                                 constant))) {
            return false;
        }
        if (wide) {
            return r_c17_write(
                emitter,
                "    ones = ones - ((ones >> 1) & UINT64_C(0x5555555555555555));\n"
                "    ones = (ones & UINT64_C(0x3333333333333333)) + ((ones >> 2) & "
                "UINT64_C(0x3333333333333333));\n"
                "    ones = (ones + (ones >> 4)) & UINT64_C(0x0f0f0f0f0f0f0f0f);\n"
                "    return (uint32_t)((ones * UINT64_C(0x0101010101010101)) >> 56);\n"
                "}\n\n");
        }
        return r_c17_write(emitter,
                           "    ones = ones - ((ones >> 1) & UINT32_C(0x55555555));\n"
                           "    ones = (ones & UINT32_C(0x33333333)) + ((ones >> 2) & "
                           "UINT32_C(0x33333333));\n"
                           "    ones = (ones + (ones >> 4)) & UINT32_C(0x0f0f0f0f);\n"
                           "    return (ones * UINT32_C(0x01010101)) >> 24;\n"
                           "}\n\n");
    case R_STANDARD_CALL_CORE_SWAP_BYTES:
    case R_STANDARD_CALL_CORE_ROTATE_LEFT:
    case R_STANDARD_CALL_CORE_ROTATE_RIGHT:
        if (operation == R_STANDARD_CALL_CORE_SWAP_BYTES) {
            if (!r_c17_format(emitter,
                              "static %s r_core_%s_%s(%s value) {\n"
                              "    %s swapped = (%s)value;\n",
                              type,
                              name,
                              suffix,
                              type,
                              work,
                              bits)) {
                return false;
            }
            if ((width == UINT32_C(16)) &&
                !r_c17_write(emitter, "    swapped = (swapped >> 8) | (swapped << 8);\n")) {
                return false;
            }
            if ((width == UINT32_C(32)) &&
                !r_c17_write(emitter,
                             "    swapped = ((swapped & UINT32_C(0x00ff00ff)) << 8) |\n"
                             "              ((swapped >> 8) & UINT32_C(0x00ff00ff));\n"
                             "    swapped = (swapped << 16) | (swapped >> 16);\n")) {
                return false;
            }
            if (wide &&
                !r_c17_write(emitter,
                             "    swapped = ((swapped & UINT64_C(0x00ff00ff00ff00ff)) << 8) |\n"
                             "              ((swapped >> 8) & UINT64_C(0x00ff00ff00ff00ff));\n"
                             "    swapped = ((swapped & UINT64_C(0x0000ffff0000ffff)) << 16) |\n"
                             "              ((swapped >> 16) & UINT64_C(0x0000ffff0000ffff));\n"
                             "    swapped = (swapped << 32) | (swapped >> 32);\n")) {
                return false;
            }
        } else if (!r_c17_format(emitter,
                                 "static %s r_core_%s_%s(%s value, uint32_t count) {\n"
                                 "    const uint32_t shift = count & UINT32_C(%u);\n"
                                 "    const %s rotated = (%s)value;\n"
                                 "    %s swapped = (rotated %s shift) |\n"
                                 "                 (rotated %s ((UINT32_C(0) - shift) & "
                                 "UINT32_C(%u)));\n",
                                 type,
                                 name,
                                 suffix,
                                 type,
                                 top,
                                 work,
                                 bits,
                                 work,
                                 operation == R_STANDARD_CALL_CORE_ROTATE_LEFT ? "<<" : ">>",
                                 operation == R_STANDARD_CALL_CORE_ROTATE_LEFT ? ">>" : "<<",
                                 top)) {
            return false;
        }
        if (!is_signed) {
            return r_c17_format(emitter,
                                "    return (%s)swapped;\n"
                                "}\n\n",
                                type);
        }
        return r_c17_format(emitter,
                            "    const %s narrowed = (%s)swapped;\n"
                            "    %s result;\n"
                            "    memcpy(&result, &narrowed, sizeof(result));\n"
                            "    return result;\n"
                            "}\n\n",
                            bits,
                            bits,
                            type);
    case R_STANDARD_CALL_CORE_WIDENING_MUL:
        if (!wide) {
            return r_c17_format(emitter,
                                "static %s r_core_%s_%s(%s left, %s right, %s *high) {\n"
                                "    const uint64_t product = (uint64_t)left * right;\n"
                                "    *high = (%s)(product >> %u);\n"
                                "    return (%s)product;\n"
                                "}\n\n",
                                type,
                                name,
                                suffix,
                                type,
                                type,
                                type,
                                type,
                                width,
                                type);
        }
        return r_c17_format(
            emitter,
            "static %s r_core_%s_%s(%s left, %s right, %s *high) {\n"
            "    const uint64_t left_low = (uint64_t)left & UINT64_C(0xffffffff);\n"
            "    const uint64_t right_low = (uint64_t)right & UINT64_C(0xffffffff);\n"
            "    const uint64_t left_high = (uint64_t)left >> 32;\n"
            "    const uint64_t right_high = (uint64_t)right >> 32;\n"
            "    const uint64_t low_low = left_low * right_low;\n"
            "    const uint64_t low_high = left_low * right_high;\n"
            "    const uint64_t high_low = left_high * right_low;\n"
            "    const uint64_t middle = (low_low >> 32) + (low_high & "
            "UINT64_C(0xffffffff)) +\n"
            "                            (high_low & UINT64_C(0xffffffff));\n"
            "    *high = (%s)(left_high * right_high + (low_high >> 32) + "
            "(high_low >> 32) +\n"
            "                (middle >> 32));\n"
            "    return (%s)((uint64_t)left * (uint64_t)right);\n"
            "}\n\n",
            type,
            name,
            suffix,
            type,
            type,
            type,
            type,
            type);
    case R_STANDARD_CALL_CORE_CARRYING_ADD:
    case R_STANDARD_CALL_CORE_BORROWING_SUB:
        if (operation == R_STANDARD_CALL_CORE_CARRYING_ADD) {
            return r_c17_format(
                emitter,
                "static %s r_core_%s_%s(%s left, %s right, _Bool carry, _Bool *out) {\n"
                "    const %s sum = (%s)(left + right);\n"
                "    const %s total = (%s)(sum + (%s)carry);\n"
                "    *out = (_Bool)((sum < left) | (total < sum));\n"
                "    return total;\n"
                "}\n\n",
                type,
                name,
                suffix,
                type,
                type,
                type,
                type,
                type,
                type,
                type);
        }
        return r_c17_format(
            emitter,
            "static %s r_core_%s_%s(%s left, %s right, _Bool borrow, _Bool *out) {\n"
            "    const %s difference = (%s)(left - right);\n"
            "    *out = (_Bool)((left < right) | (difference < (%s)borrow));\n"
            "    return (%s)(difference - (%s)borrow);\n"
            "}\n\n",
            type,
            name,
            suffix,
            type,
            type,
            type,
            type,
            type,
            type,
            type);
    case R_STANDARD_CALL_CORE_NARROWING_DIV:
        if (!wide) {
            return r_c17_format(emitter,
                                "static %s r_core_%s_%s(%s high, %s low, %s divisor, %s *rest) {\n"
                                "    const uint64_t dividend = ((uint64_t)high << %u) | low;\n"
                                "    *rest = (%s)(dividend %% divisor);\n"
                                "    return (%s)(dividend / divisor);\n"
                                "}\n\n",
                                type,
                                name,
                                suffix,
                                type,
                                type,
                                type,
                                type,
                                width,
                                type,
                                type);
        }
        /* Knuth's algorithm D over 32-bit digits after the divisor is normalized (Hacker's
           Delight, divlu); a quotient digit estimate is tested before its product with the low
           digit of the divisor can overflow. */
        return r_c17_format(
            emitter,
            "static %s r_core_%s_%s(%s high, %s low, %s divisor, %s *rest) {\n"
            "    const uint64_t base = UINT64_C(1) << 32;\n"
            "    uint32_t shift = UINT32_C(64);\n"
            "    for (uint64_t rest_bits = divisor; rest_bits != UINT64_C(0); rest_bits >>= 1) {\n"
            "        --shift;\n"
            "    }\n"
            "    const uint64_t normal = (uint64_t)divisor << shift;\n"
            "    const uint64_t normal_high = normal >> 32;\n"
            "    const uint64_t normal_low = normal & UINT64_C(0xffffffff);\n"
            "    const uint64_t spill = ((uint64_t)low >> ((UINT32_C(64) - shift) & UINT32_C(63))) "
            "&\n"
            "                           (UINT64_C(0) - (uint64_t)(shift != UINT32_C(0)));\n"
            "    const uint64_t top = ((uint64_t)high << shift) | spill;\n"
            "    const uint64_t bottom = (uint64_t)low << shift;\n"
            "    const uint64_t bottom_high = bottom >> 32;\n"
            "    const uint64_t bottom_low = bottom & UINT64_C(0xffffffff);\n"
            "    uint64_t first = top / normal_high;\n"
            "    uint64_t estimate = top - first * normal_high;\n"
            "    while ((first >= base) || (first * normal_low > ((estimate << 32) | "
            "bottom_high))) {\n"
            "        --first;\n"
            "        estimate += normal_high;\n"
            "        if (estimate >= base) {\n"
            "            break;\n"
            "        }\n"
            "    }\n"
            "    const uint64_t middle = (top << 32) + bottom_high - first * normal;\n"
            "    uint64_t second = middle / normal_high;\n"
            "    estimate = middle - second * normal_high;\n"
            "    while ((second >= base) || (second * normal_low > ((estimate << 32) | "
            "bottom_low))) {\n"
            "        --second;\n"
            "        estimate += normal_high;\n"
            "        if (estimate >= base) {\n"
            "            break;\n"
            "        }\n"
            "    }\n"
            "    *rest = (%s)(((middle << 32) + bottom_low - second * normal) >> shift);\n"
            "    return (%s)((first << 32) | second);\n"
            "}\n\n",
            type,
            name,
            suffix,
            type,
            type,
            type,
            type,
            type,
            type);
    default:
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
}

static bool r_c17_emit_core_bits_helpers(RC17Emitter *emitter) {
    for (uint32_t operation = 0U; operation < UINT32_C(10); ++operation) {
        for (RSemanticTypeKind kind = R_SEMANTIC_TYPE_I8; kind <= R_SEMANTIC_TYPE_USIZE;
             kind = (RSemanticTypeKind)((uint32_t)kind + UINT32_C(1))) {
            const uint16_t bit =
                (uint16_t)(UINT16_C(1) << ((uint32_t)kind - (uint32_t)R_SEMANTIC_TYPE_I8));
            if (((emitter->core_bits_helpers[operation] & bit) != UINT16_C(0)) &&
                !r_c17_emit_core_bits_helper(
                    emitter,
                    kind,
                    (RStandardCallOperation)((uint32_t)R_STANDARD_CALL_CORE_LEADING_ZEROS +
                                             operation))) {
                return false;
            }
        }
    }
    return true;
}
