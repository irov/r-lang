/* Missing optional aggregates use the ordinary R default, without nested JSON factories. */
static bool r_c17_json_ordinary_default_name(RC17Emitter *emitter, RSymbolId function, RTypeId id) {
    return r_c17_format(emitter, "r_jv_%u_%u", emitter->function_ordinals[function], id);
}
static bool
r_c17_json_ordinary_default(RC17Emitter *emitter, RSymbolId function, RTypeId id, uint8_t *seen) {
    if ((seen[id] & 2U) != 0U || !r_semantic_type_has_default(emitter->frontend, id))
        return true;
    seen[id] |= 2U;
    const RSemanticType *type = r_c17_type(emitter, r_c17_value_type(emitter, id));
    const RSemanticAggregate *aggregate = r_c17_aggregate_for_type(emitter, id, NULL);
    if (type->kind == R_SEMANTIC_TYPE_STRUCT) {
        for (uint32_t i = 0U; i < aggregate->field_count; ++i)
            if (!r_c17_json_ordinary_default(
                    emitter,
                    function,
                    emitter->frontend->semantic_fields[aggregate->first_field + i].type,
                    seen))
                return false;
    } else if (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY &&
               !r_c17_json_ordinary_default(emitter, function, type->base, seen))
        return false;
    if (!r_c17_write(emitter, "static inline void ") ||
        !r_c17_json_ordinary_default_name(emitter, function, id) ||
        !r_c17_write(emitter, "(RStdJsonCursor *c, ") || !r_c17_emit_value_type(emitter, id) ||
        !r_c17_write(emitter, " *out) {\n    (void)c;\n"))
        return false;
    if (type->kind == R_SEMANTIC_TYPE_STRUCT) {
        if (aggregate->field_count == 0U &&
            !r_c17_write(emitter, "    out->r_empty = UINT8_C(0);\n"))
            return false;
        for (uint32_t i = 0U; i < aggregate->field_count; ++i) {
            const RSemanticField *field =
                &emitter->frontend->semantic_fields[aggregate->first_field + i];
            if (!r_c17_write(emitter, "    ") ||
                !r_c17_json_ordinary_default_name(emitter, function, field->type) ||
                !r_c17_write(emitter, "(c, &out->") || !r_c17_emit_field_name(emitter, field) ||
                !r_c17_write(emitter, ");\n"))
                return false;
        }
    } else if (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) {
        if (!r_c17_format(emitter,
                          "    for (size_t i = 0U; i < %" PRIu64 "U; ++i) {\n        ",
                          type->length) ||
            !r_c17_json_ordinary_default_name(emitter, function, type->base) ||
            !r_c17_write(emitter, "(c, &out->r_data[i]);\n    }\n"))
            return false;
    } else if (type->kind == R_SEMANTIC_TYPE_OPTION) {
        if (!r_c17_write(emitter, "    out->r_tag = UINT32_C(0);\n"))
            return false;
    } else if (type->kind == R_SEMANTIC_TYPE_ARRAY) {
        if (!r_c17_write(emitter,
                         "    r_runtime_array_initialize(out,\n                               "
                         "c->allocator,\n") ||
            !r_c17_emit_runtime_type_info_at(emitter, type->base, 31U) ||
            !r_c17_write(emitter, ");\n"))
            return false;
    } else if (type->kind == R_SEMANTIC_TYPE_CONSTEXPR_STR) {
        if (!r_c17_write(
                emitter,
                "    *out = (r_constexpr_str){.r_data = r_empty_program_string, .r_len = 0U};\n"))
            return false;
    } else if (!r_c17_write(emitter, "    *out = 0;\n"))
        return false;
    return r_c17_write(emitter, "}\n\n");
}
