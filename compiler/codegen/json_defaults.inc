/* Defaults run after presence validation, in field declaration order. */
static bool r_c17_json_default_place(RC17Emitter *emitter,
                                     const RSemanticField *field,
                                     uint32_t slot,
                                     bool address) {
    if (slot != UINT32_MAX)
        return r_c17_format(emitter, address ? "r_slot_%u" : "*r_slot_%u", slot);
    return r_c17_write(emitter, address ? "&out->" : "out->") &&
           r_c17_emit_field_name(emitter, field);
}
static bool r_c17_json_default(RC17Emitter *emitter, const RSemanticField *field, uint32_t slot) {
    if (field->json.default_kind == R_JSON_DEFAULT_STRING) {
        const RInternEntry *text =
            &emitter->frontend->intern_entries[field->json.default_string - 1U];
        if (!r_c17_write(emitter, "        static const uint8_t text[] = "))
            return false;
        if (text->length == 0U) {
            if (!r_c17_write(emitter, "\"\";\n"))
                return false;
        } else if (!r_c17_emit_byte_string(emitter, text->bytes, text->length, 2U))
            return false;
        return r_c17_write(emitter, "        if (!r_json_cursor_default_string(c, ") &&
               r_c17_json_default_place(emitter, field, slot, true) &&
               r_c17_format(emitter, ", (RStdJsonByteView){text, %zuU})) {\n", text->length) &&
               r_c17_write(emitter, "            goto r_fail;\n        }\n");
    }
    if (field->json.default_kind == R_JSON_DEFAULT_SCALAR) {
        if (!r_c17_write(emitter, "        ") ||
            !r_c17_json_default_place(emitter, field, slot, false) || !r_c17_write(emitter, " = "))
            return false;
        RSemanticTypeKind kind = field->json.default_scalar_kind;
        uint64_t bits = field->json.default_bits;
        if (kind == R_SEMANTIC_TYPE_BOOL) {
            if (!r_c17_format(emitter, "(_Bool)%u", bits != 0U ? 1U : 0U))
                return false;
        } else if (kind == R_SEMANTIC_TYPE_CHAR) {
            if (!r_c17_format(emitter, "UINT32_C(%" PRIu64 ")", bits))
                return false;
        } else if (kind == R_SEMANTIC_TYPE_F32) {
            if (!r_c17_format(emitter, "r_f32_from_bits(UINT32_C(%" PRIu32 "))", (uint32_t)bits))
                return false;
        } else if (kind == R_SEMANTIC_TYPE_F64) {
            if (!r_c17_format(emitter, "r_f64_from_bits(UINT64_C(%" PRIu64 "))", bits))
                return false;
        } else if (!r_c17_emit_integer_literal(emitter, kind, bits))
            return false;
        return r_c17_write(emitter, ";\n");
    }
    if (field->json.default_kind != R_JSON_DEFAULT_FACTORY)
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    const RSemanticSymbol *factory = r_c17_symbol(emitter, field->json.default_factory);
    if (factory->throws_type == 0U)
        return r_c17_write(emitter, "        ") &&
               r_c17_json_default_place(emitter, field, slot, false) &&
               r_c17_write(emitter, " = ") &&
               r_c17_emit_function_name(emitter, field->json.default_factory) &&
               r_c17_write(emitter, "();\n");
    if (!r_c17_write(emitter, "        ") ||
        !r_c17_emit_value_type(emitter, factory->effect_carrier_type) ||
        !r_c17_write(emitter, " result;\n        ") ||
        !r_c17_emit_function_name(emitter, field->json.default_factory) ||
        !r_c17_write(emitter, "(&result);\n"))
        return false;
    uint32_t count = r_semantic_effect_count(emitter->frontend, factory->throws_type);
    for (uint32_t i = 0U; i < count; ++i) {
        RTypeId error = r_semantic_effect_at(emitter->frontend, factory->throws_type, i);
        bool allocation = r_c17_standard_type_named(
            emitter, r_c17_type(emitter, error), "std.alloc::alloc_error");
        if (!r_c17_format(emitter,
                          "        if (result.r_tag == UINT32_C(%u)) {\n"
                          "            c->outcome.status = %s;\n"
                          "            c->outcome.%s = result.r_payload.r_error_%08u;\n"
                          "            goto r_fail;\n"
                          "        }\n",
                          i + 1U,
                          allocation ? "R_STD_JSON_CALL_ALLOCATION_ERROR"
                                     : "R_STD_JSON_CALL_JSON_ERROR",
                          allocation ? "allocation_error" : "error",
                          i + 1U))
            return false;
    }
    return r_c17_write(emitter, "        ") &&
           r_c17_json_default_place(emitter, field, slot, false) &&
           r_c17_write(emitter, " = result.r_payload.r_ok;\n");
}
