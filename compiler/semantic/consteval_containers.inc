/*
 * L22.3 (Core R-FUNC-0023, Library R-LIB-0019..0023, R-SLIB-STRING-0001..0003): the standard
 * owners during translation-time evaluation.
 *
 * An owner is a cell of kind R_CONST_CONTAINER whose `count` is its length and whose `bits` is
 * the number of cells reserved in `items`. An array keeps its elements in order, a string its
 * bytes as `u8` scalars, a dictionary its entries in insertion order as key and value cells, and
 * a list one reference per node to separately allocated element storage, so that linking a node
 * never moves another element (R-LIB-0023). Owners are Move-only, so a transfer shares the cells
 * and destruction releases nothing: the evaluation arena is released as a whole. Allocation
 * never fails during translation; the evaluation limits bound growth instead. Capacity is not
 * observed, since the run-time growth policy decides it.
 */

static bool r_consteval_owner_cells(
    RConstEval *eval, RConstCell *owner, uint64_t needed, uint64_t width, RSourceSpan span) {
    RConstCell *grown;
    uint64_t capacity;

    if (needed <= owner->bits) {
        return true;
    }
    capacity = owner->bits < 4U ? 4U : owner->bits;
    while (capacity < needed) {
        if (capacity > UINT64_MAX / 2U) {
            return r_consteval_fail(eval, R_CONST_FAILURE_LIMIT, span);
        }
        capacity *= 2U;
    }
    if (capacity > UINT64_MAX / width) {
        return r_consteval_fail(eval, R_CONST_FAILURE_LIMIT, span);
    }
    grown = r_consteval_cells(eval, capacity * width, span);
    if (grown == NULL) {
        return false;
    }
    if ((owner->count != 0U) && (owner->items != NULL)) {
        (void)memcpy(grown, owner->items, (size_t)(owner->count * width) * sizeof(*grown));
    }
    owner->items = grown;
    owner->bits = capacity;
    return true;
}

static void r_consteval_owner_empty(RConstCell *cell, RTypeId type) {
    (void)memset(cell, 0, sizeof(*cell));
    cell->kind = R_CONST_CONTAINER;
    cell->type = type;
}

/* The owner a borrow operand designates. */
static bool r_consteval_owner_operand(
    RConstEval *eval, const RHirNode *node, uint32_t index, RConstCell **owner, uint32_t depth) {
    RConstCell reference;

    if ((index >= node->child_count) ||
        !r_consteval_expression(
            eval, r_consteval_child(eval->context, node, index), &reference, depth + 1U)) {
        return eval->failure == R_CONST_FAILURE_NONE
                   ? r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span)
                   : false;
    }
    if ((reference.kind != R_CONST_REFERENCE) || (reference.items == NULL) ||
        ((reference.items->kind != R_CONST_CONTAINER) &&
         (reference.items->kind != R_CONST_CURSOR))) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
    }
    *owner = reference.items;
    return true;
}

static bool r_consteval_operand(
    RConstEval *eval, const RHirNode *node, uint32_t index, RConstCell *value, uint32_t depth) {
    if (index >= node->child_count) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
    }
    return r_consteval_expression(
        eval, r_consteval_child(eval->context, node, index), value, depth + 1U);
}

/* An option result: `o::some(payload)` or `o::none`. */
static bool r_consteval_option_value(RConstEval *eval,
                                     RTypeId type,
                                     const RConstCell *payload,
                                     RSourceSpan span,
                                     RConstCell *result) {
    (void)memset(result, 0, sizeof(*result));
    result->kind = R_CONST_VARIANT;
    result->type = r_semantic_value_type(eval->context, type);
    if (payload == NULL) {
        return true;
    }
    result->bits = 1U;
    result->count = 1U;
    result->items = r_consteval_cells(eval, 1U, span);
    if (result->items == NULL) {
        return false;
    }
    *result->items = *payload;
    return true;
}

static void r_consteval_reference(RConstCell *result, RTypeId type, RConstCell *target) {
    (void)memset(result, 0, sizeof(*result));
    result->kind = R_CONST_REFERENCE;
    result->type = type;
    result->items = target;
}

/* The type of the payload of an option type, such as `const T*` of `o<const T*>`. */
static RTypeId r_consteval_option_payload(const RFrontendContext *context, RTypeId option) {
    const RSemanticType *type = r_semantic_type(context, r_semantic_value_type(context, option));
    return (type != NULL) && (type->kind == R_SEMANTIC_TYPE_OPTION) ? type->base
                                                                    : R_TYPE_ID_INVALID;
}

/* ---- strings ---- */

/* The bytes of a text value, a string owner or a byte slice. */
static bool r_consteval_bytes_of(RConstEval *eval,
                                 const RConstCell *cell,
                                 const uint8_t **bytes,
                                 uint8_t **scratch,
                                 size_t *length,
                                 RSourceSpan span) {
    RFrontendContext *context = eval->context;
    uint64_t index;

    *bytes = NULL;
    *scratch = NULL;
    *length = 0U;
    if (cell->kind == R_CONST_TEXT) {
        if (cell->count == 0U) {
            return true;
        }
        if ((cell->bits == 0U) || ((size_t)cell->bits > context->intern_count) ||
            (cell->count > context->intern_entries[(size_t)cell->bits - 1U].length)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
        }
        *bytes = (const uint8_t *)context->intern_entries[(size_t)cell->bits - 1U].bytes;
        *length = (size_t)cell->count;
        return true;
    }
    if ((cell->kind != R_CONST_CONTAINER) && (cell->kind != R_CONST_SLICE) &&
        (cell->kind != R_CONST_AGGREGATE)) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
    }
    if (cell->count == 0U) {
        return true;
    }
    *scratch = r_consteval_allocate(eval, (size_t)cell->count, span);
    if (*scratch == NULL) {
        return false;
    }
    for (index = 0U; index < cell->count; ++index) {
        (*scratch)[index] = (uint8_t)cell->items[index].bits;
    }
    *bytes = *scratch;
    *length = (size_t)cell->count;
    return true;
}

static bool r_consteval_append_bytes(
    RConstEval *eval, RConstCell *owner, const uint8_t *bytes, size_t length, RSourceSpan span) {
    RTypeId byte_type;
    size_t index;

    if (!r_semantic_type_from_token(eval->context, R_TOKEN_KW_U8, &byte_type)) {
        return r_consteval_fail(eval, R_CONST_FAILURE_RESOURCE, span);
    }
    if (!r_consteval_owner_cells(eval, owner, owner->count + (uint64_t)length, 1U, span)) {
        return false;
    }
    for (index = 0U; index < length; ++index) {
        r_consteval_scalar_value(&owner->items[owner->count + index], byte_type, bytes[index]);
    }
    owner->count += (uint64_t)length;
    return true;
}

static bool r_consteval_utf8_valid(const uint8_t *bytes, size_t length, size_t *invalid) {
    size_t offset = 0U;

    while (offset < length) {
        uint32_t code_point;
        size_t width;
        if (!r_utf8_decode(bytes, length, offset, &code_point, &width)) {
            *invalid = offset;
            return false;
        }
        offset += width;
    }
    return true;
}

/* A standard error that the evaluated operation reports; its value is not observed. */
static bool r_consteval_throw_standard(RConstEval *eval, const char *name, RSourceSpan span) {
    RTypeId error_type = R_TYPE_ID_INVALID;

    if (!r_semantic_intern_named_standard_type(eval->context, name, &error_type)) {
        return r_consteval_fail(eval, R_CONST_FAILURE_RESOURCE, span);
    }
    if (eval->failure == R_CONST_FAILURE_NONE) {
        (void)memset(&eval->thrown, 0, sizeof(eval->thrown));
        eval->thrown.type = error_type;
        eval->thrown_type = error_type;
    }
    return r_consteval_fail(eval, R_CONST_FAILURE_THROW, span);
}

/* The string view of the bytes of a string owner, interned as the program strings are. */
static bool r_consteval_string_view(
    RConstEval *eval, const RConstCell *owner, RTypeId type, RSourceSpan span, RConstCell *result) {
    const uint8_t *bytes;
    uint8_t *scratch;
    size_t length;
    uint32_t intern_id = 0U;

    if (!r_consteval_bytes_of(eval, owner, &bytes, &scratch, &length, span)) {
        return false;
    }
    if (!r_intern_bytes(
            eval->context, bytes == NULL ? (const uint8_t *)"" : bytes, length, &intern_id)) {
        return r_consteval_fail(eval, R_CONST_FAILURE_RESOURCE, span);
    }
    (void)memset(result, 0, sizeof(*result));
    result->kind = R_CONST_TEXT;
    result->type = type;
    result->bits = intern_id;
    result->count = (uint64_t)length;
    return true;
}

/* A compiler-made call of std.string that a format recipe describes (format.inc). */
static bool r_consteval_string_recipe(const RFrontendContext *context,
                                      const RSemanticSymbol *function) {
    const RFormatRecipe *recipe;

    if ((function == NULL) || (function->format_recipe == 0U) ||
        ((size_t)function->format_recipe > context->format_recipe_count)) {
        return false;
    }
    recipe = &context->format_recipes[(size_t)function->format_recipe - 1U];
    return (recipe->kind == R_FORMAT_STRING_FROM_STR) || (recipe->kind == R_FORMAT_STRING_AS_STR) ||
           (recipe->kind == R_FORMAT_STRING_AS_BYTES) ||
           (recipe->kind == R_FORMAT_STRING_INTO_BYTES);
}

static bool r_consteval_recipe_call(RConstEval *eval,
                                    const RSemanticSymbol *function,
                                    RConstCell *arguments,
                                    uint32_t argument_count,
                                    RSourceSpan span,
                                    RConstCell *result) {
    RFrontendContext *context = eval->context;
    const RTypeId type = r_semantic_value_type(context, function->return_type);
    const RFormatRecipeKind kind =
        context->format_recipes[(size_t)function->format_recipe - 1U].kind;
    const uint8_t *bytes;
    uint8_t *scratch;
    size_t length;

    (void)memset(result, 0, sizeof(*result));
    if (argument_count != 1U) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
    }
    switch (kind) {
    case R_FORMAT_STRING_FROM_STR:
        eval->allocates = true;
        if (!r_consteval_bytes_of(eval, &arguments[0], &bytes, &scratch, &length, span)) {
            return false;
        }
        r_consteval_owner_empty(result, type);
        return r_consteval_append_bytes(eval, result, bytes, length, span);
    case R_FORMAT_STRING_AS_STR:
    case R_FORMAT_STRING_AS_BYTES:
        if ((arguments[0].kind != R_CONST_REFERENCE) || (arguments[0].items == NULL) ||
            (arguments[0].items->kind != R_CONST_CONTAINER)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
        }
        if (kind == R_FORMAT_STRING_AS_STR) {
            return r_consteval_string_view(eval, arguments[0].items, type, span, result);
        }
        result->kind = R_CONST_SLICE;
        result->type = type;
        result->items = arguments[0].items->items;
        result->count = arguments[0].items->count;
        return true;
    default:
        if (arguments[0].kind != R_CONST_CONTAINER) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, span);
        }
        *result = arguments[0];
        result->type = type;
        return true;
    }
}

static bool r_consteval_string_call(RConstEval *eval,
                                    const RHirNode *node,
                                    RConstCell *result,
                                    uint32_t depth) {
    RFrontendContext *context = eval->context;
    const RTypeId type = r_semantic_value_type(context, node->type);
    RConstCell *owner = NULL;
    RConstCell operand;
    const uint8_t *bytes;
    uint8_t *scratch;
    size_t length;
    size_t invalid;

    (void)memset(result, 0, sizeof(*result));
    switch (node->standard_operation) {
    case R_STANDARD_CALL_STRING_CREATE:
    case R_STANDARD_CALL_STRING_WITH_CAPACITY:
        if ((node->child_count == 1U) && !r_consteval_operand(eval, node, 0U, &operand, depth)) {
            return false;
        }
        r_consteval_owner_empty(result, type);
        return true;
    case R_STANDARD_CALL_STRING_FROM_STR:
    case R_STANDARD_CALL_STRING_FROM_UTF8:
        if (!r_consteval_operand(eval, node, 0U, &operand, depth) ||
            !r_consteval_bytes_of(eval, &operand, &bytes, &scratch, &length, node->span)) {
            return false;
        }
        if ((node->standard_operation == R_STANDARD_CALL_STRING_FROM_UTF8) &&
            !r_consteval_utf8_valid(bytes, length, &invalid)) {
            return r_consteval_throw_standard(eval, "std.string::string_error", node->span);
        }
        r_consteval_owner_empty(result, type);
        return r_consteval_append_bytes(eval, result, bytes, length, node->span);
    case R_STANDARD_CALL_STRING_LEN:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth)) {
            return false;
        }
        r_consteval_scalar_value(result, type, owner->count);
        return true;
    case R_STANDARD_CALL_STRING_AS_STR:
        return r_consteval_owner_operand(eval, node, 0U, &owner, depth) &&
               r_consteval_string_view(eval, owner, type, node->span, result);
    case R_STANDARD_CALL_STRING_AS_BYTES:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth)) {
            return false;
        }
        result->kind = R_CONST_SLICE;
        result->type = type;
        result->items = owner->items;
        result->count = owner->count;
        return true;
    case R_STANDARD_CALL_STRING_INTO_BYTES:
        if (!r_consteval_operand(eval, node, 0U, &operand, depth) ||
            (operand.kind != R_CONST_CONTAINER)) {
            return eval->failure == R_CONST_FAILURE_NONE
                       ? r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span)
                       : false;
        }
        *result = operand;
        result->type = type;
        return true;
    case R_STANDARD_CALL_STRING_CLEAR:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth)) {
            return false;
        }
        owner->count = 0U;
        return true;
    case R_STANDARD_CALL_STRING_RESERVE:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth) ||
            !r_consteval_operand(eval, node, 1U, &operand, depth)) {
            return false;
        }
        if (operand.bits > UINT64_MAX - owner->count) {
            return r_consteval_fail(eval, R_CONST_FAILURE_LIMIT, node->span);
        }
        return r_consteval_owner_cells(eval, owner, owner->count + operand.bits, 1U, node->span);
    case R_STANDARD_CALL_STRING_APPEND_STR:
    case R_STANDARD_CALL_STRING_APPEND_UTF8:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth) ||
            !r_consteval_operand(eval, node, 1U, &operand, depth) ||
            !r_consteval_bytes_of(eval, &operand, &bytes, &scratch, &length, node->span)) {
            return false;
        }
        if ((node->standard_operation == R_STANDARD_CALL_STRING_APPEND_UTF8) &&
            !r_consteval_utf8_valid(bytes, length, &invalid)) {
            return r_consteval_throw_standard(eval, "std.string::string_error", node->span);
        }
        return r_consteval_append_bytes(eval, owner, bytes, length, node->span);
    case R_STANDARD_CALL_STRING_PUSH_SCALAR: {
        uint8_t encoded[4];
        size_t width;
        uint64_t scalar;
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth) ||
            !r_consteval_operand(eval, node, 1U, &operand, depth)) {
            return false;
        }
        scalar = operand.bits;
        if (scalar < 0x80U) {
            encoded[0] = (uint8_t)scalar;
            width = 1U;
        } else if (scalar < 0x800U) {
            encoded[0] = (uint8_t)(0xC0U | (scalar >> 6U));
            encoded[1] = (uint8_t)(0x80U | (scalar & 0x3FU));
            width = 2U;
        } else if (scalar < 0x10000U) {
            encoded[0] = (uint8_t)(0xE0U | (scalar >> 12U));
            encoded[1] = (uint8_t)(0x80U | ((scalar >> 6U) & 0x3FU));
            encoded[2] = (uint8_t)(0x80U | (scalar & 0x3FU));
            width = 3U;
        } else {
            encoded[0] = (uint8_t)(0xF0U | (scalar >> 18U));
            encoded[1] = (uint8_t)(0x80U | ((scalar >> 12U) & 0x3FU));
            encoded[2] = (uint8_t)(0x80U | ((scalar >> 6U) & 0x3FU));
            encoded[3] = (uint8_t)(0x80U | (scalar & 0x3FU));
            width = 4U;
        }
        return r_consteval_append_bytes(eval, owner, encoded, width, node->span);
    }
    case R_STANDARD_CALL_STRING_TRUNCATE:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth) ||
            !r_consteval_operand(eval, node, 1U, &operand, depth)) {
            return false;
        }
        /* R-SLIB-STRING-0003: a length past the end or inside a scalar is rejected. */
        if ((operand.bits > owner->count) ||
            ((operand.bits < owner->count) &&
             ((owner->items[operand.bits].bits & 0xC0U) == 0x80U))) {
            return r_consteval_throw_standard(eval, "std.string::boundary_error", node->span);
        }
        owner->count = operand.bits;
        return true;
    default:
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
    }
}

/* ---- arrays ---- */

static bool
r_consteval_array_call(RConstEval *eval, const RHirNode *node, RConstCell *result, uint32_t depth) {
    RFrontendContext *context = eval->context;
    const RTypeId type = r_semantic_value_type(context, node->type);
    RConstCell *owner = NULL;
    RConstCell operand;
    RConstCell element;
    uint64_t index;

    (void)memset(result, 0, sizeof(*result));
    switch (node->standard_operation) {
    case R_STANDARD_CALL_ARRAY_CREATE:
    case R_STANDARD_CALL_ARRAY_WITH_CAPACITY:
        if ((node->child_count == 1U) && !r_consteval_operand(eval, node, 0U, &operand, depth)) {
            return false;
        }
        r_consteval_owner_empty(result, type);
        return true;
    case R_STANDARD_CALL_ARRAY_FILLED:
        /* R-LIB-0019 (P4.2): length copies of one Copy value. */
        if (!r_consteval_operand(eval, node, 0U, &operand, depth) ||
            !r_consteval_operand(eval, node, 1U, &element, depth)) {
            return false;
        }
        r_consteval_owner_empty(result, type);
        if (!r_consteval_owner_cells(eval, result, operand.bits, 1U, node->span)) {
            return false;
        }
        for (index = 0U; index < operand.bits; ++index) {
            (void)memset(&result->items[index], 0, sizeof(*result->items));
            if (!r_consteval_store(eval, &result->items[index], &element, node->span)) {
                return false;
            }
            result->count = index + 1U;
        }
        return true;
    case R_STANDARD_CALL_ARRAY_RESERVE:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth) ||
            !r_consteval_operand(eval, node, 1U, &operand, depth)) {
            return false;
        }
        if (operand.bits > UINT64_MAX - owner->count) {
            return r_consteval_fail(eval, R_CONST_FAILURE_LIMIT, node->span);
        }
        return r_consteval_owner_cells(eval, owner, owner->count + operand.bits, 1U, node->span);
    case R_STANDARD_CALL_ARRAY_PUSH:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth) ||
            !r_consteval_operand(eval, node, 1U, &element, depth) ||
            !r_consteval_owner_cells(eval, owner, owner->count + 1U, 1U, node->span)) {
            return false;
        }
        (void)memset(&owner->items[owner->count], 0, sizeof(*owner->items));
        if (!r_consteval_store(eval, &owner->items[owner->count], &element, node->span)) {
            return false;
        }
        owner->count += 1U;
        return true;
    case R_STANDARD_CALL_ARRAY_POP:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth)) {
            return false;
        }
        if (owner->count == 0U) {
            return r_consteval_option_value(eval, node->type, NULL, node->span, result);
        }
        owner->count -= 1U;
        return r_consteval_option_value(
            eval, node->type, &owner->items[owner->count], node->span, result);
    case R_STANDARD_CALL_ARRAY_REMOVE:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth) ||
            !r_consteval_operand(eval, node, 1U, &operand, depth)) {
            return false;
        }
        if (operand.bits >= owner->count) {
            return r_consteval_option_value(eval, node->type, NULL, node->span, result);
        }
        element = owner->items[operand.bits];
        for (index = operand.bits; index + 1U < owner->count; ++index) {
            owner->items[index] = owner->items[index + 1U];
        }
        owner->count -= 1U;
        return r_consteval_option_value(eval, node->type, &element, node->span, result);
    case R_STANDARD_CALL_ARRAY_GET:
    case R_STANDARD_CALL_ARRAY_GET_MUT:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth) ||
            !r_consteval_operand(eval, node, 1U, &operand, depth)) {
            return false;
        }
        if (operand.bits >= owner->count) {
            return r_consteval_option_value(eval, node->type, NULL, node->span, result);
        }
        r_consteval_reference(
            &element, r_consteval_option_payload(context, node->type), &owner->items[operand.bits]);
        return r_consteval_option_value(eval, node->type, &element, node->span, result);
    case R_STANDARD_CALL_ARRAY_CLEAR:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth)) {
            return false;
        }
        owner->count = 0U;
        return true;
    default:
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
    }
}

/* ---- dictionaries ---- */

/* Key equality of R-LIB-0020 for the key types evaluated during translation. */
static bool r_consteval_key_equal(RConstEval *eval,
                                  const RConstCell *left,
                                  const RConstCell *right,
                                  bool *equal,
                                  RSourceSpan span) {
    const RTypeId type = r_semantic_value_type(eval->context, left->type);
    const uint32_t width = r_consteval_float_width(eval->context, type);
    const uint8_t *left_bytes;
    const uint8_t *right_bytes;
    uint8_t *left_scratch;
    uint8_t *right_scratch;
    size_t left_length;
    size_t right_length;

    if ((left->kind == R_CONST_SCALAR) && (right->kind == R_CONST_SCALAR)) {
        if (width != 0U) {
            const double a = r_consteval_float_value(width, left->bits);
            const double b = r_consteval_float_value(width, right->bits);
            *equal = (a == b) || ((a != a) && (b != b));
        } else {
            *equal = left->bits == right->bits;
        }
        return true;
    }
    if (!r_consteval_bytes_of(eval, left, &left_bytes, &left_scratch, &left_length, span) ||
        !r_consteval_bytes_of(eval, right, &right_bytes, &right_scratch, &right_length, span)) {
        return false;
    }
    *equal = (left_length == right_length) &&
             ((left_length == 0U) || (memcmp(left_bytes, right_bytes, left_length) == 0));
    return true;
}

static bool r_consteval_dict_find(RConstEval *eval,
                                  const RConstCell *dict,
                                  const RConstCell *key,
                                  uint64_t *position,
                                  bool *found,
                                  RSourceSpan span) {
    uint64_t index;

    *found = false;
    for (index = 0U; index < dict->count; ++index) {
        if (!r_consteval_step(eval, span) ||
            !r_consteval_key_equal(eval, &dict->items[2U * index], key, found, span)) {
            return false;
        }
        if (*found) {
            *position = index;
            return true;
        }
    }
    return true;
}

/* The key a lookup names through its `const K*` operand. */
static bool r_consteval_key_operand(RConstEval *eval,
                                    const RHirNode *node,
                                    uint32_t index,
                                    const RConstCell **key,
                                    uint32_t depth) {
    RConstCell reference;

    if (!r_consteval_operand(eval, node, index, &reference, depth)) {
        return false;
    }
    if ((reference.kind != R_CONST_REFERENCE) || (reference.items == NULL)) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
    }
    *key = reference.items;
    return true;
}

static bool
r_consteval_dict_call(RConstEval *eval, const RHirNode *node, RConstCell *result, uint32_t depth) {
    RFrontendContext *context = eval->context;
    const RTypeId type = r_semantic_value_type(context, node->type);
    RConstCell *owner = NULL;
    const RConstCell *key = NULL;
    RConstCell operand;
    RConstCell value;
    RConstCell element;
    uint64_t position = 0U;
    uint64_t index;
    bool found = false;

    (void)memset(result, 0, sizeof(*result));
    switch (node->standard_operation) {
    case R_STANDARD_CALL_DICT_CREATE:
    case R_STANDARD_CALL_DICT_WITH_CAPACITY:
        if ((node->child_count == 1U) && !r_consteval_operand(eval, node, 0U, &operand, depth)) {
            return false;
        }
        r_consteval_owner_empty(result, type);
        return true;
    case R_STANDARD_CALL_DICT_RESERVE:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth) ||
            !r_consteval_operand(eval, node, 1U, &operand, depth)) {
            return false;
        }
        if (operand.bits > UINT64_MAX / 2U - owner->count) {
            return r_consteval_fail(eval, R_CONST_FAILURE_LIMIT, node->span);
        }
        return r_consteval_owner_cells(eval, owner, owner->count + operand.bits, 2U, node->span);
    case R_STANDARD_CALL_DICT_INSERT:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth) ||
            !r_consteval_operand(eval, node, 1U, &operand, depth) ||
            !r_consteval_operand(eval, node, 2U, &value, depth) ||
            !r_consteval_dict_find(eval, owner, &operand, &position, &found, node->span)) {
            return false;
        }
        if (found) {
            /* R-LIB-0020: the stored key and its position stay; the old value returns. */
            element = owner->items[2U * position + 1U];
            owner->items[2U * position + 1U] = value;
            return r_consteval_option_value(eval, node->type, &element, node->span, result);
        }
        if (!r_consteval_owner_cells(eval, owner, owner->count + 1U, 2U, node->span)) {
            return false;
        }
        owner->items[2U * owner->count] = operand;
        owner->items[2U * owner->count + 1U] = value;
        owner->count += 1U;
        return r_consteval_option_value(eval, node->type, NULL, node->span, result);
    case R_STANDARD_CALL_DICT_CONTAINS:
    case R_STANDARD_CALL_DICT_GET:
    case R_STANDARD_CALL_DICT_GET_MUT:
    case R_STANDARD_CALL_DICT_REMOVE:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth) ||
            !r_consteval_key_operand(eval, node, 1U, &key, depth) ||
            !r_consteval_dict_find(eval, owner, key, &position, &found, node->span)) {
            return false;
        }
        if (node->standard_operation == R_STANDARD_CALL_DICT_CONTAINS) {
            r_consteval_scalar_value(result, type, found ? 1U : 0U);
            return true;
        }
        if (!found) {
            return r_consteval_option_value(eval, node->type, NULL, node->span, result);
        }
        if (node->standard_operation != R_STANDARD_CALL_DICT_REMOVE) {
            r_consteval_reference(&element,
                                  r_consteval_option_payload(context, node->type),
                                  &owner->items[2U * position + 1U]);
            return r_consteval_option_value(eval, node->type, &element, node->span, result);
        }
        /* Removal keeps the insertion order of the remaining entries. */
        element = owner->items[2U * position + 1U];
        for (index = position; index + 1U < owner->count; ++index) {
            owner->items[2U * index] = owner->items[2U * index + 2U];
            owner->items[2U * index + 1U] = owner->items[2U * index + 3U];
        }
        owner->count -= 1U;
        return r_consteval_option_value(eval, node->type, &element, node->span, result);
    case R_STANDARD_CALL_DICT_CLEAR:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth)) {
            return false;
        }
        owner->count = 0U;
        return true;
    case R_STANDARD_CALL_DICT_ITER:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth)) {
            return false;
        }
        result->kind = R_CONST_CURSOR;
        result->type = type;
        result->items = owner;
        return true;
    case R_STANDARD_CALL_DICT_NEXT: {
        RConstCell *cursor = NULL;
        RConstCell *pair;
        if (!r_consteval_owner_operand(eval, node, 0U, &cursor, depth) ||
            (cursor->kind != R_CONST_CURSOR) || (cursor->items == NULL)) {
            return eval->failure == R_CONST_FAILURE_NONE
                       ? r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span)
                       : false;
        }
        owner = cursor->items;
        if (cursor->bits >= owner->count) {
            return r_consteval_option_value(eval, node->type, NULL, node->span, result);
        }
        /* std.dict::entry_ref: a Copy value of a key borrow and a value borrow. */
        pair = r_consteval_cells(eval, 2U, node->span);
        if (pair == NULL) {
            return false;
        }
        r_consteval_reference(&pair[0], R_TYPE_ID_INVALID, &owner->items[2U * cursor->bits]);
        r_consteval_reference(&pair[1], R_TYPE_ID_INVALID, &owner->items[2U * cursor->bits + 1U]);
        cursor->bits += 1U;
        (void)memset(&element, 0, sizeof(element));
        element.kind = R_CONST_AGGREGATE;
        element.type = r_consteval_option_payload(context, node->type);
        element.count = 2U;
        element.items = pair;
        return r_consteval_option_value(eval, node->type, &element, node->span, result);
    }
    default:
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
    }
}

/* ---- lists ---- */

/* A node of a list is a reference to storage of its own (R-LIB-0023). */
static bool r_consteval_list_link(RConstEval *eval,
                                  RConstCell *owner,
                                  uint64_t position,
                                  const RConstCell *value,
                                  RTypeId element_type,
                                  RSourceSpan span,
                                  RConstCell *borrow) {
    RConstCell *storage = r_consteval_cells(eval, 1U, span);
    uint64_t index;

    if ((storage == NULL) || !r_consteval_owner_cells(eval, owner, owner->count + 1U, 1U, span)) {
        return false;
    }
    *storage = *value;
    for (index = owner->count; index > position; --index) {
        owner->items[index] = owner->items[index - 1U];
    }
    r_consteval_reference(&owner->items[position], R_TYPE_ID_INVALID, storage);
    owner->count += 1U;
    r_consteval_reference(borrow, element_type, storage);
    return true;
}

static bool
r_consteval_list_position(const RConstCell *owner, const RConstCell *element, uint64_t *position) {
    uint64_t index;

    for (index = 0U; index < owner->count; ++index) {
        if (owner->items[index].items == element) {
            *position = index;
            return true;
        }
    }
    return false;
}

static bool
r_consteval_list_call(RConstEval *eval, const RHirNode *node, RConstCell *result, uint32_t depth) {
    RFrontendContext *context = eval->context;
    const RTypeId type = r_semantic_value_type(context, node->type);
    RConstCell *owner = NULL;
    RConstCell operand;
    RConstCell value;
    RConstCell element;
    uint64_t position = 0U;
    uint64_t index;

    (void)memset(result, 0, sizeof(*result));
    switch (node->standard_operation) {
    case R_STANDARD_CALL_LIST_CREATE:
        r_consteval_owner_empty(result, type);
        return true;
    case R_STANDARD_CALL_LIST_PUSH_FRONT:
    case R_STANDARD_CALL_LIST_PUSH_BACK:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth) ||
            !r_consteval_operand(eval, node, 1U, &value, depth)) {
            return false;
        }
        return r_consteval_list_link(
            eval,
            owner,
            node->standard_operation == R_STANDARD_CALL_LIST_PUSH_FRONT ? 0U : owner->count,
            &value,
            type,
            node->span,
            result);
    case R_STANDARD_CALL_LIST_INSERT_BEFORE:
    case R_STANDARD_CALL_LIST_INSERT_AFTER:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth) ||
            !r_consteval_operand(eval, node, 1U, &operand, depth) ||
            !r_consteval_operand(eval, node, 2U, &value, depth)) {
            return false;
        }
        if ((operand.kind != R_CONST_REFERENCE) ||
            !r_consteval_list_position(owner, operand.items, &position)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
        }
        return r_consteval_list_link(eval,
                                     owner,
                                     node->standard_operation == R_STANDARD_CALL_LIST_INSERT_AFTER
                                         ? position + 1U
                                         : position,
                                     &value,
                                     type,
                                     node->span,
                                     result);
    case R_STANDARD_CALL_LIST_FRONT:
    case R_STANDARD_CALL_LIST_BACK:
    case R_STANDARD_CALL_LIST_FRONT_MUT:
    case R_STANDARD_CALL_LIST_BACK_MUT:
    case R_STANDARD_CALL_LIST_GET:
    case R_STANDARD_CALL_LIST_GET_MUT:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth)) {
            return false;
        }
        if ((node->standard_operation == R_STANDARD_CALL_LIST_GET) ||
            (node->standard_operation == R_STANDARD_CALL_LIST_GET_MUT)) {
            if (!r_consteval_operand(eval, node, 1U, &operand, depth)) {
                return false;
            }
            position = operand.bits;
        } else if ((node->standard_operation == R_STANDARD_CALL_LIST_BACK) ||
                   (node->standard_operation == R_STANDARD_CALL_LIST_BACK_MUT)) {
            position = owner->count == 0U ? 0U : owner->count - 1U;
        }
        if (position >= owner->count) {
            return r_consteval_option_value(eval, node->type, NULL, node->span, result);
        }
        r_consteval_reference(&element,
                              r_consteval_option_payload(context, node->type),
                              owner->items[position].items);
        return r_consteval_option_value(eval, node->type, &element, node->span, result);
    case R_STANDARD_CALL_LIST_REMOVE:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth) ||
            !r_consteval_operand(eval, node, 1U, &operand, depth)) {
            return false;
        }
        if ((operand.kind != R_CONST_REFERENCE) ||
            !r_consteval_list_position(owner, operand.items, &position)) {
            return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
        }
        *result = *owner->items[position].items;
        for (index = position; index + 1U < owner->count; ++index) {
            owner->items[index] = owner->items[index + 1U];
        }
        owner->count -= 1U;
        return true;
    case R_STANDARD_CALL_LIST_POP_FRONT:
    case R_STANDARD_CALL_LIST_POP_BACK:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth)) {
            return false;
        }
        if (owner->count == 0U) {
            return r_consteval_option_value(eval, node->type, NULL, node->span, result);
        }
        position =
            node->standard_operation == R_STANDARD_CALL_LIST_POP_FRONT ? 0U : owner->count - 1U;
        element = *owner->items[position].items;
        for (index = position; index + 1U < owner->count; ++index) {
            owner->items[index] = owner->items[index + 1U];
        }
        owner->count -= 1U;
        return r_consteval_option_value(eval, node->type, &element, node->span, result);
    case R_STANDARD_CALL_LIST_CLEAR:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth)) {
            return false;
        }
        owner->count = 0U;
        return true;
    case R_STANDARD_CALL_LIST_ITER:
        if (!r_consteval_owner_operand(eval, node, 0U, &owner, depth)) {
            return false;
        }
        result->kind = R_CONST_CURSOR;
        result->type = type;
        result->items = owner;
        return true;
    case R_STANDARD_CALL_LIST_NEXT: {
        RConstCell *cursor = NULL;
        if (!r_consteval_owner_operand(eval, node, 0U, &cursor, depth) ||
            (cursor->kind != R_CONST_CURSOR) || (cursor->items == NULL)) {
            return eval->failure == R_CONST_FAILURE_NONE
                       ? r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span)
                       : false;
        }
        owner = cursor->items;
        if (cursor->bits >= owner->count) {
            return r_consteval_option_value(eval, node->type, NULL, node->span, result);
        }
        r_consteval_reference(&element,
                              r_consteval_option_payload(context, node->type),
                              owner->items[cursor->bits].items);
        cursor->bits += 1U;
        return r_consteval_option_value(eval, node->type, &element, node->span, result);
    }
    default:
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
    }
}

/* The standard owner operations evaluated during translation; capacity is not observed. */
static bool r_consteval_owner_operation(RStandardCallOperation operation) {
    switch (operation) {
    case R_STANDARD_CALL_STRING_CREATE:
    case R_STANDARD_CALL_STRING_WITH_CAPACITY:
    case R_STANDARD_CALL_STRING_FROM_STR:
    case R_STANDARD_CALL_STRING_FROM_UTF8:
    case R_STANDARD_CALL_STRING_LEN:
    case R_STANDARD_CALL_STRING_AS_STR:
    case R_STANDARD_CALL_STRING_AS_BYTES:
    case R_STANDARD_CALL_STRING_INTO_BYTES:
    case R_STANDARD_CALL_STRING_CLEAR:
    case R_STANDARD_CALL_STRING_RESERVE:
    case R_STANDARD_CALL_STRING_APPEND_STR:
    case R_STANDARD_CALL_STRING_APPEND_UTF8:
    case R_STANDARD_CALL_STRING_PUSH_SCALAR:
    case R_STANDARD_CALL_STRING_TRUNCATE:
    case R_STANDARD_CALL_ARRAY_CREATE:
    case R_STANDARD_CALL_ARRAY_WITH_CAPACITY:
    case R_STANDARD_CALL_ARRAY_FILLED:
    case R_STANDARD_CALL_ARRAY_RESERVE:
    case R_STANDARD_CALL_ARRAY_PUSH:
    case R_STANDARD_CALL_ARRAY_POP:
    case R_STANDARD_CALL_ARRAY_REMOVE:
    case R_STANDARD_CALL_ARRAY_GET:
    case R_STANDARD_CALL_ARRAY_GET_MUT:
    case R_STANDARD_CALL_ARRAY_CLEAR:
    case R_STANDARD_CALL_DICT_CREATE:
    case R_STANDARD_CALL_DICT_WITH_CAPACITY:
    case R_STANDARD_CALL_DICT_RESERVE:
    case R_STANDARD_CALL_DICT_INSERT:
    case R_STANDARD_CALL_DICT_CONTAINS:
    case R_STANDARD_CALL_DICT_GET:
    case R_STANDARD_CALL_DICT_GET_MUT:
    case R_STANDARD_CALL_DICT_REMOVE:
    case R_STANDARD_CALL_DICT_CLEAR:
    case R_STANDARD_CALL_DICT_ITER:
    case R_STANDARD_CALL_DICT_NEXT:
    case R_STANDARD_CALL_LIST_CREATE:
    case R_STANDARD_CALL_LIST_PUSH_FRONT:
    case R_STANDARD_CALL_LIST_PUSH_BACK:
    case R_STANDARD_CALL_LIST_INSERT_BEFORE:
    case R_STANDARD_CALL_LIST_INSERT_AFTER:
    case R_STANDARD_CALL_LIST_FRONT:
    case R_STANDARD_CALL_LIST_BACK:
    case R_STANDARD_CALL_LIST_FRONT_MUT:
    case R_STANDARD_CALL_LIST_BACK_MUT:
    case R_STANDARD_CALL_LIST_GET:
    case R_STANDARD_CALL_LIST_GET_MUT:
    case R_STANDARD_CALL_LIST_REMOVE:
    case R_STANDARD_CALL_LIST_POP_FRONT:
    case R_STANDARD_CALL_LIST_POP_BACK:
    case R_STANDARD_CALL_LIST_CLEAR:
    case R_STANDARD_CALL_LIST_ITER:
    case R_STANDARD_CALL_LIST_NEXT:
        return true;
    default:
        return false;
    }
}

/* The operations that only observe an owner, which a frozen constant also supports. */
static bool r_consteval_owner_observes(RStandardCallOperation operation) {
    switch (operation) {
    case R_STANDARD_CALL_STRING_LEN:
    case R_STANDARD_CALL_STRING_AS_STR:
    case R_STANDARD_CALL_STRING_AS_BYTES:
    case R_STANDARD_CALL_ARRAY_GET:
    case R_STANDARD_CALL_DICT_CONTAINS:
    case R_STANDARD_CALL_DICT_GET:
    case R_STANDARD_CALL_DICT_ITER:
    case R_STANDARD_CALL_DICT_NEXT:
    case R_STANDARD_CALL_LIST_FRONT:
    case R_STANDARD_CALL_LIST_BACK:
    case R_STANDARD_CALL_LIST_GET:
    case R_STANDARD_CALL_LIST_ITER:
    case R_STANDARD_CALL_LIST_NEXT:
        return true;
    default:
        return false;
    }
}

static bool
r_consteval_owner_call(RConstEval *eval, const RHirNode *node, RConstCell *result, uint32_t depth) {
    if (!r_consteval_owner_observes(node->standard_operation)) {
        eval->allocates = true;
    }
    switch (node->standard_operation) {
    case R_STANDARD_CALL_STRING_CREATE:
    case R_STANDARD_CALL_STRING_WITH_CAPACITY:
    case R_STANDARD_CALL_STRING_FROM_STR:
    case R_STANDARD_CALL_STRING_FROM_UTF8:
    case R_STANDARD_CALL_STRING_LEN:
    case R_STANDARD_CALL_STRING_AS_STR:
    case R_STANDARD_CALL_STRING_AS_BYTES:
    case R_STANDARD_CALL_STRING_INTO_BYTES:
    case R_STANDARD_CALL_STRING_CLEAR:
    case R_STANDARD_CALL_STRING_RESERVE:
    case R_STANDARD_CALL_STRING_APPEND_STR:
    case R_STANDARD_CALL_STRING_APPEND_UTF8:
    case R_STANDARD_CALL_STRING_PUSH_SCALAR:
    case R_STANDARD_CALL_STRING_TRUNCATE:
        return r_consteval_string_call(eval, node, result, depth);
    case R_STANDARD_CALL_ARRAY_CREATE:
    case R_STANDARD_CALL_ARRAY_WITH_CAPACITY:
    case R_STANDARD_CALL_ARRAY_FILLED:
    case R_STANDARD_CALL_ARRAY_RESERVE:
    case R_STANDARD_CALL_ARRAY_PUSH:
    case R_STANDARD_CALL_ARRAY_POP:
    case R_STANDARD_CALL_ARRAY_REMOVE:
    case R_STANDARD_CALL_ARRAY_GET:
    case R_STANDARD_CALL_ARRAY_GET_MUT:
    case R_STANDARD_CALL_ARRAY_CLEAR:
        return r_consteval_array_call(eval, node, result, depth);
    case R_STANDARD_CALL_DICT_CREATE:
    case R_STANDARD_CALL_DICT_WITH_CAPACITY:
    case R_STANDARD_CALL_DICT_RESERVE:
    case R_STANDARD_CALL_DICT_INSERT:
    case R_STANDARD_CALL_DICT_CONTAINS:
    case R_STANDARD_CALL_DICT_GET:
    case R_STANDARD_CALL_DICT_GET_MUT:
    case R_STANDARD_CALL_DICT_REMOVE:
    case R_STANDARD_CALL_DICT_CLEAR:
    case R_STANDARD_CALL_DICT_ITER:
    case R_STANDARD_CALL_DICT_NEXT:
        return r_consteval_dict_call(eval, node, result, depth);
    default:
        return r_consteval_list_call(eval, node, result, depth);
    }
}

/* L22.4: the owner a frozen initializer holds, read back for further evaluation. */
static bool
r_consteval_frozen(RConstEval *eval, const RHirNode *node, RConstCell *result, uint32_t depth) {
    RFrontendContext *context = eval->context;
    const RTypeId type = r_semantic_value_type(context, node->type);
    const RConstevalOwner owner = r_consteval_owner(context, type);
    const uint64_t width = owner == R_CONSTEVAL_OWNER_DICT ? 2U : 1U;
    RConstCell element;
    uint32_t index;

    r_consteval_owner_empty(result, type);
    if (owner == R_CONSTEVAL_OWNER_STRING) {
        const uint8_t *bytes;
        uint8_t *scratch;
        size_t length;
        return (node->child_count == 1U) && r_consteval_operand(eval, node, 0U, &element, depth) &&
               r_consteval_bytes_of(eval, &element, &bytes, &scratch, &length, node->span) &&
               r_consteval_append_bytes(eval, result, bytes, length, node->span);
    }
    if (((uint64_t)node->child_count % width) != 0U) {
        return r_consteval_fail(eval, R_CONST_FAILURE_UNSUPPORTED, node->span);
    }
    for (index = 0U; index < node->child_count; ++index) {
        if (!r_consteval_operand(eval, node, index, &element, depth)) {
            return false;
        }
        if (owner == R_CONSTEVAL_OWNER_LIST) {
            RConstCell borrow;
            if (!r_consteval_list_link(eval,
                                       result,
                                       result->count,
                                       &element,
                                       R_TYPE_ID_INVALID,
                                       node->span,
                                       &borrow)) {
                return false;
            }
            continue;
        }
        if (!r_consteval_owner_cells(eval, result, index / width + 1U, width, node->span)) {
            return false;
        }
        result->items[index] = element;
        if ((index % width) == width - 1U) {
            result->count += 1U;
        }
    }
    return true;
}
