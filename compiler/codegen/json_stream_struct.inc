static bool r_c17_json_stream_slots(RC17Emitter *emitter, const RSemanticAggregate *aggregate) {
    for (uint32_t i = 0U; i < aggregate->json_field_count; ++i) {
        const RJsonSchemaField *entry =
            &emitter->frontend->json_fields[aggregate->first_json_field + i];
        const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
        if (!r_c17_write(emitter, "    ") || !r_c17_emit_value_type(emitter, field->type) ||
            !r_c17_format(emitter, " *r_slot_%u = &", i) ||
            !(entry->parent == UINT32_MAX ? r_c17_write(emitter, "out->")
                                          : r_c17_format(emitter, "r_slot_%u->", entry->parent)) ||
            !r_c17_emit_field_name(emitter, field) ||
            !r_c17_format(emitter, ";\n    (void)r_slot_%u;\n", i))
            return false;
    }
    return true;
}
static bool r_c17_json_stream_select(RC17Emitter *emitter, const RSemanticAggregate *aggregate) {
    for (uint32_t pass = 0U; pass < 2U; ++pass) {
        if (pass == 1U && !r_c17_write(emitter, "        if (f->selected == -1) {\n"))
            return false;
        uint32_t depth = pass == 0U ? 2U : 3U;
        for (uint32_t i = 0U; i < aggregate->json_field_count; ++i) {
            const RJsonSchemaField *entry =
                &emitter->frontend->json_fields[aggregate->first_json_field + i];
            const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
            if (entry->embedded || entry->collector || entry->excluded ||
                (pass == 1U && (field->json.flags & R_JSON_FIELD_CASE) != 0U &&
                 !field->json.ignore_case))
                continue;
            if (!r_c17_indent(emitter, depth) || !r_c17_write(emitter, "if ("))
                return false;
            if (pass == 1U && (field->json.flags & R_JSON_FIELD_CASE) == 0U &&
                !r_c17_write(emitter, "c->options.ignore_case && "))
                return false;
            if (!r_c17_format(emitter,
                              "r_std_json_name_equal(c->token.text, key_%u, %s)) {\n",
                              i,
                              pass == 0U ? "false" : "true"))
                return false;
            if (pass == 1U && !r_c17_write(emitter,
                                           "                if (f->selected != -1) {\n"
                                           "                    (void)r_json_cursor_fail(c, "
                                           "R_STD_JSON_ERROR_AMBIGUOUS_FIELD);\n"
                                           "                    goto r_fail;\n                }\n"))
                return false;
            if (!r_c17_indent(emitter, depth + 1U) ||
                !r_c17_format(emitter, "f->selected = %u;\n", i) || !r_c17_indent(emitter, depth) ||
                !r_c17_write(emitter, "}\n"))
                return false;
        }
        if (pass == 1U && !r_c17_write(emitter, "        }\n"))
            return false;
    }
    return true;
}
static bool
r_c17_json_stream_struct(RC17Emitter *emitter, RSymbolId fn, RTypeId id, uint8_t *seen) {
    const RSemanticAggregate *aggregate = r_c17_aggregate_for_type(emitter, id, NULL);
    uint32_t count = aggregate->json_field_count, collector = UINT32_MAX;
    RTypeId member_type = 0U;
    bool dict = false;
    if (aggregate->json_unmarshal_function != 0U)
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    for (uint32_t i = 0U; i < count; ++i) {
        const RJsonSchemaField *entry =
            &emitter->frontend->json_fields[aggregate->first_json_field + i];
        const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
        if (entry->embedded)
            continue;
        if (entry->collector) {
            collector = i;
            const RSemanticType *type = r_c17_type(emitter, field->type);
            dict = type->kind == R_SEMANTIC_TYPE_DICT;
            member_type = dict ? type->second : field->type;
            if (!r_c17_json_stream_types(emitter, fn, member_type, seen))
                return false;
            continue;
        }
        if (!entry->excluded && !r_c17_json_stream_types(emitter, fn, field->type, seen))
            return false;
        if (field->json.default_kind == R_JSON_DEFAULT_NONE) {
            bool ordinary =
                entry->excluded && r_semantic_type_has_default(emitter->frontend, field->type);
            if (!(ordinary ? r_c17_json_ordinary_default(emitter, fn, field->type, seen)
                           : r_c17_json_decode_types(emitter, fn, field->type, seen)))
                return false;
        }
    }
    if (!r_c17_write(emitter, "typedef struct ") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") ||
        !r_c17_format(
            emitter,
            " {\n    RStdJsonDecodeFrame base;\n    int32_t selected;\n    bool present[%u];\n",
            count == 0U ? 1U : count))
        return false;
    if (collector != UINT32_MAX &&
        (!r_c17_write(emitter,
                      "    RStdString key;\n    bool key_live;\n    bool item_live;\n    ") ||
         !r_c17_emit_value_type(emitter, member_type) || !r_c17_write(emitter, " item;\n")))
        return false;
    if (!r_c17_write(emitter, "} ") || !r_c17_json_stream_name(emitter, fn, id, "frame") ||
        !r_c17_write(emitter, ";\nstatic RStdJsonDecodeStep ") ||
        !r_c17_json_stream_name(emitter, fn, id, "step") ||
        !r_c17_write(emitter, "(RStdJsonCursor *c, RStdJsonDecodeFrame *b) {\n    ") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") || !r_c17_write(emitter, " *f = (") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") || !r_c17_write(emitter, " *)b;\n    ") ||
        !r_c17_emit_value_type(emitter, id) ||
        !r_c17_write(emitter,
                     " *out = b->output;\n    (void)out;\n    bool *r_present = f->present;\n") ||
        !r_c17_json_stream_slots(emitter, aggregate))
        return false;
    for (uint32_t i = 0U; i < count; ++i) {
        const RJsonSchemaField *entry =
            &emitter->frontend->json_fields[aggregate->first_json_field + i];
        const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
        if (entry->embedded || entry->collector || entry->excluded)
            continue;
        uint32_t name = field->json.flags & R_JSON_FIELD_NAME ? field->json.name_intern_id
                                                              : field->name_intern_id;
        const RInternEntry *key = &emitter->frontend->intern_entries[name - 1U];
        if (!r_c17_format(emitter, "    static const uint8_t key_bytes_%u[] = ", i) ||
            !(key->length == 0U ? r_c17_write(emitter, "\"\";\n")
                                : r_c17_emit_byte_string(emitter, key->bytes, key->length, 1U)) ||
            !r_c17_format(emitter,
                          "    RStdJsonByteView key_%u = {key_bytes_%u, %zuU};\n",
                          i,
                          i,
                          key->length))
            return false;
    }
    if (!r_c17_write(emitter,
                     "    if (b->state == 3U) {\n"
                     "        if (f->selected >= 0) {\n            r_present[(size_t)f->selected] "
                     "= true;\n        }\n"))
        return false;
    if (collector != UINT32_MAX) {
        if (!r_c17_write(emitter,
                         "        if (f->selected == -1) {\n            f->item_live = true;\n"))
            return false;
        if (dict) {
            if (!r_c17_format(emitter,
                              "            if (!r_json_cursor_dict_insert(c, r_slot_%u, &f->key, "
                              "&f->item)) {\n"
                              "                goto r_fail;\n            }\n",
                              collector))
                return false;
        } else if (!r_c17_write(
                       emitter,
                       "            RStdStringView key = r_std_string_as_str(&f->key);\n") ||
                   !r_c17_format(
                       emitter,
                       "            c->outcome =\n"
                       "                r_std_json_insert(r_slot_%u, (RStdJsonByteView){key.data, "
                       "key.length}, &f->item);\n"
                       "            if (c->outcome.status != R_STD_JSON_CALL_SUCCESS) {\n"
                       "                goto r_fail;\n            }\n",
                       collector))
            return false;
        if (!r_c17_write(
                emitter,
                "            f->item_live = false;\n            r_std_string_destroy(&f->key);\n"
                "            f->key_live = false;\n        }\n"))
            return false;
    }
    if (!r_c17_write(emitter,
                     "        b->state = 1U;\n    }\n"
                     "    if (!c->has_token) {\n        return R_STD_JSON_DECODE_WAIT;\n    }\n"
                     "    if (b->state == 0U) {\n"
                     "        if (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_OBJECT_BEGIN)) {\n     "
                     "       goto r_fail;\n        }\n"
                     "        b->state = 1U;\n        (void)r_json_cursor_next(c);\n        return "
                     "R_STD_JSON_DECODE_PROGRESS;\n    }\n"
                     "    if (b->state == 1U) {\n"
                     "        if (c->token.kind == R_STD_JSON_TOKEN_OBJECT_END) {\n            "
                     "goto r_finish;\n        }\n"
                     "        if (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_KEY)) {\n            "
                     "goto r_fail;\n        }\n"
                     "        f->selected = -1;\n") ||
        !r_c17_json_stream_select(emitter, aggregate) ||
        !r_c17_write(emitter,
                     "        if (f->selected >= 0 && r_present[(size_t)f->selected]) {\n"
                     "            (void)r_json_cursor_fail(c, R_STD_JSON_ERROR_DUPLICATE_KEY);\n   "
                     "         goto r_fail;\n        }\n"
                     "        if (f->selected == -1 && c->options.reject_unknown_fields) {\n"
                     "            (void)r_json_cursor_fail(c, R_STD_JSON_ERROR_UNKNOWN_FIELD);\n   "
                     "         goto r_fail;\n        }\n"))
        return false;
    if (collector != UINT32_MAX) {
        if (!r_c17_write(emitter, "        if (f->selected == -1) {\n") ||
            !r_c17_json_embedded_collector(emitter, aggregate, collector, 3U) ||
            !r_c17_write(emitter,
                         "            f->key = r_json_scanner_take_text(&c->scanner);\n"
                         "            f->key_live = true;\n        }\n"))
            return false;
    }
    if (!r_c17_write(emitter,
                     "        b->state = 2U;\n        (void)r_json_cursor_next(c);\n"
                     "        return R_STD_JSON_DECODE_PROGRESS;\n    }\n"
                     "    b->state = 3U;\n    switch (f->selected) {\n"))
        return false;
    for (uint32_t i = 0U; i < count; ++i) {
        const RJsonSchemaField *entry =
            &emitter->frontend->json_fields[aggregate->first_json_field + i];
        const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
        if (entry->embedded || entry->collector || entry->excluded)
            continue;
        if (!r_c17_format(emitter, "    case %u:\n        if (!", i) ||
            !r_c17_json_stream_name(emitter, fn, field->type, "create") ||
            !r_c17_format(
                emitter,
                "(c, r_slot_%u, %s)) {\n            goto r_fail;\n        }\n        break;\n",
                i,
                field->json.flags & R_JSON_FIELD_STRING ? "true" : "false"))
            return false;
    }
    if (!r_c17_write(emitter, "    default:\n        if (!"))
        return false;
    if (collector == UINT32_MAX) {
        if (!r_c17_write(emitter, "r_json_decode_skip_create(c, out, false)"))
            return false;
    } else if (!r_c17_json_stream_name(emitter, fn, member_type, "create") ||
               !r_c17_write(emitter, "(c, &f->item, false)"))
        return false;
    if (!r_c17_write(emitter,
                     ") {\n            goto r_fail;\n        }\n        break;\n    }\n"
                     "    return R_STD_JSON_DECODE_PROGRESS;\nr_finish:\n"))
        return false;
    for (uint32_t i = 0U; i < count; ++i) {
        const RJsonSchemaField *entry =
            &emitter->frontend->json_fields[aggregate->first_json_field + i];
        const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
        if (entry->embedded || entry->collector || entry->excluded ||
            field->json.flags & R_JSON_FIELD_OPTIONAL)
            continue;
        if (!r_c17_format(emitter,
                          "    if (!r_present[%u]) {\n"
                          "        (void)r_json_cursor_fail(c, R_STD_JSON_ERROR_MISSING_FIELD);\n  "
                          "      goto r_fail;\n    }\n",
                          i))
            return false;
    }
    if (aggregate->field_count == 0U && !r_c17_write(emitter, "    out->r_empty = UINT8_C(0);\n"))
        return false;
    if (!r_c17_json_embedded_defaults(emitter, fn, aggregate, UINT32_MAX, 0U, count) ||
        !r_c17_write(emitter,
                     "    (void)r_json_cursor_next(c);\n    return R_STD_JSON_DECODE_COMPLETE;\n"
                     "r_fail:\n    return R_STD_JSON_DECODE_ERROR;\n}\nstatic void ") ||
        !r_c17_json_stream_name(emitter, fn, id, "drop") ||
        !r_c17_write(emitter, "(RStdJsonDecodeFrame *b, bool complete) {\n    ") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") || !r_c17_write(emitter, " *f = (") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") || !r_c17_write(emitter, " *)b;\n    ") ||
        !r_c17_emit_value_type(emitter, id) ||
        !r_c17_write(emitter,
                     " *out = b->output;\n    (void)out;\n    bool *r_present = f->present;\n    "
                     "(void)r_present;\n") ||
        !r_c17_json_stream_slots(emitter, aggregate) ||
        !r_c17_write(emitter, "    if (complete) {\n        return;\n    }\n"))
        return false;
    if (collector != UINT32_MAX) {
        if (!r_c17_write(emitter,
                         "    if (f->key_live) {\n        r_std_string_destroy(&f->key);\n    }\n"))
            return false;
        if (r_c17_type_requires_drop(emitter, member_type) &&
            (!r_c17_write(emitter, "    if (f->item_live) {\n        ") ||
             !r_c17_emit_type_drop_name(emitter, member_type) ||
             !r_c17_write(emitter, "(&f->item);\n    }\n")))
            return false;
    }
    for (uint32_t at = count; at != 0U; --at) {
        uint32_t i = at - 1U;
        const RJsonSchemaField *entry =
            &emitter->frontend->json_fields[aggregate->first_json_field + i];
        const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
        if (!r_c17_type_requires_drop(emitter, field->type))
            continue;
        if (!r_c17_format(emitter, "    if (r_present[%u]", i))
            return false;
        for (uint32_t parent = entry->parent; parent != UINT32_MAX;
             parent = emitter->frontend->json_fields[aggregate->first_json_field + parent].parent)
            if (!r_c17_format(emitter, " && !r_present[%u]", parent))
                return false;
        if (!r_c17_write(emitter, ") {\n        ") ||
            !r_c17_emit_type_drop_name(emitter, field->type) ||
            !r_c17_format(emitter, "(r_slot_%u);\n    }\n", i))
            return false;
    }
    return r_c17_write(emitter, "}\n") && r_c17_json_stream_create_body(emitter, fn, id, true);
}
