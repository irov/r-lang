/* R-TYPE-0046 (L32): format glue. A formatted value whose text the helpers do not append by
 * themselves reaches one generated function per formatted type,
 *
 *     static RStdFormatAllocResult r_type_format_<id>(const void *value, RStdFormatBuilder *out);
 *
 * which appends the text of the value: a nominal type calls its core::Format implementation, and
 * standard formatting composes the glue of the components (`some(v)` or `none`, `[a, b]`,
 * `(a, b)`) with the texts of scalars, strings and network addresses. On failure it reports the
 * allocation error, and the builder keeps the text appended before it. The glue is spliced in
 * after the function prototypes, like the clone glue, because an implementation is an ordinary R
 * function. */

static bool r_c17_emit_format_glue_name(RC17Emitter *emitter, RTypeId type_id) {
    return r_c17_format(emitter, "r_type_format_%08" PRIu32, r_c17_value_type(emitter, type_id));
}

static bool r_c17_format_standard_named(const RC17Emitter *emitter,
                                        const RSemanticType *type,
                                        const char *name) {
    return r_c17_standard_type_named(emitter, type, name);
}

/* Marks `type_id` and each component whose text needs its own function, and preflights the
 * implementations the glue calls. */
static bool r_c17_require_format_glue(RC17Emitter *emitter, RTypeId type_id, uint32_t depth) {
    const RTypeId value_id = r_c17_value_type(emitter, type_id);
    const RSemanticType *type = r_c17_type(emitter, value_id);
    if (type == NULL || depth > emitter->frontend->options.limits.max_nesting ||
        (size_t)value_id > emitter->frontend->semantic_type_count)
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    if (emitter->format_glue_types[value_id] != 0U)
        return true;
    emitter->format_glue_types[value_id] = 1U;
    emitter->uses_format_glue = true;
    emitter->uses_std_format = true;
    if (!r_c17_preflight_type_dependency(emitter, value_id, 0U))
        return false;
    if (type->kind >= R_SEMANTIC_TYPE_I8 && type->kind <= R_SEMANTIC_TYPE_USIZE)
        return true;
    switch (type->kind) {
    case R_SEMANTIC_TYPE_BOOL:
    case R_SEMANTIC_TYPE_CHAR:
    case R_SEMANTIC_TYPE_STR:
    case R_SEMANTIC_TYPE_CONSTEXPR_STR:
    case R_SEMANTIC_TYPE_F32:
    case R_SEMANTIC_TYPE_F64:
    case R_SEMANTIC_TYPE_C_ABI_NUMERIC:
        return true;
    case R_SEMANTIC_TYPE_OPTION:
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
    case R_SEMANTIC_TYPE_SLICE:
        return r_c17_require_format_glue(emitter, type->base, depth + 1U);
    case R_SEMANTIC_TYPE_ARRAY:
        emitter->uses_runtime_array = true;
        return r_c17_require_format_glue(emitter, type->base, depth + 1U);
    case R_SEMANTIC_TYPE_STANDARD:
        return r_c17_format_standard_named(emitter, type, "std.string::string") ||
                       r_c17_format_standard_named(emitter, type, "std.net::ip_address") ||
                       r_c17_format_standard_named(emitter, type, "std.net::socket_address")
                   ? true
                   : r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM: {
        const RSemanticAggregate *aggregate = r_c17_clone_aggregate(emitter, value_id);
        if (aggregate == NULL)
            return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
        if (!aggregate->is_tuple)
            return aggregate->format_function != R_SYMBOL_ID_INVALID
                       ? r_c17_preflight_function(emitter, aggregate->format_function)
                       : r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
        for (uint32_t i = 0U; i < aggregate->field_count; ++i)
            if (!r_c17_require_format_glue(
                    emitter,
                    emitter->frontend->semantic_fields[aggregate->first_field + i].type,
                    depth + 1U))
                return false;
        return true;
    }
    default:
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    }
}

static bool r_c17_emit_format_header(RC17Emitter *emitter, RTypeId type_id, bool typed) {
    if (!r_c17_write(emitter, "static RStdFormatAllocResult ") ||
        !r_c17_emit_format_glue_name(emitter, type_id) ||
        !r_c17_write(emitter, "(const void *value_pointer, RStdFormatBuilder *out) {\n"))
        return false;
    return !typed || (r_c17_write(emitter, "    const ") &&
                      r_c17_emit_value_type(emitter, type_id) &&
                      r_c17_write(emitter, " *value = value_pointer;\n"));
}

/* `result = append(text)` and an early return when the append failed. */
static bool r_c17_emit_format_text_step(RC17Emitter *emitter, const char *text) {
    return r_c17_format(emitter,
                        "    result = r_std_format_append_str(out, (RStdStringView){(const uint8_t "
                        "*)\"%s\", %zuU});\n"
                        "    if (result.status != R_STD_FORMAT_CALL_SUCCESS) {\n"
                        "        return result;\n"
                        "    }\n",
                        text,
                        strlen(text));
}

static bool r_c17_emit_format_component_step(RC17Emitter *emitter,
                                             RTypeId type_id,
                                             const char *place) {
    return r_c17_write(emitter, "    result = ") && r_c17_emit_format_glue_name(emitter, type_id) &&
           r_c17_format(emitter,
                        "(&%s, out);\n"
                        "    if (result.status != R_STD_FORMAT_CALL_SUCCESS) {\n"
                        "        return result;\n"
                        "    }\n",
                        place);
}

static bool r_c17_emit_format_closing(RC17Emitter *emitter, const char *text) {
    return r_c17_format(emitter,
                        "    return r_std_format_append_str(out, (RStdStringView){(const uint8_t "
                        "*)\"%s\", %zuU});\n"
                        "}\n"
                        "\n",
                        text,
                        strlen(text));
}

/* `[a, b]` over `count` elements at `element` (C expressions over `index`). */
static bool r_c17_emit_format_sequence_body(RC17Emitter *emitter,
                                            RTypeId element_type,
                                            const char *count,
                                            const char *element) {
    return r_c17_write(emitter,
                       "    RStdFormatAllocResult result =\n"
                       "        r_std_format_append_str(out, (RStdStringView){(const uint8_t "
                       "*)\"[\", 1U});\n") &&
           r_c17_format(emitter,
                        "    size_t index = 0U;\n"
                        "\n"
                        "    for (index = 0U; (result.status == R_STD_FORMAT_CALL_SUCCESS) && (index < "
                        "%s); ++index) {\n"
                        "        if (index != 0U) {\n"
                        "            result = r_std_format_append_str(out, (RStdStringView){(const "
                        "uint8_t *)\", \", 2U});\n"
                        "        }\n"
                        "        if (result.status == R_STD_FORMAT_CALL_SUCCESS) {\n"
                        "            result = ",
                        count) &&
           r_c17_emit_format_glue_name(emitter, element_type) &&
           r_c17_format(emitter,
                        "(%s, out);\n"
                        "        }\n"
                        "    }\n"
                        "    if (result.status != R_STD_FORMAT_CALL_SUCCESS) {\n"
                        "        return result;\n"
                        "    }\n",
                        element) &&
           r_c17_emit_format_closing(emitter, "]");
}

/* A nominal type calls its implementation; its checked alloc_error is the glue failure. */
static bool r_c17_emit_format_hook_body(RC17Emitter *emitter, RSymbolId hook_id) {
    const RSemanticSymbol *hook = &emitter->frontend->semantic_symbols[hook_id - 1U];
    const RTypeId *parameters =
        &emitter->frontend->semantic_parameter_types[hook->first_parameter_type];
    if (hook->parameter_count != 2U)
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    if (hook->throws_type == R_TYPE_ID_INVALID)
        return r_c17_write(emitter, "    ") && r_c17_emit_function_name(emitter, hook_id) &&
               r_c17_write(emitter, "((") && r_c17_emit_value_type(emitter, parameters[0]) &&
               r_c17_write(emitter, ")value_pointer, (") &&
               r_c17_emit_value_type(emitter, parameters[1]) &&
               r_c17_write(emitter,
                           ")out);\n"
                           "    return (RStdFormatAllocResult){0};\n"
                           "}\n"
                           "\n");
    return r_c17_write(emitter, "    ") &&
           r_c17_emit_value_type(emitter, hook->effect_carrier_type) &&
           r_c17_write(emitter,
                       " carrier = {0};\n"
                       "    RStdFormatAllocResult result = {0};\n"
                       "\n"
                       "    ") &&
           r_c17_emit_function_name(emitter, hook_id) && r_c17_write(emitter, "(&carrier, (") &&
           r_c17_emit_value_type(emitter, parameters[0]) &&
           r_c17_write(emitter, ")value_pointer, (") &&
           r_c17_emit_value_type(emitter, parameters[1]) &&
           r_c17_write(emitter,
                       ")out);\n"
                       "    if (carrier.r_tag != UINT32_C(0)) {\n"
                       "        result.status = R_STD_FORMAT_CALL_ERROR;\n"
                       "        result.error = carrier.r_payload.r_error_00000001;\n"
                       "    }\n"
                       "    return result;\n"
                       "}\n"
                       "\n");
}

/* A tuple `(a, b)`: its elements in order. */
static bool r_c17_emit_format_tuple_body(RC17Emitter *emitter,
                                         const RSemanticAggregate *aggregate) {
    if (!r_c17_write(emitter, "    RStdFormatAllocResult result = {0};\n\n") ||
        !r_c17_emit_format_text_step(emitter, "("))
        return false;
    for (uint32_t i = 0U; i < aggregate->field_count; ++i) {
        const RSemanticField *field =
            &emitter->frontend->semantic_fields[aggregate->first_field + i];
        char member[128], place[160];
        const size_t start = emitter->output.length;
        if (!r_c17_emit_field_name(emitter, field) ||
            !r_c17_capture_since(emitter, start, member, sizeof(member)))
            return false;
        (void)snprintf(place, sizeof(place), "value->%s", member);
        if ((i != 0U && !r_c17_emit_format_text_step(emitter, ", ")) ||
            !r_c17_emit_format_component_step(emitter, field->type, place))
            return false;
    }
    return r_c17_emit_format_closing(emitter, ")");
}

static bool r_c17_emit_format_scalar_body(RC17Emitter *emitter,
                                          RTypeId type_id,
                                          const RSemanticType *type) {
    RSemanticTypeKind kind = type->kind;
    RTokenKind c_token = R_TOKEN_INVALID;
    if (kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) {
        if (!r_c17_c_abi_token(type, &c_token))
            return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        if (c_token == R_TOKEN_KW_C_BOOL)
            kind = R_SEMANTIC_TYPE_BOOL;
    }
    const bool floating = kind == R_SEMANTIC_TYPE_F32 || kind == R_SEMANTIC_TYPE_F64 ||
                          c_token == R_TOKEN_KW_C_FLOAT || c_token == R_TOKEN_KW_C_DOUBLE ||
                          c_token == R_TOKEN_KW_C_LONG_DOUBLE;
    const char *plain = "(RStdFormatSpec){0U, 0U, 10U, 0, 0, 0}";
    if (floating) {
        const bool single = kind == R_SEMANTIC_TYPE_F32 || c_token == R_TOKEN_KW_C_FLOAT;
        const uint32_t source_kind = kind == R_SEMANTIC_TYPE_F32      ? 0U
                                     : kind == R_SEMANTIC_TYPE_F64    ? 1U
                                     : c_token == R_TOKEN_KW_C_FLOAT  ? 2U
                                     : c_token == R_TOKEN_KW_C_DOUBLE ? 3U
                                                                      : 4U;
        return r_c17_emit_format_header(emitter, type_id, true) &&
               r_c17_write(emitter,
                           single ? "    return r_library_internal_format_float32(out, *value, "
                                  : "    return r_library_internal_format_float(out, (long "
                                    "double)*value, ") &&
               r_c17_format(emitter, "UINT32_C(%" PRIu32 "), %s);\n}\n\n", source_kind, plain);
    }
    if (kind == R_SEMANTIC_TYPE_BOOL)
        return r_c17_emit_format_header(emitter, type_id, true) &&
               r_c17_write(emitter,
                           "    return r_std_format_append_str(out,\n"
                           "                                   *value ? (RStdStringView){(const "
                           "uint8_t *)\"true\", 4U}\n"
                           "                                          : (RStdStringView){(const "
                           "uint8_t *)\"false\", 5U});\n"
                           "}\n"
                           "\n");
    if (kind == R_SEMANTIC_TYPE_CHAR)
        return r_c17_emit_format_header(emitter, type_id, true) &&
               r_c17_write(emitter,
                           "    return r_std_format_append_char(out, *value);\n"
                           "}\n"
                           "\n");
    if (kind == R_SEMANTIC_TYPE_STR)
        return r_c17_emit_format_header(emitter, type_id, true) &&
               r_c17_write(emitter,
                           "    return r_std_format_append_str(out, (RStdStringView){value->data, "
                           "value->length});\n"
                           "}\n"
                           "\n");
    if (kind == R_SEMANTIC_TYPE_CONSTEXPR_STR)
        return r_c17_emit_format_header(emitter, type_id, true) &&
               r_c17_write(emitter,
                           "    return r_std_format_append_str(out, (RStdStringView){value->r_data, "
                           "value->r_len});\n"
                           "}\n"
                           "\n");
    const RSemanticTypeKind representation =
        r_semantic_integer_representation(emitter->frontend, type_id);
    const bool negative =
        representation >= R_SEMANTIC_TYPE_I8 && representation <= R_SEMANTIC_TYPE_ISIZE;
    return r_c17_emit_format_header(emitter, type_id, true) &&
           r_c17_write(emitter, "    RStdConvertParsedInteger integer = {0};\n\n") &&
           (!negative || r_c17_write(emitter, "    integer.negative = *value < 0;\n")) &&
           r_c17_write(emitter, "    integer.magnitude = (uint64_t)*value;\n") &&
           (!negative || r_c17_write(emitter,
                                     "    if (integer.negative) {\n"
                                     "        integer.magnitude = UINT64_C(0) - integer.magnitude;\n"
                                     "    }\n")) &&
           r_c17_format(emitter,
                        "    return r_library_internal_format_integer(out, integer, %s);\n"
                        "}\n"
                        "\n",
                        plain);
}

static bool r_c17_emit_format_glue_definition(RC17Emitter *emitter, RTypeId type_id) {
    const RSemanticType *type = r_c17_type(emitter, type_id);
    char element[128];
    if (type == NULL)
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    switch (type->kind) {
    case R_SEMANTIC_TYPE_OPTION:
        return r_c17_emit_format_header(emitter, type_id, true) &&
               r_c17_write(emitter,
                           "    RStdFormatAllocResult result = {0};\n"
                           "\n"
                           "    if (value->r_tag != UINT32_C(1)) {\n"
                           "        return r_std_format_append_str(out, (RStdStringView){(const "
                           "uint8_t *)\"none\", 4U});\n"
                           "    }\n") &&
               r_c17_emit_format_text_step(emitter, "some(") &&
               r_c17_emit_format_component_step(emitter, type->base, "value->r_payload.r_some") &&
               r_c17_emit_format_closing(emitter, ")");
    case R_SEMANTIC_TYPE_FIXED_ARRAY: {
        char count[64];
        (void)snprintf(count, sizeof(count), "(size_t)UINT64_C(%" PRIu64 ")", type->length);
        return r_c17_emit_format_header(emitter, type_id, true) &&
               r_c17_emit_format_sequence_body(emitter, type->base, count, "&value->r_data[index]");
    }
    case R_SEMANTIC_TYPE_SLICE:
        return r_c17_emit_format_header(emitter, type_id, true) &&
               r_c17_emit_format_sequence_body(
                   emitter, type->base, "value->r_len", "&value->r_data[index]");
    case R_SEMANTIC_TYPE_ARRAY:
        (void)snprintf(element,
                       sizeof(element),
                       "(const unsigned char *)value->data + index * value->element.size");
        return r_c17_emit_format_header(emitter, type_id, false) &&
               r_c17_write(emitter, "    const RRuntimeArray *value = value_pointer;\n") &&
               r_c17_emit_format_sequence_body(emitter, type->base, "value->length", element);
    case R_SEMANTIC_TYPE_STANDARD:
        if (r_c17_format_standard_named(emitter, type, "std.string::string"))
            return r_c17_emit_format_header(emitter, type_id, false) &&
                   r_c17_write(emitter,
                               "    return r_std_format_append_str(out, "
                               "r_std_string_as_str(value_pointer));\n"
                               "}\n"
                               "\n");
        {
            const bool socket =
                r_c17_format_standard_named(emitter, type, "std.net::socket_address");
            return r_c17_emit_format_header(emitter, type_id, false) &&
                   r_c17_format(emitter,
                                "    uint8_t text[64];\n"
                                "    const size_t length =\n"
                                "        %s(*(const %s *)value_pointer, text);\n"
                                "\n"
                                "    return r_std_format_append_str(out, (RStdStringView){text, "
                                "length});\n"
                                "}\n"
                                "\n",
                                socket ? "r_library_internal_net_socket_text"
                                       : "r_library_internal_net_ip_text",
                                socket ? "RStdNetSocketAddress" : "RStdNetIpAddress");
        }
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM: {
        const RSemanticAggregate *aggregate = r_c17_clone_aggregate(emitter, type_id);
        if (aggregate == NULL)
            return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        if (aggregate->is_tuple)
            return r_c17_emit_format_header(emitter, type_id, true) &&
                   r_c17_emit_format_tuple_body(emitter, aggregate);
        return aggregate->format_function != R_SYMBOL_ID_INVALID &&
               r_c17_emit_format_header(emitter, type_id, false) &&
               r_c17_emit_format_hook_body(emitter, aggregate->format_function);
    }
    default:
        return r_c17_emit_format_scalar_body(emitter, type_id, type);
    }
}

/* The format glue the functions requested, spliced in after the function prototypes. */
static bool r_c17_emit_format_glue(RC17Emitter *emitter) {
    RC17Buffer program = emitter->output;
    RC17Buffer scratch = {0};
    const size_t count = emitter->frontend->semantic_type_count;
    bool success = true;

    if (!emitter->uses_format_glue) {
        return true;
    }
    emitter->output = scratch;
    for (size_t index = 1U; success && index <= count; ++index) {
        if (emitter->format_glue_types[index] == 0U)
            continue;
        success = r_c17_write(emitter, "static RStdFormatAllocResult ") &&
                  r_c17_emit_format_glue_name(emitter, (RTypeId)index) &&
                  r_c17_write(emitter, "(const void *value_pointer, RStdFormatBuilder *out);\n");
    }
    success = success && r_c17_write(emitter, "\n");
    for (size_t index = 1U; success && index <= count; ++index) {
        if (emitter->format_glue_types[index] != 0U)
            success = r_c17_emit_format_glue_definition(emitter, (RTypeId)index);
    }
    scratch = emitter->output;
    emitter->output = program;
    if (success && (scratch.length != 0U)) {
        success = r_c17_buffer_reserve(emitter, scratch.length);
        if (success) {
            (void)memmove(emitter->output.bytes + emitter->clone_glue_anchor + scratch.length,
                          emitter->output.bytes + emitter->clone_glue_anchor,
                          emitter->output.length - emitter->clone_glue_anchor);
            (void)memcpy(emitter->output.bytes + emitter->clone_glue_anchor,
                         scratch.bytes,
                         scratch.length);
            emitter->output.length += scratch.length;
        }
    }
    r_c17_free(emitter, scratch.bytes);
    return success;
}

/* Sync call site. RENDER builds a new string in a fresh builder, which a failure destroys;
 * APPEND appends to the caller's builder. A failure transfers alloc_error. */
static bool r_c17_emit_core_format(RC17Emitter *emitter,
                                   const RHirNode *node,
                                   uint32_t depth,
                                   RC17Value *result) {
    const bool render = node->standard_operation == R_STANDARD_CALL_CORE_FORMAT_RENDER;
    RC17Value subject, output;
    uint32_t value = 0U, builder = 0U, status;
    if (node->child_count != (render ? 1U : 2U) ||
        !r_c17_emit_expression(emitter, r_c17_child(emitter, node, 0U), depth, &subject) ||
        !subject.has_value ||
        (!render &&
         (!r_c17_emit_expression(emitter, r_c17_child(emitter, node, 1U), depth, &output) ||
          !output.has_value)))
        return false;
    if (render &&
        (!r_c17_new_temporary(emitter, node->type, depth, &value) ||
         !r_c17_write(emitter, " = {0};\n") || !r_c17_take_temporary(emitter, &builder) ||
         !r_c17_indent(emitter, depth) || !r_c17_write(emitter, "RStdFormatBuilder ") ||
         !r_c17_emit_temporary_name(emitter, builder) ||
         !r_c17_write(emitter, " = r_std_format_create(r_runtime_hosted_allocator()).value;\n")))
        return false;
    if (!r_c17_take_temporary(emitter, &status) || !r_c17_indent(emitter, depth) ||
        !r_c17_write(emitter, "RStdFormatAllocResult ") ||
        !r_c17_emit_temporary_name(emitter, status) || !r_c17_write(emitter, " = ") ||
        !r_c17_emit_format_glue_name(emitter, node->runtime_type) || !r_c17_write(emitter, "(") ||
        !r_c17_emit_temporary_name(emitter, subject.temporary) || !r_c17_write(emitter, ", ") ||
        (render ? (!r_c17_write(emitter, "&") || !r_c17_emit_temporary_name(emitter, builder))
                : !r_c17_emit_temporary_name(emitter, output.temporary)) ||
        !r_c17_write(emitter, ");\n"))
        return false;
    if (render &&
        (!r_c17_indent(emitter, depth) || !r_c17_write(emitter, "if (") ||
         !r_c17_emit_temporary_name(emitter, status) ||
         !r_c17_write(emitter, ".status == R_STD_FORMAT_CALL_SUCCESS) {\n") ||
         !r_c17_indent(emitter, depth + 1U) || !r_c17_emit_temporary_name(emitter, value) ||
         !r_c17_write(emitter, " = r_std_format_finish(&") ||
         !r_c17_emit_temporary_name(emitter, builder) || !r_c17_write(emitter, ");\n") ||
         !r_c17_indent(emitter, depth) || !r_c17_write(emitter, "} else {\n") ||
         !r_c17_indent(emitter, depth + 1U) ||
         !r_c17_write(emitter, "r_std_format_builder_destroy(&") ||
         !r_c17_emit_temporary_name(emitter, builder) || !r_c17_write(emitter, ");\n") ||
         !r_c17_indent(emitter, depth) || !r_c17_write(emitter, "}\n")))
        return false;
    if (r_c17_value_kind(emitter, node->auxiliary_type) == R_SEMANTIC_TYPE_EFFECT_CARRIER
            ? !r_c17_emit_alloc_failure_transfer(
                  emitter, node, status, "R_STD_FORMAT_CALL_SUCCESS", depth)
            : (!r_c17_indent(emitter, depth) || !r_c17_write(emitter, "(void)") ||
               !r_c17_emit_temporary_name(emitter, status) || !r_c17_write(emitter, ";\n")))
        return false;
    result->type = r_c17_value_type(emitter, node->type);
    result->temporary = value;
    result->has_value = render;
    return true;
}

/* Async call site: the text or the failure is written to the frame carrier. */
static bool r_c17_emit_async_core_format(RC17Emitter *emitter,
                                         const RMirFunction *mir,
                                         const RSemanticSymbol *function,
                                         const RMirInstruction *instruction,
                                         uint32_t depth) {
    const bool render = instruction->standard_operation == R_STANDARD_CALL_CORE_FORMAT_RENDER;
    const RMirValueId subject = r_c17_mir_operand(emitter, instruction, 0U);
    const uint32_t id = instruction->result;
    if (render && (!r_c17_indent(emitter, depth) ||
                   !r_c17_format(emitter,
                                 "RStdFormatBuilder r_format_text_%08" PRIu32
                                 " = r_std_format_create(r_runtime_hosted_allocator()).value;\n",
                                 id)))
        return false;
    if (!r_c17_indent(emitter, depth) ||
        !r_c17_format(emitter, "RStdFormatAllocResult r_format_glue_%08" PRIu32 " = ", id) ||
        !r_c17_emit_format_glue_name(emitter, instruction->runtime_type) ||
        !r_c17_write(emitter, "(") ||
        !r_c17_emit_async_storage_reference(emitter, mir, function, false, subject) ||
        !r_c17_write(emitter, ", ") ||
        (render ? !r_c17_format(emitter, "&r_format_text_%08" PRIu32, id)
                : !r_c17_emit_async_storage_reference(
                      emitter, mir, function, false, r_c17_mir_operand(emitter, instruction, 1U))) ||
        !r_c17_write(emitter, ");\n") ||
        !r_c17_emit_async_zero_result(emitter, mir, function, instruction, depth) ||
        !r_c17_indent(emitter, depth) ||
        !r_c17_format(emitter,
                      "if (r_format_glue_%08" PRIu32 ".status == R_STD_FORMAT_CALL_SUCCESS) {\n",
                      id))
        return false;
    if (render && (!r_c17_indent(emitter, depth + 1U) ||
                   !r_c17_emit_async_storage_reference(emitter, mir, function, false, id) ||
                   !r_c17_format(emitter,
                                 ".r_payload.r_ok = r_std_format_finish(&r_format_text_%08" PRIu32
                                 ");\n",
                                 id)))
        return false;
    if (!r_c17_indent(emitter, depth + 1U) ||
        !r_c17_emit_async_storage_reference(emitter, mir, function, false, id) ||
        !r_c17_write(emitter, ".r_tag = UINT32_C(0);\n") || !r_c17_indent(emitter, depth) ||
        !r_c17_write(emitter, "} else {\n"))
        return false;
    if (render &&
        (!r_c17_indent(emitter, depth + 1U) ||
         !r_c17_format(
             emitter, "r_std_format_builder_destroy(&r_format_text_%08" PRIu32 ");\n", id)))
        return false;
    return r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, id) &&
           r_c17_write(emitter, ".r_tag = UINT32_C(1);\n") && r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, id) &&
           r_c17_format(emitter,
                        ".r_payload.r_error_00000001 = r_format_glue_%08" PRIu32 ".error;\n",
                        id) &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "}\n") &&
           r_c17_emit_async_mark_value_initialized(
               emitter, mir, function, instruction->type, instruction->result, true, depth);
}
