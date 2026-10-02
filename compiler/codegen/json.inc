#include "json_hooks.inc"

#include "json_decode.inc"
#include "json_encode.inc"
#include "json_stream.inc"

#include "json_reader.inc"
#include "json_tree.inc"

/* M32.6: std.json::schema::<T>() parses the schema text that the semantic pass built for T. */
/* M32.6: the schema text parsed at run time; the entry of each type with a schema hook is the
   value of its hook, inserted under $defs. A hook throws the errors of the operation itself, so
   its outcome carrier is the carrier of the operation. */
static bool r_c17_emit_json_schema(RC17Emitter *emitter, RSymbolId function_id) {
    const RFrontendContext *context = emitter->frontend;
    const RSemanticSymbol *function = r_c17_symbol(emitter, function_id);
    const RInternEntry *text;
    bool hooked = false;
    if (function->json_schema_text == 0U || function->json_schema_text > context->intern_count)
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    text = &context->intern_entries[function->json_schema_text - 1U];
    for (size_t entry = 0U; entry < context->json_schema_hook_count; ++entry)
        hooked = hooked || context->json_schema_hooks[entry].function == function_id;
    if (!r_c17_emit_function_signature(emitter, function_id, true) ||
        !r_c17_write(emitter, " {\n    static const uint8_t r_text[] = ") ||
        !r_c17_emit_byte_string(emitter, text->bytes, text->length, 1U) ||
        !r_c17_format(emitter,
                      "    RStdJsonByteView r_source = {.data = r_text, .length = %zuU};\n",
                      (size_t)text->length) ||
        !r_c17_write(emitter,
                     "    RStdJsonValueResult r_result =\n"
                     "        r_std_json_parse(r_runtime_hosted_allocator(), r_source);\n"))
        return false;
    if (hooked) {
        if (!r_c17_write(emitter,
                         "    if (r_result.outcome.status == R_STD_JSON_CALL_SUCCESS) {\n"
                         "        static const uint8_t r_defs_key[] = \"$defs\";\n"
                         "        RStdJsonValueResult r_defs = r_std_json_take_field(\n"
                         "            &r_result.value, (RStdJsonByteView){.data = r_defs_key, .length = 5U});\n"
                         "        if (r_defs.outcome.status != R_STD_JSON_CALL_SUCCESS) {\n"
                         "            r_std_json_value_destroy(&r_result.value);\n"
                         "            r_result.outcome = r_defs.outcome;\n"
                         "        } else {\n"))
            return false;
        uint32_t ordinal = 0U;
        for (size_t entry = 0U; entry < context->json_schema_hook_count; ++entry) {
            const RJsonSchemaHook *hook = &context->json_schema_hooks[entry];
            const RSemanticSymbol *hook_symbol;
            const RInternEntry *name;
            if (hook->function != function_id)
                continue;
            hook_symbol = r_c17_symbol(emitter, hook->hook);
            if (hook_symbol == NULL || hook->name_intern_id == 0U ||
                hook->name_intern_id > context->intern_count ||
                hook_symbol->effect_carrier_type != function->effect_carrier_type)
                return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
            name = &context->intern_entries[hook->name_intern_id - 1U];
            ordinal += 1U;
            if (!r_c17_format(emitter, "            if (r_defs.outcome.status == R_STD_JSON_CALL_SUCCESS) {\n"
                                       "                static const uint8_t r_hook_key_%u[] = ",
                              (unsigned)ordinal) ||
                !r_c17_emit_byte_string(emitter, name->bytes, name->length, 4U) ||
                !r_c17_write(emitter, "                ") ||
                !r_c17_emit_value_type(emitter, function->effect_carrier_type) ||
                !r_c17_format(emitter, " r_hook_%u;\n                ", (unsigned)ordinal) ||
                !r_c17_emit_function_name(emitter, hook->hook) ||
                !r_c17_format(emitter,
                              "(&r_hook_%u);\n"
                              "                if (r_hook_%u.r_tag != UINT32_C(0)) {\n"
                              "                    r_std_json_value_destroy(&r_defs.value);\n"
                              "                    r_std_json_value_destroy(&r_result.value);\n"
                              "                    *r_effect_out = r_hook_%u;\n"
                              "                    return;\n"
                              "                }\n"
                              "                RStdJsonResult r_inserted_%u = r_std_json_insert(\n"
                              "                    &r_defs.value,\n"
                              "                    (RStdJsonByteView){.data = r_hook_key_%u, .length = %zuU},\n"
                              "                    &r_hook_%u.r_payload.r_ok);\n"
                              "                if (r_inserted_%u.status != R_STD_JSON_CALL_SUCCESS) {\n"
                              "                    r_std_json_value_destroy(&r_hook_%u.r_payload.r_ok);\n"
                              "                    r_defs.outcome = r_inserted_%u;\n"
                              "                }\n"
                              "            }\n",
                              (unsigned)ordinal, (unsigned)ordinal, (unsigned)ordinal, (unsigned)ordinal,
                              (unsigned)ordinal, (size_t)name->length, (unsigned)ordinal, (unsigned)ordinal,
                              (unsigned)ordinal, (unsigned)ordinal))
                return false;
        }
        if (!r_c17_write(emitter,
                         "            if (r_defs.outcome.status == R_STD_JSON_CALL_SUCCESS) {\n"
                         "                RStdJsonResult r_back = r_std_json_insert(\n"
                         "                    &r_result.value,\n"
                         "                    (RStdJsonByteView){.data = r_defs_key, .length = 5U},\n"
                         "                    &r_defs.value);\n"
                         "                if (r_back.status != R_STD_JSON_CALL_SUCCESS) {\n"
                         "                    r_std_json_value_destroy(&r_defs.value);\n"
                         "                    r_std_json_value_destroy(&r_result.value);\n"
                         "                    r_result.outcome = r_back;\n"
                         "                }\n"
                         "            } else {\n"
                         "                r_std_json_value_destroy(&r_defs.value);\n"
                         "                r_std_json_value_destroy(&r_result.value);\n"
                         "                r_result.outcome = r_defs.outcome;\n"
                         "            }\n"
                         "        }\n"
                         "    }\n"))
            return false;
    }
    return r_c17_write(
        emitter,
        "    if (r_result.outcome.status == R_STD_JSON_CALL_SUCCESS) {\n"
        "        r_effect_out->r_payload.r_ok = r_result.value;\n"
        "        r_effect_out->r_tag = UINT32_C(0);\n"
        "    } else if (r_result.outcome.status == R_STD_JSON_CALL_ALLOCATION_ERROR) {\n"
        "        r_effect_out->r_payload.r_error_00000001 = "
        "r_result.outcome.allocation_error;\n"
        "        r_effect_out->r_tag = UINT32_C(1);\n"
        "    } else {\n"
        "        r_effect_out->r_payload.r_error_00000002 = r_result.outcome.error;\n"
        "        r_effect_out->r_tag = UINT32_C(2);\n"
        "    }\n}\n\n");
}

/* Native tree operations share the ordinary checked carrier and ownership lowering. */
static bool r_c17_emit_json_function(RC17Emitter *emitter, RSymbolId function_id) {
    const RSemanticSymbol *function = r_c17_symbol(emitter, function_id);
    const RHirNode *node = r_c17_node(emitter, emitter->function_nodes[function_id]);
    RJsonOperation operation = (RJsonOperation)function->json_operation;
    const char *name = NULL;
    const char *carrier = "RStdJsonValueResult";
    bool allocator = false, bytes = false, view = false;
    bool checked = function->throws_type != 0U;
    if (operation >= R_JSON_OPERATION_NEW_READER && operation <= R_JSON_OPERATION_TAKE_BYTES)
        return r_c17_json_reader_function(emitter, function_id);
    if (operation == R_JSON_OPERATION_SCHEMA)
        return r_c17_emit_json_schema(emitter, function_id);
    if (operation >= R_JSON_OPERATION_NEW_DECODER && operation <= R_JSON_OPERATION_TAKE)
        return r_c17_json_stream_function(emitter, function_id);
    if (operation >= R_JSON_OPERATION_NAME_EQUAL && operation <= R_JSON_OPERATION_TAKE_FIELD)
        return r_c17_json_tree_function(emitter, function_id);
    if (operation == R_JSON_OPERATION_MARSHAL || operation == R_JSON_OPERATION_MARSHAL_WITH_OPTIONS)
        return r_c17_emit_json_marshal(emitter, function_id);
    if (operation == R_JSON_OPERATION_UNMARSHAL ||
        operation == R_JSON_OPERATION_UNMARSHAL_WITH_OPTIONS)
        return r_c17_emit_json_unmarshal(emitter, function_id);
    switch (operation) {
    case R_JSON_OPERATION_PARSE_WITH_OPTIONS:
        name = "r_std_json_parse_with_options";
        allocator = bytes = true;
        break;
    case R_JSON_OPERATION_STRINGIFY_WITH_OPTIONS:
        name = "r_std_json_stringify_with_options";
        allocator = true;
        carrier = "RStdJsonStringResult";
        break;
    case R_JSON_OPERATION_PARSE:
        name = "r_std_json_parse";
        allocator = bytes = true;
        break;
    case R_JSON_OPERATION_STRINGIFY:
        name = "r_std_json_stringify";
        allocator = true;
        carrier = "RStdJsonStringResult";
        break;
    case R_JSON_OPERATION_KIND:
        name = "r_std_json_kind";
        break;
    case R_JSON_OPERATION_LEN:
        name = "r_std_json_len";
        break;
    case R_JSON_OPERATION_TEXT:
        name = "r_std_json_text";
        view = true;
        break;
    case R_JSON_OPERATION_BOOLEAN:
        name = "r_std_json_boolean";
        break;
    case R_JSON_OPERATION_NULL:
        name = "r_std_json_null";
        break;
    case R_JSON_OPERATION_FROM_BOOL:
        name = "r_std_json_from_bool";
        allocator = true;
        break;
    case R_JSON_OPERATION_FROM_STRING:
        name = "r_std_json_from_string";
        allocator = bytes = true;
        break;
    case R_JSON_OPERATION_FROM_NUMBER:
        name = "r_std_json_from_number";
        allocator = true;
        break;
    case R_JSON_OPERATION_ARRAY:
        name = "r_std_json_array";
        allocator = true;
        break;
    case R_JSON_OPERATION_OBJECT:
        name = "r_std_json_object";
        allocator = true;
        break;
    case R_JSON_OPERATION_PARSE_NUMBER:
        name = "r_std_json_parse_number";
        allocator = bytes = true;
        carrier = "RStdJsonNumberResult";
        break;
    case R_JSON_OPERATION_NUMBER_TEXT:
        name = "r_std_json_number_text";
        view = true;
        break;
    case R_JSON_OPERATION_NUMBER_IS_ZERO:
        name = "r_std_json_number_is_zero";
        break;
    default:
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    }
    if (!r_c17_emit_function_signature(emitter, function_id, true) || !r_c17_write(emitter, " {\n"))
        return false;
    if (bytes) {
        if (!r_c17_write(emitter, "    RStdJsonByteView r_source = {\n        .data = ") ||
            !r_c17_format_argument(emitter, node, 0U) ||
            !r_c17_write(emitter, ".r_data,\n        .length = ") ||
            !r_c17_format_argument(emitter, node, 0U) || !r_c17_write(emitter, ".r_len,\n    };\n"))
            return false;
    }
    if (!r_c17_write(emitter, "    "))
        return false;
    if (checked) {
        if (!r_c17_format(
                emitter,
                "%s r_result =%s",
                carrier,
                operation == R_JSON_OPERATION_STRINGIFY_WITH_OPTIONS ? " "
                : ((!bytes && function->parameter_count != 0U) || function->parameter_count == 2U)
                    ? "\n        "
                    : " "))
            return false;
    } else if (view) {
        if (!r_c17_write(emitter, "RStdJsonByteView r_result = "))
            return false;
    } else if (!r_c17_write(emitter, "return "))
        return false;
    if (!r_c17_format(emitter, "%s(", name))
        return false;
    if (operation == R_JSON_OPERATION_STRINGIFY_WITH_OPTIONS && !r_c17_write(emitter, "\n        "))
        return false;
    if (allocator && !r_c17_write(emitter, "r_runtime_hosted_allocator()"))
        return false;
    if (function->parameter_count != 0U) {
        if (allocator && !r_c17_write(emitter, ", "))
            return false;
        if (bytes) {
            if (!r_c17_write(emitter, "r_source"))
                return false;
        } else if (!r_c17_format_argument(emitter, node, 0U))
            return false;
    }
    if (function->parameter_count == 2U &&
        (!r_c17_write(emitter, ", ") || !r_c17_format_argument(emitter, node, 1U)))
        return false;
    if (!r_c17_write(emitter, ");\n"))
        return false;
    if (view) {
        if (!r_c17_write(emitter, "    return (") ||
            !r_c17_emit_value_type(emitter, function->return_type) ||
            !r_c17_write(emitter, "){.data = r_result.data, .length = r_result.length};\n"))
            return false;
    }
    if (checked) {
        if (!r_c17_write(
                emitter,
                "    if (r_result.outcome.status == R_STD_JSON_CALL_SUCCESS) {\n"
                "        r_effect_out->r_payload.r_ok = r_result.value;\n"
                "        r_effect_out->r_tag = UINT32_C(0);\n"
                "    } else if (r_result.outcome.status == R_STD_JSON_CALL_ALLOCATION_ERROR) {\n"
                "        r_effect_out->r_payload.r_error_00000001 = "
                "r_result.outcome.allocation_error;\n"
                "        r_effect_out->r_tag = UINT32_C(1);\n"
                "    } else {\n"
                "        r_effect_out->r_payload.r_error_00000002 = r_result.outcome.error;\n"
                "        r_effect_out->r_tag = UINT32_C(2);\n"
                "    }\n"))
            return false;
    }
    return r_c17_write(emitter, "}\n\n");
}
