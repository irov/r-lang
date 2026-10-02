static bool r_c17_json_stream_enum(RC17Emitter *emitter, RSymbolId fn, RTypeId id, uint8_t *seen) {
    const RSemanticAggregate *aggregate = r_c17_aggregate_for_type(emitter, id, NULL);
    for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
        RTypeId payload =
            emitter->frontend->semantic_variants[aggregate->first_variant + i].payload_type;
        if (payload != 0U && !r_c17_json_stream_types(emitter, fn, payload, seen))
            return false;
    }
    if (!r_c17_write(emitter, "typedef struct ") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") ||
        !r_c17_write(emitter, " {\n    RStdJsonDecodeFrame base;\n    uint32_t variant;\n} ") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") ||
        !r_c17_write(emitter, ";\nstatic RStdJsonDecodeStep ") ||
        !r_c17_json_stream_name(emitter, fn, id, "step") ||
        !r_c17_write(emitter, "(RStdJsonCursor *c, RStdJsonDecodeFrame *b) {\n    ") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") || !r_c17_write(emitter, " *f = (") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") || !r_c17_write(emitter, " *)b;\n    ") ||
        !r_c17_emit_value_type(emitter, id) ||
        !r_c17_write(emitter, " *out = b->output;\n    (void)f;\n"))
        return false;
    for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
        const RSemanticVariant *variant =
            &emitter->frontend->semantic_variants[aggregate->first_variant + i];
        const RInternEntry *name = &emitter->frontend->intern_entries[variant->name_intern_id - 1U];
        if (!r_c17_format(emitter, "    static const uint8_t name_%u[] = ", i) ||
            !r_c17_emit_byte_string(emitter, name->bytes, name->length, 1U) ||
            !r_c17_format(
                emitter, "    RStdJsonByteView key_%u = {name_%u, %zuU};\n", i, i, name->length))
            return false;
    }
    if (aggregate->is_tagged && !r_c17_write(emitter,
                                             "    if (b->state == 3U) {\n        out->r_tag = "
                                             "f->variant;\n        b->state = 4U;\n    }\n"))
        return false;
    if (!r_c17_write(emitter,
                     "    if (!c->has_token) {\n        return R_STD_JSON_DECODE_WAIT;\n    }\n"))
        return false;
    if (aggregate->is_tagged &&
        !r_c17_write(emitter,
                     "    if (b->state == 4U) {\n"
                     "        if (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_OBJECT_END)) {\n       "
                     "     goto r_fail;\n        }\n"
                     "        (void)r_json_cursor_next(c);\n        return "
                     "R_STD_JSON_DECODE_COMPLETE;\n    }\n"
                     "    if (b->state == 0U && c->token.kind == R_STD_JSON_TOKEN_OBJECT_BEGIN) {\n"
                     "        b->state = 1U;\n        (void)r_json_cursor_next(c);\n        return "
                     "R_STD_JSON_DECODE_PROGRESS;\n    }\n"))
        return false;
    if (!r_c17_write(emitter, "    if (b->state == 0U) {\n") ||
        !r_c17_write(emitter,
                     "        if (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_STRING)) {\n           "
                     " goto r_fail;\n        }\n"))
        return false;
    for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
        const RSemanticVariant *variant =
            &emitter->frontend->semantic_variants[aggregate->first_variant + i];
        if (variant->payload_type != 0U)
            continue;
        if (!r_c17_format(
                emitter, "        if (r_std_json_name_equal(c->token.text, key_%u, false)) {\n", i))
            return false;
        if (aggregate->is_tagged) {
            if (!r_c17_format(emitter, "            out->r_tag = UINT32_C(%u);\n", i))
                return false;
        } else if (!r_c17_write(emitter, "            *out = ") ||
                   !r_c17_emit_enum_literal(emitter, id, variant->value) ||
                   !r_c17_write(emitter, ";\n"))
            return false;
        if (!r_c17_write(emitter,
                         "            (void)r_json_cursor_next(c);\n            return "
                         "R_STD_JSON_DECODE_COMPLETE;\n        }\n"))
            return false;
    }
    if (!r_c17_write(emitter, "        goto r_type;\n    }\n"))
        return false;
    if (aggregate->is_tagged) {
        if (!r_c17_write(emitter,
                         "    if (b->state == 1U) {\n"
                         "        if (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_KEY)) {\n          "
                         "  goto r_fail;\n        }\n"))
            return false;
        for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
            const RSemanticVariant *variant =
                &emitter->frontend->semantic_variants[aggregate->first_variant + i];
            if (variant->payload_type == 0U)
                continue;
            if (!r_c17_format(emitter,
                              "        if (r_std_json_name_equal(c->token.text, key_%u, false)) {\n"
                              "            f->variant = %uU;\n            b->state = 2U;\n"
                              "            (void)r_json_cursor_next(c);\n            return "
                              "R_STD_JSON_DECODE_PROGRESS;\n        }\n",
                              i,
                              i))
                return false;
        }
        if (!r_c17_write(
                emitter,
                "        goto r_type;\n    }\n    b->state = 3U;\n    switch (f->variant) {\n"))
            return false;
        for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
            const RSemanticVariant *variant =
                &emitter->frontend->semantic_variants[aggregate->first_variant + i];
            if (variant->payload_type == 0U)
                continue;
            if (!r_c17_format(emitter, "    case %u:\n        if (!", i) ||
                !r_c17_json_stream_name(emitter, fn, variant->payload_type, "create") ||
                !r_c17_format(emitter,
                              "(c, &out->r_payload.r_v%08u, false)) {\n            goto r_fail;\n  "
                              "      }\n        break;\n",
                              i))
                return false;
        }
        if (!r_c17_write(emitter,
                         "    default:\n        goto r_type;\n    }\n    return "
                         "R_STD_JSON_DECODE_PROGRESS;\n"))
            return false;
    }
    if (!r_c17_write(emitter,
                     "r_type:\n    (void)r_json_cursor_fail(c, R_STD_JSON_ERROR_TYPE);\n"
                     "r_fail:\n    return R_STD_JSON_DECODE_ERROR;\n}\n"))
        return false;
    bool drop = aggregate->is_tagged && r_c17_type_requires_drop(emitter, id);
    if (drop) {
        if (!r_c17_write(emitter, "static void ") ||
            !r_c17_json_stream_name(emitter, fn, id, "drop") ||
            !r_c17_write(emitter,
                         "(RStdJsonDecodeFrame *b, bool complete) {\n"
                         "    if (complete || b->state != 4U) {\n        return;\n    }\n    ") ||
            !r_c17_emit_value_type(emitter, id) ||
            !r_c17_write(emitter, " *out = b->output;\n    switch (out->r_tag) {\n"))
            return false;
        for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
            RTypeId payload =
                emitter->frontend->semantic_variants[aggregate->first_variant + i].payload_type;
            if (payload == 0U || !r_c17_type_requires_drop(emitter, payload))
                continue;
            if (!r_c17_format(emitter, "    case %u:\n        ", i) ||
                !r_c17_emit_type_drop_name(emitter, payload) ||
                !r_c17_format(emitter, "(&out->r_payload.r_v%08u);\n        break;\n", i))
                return false;
        }
        if (!r_c17_write(emitter, "    default:\n        break;\n    }\n}\n"))
            return false;
    }
    return r_c17_json_stream_create_body(emitter, fn, id, drop);
}
