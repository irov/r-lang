/* One token loop serves the checked flattened schema. Slots are ordinary typed field addresses. */
static bool r_c17_json_embedded_collector(RC17Emitter *emitter,
                                          const RSemanticAggregate *aggregate,
                                          uint32_t slot,
                                          uint32_t depth) {
    const RJsonSchemaField *entry =
        &emitter->frontend->json_fields[aggregate->first_json_field + slot];
    const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
    const RSemanticType *type = r_c17_type(emitter, field->type);
    if (!r_c17_indent(emitter, depth) || !r_c17_format(emitter, "if (!r_present[%u]) {\n", slot))
        return false;
    if (type->kind == R_SEMANTIC_TYPE_DICT) {
        size_t alignment = (size_t)(depth + 1U) * 4U + 30U;
        if (!r_c17_indent(emitter, depth + 1U) ||
            !r_c17_write(emitter, "r_json_cursor_dict_initialize(c,\n") ||
            !r_c17_spaces(emitter, alignment) || !r_c17_format(emitter, "r_slot_%u,\n", slot) ||
            !r_c17_emit_runtime_type_info_at(emitter, type->second, alignment) ||
            !r_c17_write(emitter, ");\n"))
            return false;
    } else if (!r_c17_indent(emitter, depth + 1U) ||
               !r_c17_write(emitter,
                            "RStdJsonValueResult object = r_std_json_object(c->allocator);\n") ||
               !r_c17_indent(emitter, depth + 1U) ||
               !r_c17_write(emitter, "if (object.outcome.status != R_STD_JSON_CALL_SUCCESS) {\n") ||
               !r_c17_indent(emitter, depth + 2U) ||
               !r_c17_write(emitter, "c->outcome = object.outcome;\n") ||
               !r_c17_indent(emitter, depth + 2U) || !r_c17_write(emitter, "goto r_fail;\n") ||
               !r_c17_indent(emitter, depth + 1U) || !r_c17_write(emitter, "}\n") ||
               !r_c17_indent(emitter, depth + 1U) ||
               !r_c17_format(emitter, "*r_slot_%u = object.value;\n", slot))
        return false;
    return r_c17_indent(emitter, depth + 1U) &&
           r_c17_format(emitter, "r_present[%u] = true;\n", slot) && r_c17_indent(emitter, depth) &&
           r_c17_write(emitter, "}\n");
}
static bool r_c17_json_embedded_defaults(RC17Emitter *emitter,
                                         RSymbolId function,
                                         const RSemanticAggregate *aggregate,
                                         uint32_t parent,
                                         uint32_t first,
                                         uint32_t end) {
    for (uint32_t i = first; i < end;) {
        const RJsonSchemaField *entry =
            &emitter->frontend->json_fields[aggregate->first_json_field + i];
        const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
        if (entry->parent != parent)
            return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        if (entry->embedded) {
            if (!r_c17_json_embedded_defaults(emitter, function, aggregate, i, i + 1U, entry->end))
                return false;
            const RSemanticAggregate *child = r_c17_aggregate_for_type(emitter, field->type, NULL);
            if (child->field_count == 0U &&
                !r_c17_format(emitter, "    r_slot_%u->r_empty = UINT8_C(0);\n", i))
                return false;
            if (!r_c17_format(emitter, "    r_present[%u] = true;\n", i))
                return false;
        } else if (entry->collector) {
            if (!r_c17_json_embedded_collector(emitter, aggregate, i, 1U))
                return false;
        } else {
            if (!r_c17_format(emitter, "    if (!r_present[%u]) {\n", i))
                return false;
            if (field->json.default_kind != R_JSON_DEFAULT_NONE) {
                if (!r_c17_json_default(emitter, field, i))
                    return false;
            } else if (entry->excluded &&
                       r_semantic_type_has_default(emitter->frontend, field->type)) {
                if (!r_c17_write(emitter, "        ") ||
                    !r_c17_json_ordinary_default_name(emitter, function, field->type) ||
                    !r_c17_format(emitter, "(c, r_slot_%u);\n", i))
                    return false;
            } else if (!r_c17_write(emitter, "        if (!") ||
                       !r_c17_json_decode_name(emitter, function, field->type) ||
                       !r_c17_format(
                           emitter,
                           "(c, r_slot_%u, false, true)) {\n            goto r_fail;\n        }\n",
                           i))
                return false;
            if (!r_c17_format(emitter, "        r_present[%u] = true;\n    }\n", i))
                return false;
        }
        i = entry->end;
    }
    return true;
}
static bool r_c17_json_embedded_unknown(RC17Emitter *emitter,
                                        RSymbolId function,
                                        const RSemanticAggregate *aggregate,
                                        uint32_t collector) {
    const RJsonSchemaField *entry =
        &emitter->frontend->json_fields[aggregate->first_json_field + collector];
    const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
    const RSemanticType *type = r_c17_type(emitter, field->type);
    bool dict = type->kind == R_SEMANTIC_TYPE_DICT;
    /* Allocate the collector before the unknown key so its failure has no untracked temporary. */
    if (!r_c17_write(emitter, "        if (r_selected == -1) {\n") ||
        !r_c17_json_embedded_collector(emitter, aggregate, collector, 3U) ||
        !r_c17_write(
            emitter,
            "            RStdString owned_key;\n"
            "            if (!r_json_cursor_default_string(c, &owned_key, c->token.text)) {\n"
            "                goto r_fail;\n            }\n"
            "            if (!r_json_cursor_next(c)) {\n"
            "                r_std_string_destroy(&owned_key);\n                goto r_fail;\n     "
            "       }\n"
            "            ") ||
        !(dict ? r_c17_emit_value_type(emitter, type->second)
               : r_c17_write(emitter, "RStdJsonValue")) ||
        !r_c17_write(emitter, " member;\n            if (!"))
        return false;
    if (dict) {
        if (!r_c17_json_decode_name(emitter, function, type->second) ||
            !r_c17_write(emitter, "(c, &member, false, false)"))
            return false;
    } else if (!r_c17_write(emitter, "r_json_cursor_value(c, &member, false)"))
        return false;
    if (!r_c17_write(emitter,
                     ") {\n                r_std_string_destroy(&owned_key);\n                goto "
                     "r_fail;\n            }\n"))
        return false;
    if (dict) {
        if (!r_c17_format(emitter,
                          "            bool inserted = r_json_cursor_dict_insert(c, r_slot_%u, "
                          "&owned_key, &member);\n",
                          collector))
            return false;
    } else if (!r_c17_write(
                   emitter,
                   "            RStdStringView key_view = r_std_string_as_str(&owned_key);\n") ||
               !r_c17_format(
                   emitter,
                   "            c->outcome = r_std_json_insert(\n"
                   "                r_slot_%u, (RStdJsonByteView){key_view.data, key_view.length}, "
                   "&member);\n"
                   "            bool inserted = c->outcome.status == R_STD_JSON_CALL_SUCCESS;\n",
                   collector))
        return false;
    if (!r_c17_write(emitter,
                     "            r_std_string_destroy(&owned_key);\n"
                     "            if (!inserted) {\n"))
        return false;
    if (!dict) {
        if (!r_c17_write(emitter, "                r_std_json_value_destroy(&member);\n"))
            return false;
    } else if (r_c17_type_requires_drop(emitter, type->second) &&
               (!r_c17_write(emitter, "                ") ||
                !r_c17_emit_type_drop_name(emitter, type->second) ||
                !r_c17_write(emitter, "(&member);\n")))
        return false;
    return r_c17_write(
        emitter, "                goto r_fail;\n            }\n            continue;\n        }\n");
}
static bool r_c17_json_decode_embedded(RC17Emitter *emitter, RSymbolId function, RTypeId id) {
    const RSemanticAggregate *aggregate = r_c17_aggregate_for_type(emitter, id, NULL);
    uint32_t count = aggregate->json_field_count;
    uint32_t collector = UINT32_MAX;
    if (!r_c17_write(emitter, "    if (d) {\n"))
        return false;
    if (aggregate->is_default_initializable) {
        if (!r_c17_write(emitter, "        ") ||
            !r_c17_json_ordinary_default_name(emitter, function, id) ||
            !r_c17_write(emitter, "(c, out);\n        return true;\n"))
            return false;
    } else if (!r_c17_write(emitter,
                            "        return r_json_cursor_fail(c, R_STD_JSON_ERROR_TYPE);\n"))
        return false;
    if (!r_c17_format(
            emitter, "    }\n    bool r_present[%u] = {false};\n", count == 0U ? 1U : count))
        return false;
    for (uint32_t i = 0U; i < count; ++i) {
        const RJsonSchemaField *entry =
            &emitter->frontend->json_fields[aggregate->first_json_field + i];
        const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
        if (!r_c17_write(emitter, "    ") || !r_c17_emit_value_type(emitter, field->type) ||
            !r_c17_format(emitter, " *r_slot_%u = &", i))
            return false;
        if (!(entry->parent == UINT32_MAX ? r_c17_write(emitter, "out->")
                                          : r_c17_format(emitter, "r_slot_%u->", entry->parent)) ||
            !r_c17_emit_field_name(emitter, field) || !r_c17_write(emitter, ";\n"))
            return false;
        if (entry->collector)
            collector = i;
        if (entry->embedded || entry->collector || entry->excluded)
            continue;
        uint32_t name = (field->json.flags & R_JSON_FIELD_NAME) != 0U ? field->json.name_intern_id
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
                     "    if (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_OBJECT_BEGIN) || "
                     "!r_json_cursor_next(c)) {\n"
                     "        goto r_fail;\n    }\n"
                     "    while (c->has_token && c->token.kind != R_STD_JSON_TOKEN_OBJECT_END) {\n"
                     "        int32_t r_selected = -1;\n"
                     "        RStdJsonByteView key = c->token.text;\n        (void)key;\n"
                     "        if (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_KEY)) {\n            "
                     "goto r_fail;\n        }\n"))
        return false;
    for (uint32_t pass = 0U; pass < 2U; ++pass) {
        uint32_t indent = pass == 0U ? 2U : 3U;
        if (pass == 1U && !r_c17_write(emitter, "        if (r_selected == -1) {\n"))
            return false;
        for (uint32_t i = 0U; i < count; ++i) {
            const RJsonSchemaField *entry =
                &emitter->frontend->json_fields[aggregate->first_json_field + i];
            const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
            if (entry->embedded || entry->collector || entry->excluded ||
                (pass == 1U && (field->json.flags & R_JSON_FIELD_CASE) != 0U &&
                 !field->json.ignore_case))
                continue;
            if (!r_c17_indent(emitter, indent) || !r_c17_write(emitter, "if ("))
                return false;
            if (pass == 1U && (field->json.flags & R_JSON_FIELD_CASE) == 0U &&
                !r_c17_write(emitter, "c->options.ignore_case && "))
                return false;
            if (!r_c17_format(emitter,
                              "r_std_json_name_equal(key, key_%u, %s)) {\n",
                              i,
                              pass == 1U ? "true" : "false"))
                return false;
            if (pass == 1U && !r_c17_write(emitter,
                                           "                if (r_selected != -1) {\n"
                                           "                    (void)r_json_cursor_fail(c, "
                                           "R_STD_JSON_ERROR_AMBIGUOUS_FIELD);\n"
                                           "                    goto r_fail;\n                }\n"))
                return false;
            if (!r_c17_indent(emitter, indent + 1U) ||
                !r_c17_format(emitter, "r_selected = %u;\n", i) || !r_c17_indent(emitter, indent) ||
                !r_c17_write(emitter, "}\n"))
                return false;
        }
        if (pass == 1U && !r_c17_write(emitter, "        }\n"))
            return false;
    }
    if (!r_c17_write(emitter,
                     "        if (r_selected >= 0 && r_present[(size_t)r_selected]) {\n"
                     "            (void)r_json_cursor_fail(c, R_STD_JSON_ERROR_DUPLICATE_KEY);\n   "
                     "         goto r_fail;\n        }\n"
                     "        if (r_selected == -1 && c->options.reject_unknown_fields) {\n"
                     "            (void)r_json_cursor_fail(c, R_STD_JSON_ERROR_UNKNOWN_FIELD);\n   "
                     "         goto r_fail;\n        }\n"))
        return false;
    if (collector != UINT32_MAX &&
        !r_c17_json_embedded_unknown(emitter, function, aggregate, collector))
        return false;
    if (!r_c17_write(emitter,
                     "        if (!r_json_cursor_next(c)) {\n            goto r_fail;\n        }\n"
                     "        switch (r_selected) {\n"))
        return false;
    for (uint32_t i = 0U; i < count; ++i) {
        const RJsonSchemaField *entry =
            &emitter->frontend->json_fields[aggregate->first_json_field + i];
        const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
        if (entry->embedded || entry->collector || entry->excluded)
            continue;
        if (!r_c17_format(emitter, "        case %u:\n            if (!", i) ||
            !r_c17_json_decode_name(emitter, function, field->type) ||
            !r_c17_format(
                emitter,
                "(c, r_slot_%u, %s, false)) {\n                goto r_fail;\n            }\n"
                "            r_present[%u] = true;\n            break;\n",
                i,
                (field->json.flags & R_JSON_FIELD_STRING) != 0U ? "true" : "false",
                i))
            return false;
    }
    if (!r_c17_write(emitter,
                     "        default:\n            if (!r_json_cursor_skip(c)) {\n                "
                     "goto r_fail;\n            }\n"
                     "            break;\n        }\n    }\n"
                     "    if (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_OBJECT_END)) {\n        "
                     "goto r_fail;\n    }\n"))
        return false;
    for (uint32_t i = 0U; i < count; ++i) {
        const RJsonSchemaField *entry =
            &emitter->frontend->json_fields[aggregate->first_json_field + i];
        const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
        if (entry->embedded || entry->collector || entry->excluded ||
            (field->json.flags & R_JSON_FIELD_OPTIONAL) != 0U)
            continue;
        if (!r_c17_format(emitter,
                          "    if (!r_present[%u]) {\n"
                          "        (void)r_json_cursor_fail(c, R_STD_JSON_ERROR_MISSING_FIELD);\n  "
                          "      goto r_fail;\n    }\n",
                          i))
            return false;
    }
    if (!r_c17_json_embedded_defaults(emitter, function, aggregate, UINT32_MAX, 0U, count) ||
        !r_c17_write(emitter,
                     "    if (r_json_cursor_next(c)) {\n        return true;\n    }\nr_fail:\n"))
        return false;
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
    return r_c17_write(emitter, "    return false;\n");
}
