/* Library R-LIB-0027 (L45): the bit and wide integer operations of core. `core::count_ones_u64(x)`
   names its integer type by suffix, as the arithmetic of R-LIB-0001 does, and `core::count_ones(x)`
   selects the suffix from its first operand (standard_methods.inc). Each call lowers to one
   R_HIR_STANDARD_CALL whose auxiliary type is the integer type: the counts are u32, a byte swap
   or rotation keeps the type, and the wide operations of the unsigned types give a tuple
   (R-TYPE-0052) of two parts, the low and high halves of a product, a sum or difference and its
   carry or borrow, or a quotient and its remainder. The C17 helpers are portable idioms that the
   pinned compiler reduces to single instructions where the target has them (compiler/README.md,
   *Bit and wide integer operations*). */

/* Whether `name` is `<operation>_<suffix>` for one of the operations; the suffix is checked when
   the call is lowered. */
static bool r_core_bits_named(const RFrontendContext *context,
                              const RAstTokenView *name,
                              RStandardCallOperation *operation) {
    for (uint32_t index = (uint32_t)R_STANDARD_CALL_CORE_LEADING_ZEROS;
         index <= (uint32_t)R_STANDARD_CALL_CORE_NARROWING_DIV;
         ++index) {
        char prefix[32];
        (void)snprintf(prefix,
                       sizeof(prefix),
                       "%s_",
                       r_standard_core_bits_name((RStandardCallOperation)index));
        if (r_semantic_token_text_starts_with_owned(context, name, prefix)) {
            *operation = (RStandardCallOperation)index;
            return true;
        }
    }
    return false;
}

/* The type operand `index` of `operation` shall have: the integer type, the u32 count of a
   rotation, or the bool carry or borrow of a wide addition or subtraction. */
static RTypeId r_core_bits_operand_type(RStandardCallOperation operation,
                                        uint32_t index,
                                        RTypeId integer_type,
                                        RTypeId u32_type,
                                        RTypeId bool_type) {
    if ((index == UINT32_C(1)) && ((operation == R_STANDARD_CALL_CORE_ROTATE_LEFT) ||
                                   (operation == R_STANDARD_CALL_CORE_ROTATE_RIGHT))) {
        return u32_type;
    }
    if ((index == UINT32_C(2)) && ((operation == R_STANDARD_CALL_CORE_CARRYING_ADD) ||
                                   (operation == R_STANDARD_CALL_CORE_BORROWING_SUB))) {
        return bool_type;
    }
    return integer_type;
}

/* The result: a u32 count, the integer type, or the tuple of a wide operation. */
static bool r_core_bits_result_type(RFrontendContext *context,
                                    RStandardCallOperation operation,
                                    RTypeId integer_type,
                                    RSourceSpan span,
                                    RTypeId *result_type) {
    RTypeId element_storage[2] = {integer_type, integer_type};
    RTypeVector elements = {element_storage, 2U, 2U};

    if (operation <= R_STANDARD_CALL_CORE_COUNT_ONES) {
        return r_semantic_type_from_token(context, R_TOKEN_KW_U32, result_type);
    }
    if (operation < R_STANDARD_CALL_CORE_WIDENING_MUL) {
        *result_type = integer_type;
        return true;
    }
    if (((operation == R_STANDARD_CALL_CORE_CARRYING_ADD) ||
         (operation == R_STANDARD_CALL_CORE_BORROWING_SUB)) &&
        !r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, &element_storage[1])) {
        return false;
    }
    return r_tuple_type(context, &elements, span, result_type);
}

static bool r_body_lower_core_bits(RBodyContext *body,
                                   RAstRef argument_list,
                                   const RAstNodeView *arguments_view,
                                   RSourceSpan call_span,
                                   RSourceSpan operation_span,
                                   RStandardCallOperation operation,
                                   RTokenKind selected_type,
                                   RExpressionResult *result) {
    static const char *const operand_counts[] = {
        "core integer operation requires exactly one operand",
        "core integer operation requires exactly two operands",
        "core integer operation requires exactly three operands"};
    RFrontendContext *context = body->frontend;
    const uint32_t count = r_standard_core_bits_operand_count(operation);
    const bool wide = operation >= R_STANDARD_CALL_CORE_WIDENING_MUL;
    RTokenKind type_token = selected_type;
    RTypeId integer_type, u32_type, bool_type, result_type;
    RHirNodeId child_storage[3];
    RHirVector children;
    RHirNodeId node_id;
    RHirNode *node;
    bool is_signed;
    uint32_t width;
    char prefix[32];

    r_body_expression_invalid(result);
    (void)snprintf(prefix, sizeof(prefix), "%s_", r_standard_core_bits_name(operation));
    if ((type_token == R_TOKEN_INVALID) &&
        !r_body_core_integer_suffix(context, operation_span, prefix, &type_token)) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-LIB-0027",
                                 "core integer operation has an unsupported type suffix",
                                 operation_span);
    }
    if (!r_semantic_type_from_token(context, type_token, &integer_type) ||
        !r_semantic_type_from_token(context, R_TOKEN_KW_U32, &u32_type) ||
        !r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, &bool_type)) {
        return false;
    }
    if (!r_semantic_integer_properties(
            r_semantic_value_kind(context, integer_type), &is_signed, &width)) {
        return r_body_invariant(body);
    }
    if (wide && is_signed) {
        return r_body_diagnostic(body,
                                 "R-DIAG-TYPE-001",
                                 "R-LIB-0027",
                                 "a wide integer operation takes an unsigned type",
                                 operation_span);
    }
    if (arguments_view->child_count != count) {
        return r_body_diagnostic(
            body, "R-DIAG-TYPE-001", "R-LIB-0027", operand_counts[count - 1U], call_span);
    }
    for (uint32_t index = 0U; index < count; ++index) {
        const RTypeId expected =
            r_core_bits_operand_type(operation, index, integer_type, u32_type, bool_type);
        RExpressionResult argument = {0};
        RAstRef argument_ref;

        if (!r_ast_ref_child(context, argument_list, index, &argument_ref) ||
            !r_body_lower_value(body, argument_ref, expected, &argument)) {
            return false;
        }
        if (!argument.valid) {
            return true;
        }
        if (r_semantic_value_type(context, argument.type) != expected) {
            return r_body_diagnostic(body,
                                     "R-DIAG-TYPE-001",
                                     "R-LIB-0027",
                                     expected == u32_type ? "the count of a rotation is a u32"
                                     : expected == bool_type
                                         ? "the carry or borrow of a wide operation is a bool"
                                         : "core integer operand type shall match the function "
                                           "suffix",
                                     call_span);
        }
        child_storage[index] = argument.node;
    }
    if (!r_core_bits_result_type(context, operation, integer_type, call_span, &result_type)) {
        return context->resource_status == R_FRONTEND_OK ? r_body_cascade(body) : false;
    }
    children.items = child_storage;
    children.count = count;
    children.capacity = count;
    if (!r_body_append_node(body,
                            R_HIR_STANDARD_CALL,
                            call_span,
                            result_type,
                            R_SYMBOL_ID_INVALID,
                            R_TOKEN_INVALID,
                            UINT64_C(0),
                            false,
                            &children,
                            &node_id)) {
        return false;
    }
    node = r_body_hir_node(body, node_id);
    if (node == NULL) {
        return r_body_invariant(body);
    }
    node->standard_operation = operation;
    node->auxiliary_type = integer_type;
    result->node = node_id;
    result->type = result_type;
    result->valid = true;
    return true;
}

/* The value of an operation over operand bits of `width` (masked to it, a signed value by its
   bits): `first`, and for the wide operations the `second` part. A false return names the panic
   of a narrowing division whose divisor is zero or not above the high half. */
static bool r_core_bits_evaluate(RStandardCallOperation operation,
                                 uint32_t width,
                                 uint64_t a,
                                 uint64_t b,
                                 uint64_t c,
                                 uint64_t *first,
                                 uint64_t *second,
                                 const char **panic) {
    const uint64_t mask = width >= 64U ? UINT64_MAX : ((UINT64_C(1) << width) - 1U);
    uint64_t value = a & mask;
    uint64_t count = 0U;

    *second = 0U;
    switch (operation) {
    case R_STANDARD_CALL_CORE_LEADING_ZEROS:
        while ((count < width) && ((value & (UINT64_C(1) << (width - 1U - count))) == 0U)) {
            ++count;
        }
        *first = count;
        return true;
    case R_STANDARD_CALL_CORE_TRAILING_ZEROS:
        while ((count < width) && ((value & (UINT64_C(1) << count)) == 0U)) {
            ++count;
        }
        *first = count;
        return true;
    case R_STANDARD_CALL_CORE_COUNT_ONES:
        for (; value != 0U; value &= value - 1U) {
            ++count;
        }
        *first = count;
        return true;
    case R_STANDARD_CALL_CORE_SWAP_BYTES:
        for (uint32_t byte = 0U; byte < width / 8U; ++byte) {
            count |= ((value >> (8U * byte)) & 0xffU) << (width - 8U - 8U * byte);
        }
        *first = count;
        return true;
    case R_STANDARD_CALL_CORE_ROTATE_LEFT:
    case R_STANDARD_CALL_CORE_ROTATE_RIGHT: {
        const uint32_t shift = (uint32_t)((operation == R_STANDARD_CALL_CORE_ROTATE_LEFT
                                               ? (b & 0xffffffffU)
                                               : (UINT64_C(0) - (b & 0xffffffffU))) %
                                          width);
        *first = shift == 0U ? value : (((value << shift) | (value >> (width - shift))) & mask);
        return true;
    }
    case R_STANDARD_CALL_CORE_WIDENING_MUL: {
        const uint64_t right = b & mask;
        if (width < 64U) {
            const uint64_t product = value * right;
            *first = product & mask;
            *second = product >> width;
            return true;
        }
        const uint64_t a0 = value & 0xffffffffU, a1 = value >> 32U;
        const uint64_t b0 = right & 0xffffffffU, b1 = right >> 32U;
        const uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
        const uint64_t middle = (p00 >> 32U) + (p01 & 0xffffffffU) + (p10 & 0xffffffffU);
        *first = value * right;
        *second = p11 + (p01 >> 32U) + (p10 >> 32U) + (middle >> 32U);
        return true;
    }
    case R_STANDARD_CALL_CORE_CARRYING_ADD:
    case R_STANDARD_CALL_CORE_BORROWING_SUB: {
        const uint64_t right = b & mask;
        const uint64_t carry = c & 1U;
        if (operation == R_STANDARD_CALL_CORE_CARRYING_ADD) {
            const uint64_t sum = (value + right) & mask;
            const uint64_t total = (sum + carry) & mask;
            *first = total;
            *second = ((sum < value) || (total < sum)) ? 1U : 0U;
        } else {
            const uint64_t difference = (value - right) & mask;
            *first = (difference - carry) & mask;
            *second = ((value < right) || (difference < carry)) ? 1U : 0U;
        }
        return true;
    }
    case R_STANDARD_CALL_CORE_NARROWING_DIV: {
        const uint64_t low = b & mask;
        const uint64_t divisor = c & mask;
        uint64_t remainder = value;
        uint64_t quotient = 0U;
        if (divisor == 0U) {
            *panic = "division_by_zero";
            return false;
        }
        if (value >= divisor) {
            *panic = "integer_overflow";
            return false;
        }
        /* Restoring division one bit at a time: the remainder stays below the divisor, so the
           shifted remainder and its top bit fit the evaluation. */
        for (uint32_t bit = width; bit != 0U; --bit) {
            const uint64_t top = (remainder >> (width - 1U)) & 1U;
            remainder = ((remainder << 1U) | ((low >> (bit - 1U)) & 1U)) & mask;
            quotient = (quotient << 1U) & mask;
            if ((top != 0U) || (remainder >= divisor)) {
                remainder = (remainder - divisor) & mask;
                quotient |= 1U;
            }
        }
        *first = quotient;
        *second = remainder;
        return true;
    }
    default:
        *first = 0U;
        return true;
    }
}
