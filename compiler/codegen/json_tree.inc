static bool r_c17_json_tree_function(RC17Emitter *emitter, RSymbolId function_id) {
    const RSemanticSymbol *function = r_c17_symbol(emitter, function_id);
    const RHirNode *node = r_c17_node(emitter, emitter->function_nodes[function_id]);
    RJsonOperation op = (RJsonOperation)function->json_operation;
    const char *name = op == R_JSON_OPERATION_NAME_EQUAL   ? "r_std_json_name_equal"
                       : op == R_JSON_OPERATION_GET        ? "r_std_json_get"
                       : op == R_JSON_OPERATION_FIND       ? "r_std_json_find"
                       : op == R_JSON_OPERATION_KEY_AT     ? "r_std_json_key_at"
                       : op == R_JSON_OPERATION_APPEND     ? "r_std_json_append"
                       : op == R_JSON_OPERATION_INSERT     ? "r_std_json_insert"
                       : op == R_JSON_OPERATION_TAKE_INDEX ? "r_std_json_take_index"
                                                           : "r_std_json_take_field";
    bool consumes = op == R_JSON_OPERATION_APPEND || op == R_JSON_OPERATION_INSERT;
    bool checked = function->throws_type != 0U;
    bool pointer = op == R_JSON_OPERATION_GET || op == R_JSON_OPERATION_FIND;
    bool view = op == R_JSON_OPERATION_KEY_AT;
    if (!r_c17_emit_function_signature(emitter, function_id, true) || !r_c17_write(emitter, " {\n"))
        return false;
    for (uint32_t i = 0U; i < function->parameter_count; ++i) {
        const RSemanticType *type = r_c17_type(
            emitter,
            emitter->frontend->semantic_parameter_types[function->first_parameter_type + i]);
        if (type->kind != R_SEMANTIC_TYPE_SLICE)
            continue;
        if (!r_c17_format(emitter, "    RStdJsonByteView bytes_%u = {\n        .data = ", i) ||
            !r_c17_format_argument(emitter, node, i) ||
            !r_c17_write(emitter, ".r_data,\n        .length = ") ||
            !r_c17_format_argument(emitter, node, i) || !r_c17_write(emitter, ".r_len,\n    };\n"))
            return false;
    }
    if (!r_c17_write(emitter, "    ") ||
        !r_c17_write(emitter,
                     pointer    ? "const RStdJsonValue *value = "
                     : view     ? "RStdJsonByteView value = "
                     : consumes ? "RStdJsonResult outcome = "
                     : checked  ? "RStdJsonValueResult value = "
                                : "return ") ||
        !r_c17_format(emitter, "%s(", name))
        return false;
    for (uint32_t i = 0U; i < function->parameter_count; ++i) {
        const RSemanticType *type = r_c17_type(
            emitter,
            emitter->frontend->semantic_parameter_types[function->first_parameter_type + i]);
        if (i != 0U && !r_c17_write(emitter, ", "))
            return false;
        if (type->kind == R_SEMANTIC_TYPE_SLICE) {
            if (!r_c17_format(emitter, "bytes_%u", i))
                return false;
        } else if ((consumes && i + 1U == function->parameter_count &&
                    !r_c17_write(emitter, "&")) ||
                   !r_c17_format_argument(emitter, node, i))
            return false;
    }
    if (!r_c17_write(emitter, ");\n"))
        return false;
    if (pointer) {
        if (!r_c17_write(emitter, "    ") ||
            !r_c17_emit_value_type(emitter, function->return_type) ||
            !r_c17_write(
                emitter,
                " result = {0};\n    if (value != NULL) {\n        result.r_tag = UINT32_C(1);\n   "
                "     result.r_payload.r_some = value;\n    }\n    return result;\n"))
            return false;
    } else if (view) {
        if (!r_c17_write(emitter, "    return (") ||
            !r_c17_emit_value_type(emitter, function->return_type) ||
            !r_c17_write(emitter, "){.data = value.data, .length = value.length};\n"))
            return false;
    } else if (checked) {
        if (consumes) {
            if (!r_c17_write(emitter, "    r_std_json_value_destroy(&") ||
                !r_c17_format_argument(emitter, node, function->parameter_count - 1U) ||
                !r_c17_write(emitter, ");\n"))
                return false;
        } else if (!r_c17_write(emitter, "    RStdJsonResult outcome = value.outcome;\n"))
            return false;
        if (!r_c17_write(emitter, "    if (outcome.status == R_STD_JSON_CALL_SUCCESS) {\n"))
            return false;
        if (!consumes &&
            !r_c17_write(emitter, "        r_effect_out->r_payload.r_ok = value.value;\n"))
            return false;
        if (!r_c17_write(
                emitter,
                "        r_effect_out->r_tag = UINT32_C(0);\n"
                "    } else if (outcome.status == R_STD_JSON_CALL_ALLOCATION_ERROR) {\n"
                "        r_effect_out->r_tag = UINT32_C(1);\n"
                "        r_effect_out->r_payload.r_error_00000001 = outcome.allocation_error;\n"
                "    } else {\n"
                "        r_effect_out->r_tag = UINT32_C(2);\n"
                "        r_effect_out->r_payload.r_error_00000002 = outcome.error;\n"
                "    }\n"))
            return false;
    }
    return r_c17_write(emitter, "}\n\n");
}
