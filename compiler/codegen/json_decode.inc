#include "json_defaults.inc"
#include "json_ordinary_default.inc"

/* Typed readers consume scanner tokens directly; only initialized children are destroyed. */
static bool r_c17_json_decode_name(RC17Emitter *emitter, RSymbolId function, RTypeId type) {
    return r_c17_format(
        emitter, "r_jd_%08" PRIu32 "_%08" PRIu32, emitter->function_ordinals[function], type);
}
static bool r_c17_json_decode_signature(RC17Emitter *emitter, RSymbolId function, RTypeId type) {
    return r_c17_write(emitter, "static inline bool ") &&
           r_c17_json_decode_name(emitter, function, type) &&
           r_c17_write(emitter, "(RStdJsonCursor *c, ") && r_c17_emit_value_type(emitter, type) &&
           r_c17_write(emitter, " *out, bool q, bool d)");
}
static bool r_c17_json_field_key(RC17Emitter *emitter, const RSemanticField *field) {
    uint32_t id =
        field->json.flags & R_JSON_FIELD_NAME ? field->json.name_intern_id : field->name_intern_id;
    const RInternEntry *key = &emitter->frontend->intern_entries[id - 1U];
    return r_c17_format(
        emitter, "(RStdJsonByteView){r_key_%u, %zuU}", field->layout_index, key->length);
}
static bool r_c17_json_field_read(RC17Emitter *emitter,
                                  RSymbolId function,
                                  const RSemanticField *field,
                                  bool default_value) {
    if (default_value && field->json.default_kind != R_JSON_DEFAULT_NONE)
        return r_c17_json_default(emitter, field, UINT32_MAX);
    if (default_value && (field->is_protected || (field->json.flags & R_JSON_FIELD_SKIP) != 0U) &&
        r_semantic_type_has_default(emitter->frontend, field->type))
        return r_c17_write(emitter, "        ") &&
               r_c17_json_ordinary_default_name(emitter, function, field->type) &&
               r_c17_write(emitter, "(c, &out->") && r_c17_emit_field_name(emitter, field) &&
               r_c17_write(emitter, ");\n");
    uint32_t depth = default_value ? 2U : 4U;
    return r_c17_indent(emitter, depth) && r_c17_write(emitter, "if (!") &&
           r_c17_json_decode_name(emitter, function, field->type) &&
           r_c17_write(emitter, "(c, &out->") && r_c17_emit_field_name(emitter, field) &&
           r_c17_format(emitter,
                        ", %s, %s)) {\n",
                        field->json.flags & R_JSON_FIELD_STRING ? "true" : "false",
                        default_value ? "true" : "false") &&
           r_c17_indent(emitter, depth + 1U) && r_c17_write(emitter, "goto r_fail;\n") &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "}\n");
}
#include "json_embedded.inc"

static bool r_c17_json_decode_struct(RC17Emitter *emitter, RSymbolId function, RTypeId id) {
    const RSemanticAggregate *aggregate = r_c17_aggregate_for_type(emitter, id, NULL);
    for (uint32_t i = 0U; i < aggregate->field_count; ++i)
        if ((emitter->frontend->semantic_fields[aggregate->first_field + i].json.flags &
             R_JSON_FIELD_EMBED) != 0U)
            return r_c17_json_decode_embedded(emitter, function, id);
    if (aggregate->is_default_initializable &&
        (!r_c17_write(emitter, "    if (d) {\n        ") ||
         !r_c17_json_ordinary_default_name(emitter, function, id) ||
         !r_c17_write(emitter, "(c, out);\n        return true;\n    }\n")))
        return false;
    if (aggregate->field_count == 0U && !r_c17_write(emitter, "    out->r_empty = UINT8_C(0);\n"))
        return false;
    for (uint32_t i = 0U; i < aggregate->field_count; ++i) {
        const RSemanticField *field =
            &emitter->frontend->semantic_fields[aggregate->first_field + i];
        if (field->is_protected || (field->json.flags & R_JSON_FIELD_SKIP) != 0U)
            continue;
        uint32_t key_id = field->json.flags & R_JSON_FIELD_NAME ? field->json.name_intern_id
                                                                : field->name_intern_id;
        const RInternEntry *key = &emitter->frontend->intern_entries[key_id - 1U];
        if (!r_c17_format(emitter, "    static const uint8_t r_key_%u[] = ", field->layout_index))
            return false;
        if (key->length == 0U) {
            if (!r_c17_write(emitter, "\"\";\n"))
                return false;
        } else if (!r_c17_emit_byte_string(emitter, key->bytes, key->length, 1U))
            return false;
    }
    if (!r_c17_format(emitter,
                      "    bool r_present[%u] = {false};\n"
                      "    (void)r_present;\n"
                      "    if (!d) {\n"
                      "        if (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_OBJECT_BEGIN) || "
                      "!r_json_cursor_next(c)) {\n"
                      "            goto r_fail;\n"
                      "        }\n"
                      "        while (c->has_token && c->token.kind != "
                      "R_STD_JSON_TOKEN_OBJECT_END) {\n"
                      "            int32_t r_selected = -1;\n"
                      "            RStdJsonByteView r_key = c->token.text;\n"
                      "            (void)r_key;\n"
                      "            if (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_KEY)) {\n"
                      "                goto r_fail;\n"
                      "            }\n",
                      aggregate->field_count == 0U ? 1U : aggregate->field_count))
        return false;
    for (uint32_t pass = 0U; pass < 2U; ++pass) {
        if (pass != 0U && !r_c17_write(emitter, "            if (r_selected == -1) {\n"))
            return false;
        for (uint32_t i = 0U; i < aggregate->field_count; ++i) {
            const RSemanticField *field =
                &emitter->frontend->semantic_fields[aggregate->first_field + i];
            if (field->is_protected || (field->json.flags & R_JSON_FIELD_SKIP) != 0U)
                continue;
            if (pass != 0U && (field->json.flags & R_JSON_FIELD_CASE) != 0U &&
                !field->json.ignore_case)
                continue;
            if (!r_c17_indent(emitter, pass == 0U ? 3U : 4U) || !r_c17_write(emitter, "if ("))
                return false;
            if (pass != 0U && (field->json.flags & R_JSON_FIELD_CASE) == 0U &&
                !r_c17_write(emitter, "c->options.ignore_case &&\n                    "))
                return false;
            if (!r_c17_write(emitter, "r_std_json_name_equal(r_key, ") ||
                !r_c17_json_field_key(emitter, field) ||
                !r_c17_format(emitter, ", %s)) {\n", pass == 0U ? "false" : "true"))
                return false;
            if (pass != 0U && !r_c17_write(emitter,
                                           "                    if (r_selected != -1) {\n"
                                           "                        (void)r_json_cursor_fail(c, "
                                           "R_STD_JSON_ERROR_AMBIGUOUS_FIELD);\n"
                                           "                        goto r_fail;\n"
                                           "                    }\n"))
                return false;
            if (!r_c17_indent(emitter, pass == 0U ? 4U : 5U) ||
                !r_c17_format(emitter, "r_selected = %u;\n", i) ||
                !r_c17_indent(emitter, pass == 0U ? 3U : 4U) || !r_c17_write(emitter, "}\n"))
                return false;
        }
        if (pass != 0U && !r_c17_write(emitter, "            }\n"))
            return false;
    }
    if (!r_c17_write(
            emitter,
            "            if (r_selected >= 0 && r_present[(size_t)r_selected]) {\n"
            "                (void)r_json_cursor_fail(c, R_STD_JSON_ERROR_DUPLICATE_KEY);\n"
            "                goto r_fail;\n"
            "            }\n"
            "            if (r_selected == -1 && c->options.reject_unknown_fields) {\n"
            "                (void)r_json_cursor_fail(c, R_STD_JSON_ERROR_UNKNOWN_FIELD);\n"
            "                goto r_fail;\n"
            "            }\n"
            "            if (!r_json_cursor_next(c)) {\n"
            "                goto r_fail;\n"
            "            }\n"
            "            switch (r_selected) {\n"))
        return false;
    for (uint32_t i = 0U; i < aggregate->field_count; ++i) {
        const RSemanticField *field =
            &emitter->frontend->semantic_fields[aggregate->first_field + i];
        if (field->is_protected || (field->json.flags & R_JSON_FIELD_SKIP) != 0U)
            continue;
        if (!r_c17_format(emitter, "            case %u:\n", i) ||
            !r_c17_json_field_read(emitter, function, field, false) ||
            !r_c17_format(
                emitter, "                r_present[%u] = true;\n                break;\n", i))
            return false;
    }
    if (!r_c17_write(emitter,
                     "            default:\n"
                     "                if (!r_json_cursor_skip(c)) {\n"
                     "                    goto r_fail;\n"
                     "                }\n"
                     "                break;\n"
                     "            }\n"
                     "        }\n"
                     "        if (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_OBJECT_END)) {\n"
                     "            goto r_fail;\n"
                     "        }\n"))
        return false;
    for (uint32_t i = 0U; i < aggregate->field_count; ++i) {
        const RSemanticField *field =
            &emitter->frontend->semantic_fields[aggregate->first_field + i];
        if (field->is_protected ||
            (field->json.flags & (R_JSON_FIELD_SKIP | R_JSON_FIELD_OPTIONAL)) != 0U)
            continue;
        if (!r_c17_format(
                emitter,
                "        if (!r_present[%u]) {\n"
                "            (void)r_json_cursor_fail(c, R_STD_JSON_ERROR_MISSING_FIELD);\n"
                "            goto r_fail;\n"
                "        }\n",
                i))
            return false;
    }
    if (!r_c17_write(emitter, "    }\n"))
        return false;
    for (uint32_t i = 0U; i < aggregate->field_count; ++i) {
        const RSemanticField *field =
            &emitter->frontend->semantic_fields[aggregate->first_field + i];
        if (!r_c17_format(emitter, "    if (!r_present[%u]) {\n", i) ||
            !r_c17_json_field_read(emitter, function, field, true) ||
            !r_c17_format(emitter, "        r_present[%u] = true;\n    }\n", i))
            return false;
    }
    if (!r_c17_write(emitter,
                     "    if (d || r_json_cursor_next(c)) {\n        return true;\n    }\n"
                     "r_fail:\n"))
        return false;
    for (uint32_t i = aggregate->field_count; i > 0U; --i) {
        const RSemanticField *field =
            &emitter->frontend->semantic_fields[aggregate->first_field + i - 1U];
        if (!r_c17_type_requires_drop(emitter, field->type))
            continue;
        if (!r_c17_format(emitter, "    if (r_present[%u]) {\n        ", i - 1U) ||
            !r_c17_emit_type_drop_name(emitter, field->type) || !r_c17_write(emitter, "(&out->") ||
            !r_c17_emit_field_name(emitter, field) || !r_c17_write(emitter, ");\n    }\n"))
            return false;
    }
    return r_c17_write(emitter, "    return false;\n");
}
static bool r_c17_json_decode_enum(RC17Emitter *emitter, RSymbolId function, RTypeId id) {
    const RSemanticAggregate *aggregate = r_c17_aggregate_for_type(emitter, id, NULL);
    for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
        const RSemanticVariant *variant =
            &emitter->frontend->semantic_variants[aggregate->first_variant + i];
        const RInternEntry *name = &emitter->frontend->intern_entries[variant->name_intern_id - 1U];
        if (!r_c17_format(emitter, "    static const uint8_t name_%u[] = ", i) ||
            !r_c17_emit_byte_string(emitter, name->bytes, name->length, 1U))
            return false;
    }
    if (!r_c17_write(emitter, "    if (d) {\n"))
        return false;
    bool zero = false;
    for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
        const RSemanticVariant *variant =
            &emitter->frontend->semantic_variants[aggregate->first_variant + i];
        if (variant->value != 0U)
            continue;
        zero = true;
        if (variant->payload_type != 0U &&
            (!r_c17_write(emitter, "        if (!") ||
             !r_c17_json_decode_name(emitter, function, variant->payload_type) ||
             !r_c17_format(emitter,
                           "(c, &out->r_payload.r_v%08u, false, true)) {\n            return "
                           "false;\n        }\n",
                           i)))
            return false;
        if (aggregate->is_tagged) {
            if (!r_c17_format(emitter, "        out->r_tag = UINT32_C(%u);\n", i))
                return false;
        } else if (!r_c17_write(emitter, "        *out = ") ||
                   !r_c17_emit_enum_literal(emitter, id, 0U) || !r_c17_write(emitter, ";\n"))
            return false;
        if (!r_c17_write(emitter, "        return true;\n"))
            return false;
        break;
    }
    if (!zero &&
        !r_c17_write(emitter, "        return r_json_cursor_fail(c, R_STD_JSON_ERROR_TYPE);\n"))
        return false;
    if (!r_c17_write(emitter,
                     "    }\n    bool object = c->has_token && c->token.kind == "
                     "R_STD_JSON_TOKEN_OBJECT_BEGIN;\n"
                     "    if (object && !r_json_cursor_next(c)) {\n        return false;\n    }\n"
                     "    if (!r_json_cursor_expect(c, object ? R_STD_JSON_TOKEN_KEY : "
                     "R_STD_JSON_TOKEN_STRING)) {\n"
                     "        return false;\n    }\n"))
        return false;
    for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
        const RSemanticVariant *variant =
            &emitter->frontend->semantic_variants[aggregate->first_variant + i];
        const RInternEntry *name = &emitter->frontend->intern_entries[variant->name_intern_id - 1U];
        if (!r_c17_format(emitter,
                          "    RStdJsonByteView key_%u = {name_%u, %zuU};\n"
                          "    if (r_std_json_name_equal(c->token.text, key_%u, false)) {\n",
                          i,
                          i,
                          name->length,
                          i) ||
            !r_c17_format(
                emitter,
                "        if (%sobject) {\n"
                "            return r_json_cursor_fail(c, R_STD_JSON_ERROR_TYPE);\n        }\n",
                variant->payload_type == 0U ? "" : "!"))
            return false;
        if (!r_c17_write(
                emitter,
                "        if (!r_json_cursor_next(c)) {\n            return false;\n        }\n"))
            return false;
        if (variant->payload_type != 0U) {
            if (!r_c17_write(emitter, "        if (!") ||
                !r_c17_json_decode_name(emitter, function, variant->payload_type) ||
                !r_c17_format(emitter,
                              "(c, &out->r_payload.r_v%08u, false, false)) {\n            return "
                              "false;\n        }\n",
                              i) ||
                !r_c17_write(emitter,
                             "        if (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_OBJECT_END) || "
                             "!r_json_cursor_next(c)) {\n"))
                return false;
            if (r_c17_type_requires_drop(emitter, variant->payload_type) &&
                (!r_c17_write(emitter, "            ") ||
                 !r_c17_emit_type_drop_name(emitter, variant->payload_type) ||
                 !r_c17_format(emitter, "(&out->r_payload.r_v%08u);\n", i)))
                return false;
            if (!r_c17_write(emitter, "            return false;\n        }\n"))
                return false;
        }
        if (aggregate->is_tagged) {
            if (!r_c17_format(emitter, "        out->r_tag = UINT32_C(%u);\n", i))
                return false;
        } else if (!r_c17_write(emitter, "        *out = ") ||
                   !r_c17_emit_enum_literal(emitter, id, variant->value) ||
                   !r_c17_write(emitter, ";\n"))
            return false;
        if (!r_c17_write(emitter, "        return true;\n    }\n"))
            return false;
    }
    return r_c17_write(emitter, "    return r_json_cursor_fail(c, R_STD_JSON_ERROR_TYPE);\n");
}
static bool r_c17_json_decode_array(RC17Emitter *emitter, RSymbolId function, RTypeId id) {
    const RSemanticType *type = r_c17_type(emitter, id);
    bool list = type->kind == R_SEMANTIC_TYPE_LIST;
    if (!r_c17_write(emitter,
                     list ? "    (void)r_runtime_list_initialize(out,\n                            "
                            "        c->allocator,\n"
                          : "    r_runtime_array_initialize(out,\n                               "
                            "c->allocator,\n") ||
        !r_c17_emit_runtime_type_info_at(emitter, type->base, list ? 36U : 31U) ||
        !r_c17_write(emitter,
                     ");\n    if (d) {\n        return true;\n    }\n"
                     "    if (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_ARRAY_BEGIN) || "
                     "!r_json_cursor_next(c)) {\n"
                     "        goto r_fail;\n    }\n"
                     "    while (c->has_token && c->token.kind != R_STD_JSON_TOKEN_ARRAY_END) {\n  "
                     "      ") ||
        !r_c17_emit_value_type(emitter, type->base) ||
        !r_c17_write(emitter, " member;\n        if (!") ||
        !r_c17_json_decode_name(emitter, function, type->base) ||
        !r_c17_write(emitter,
                     "(c, &member, false, false)) {\n            goto r_fail;\n        }\n") ||
        !r_c17_write(emitter,
                     list ? "        if (!r_json_cursor_list_push(c, out, &member)) {\n"
                          : "        if (!r_json_cursor_array_push(c, out, &member)) {\n"))
        return false;
    if (r_c17_type_requires_drop(emitter, type->base) &&
        (!r_c17_write(emitter, "            ") || !r_c17_emit_type_drop_name(emitter, type->base) ||
         !r_c17_write(emitter, "(&member);\n")))
        return false;
    return r_c17_write(emitter,
                       "            goto r_fail;\n        }\n    }\n"
                       "    if (r_json_cursor_expect(c, R_STD_JSON_TOKEN_ARRAY_END) && "
                       "r_json_cursor_next(c)) {\n"
                       "        return true;\n    }\nr_fail:\n") &&
           r_c17_write(emitter,
                       list ? "    r_runtime_list_destroy(out);\n    return false;\n"
                            : "    r_runtime_array_destroy(out);\n    return false;\n");
}
static bool r_c17_json_decode_dict(RC17Emitter *emitter, RSymbolId function, RTypeId id) {
    const RSemanticType *type = r_c17_type(emitter, id);
    if (!r_c17_write(
            emitter,
            "    r_json_cursor_dict_initialize(c,\n                                  out,\n") ||
        !r_c17_emit_runtime_type_info_at(emitter, type->second, 34U) ||
        !r_c17_write(emitter,
                     ");\n    if (d) {\n        return true;\n    }\n"
                     "    if (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_OBJECT_BEGIN) || "
                     "!r_json_cursor_next(c)) {\n"
                     "        goto r_fail;\n    }\n"
                     "    while (c->has_token && c->token.kind != R_STD_JSON_TOKEN_OBJECT_END) {\n"
                     "        RStdString key;\n        ") ||
        !r_c17_emit_value_type(emitter, type->second) ||
        !r_c17_write(emitter,
                     " member;\n"
                     "        if (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_KEY) ||\n"
                     "            !r_json_cursor_default_string(c, &key, c->token.text)) {\n"
                     "            goto r_fail;\n        }\n"
                     "        if (!r_json_cursor_next(c) || !") ||
        !r_c17_json_decode_name(emitter, function, type->second) ||
        !r_c17_write(
            emitter,
            "(c, &member, false, false)) {\n"
            "            r_std_string_destroy(&key);\n            goto r_fail;\n        }\n"
            "        if (!r_json_cursor_dict_insert(c, out, &key, &member)) {\n"
            "            r_std_string_destroy(&key);\n"))
        return false;
    if (r_c17_type_requires_drop(emitter, type->second) &&
        (!r_c17_write(emitter, "            ") ||
         !r_c17_emit_type_drop_name(emitter, type->second) ||
         !r_c17_write(emitter, "(&member);\n")))
        return false;
    return r_c17_write(
        emitter,
        "            goto r_fail;\n        }\n    }\n"
        "    if (r_json_cursor_expect(c, R_STD_JSON_TOKEN_OBJECT_END) && r_json_cursor_next(c)) {\n"
        "        return true;\n    }\nr_fail:\n    r_runtime_dict_destroy(out);\n    return "
        "false;\n");
}
static bool r_c17_json_decode_body(RC17Emitter *emitter, RSymbolId function, RTypeId id) {
    const RSemanticType *type = r_c17_type(emitter, r_c17_value_type(emitter, id));
    RSemanticTypeKind kind = type->kind;
    bool c_bool = kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC && type->length == R_TOKEN_KW_C_BOOL;
    bool floating = kind == R_SEMANTIC_TYPE_F32 || kind == R_SEMANTIC_TYPE_F64 ||
                    (kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC &&
                     (type->length == R_TOKEN_KW_C_FLOAT || type->length == R_TOKEN_KW_C_DOUBLE));
    if (!r_c17_write(emitter, "    (void)q;\n"))
        return false;
    const RSemanticAggregate *custom = r_c17_aggregate_for_type(emitter, id, NULL);
    if (custom != NULL && custom->json_unmarshal_function != 0U)
        return r_c17_json_decode_hook(emitter, function, custom);
    if (r_c17_type_is_exact_named_standard(emitter, id, "std.json::value"))
        return r_c17_write(emitter,
                           "    if (d) {\n        *out = (RStdJsonValue){0};\n        return "
                           "true;\n    }\n    return r_json_cursor_value(c, out, q);\n");
    if (r_c17_type_is_exact_named_standard(emitter, id, "std.json::number"))
        return r_c17_write(
            emitter,
            "    if (d) {\n        return r_json_cursor_fail(c, R_STD_JSON_ERROR_TYPE);\n    }\n   "
            " return r_json_cursor_number(c, out, q);\n");
    if (kind == R_SEMANTIC_TYPE_ARRAY || kind == R_SEMANTIC_TYPE_LIST)
        return r_c17_json_decode_array(emitter, function, id);
    if (kind == R_SEMANTIC_TYPE_DICT)
        return r_c17_json_decode_dict(emitter, function, id);
    if (kind == R_SEMANTIC_TYPE_ENUM)
        return r_c17_json_decode_enum(emitter, function, id);
    if (kind == R_SEMANTIC_TYPE_STRUCT)
        return r_c17_json_decode_struct(emitter, function, id);
    if (r_c17_type_is_exact_named_standard(emitter, id, "std.string::string"))
        return r_c17_write(emitter,
                           "    if (d) {\n"
                           "        return r_json_cursor_default_string(c, out, "
                           "(RStdJsonByteView){NULL, 0U});\n"
                           "    }\n    return r_json_cursor_string(c, out);\n");
    if (kind == R_SEMANTIC_TYPE_OPTION) {
        return r_c17_write(emitter,
                           "    if (d || (c->has_token && c->token.kind == "
                           "R_STD_JSON_TOKEN_NULL)) {\n"
                           "        out->r_tag = UINT32_C(0);\n"
                           "        return d || r_json_cursor_next(c);\n    }\n    if (!") &&
               r_c17_json_decode_name(emitter, function, type->base) &&
               r_c17_write(emitter,
                           "(c, &out->r_payload.r_some, q, false)) {\n"
                           "        return false;\n    }\n    out->r_tag = UINT32_C(1);\n    "
                           "return true;\n");
    }
    if (kind == R_SEMANTIC_TYPE_FIXED_ARRAY) {
        if (!r_c17_write(
                emitter,
                "    size_t r_initialized = 0U;\n"
                "    if (!d && (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_ARRAY_BEGIN) ||\n"
                "                       !r_json_cursor_next(c))) {\n"
                "        goto r_fail;\n    }\n") ||
            !r_c17_format(emitter,
                          "    while (r_initialized < %" PRIu64 "U) {\n        if (!",
                          type->length) ||
            !r_c17_json_decode_name(emitter, function, type->base) ||
            !r_c17_write(emitter,
                         "(c, &out->r_data[r_initialized], false, d)) {\n"
                         "            goto r_fail;\n        }\n        ++r_initialized;\n    }\n"
                         "    if (d || (r_json_cursor_expect(c, R_STD_JSON_TOKEN_ARRAY_END) &&\n"
                         "                      r_json_cursor_next(c))) {\n"
                         "        return true;\n    }\nr_fail:\n"))
            return false;
        if (r_c17_type_requires_drop(emitter, type->base) &&
            (!r_c17_write(
                 emitter,
                 "    while (r_initialized != 0U) {\n        --r_initialized;\n        ") ||
             !r_c17_emit_type_drop_name(emitter, type->base) ||
             !r_c17_write(emitter, "(&out->r_data[r_initialized]);\n    }\n")))
            return false;
        return r_c17_write(emitter, "    return false;\n");
    }
    if (!r_c17_write(emitter, "    if (d) {\n        *out = 0;\n        return true;\n    }\n"))
        return false;
    if (kind == R_SEMANTIC_TYPE_BOOL || c_bool)
        return r_c17_write(emitter, "    return r_json_cursor_boolean(c, out);\n");
    if (kind == R_SEMANTIC_TYPE_CHAR)
        return r_c17_write(emitter, "    return r_json_cursor_char(c, out);\n");
    if (kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC && type->length == R_TOKEN_KW_C_LONG_DOUBLE)
        return r_c17_write(emitter, "    return r_json_cursor_long_double(c, q, out);\n");
    if (floating)
        return r_c17_write(emitter, "    double r_value;\n    if (!r_json_cursor_float(c, q, ") &&
               r_c17_write(emitter,
                           kind == R_SEMANTIC_TYPE_F32 || type->length == R_TOKEN_KW_C_FLOAT
                               ? "true"
                               : "false") &&
               r_c17_write(emitter, ", &r_value)) {\n        return false;\n    }\n    *out = (") &&
               r_c17_emit_value_type(emitter, id) &&
               r_c17_write(emitter, ")r_value;\n    return true;\n");
    {
        RSemanticTypeKind rep = r_semantic_integer_representation(emitter->frontend, id);
        bool negative = rep >= R_SEMANTIC_TYPE_I8 && rep <= R_SEMANTIC_TYPE_ISIZE;
        unsigned bits = rep == R_SEMANTIC_TYPE_I8 || rep == R_SEMANTIC_TYPE_U8     ? 8U
                        : rep == R_SEMANTIC_TYPE_I16 || rep == R_SEMANTIC_TYPE_U16 ? 16U
                        : rep == R_SEMANTIC_TYPE_I32 || rep == R_SEMANTIC_TYPE_U32 ? 32U
                                                                                   : 64U;
        uint64_t positive =
            negative ? (UINT64_C(1) << (bits - 1U)) - 1U : UINT64_MAX >> (64U - bits);
        uint64_t minimum = negative ? UINT64_C(1) << (bits - 1U) : 0U;
        if (!r_c17_format(emitter,
                          "    bool r_negative;\n    uint64_t r_magnitude;\n"
                          "    uint64_t r_positive = UINT64_C(%" PRIu64 ");\n"
                          "    uint64_t r_minimum = UINT64_C(%" PRIu64 ");\n"
                          "    if (!r_json_cursor_integer(c, q, r_positive, r_minimum, "
                          "&r_negative, &r_magnitude)) {\n"
                          "        return false;\n    }\n",
                          positive,
                          minimum))
            return false;
        if (negative) {
            if (!r_c17_write(emitter,
                             "    if (r_negative && r_magnitude != 0U) {\n        *out = (") ||
                !r_c17_emit_value_type(emitter, id) ||
                !r_c17_write(
                    emitter,
                    ")(-(int64_t)(r_magnitude - 1U) - 1);\n    } else {\n        *out = (") ||
                !r_c17_emit_value_type(emitter, id) ||
                !r_c17_write(emitter, ")r_magnitude;\n    }\n"))
                return false;
        } else if (!r_c17_write(emitter, "    *out = (") || !r_c17_emit_value_type(emitter, id) ||
                   !r_c17_write(emitter, ")r_magnitude;\n"))
            return false;
        return r_c17_write(emitter, "    return true;\n");
    }
}
static bool
r_c17_json_decode_types(RC17Emitter *emitter, RSymbolId function, RTypeId id, uint8_t *seen) {
    const RSemanticType *type = r_c17_type(emitter, r_c17_value_type(emitter, id));
    if ((seen[id] & 1U) != 0U)
        return true;
    seen[id] |= 1U;
    if (!r_c17_json_decode_signature(emitter, function, id) || !r_c17_write(emitter, ";\n"))
        return false;
    if (type->kind == R_SEMANTIC_TYPE_STRUCT &&
        !r_c17_json_ordinary_default(emitter, function, id, seen))
        return false;
    const RSemanticAggregate *custom = r_c17_aggregate_for_type(emitter, id, NULL);
    if (custom != NULL && custom->json_unmarshal_function != 0U)
        return r_c17_json_decode_signature(emitter, function, id) && r_c17_write(emitter, " {\n") &&
               r_c17_json_decode_hook(emitter, function, custom) && r_c17_write(emitter, "}\n\n");
    if (type->kind == R_SEMANTIC_TYPE_ENUM) {
        const RSemanticAggregate *aggregate = r_c17_aggregate_for_type(emitter, id, NULL);
        for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
            RTypeId payload =
                emitter->frontend->semantic_variants[aggregate->first_variant + i].payload_type;
            if (payload != 0U && !r_c17_json_decode_types(emitter, function, payload, seen))
                return false;
        }
    }
    if (type->kind == R_SEMANTIC_TYPE_STRUCT) {
        const RSemanticAggregate *aggregate = r_c17_aggregate_for_type(emitter, id, NULL);
        for (uint32_t i = 0U; i < aggregate->json_field_count; ++i) {
            const RJsonSchemaField *entry =
                &emitter->frontend->json_fields[aggregate->first_json_field + i];
            const RSemanticField *field = &emitter->frontend->semantic_fields[entry->field];
            if (entry->embedded ||
                (entry->excluded && (field->json.flags & R_JSON_FIELD_DEFAULT) != 0U))
                continue;
            if (entry->excluded && r_semantic_type_has_default(emitter->frontend, field->type)) {
                if (!r_c17_json_ordinary_default(emitter, function, field->type, seen))
                    return false;
                continue;
            }
            RTypeId child = field->type;
            if (entry->collector) {
                const RSemanticType *collection = r_c17_type(emitter, child);
                if (collection->kind != R_SEMANTIC_TYPE_DICT)
                    continue;
                child = collection->second;
            }
            if (!r_c17_json_decode_types(emitter, function, child, seen))
                return false;
        }
    } else if (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY || type->kind == R_SEMANTIC_TYPE_OPTION ||
               type->kind == R_SEMANTIC_TYPE_ARRAY || type->kind == R_SEMANTIC_TYPE_LIST) {
        if (!r_c17_json_decode_types(emitter, function, type->base, seen))
            return false;
    }
    if (type->kind == R_SEMANTIC_TYPE_DICT &&
        !r_c17_json_decode_types(emitter, function, type->second, seen))
        return false;
    return r_c17_json_decode_signature(emitter, function, id) && r_c17_write(emitter, " {\n") &&
           r_c17_json_decode_body(emitter, function, id) && r_c17_write(emitter, "}\n\n");
}
static bool r_c17_emit_json_unmarshal(RC17Emitter *emitter, RSymbolId function_id) {
    const RSemanticSymbol *function = r_c17_symbol(emitter, function_id);
    const RHirNode *node = r_c17_node(emitter, emitter->function_nodes[function_id]);
    uint8_t *seen = r_c17_allocate(emitter, emitter->frontend->semantic_type_count + 1U);
    bool success;
    if (seen == NULL)
        return false;
    (void)memset(seen, 0, emitter->frontend->semantic_type_count + 1U);
    success = r_c17_json_decode_types(emitter, function_id, function->json_type, seen);
    r_c17_free(emitter, seen);
    if (!success || !r_c17_emit_function_signature(emitter, function_id, true) ||
        !r_c17_write(emitter, " {\n    RStdJsonCursor c;\n    RStdJsonResult r_status;\n    ") ||
        !r_c17_emit_value_type(emitter, function->json_type) ||
        !r_c17_write(emitter, " r_value;\n    RStdJsonByteView r_source = {\n        .data = ") ||
        !r_c17_format_argument(emitter, node, 0U) ||
        !r_c17_write(emitter, ".r_data,\n        .length = ") ||
        !r_c17_format_argument(emitter, node, 0U) ||
        !r_c17_write(emitter,
                     ".r_len,\n    };\n"
                     "    RStdJsonOptions r_options = ") ||
        !(function->parameter_count == 2U ? r_c17_format_argument(emitter, node, 1U)
                                          : r_c17_write(emitter, "R_STD_JSON_DEFAULT_OPTIONS")) ||
        !r_c17_write(emitter,
                     ";\n"
                     "    (void)r_json_cursor_initialize(&c, r_runtime_hosted_allocator(), "
                     "r_source, r_options);\n"
                     "    bool r_built = ") ||
        !r_c17_json_decode_name(emitter, function_id, function->json_type) ||
        !r_c17_write(emitter,
                     "(&c, &r_value, false, false);\n"
                     "    r_status = r_json_cursor_finish(&c);\n"
                     "    if (r_status.status == R_STD_JSON_CALL_SUCCESS) {\n"
                     "        r_effect_out->r_payload.r_ok = r_value;\n"
                     "        r_effect_out->r_tag = UINT32_C(0);\n"
                     "    } else {\n"))
        return false;
    if (r_c17_type_requires_drop(emitter, function->json_type)) {
        if (!r_c17_write(emitter, "        if (r_built) {\n            ") ||
            !r_c17_emit_type_drop_name(emitter, function->json_type) ||
            !r_c17_write(emitter, "(&r_value);\n        }\n"))
            return false;
    } else if (!r_c17_write(emitter, "        (void)r_built;\n"))
        return false;
    return r_c17_write(
        emitter,
        "        if (r_status.status == R_STD_JSON_CALL_ALLOCATION_ERROR) {\n"
        "            r_effect_out->r_payload.r_error_00000001 = r_status.allocation_error;\n"
        "            r_effect_out->r_tag = UINT32_C(1);\n"
        "        } else {\n"
        "            r_effect_out->r_payload.r_error_00000002 = r_status.error;\n"
        "            r_effect_out->r_tag = UINT32_C(2);\n"
        "        }\n    }\n}\n\n");
}
