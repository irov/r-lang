/* Hook results use their existing carrier; a failed hook never initializes the field. */
static bool r_c17_json_hook_call(RC17Emitter *emitter, RSymbolId id, bool decode) {
    const RSemanticSymbol *hook = r_c17_symbol(emitter, id);
    if (hook->throws_type == 0U)
        return r_c17_write(emitter, decode ? "    *out = " : "    r.value = ") &&
               r_c17_emit_function_name(emitter, id) &&
               r_c17_write(emitter, decode ? "(&tree);\n" : "(v);\n");
    if (!r_c17_write(emitter, "    ") ||
        !r_c17_emit_value_type(emitter, hook->effect_carrier_type) ||
        !r_c17_write(emitter, " result;\n    ") || !r_c17_emit_function_name(emitter, id) ||
        !r_c17_write(emitter, decode ? "(&result, &tree);\n" : "(&result, v);\n"))
        return false;
    for (uint32_t i = 0U; i < r_semantic_effect_count(emitter->frontend, hook->throws_type); ++i) {
        RTypeId error = r_semantic_effect_at(emitter->frontend, hook->throws_type, i);
        bool allocation = r_c17_standard_type_named(
            emitter, r_c17_type(emitter, error), "std.alloc::alloc_error");
        const char *outcome = decode ? "c->outcome" : "r.outcome";
        if (!r_c17_format(emitter,
                          "    if (result.r_tag == UINT32_C(%u)) {\n"
                          "        %s.status = %s;\n"
                          "        %s.%s = result.r_payload.r_error_%08u;\n",
                          i + 1U,
                          outcome,
                          allocation ? "R_STD_JSON_CALL_ALLOCATION_ERROR"
                                     : "R_STD_JSON_CALL_JSON_ERROR",
                          outcome,
                          allocation ? "allocation_error" : "error",
                          i + 1U) ||
            !r_c17_write(
                emitter,
                decode ? "        r_std_json_value_destroy(&tree);\n        return false;\n    }\n"
                       : "        return r;\n    }\n"))
            return false;
    }
    return r_c17_write(emitter,
                       decode ? "    *out = result.r_payload.r_ok;\n"
                              : "    r.value = result.r_payload.r_ok;\n");
}
static bool r_c17_json_encode_hook(RC17Emitter *emitter, RSymbolId hook) {
    return r_c17_write(emitter, "    RStdJsonValueResult r = {0};\n") &&
           r_c17_json_hook_call(emitter, hook, false) &&
           r_c17_write(emitter,
                       "    if (q) {\n"
                       "        r.outcome = r_json_quote_number(&r.value);\n"
                       "        if (r.outcome.status != R_STD_JSON_CALL_SUCCESS) {\n"
                       "            r_std_json_value_destroy(&r.value);\n"
                       "        }\n    }\n    return r;\n");
}
static bool r_c17_json_decode_hook(RC17Emitter *emitter,
                                   RSymbolId function,
                                   const RSemanticAggregate *aggregate) {
    if (!r_c17_write(emitter, "    if (d) {\n"))
        return false;
    if (aggregate->is_default_initializable && aggregate->kind == R_SEMANTIC_AGGREGATE_STRUCT) {
        if (!r_c17_format(emitter,
                          "        r_jv_%u_%u(c, out);\n        return true;\n",
                          emitter->function_ordinals[function],
                          aggregate->type))
            return false;
    } else if (!r_c17_write(emitter,
                            "        return r_json_cursor_fail(c, R_STD_JSON_ERROR_TYPE);\n"))
        return false;
    return r_c17_write(
               emitter,
               "    }\n    RStdJsonValue tree;\n"
               "    if (!r_json_cursor_value(c, &tree, q)) {\n        return false;\n    }\n") &&
           r_c17_json_hook_call(emitter, aggregate->json_unmarshal_function, true) &&
           r_c17_write(emitter, "    r_std_json_value_destroy(&tree);\n    return true;\n");
}
