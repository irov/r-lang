/* R-FUNC-0026 (L35): each instance of a function with `@recursion(depth = N)` counts its
 * activations on the thread in a thread-local counter beside its definition. The entry throws
 * core::recursion_error when N activations exist and otherwise begins one; the compiler-owned
 * finally of the body ends it. The entry carries the mark that tools/compute_stack_entries.py
 * reads to count N frames of the function on a call cycle (R-FUNC-0004). */

static bool r_c17_emit_recursion_counter_name(RC17Emitter *emitter, RSymbolId function_id) {
    if ((function_id == R_SYMBOL_ID_INVALID) ||
        ((size_t)function_id > emitter->frontend->semantic_symbol_count) ||
        (emitter->function_ordinals[function_id] == UINT32_C(0))) {
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    return r_c17_format(
        emitter, "r_activations_%08" PRIu32, emitter->function_ordinals[function_id]);
}

static bool r_c17_emit_recursion_counter(RC17Emitter *emitter, RSymbolId function_id) {
    const RSemanticSymbol *function = r_c17_symbol(emitter, function_id);
    if ((function == NULL) || (function->recursion_depth == 0U)) {
        return true;
    }
    return r_c17_write(emitter, "static _Thread_local size_t ") &&
           r_c17_emit_recursion_counter_name(emitter, function_id) &&
           r_c17_write(emitter, ";\n\n");
}

static bool r_c17_preflight_recursion_call(RC17Emitter *emitter,
                                           const RHirNode *node,
                                           RSymbolId function_symbol,
                                           size_t depth) {
    const RSemanticSymbol *function = r_c17_symbol(emitter, function_symbol);
    const bool enters = node->standard_operation == R_STANDARD_CALL_CORE_RECURSION_ENTER;

    if ((function == NULL) || (function->recursion_depth == 0U) ||
        (node->symbol != function_symbol) ||
        (r_c17_value_kind(emitter, node->type) != R_SEMANTIC_TYPE_VOID) ||
        (enters && ((node->integer_value != (uint64_t)function->recursion_depth) ||
                    !r_c17_type_is_single_effect_carrier(emitter,
                                                         node->auxiliary_type,
                                                         node->type,
                                                         "core::recursion_error",
                                                         NULL) ||
                    !r_c17_preflight_type_dependency(emitter, node->auxiliary_type, depth + 1U)))) {
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    }
    return true;
}

static bool r_c17_emit_recursion_call(RC17Emitter *emitter,
                                      const RHirNode *node,
                                      uint32_t depth,
                                      RC17Value *result) {
    RTypeId error_type = R_TYPE_ID_INVALID;
    uint32_t error = UINT32_C(0);

    (void)memset(result, 0, sizeof(*result));
    result->type = r_c17_value_type(emitter, node->type);
    result->has_value = false;
    if (node->standard_operation == R_STANDARD_CALL_CORE_RECURSION_LEAVE) {
        return r_c17_indent(emitter, depth) &&
               r_c17_emit_recursion_counter_name(emitter, node->symbol) &&
               r_c17_write(emitter, " -= 1U;\n");
    }
    return r_c17_type_is_single_effect_carrier(emitter,
                                               node->auxiliary_type,
                                               node->type,
                                               "core::recursion_error",
                                               &error_type) &&
           r_c17_indent(emitter, depth) &&
           r_c17_format(emitter,
                        "/* R_STACK_RECURSION: %" PRIu64 " */\n",
                        node->integer_value) &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "if (") &&
           r_c17_emit_recursion_counter_name(emitter, node->symbol) &&
           r_c17_format(emitter, " >= (size_t)%" PRIu64 "U) {\n", node->integer_value) &&
           r_c17_new_temporary(emitter, error_type, depth + 1U, &error) &&
           r_c17_format(emitter, " = {.depth = (size_t)%" PRIu64 "U};\n", node->integer_value) &&
           r_c17_emit_checked_transfer(
               emitter, error_type, error, node, UINT32_MAX, depth + 1U) &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "}\n") &&
           r_c17_indent(emitter, depth) &&
           r_c17_emit_recursion_counter_name(emitter, node->symbol) &&
           r_c17_write(emitter, " += 1U;\n");
}
