/* Encoders borrow concrete R values and build owned output only after early omission tests. */
static bool
r_c17_json_encode_name(RC17Emitter *emitter, RSymbolId function, RTypeId type, bool zero) {
    return r_c17_format(
        emitter, zero ? "r_jz_%u_%u" : "r_je_%u_%u", emitter->function_ordinals[function], type);
}
static bool r_c17_json_encode_enter(RC17Emitter *emitter, uint32_t indent) {
    return r_c17_indent(emitter, indent) &&
           r_c17_write(emitter, "if (c->depth >= c->options.max_depth) {\n") &&
           r_c17_indent(emitter, indent + 1U) &&
           r_c17_write(emitter, "return (RStdJsonValueResult){\n") &&
           r_c17_indent(emitter, indent + 2U) &&
           r_c17_write(emitter, ".outcome.status = R_STD_JSON_CALL_JSON_ERROR,\n") &&
           r_c17_indent(emitter, indent + 2U) &&
           r_c17_write(emitter, ".outcome.error.code = R_STD_JSON_ERROR_DEPTH_LIMIT,\n") &&
           r_c17_indent(emitter, indent + 1U) && r_c17_write(emitter, "};\n") &&
           r_c17_indent(emitter, indent) && r_c17_write(emitter, "}\n") &&
           r_c17_indent(emitter, indent) &&
           r_c17_write(emitter, "RStdJsonEncodeContext nested = *c;\n") &&
           r_c17_indent(emitter, indent) && r_c17_write(emitter, "++nested.depth;\n") &&
           r_c17_indent(emitter, indent) && r_c17_write(emitter, "c = &nested;\n");
}
static bool
r_c17_json_encode_enum(RC17Emitter *emitter, RSymbolId function, RTypeId id, bool zero) {
    const RSemanticAggregate *aggregate = r_c17_aggregate_for_type(emitter, id, NULL);
    for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
        const RSemanticVariant *variant =
            &emitter->frontend->semantic_variants[aggregate->first_variant + i];
        const RInternEntry *name = &emitter->frontend->intern_entries[variant->name_intern_id - 1U];
        if (zero && variant->value != 0U)
            continue;
        if (aggregate->is_tagged) {
            if (!r_c17_format(emitter, "    if (v->r_tag == UINT32_C(%u)) {\n", i))
                return false;
        } else if (!r_c17_write(emitter, "    if (*v == ") ||
                   !r_c17_emit_enum_literal(emitter, id, variant->value) ||
                   !r_c17_write(emitter, ") {\n"))
            return false;
        if (zero) {
            if (variant->payload_type == 0U) {
                if (!r_c17_write(emitter, "        return true;\n"))
                    return false;
            } else if (!r_c17_write(emitter, "        return ") ||
                       !r_c17_json_encode_name(emitter, function, variant->payload_type, true) ||
                       !r_c17_format(emitter, "(&v->r_payload.r_v%08u);\n", i))
                return false;
        } else {
            if (!r_c17_write(emitter, "        static const uint8_t name[] = ") ||
                !r_c17_emit_byte_string(emitter, name->bytes, name->length, 2U) ||
                !r_c17_format(
                    emitter, "        RStdJsonByteView key = {name, %zuU};\n", name->length))
                return false;
            if (variant->payload_type == 0U) {
                if (!r_c17_write(emitter, "        return r_std_json_from_string(a, key);\n"))
                    return false;
            } else if (!r_c17_json_encode_enter(emitter, 2U) ||
                       !r_c17_write(emitter, "        RStdJsonValueResult member = ") ||
                       !r_c17_json_encode_name(emitter, function, variant->payload_type, false) ||
                       !r_c17_format(emitter, "(c, &v->r_payload.r_v%08u, false);\n", i) ||
                       !r_c17_write(emitter,
                                    "        if (member.outcome.status != R_STD_JSON_CALL_SUCCESS) "
                                    "{\n            return member;\n        }\n"
                                    "        RStdJsonValueResult r = r_std_json_object(a);\n"
                                    "        if (r.outcome.status == R_STD_JSON_CALL_SUCCESS) {\n"
                                    "            r.outcome = r_std_json_insert(&r.value, key, "
                                    "&member.value);\n        }\n"
                                    "        r_std_json_value_destroy(&member.value);\n"
                                    "        if (r.outcome.status != R_STD_JSON_CALL_SUCCESS) {\n"
                                    "            r_std_json_value_destroy(&r.value);\n        }\n  "
                                    "      return r;\n"))
                return false;
        }
        if (!r_c17_write(emitter, "    }\n"))
            return false;
    }
    return zero ? r_c17_write(emitter, "    return false;\n")
                : r_c17_write(emitter,
                              "    return (RStdJsonValueResult){\n"
                              "        .outcome.status = R_STD_JSON_CALL_JSON_ERROR,\n"
                              "        .outcome.error.code = R_STD_JSON_ERROR_TYPE,\n    };\n");
}
static bool r_c17_json_zero_signature(RC17Emitter *emitter, RSymbolId function, RTypeId id) {
    return r_c17_write(emitter, "static inline bool ") &&
           r_c17_json_encode_name(emitter, function, id, true) && r_c17_write(emitter, "(const ") &&
           r_c17_emit_value_type(emitter, id) && r_c17_write(emitter, " *v)");
}
static bool r_c17_json_zero_definition(RC17Emitter *emitter, RSymbolId function, RTypeId id) {
    const RSemanticType *type = r_c17_type(emitter, r_c17_value_type(emitter, id));
    if (!r_c17_json_zero_signature(emitter, function, id) ||
        !r_c17_write(emitter, " {\n    (void)v;\n"))
        return false;
    const RSemanticAggregate *custom = r_c17_aggregate_for_type(emitter, id, NULL);
    if (custom != NULL && custom->json_is_zero_function != 0U)
        return r_c17_write(emitter, "    return ") &&
               r_c17_emit_function_name(emitter, custom->json_is_zero_function) &&
               r_c17_write(emitter, "(v);\n}\n\n");
    if (r_c17_type_is_exact_named_standard(emitter, id, "std.json::value"))
        return r_c17_write(emitter, "    return r_std_json_kind(v) == R_STD_JSON_NULL;\n}\n\n");
    if (r_c17_type_is_exact_named_standard(emitter, id, "std.json::number"))
        return r_c17_write(emitter, "    return r_std_json_number_is_zero(v);\n}\n\n");
    if (type->kind == R_SEMANTIC_TYPE_ENUM)
        return r_c17_json_encode_enum(emitter, function, id, true) && r_c17_write(emitter, "}\n\n");
    if (type->kind >= R_SEMANTIC_TYPE_BOOL && type->kind <= R_SEMANTIC_TYPE_CHAR)
        return r_c17_write(emitter, "    return *v == 0;\n}\n\n");
    if (type->kind == R_SEMANTIC_TYPE_OPTION)
        return r_c17_write(emitter, "    return v->r_tag == UINT32_C(0);\n}\n\n");
    if (r_c17_type_is_exact_named_standard(emitter, id, "std.string::string"))
        return r_c17_write(emitter, "    return r_std_string_as_str(v).length == 0U;\n}\n\n");
    if (type->kind == R_SEMANTIC_TYPE_ARRAY || type->kind == R_SEMANTIC_TYPE_LIST ||
        type->kind == R_SEMANTIC_TYPE_DICT || type->kind == R_SEMANTIC_TYPE_STR)
        return r_c17_write(emitter, "    return v->length == 0U;\n}\n\n");
    if (type->kind == R_SEMANTIC_TYPE_SLICE || type->kind == R_SEMANTIC_TYPE_CONSTEXPR_STR)
        return r_c17_write(emitter, "    return v->r_len == 0U;\n}\n\n");
    if (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY) {
        return r_c17_format(emitter,
                            "    for (size_t i = 0U; i < %" PRIu64 "U; ++i) {\n        if (!",
                            type->length) &&
               r_c17_json_encode_name(emitter, function, type->base, true) &&
               r_c17_write(emitter,
                           "(&v->r_data[i])) {\n            return false;\n        }\n    }\n    "
                           "return true;\n}\n\n");
    }
    if (type->kind == R_SEMANTIC_TYPE_STRUCT) {
        const RSemanticAggregate *aggregate = r_c17_aggregate_for_type(emitter, id, NULL);
        for (uint32_t i = 0U; i < aggregate->field_count; ++i) {
            const RSemanticField *field =
                &emitter->frontend->semantic_fields[aggregate->first_field + i];
            if (!r_c17_write(emitter, "    if (!") ||
                !r_c17_json_encode_name(emitter, function, field->type, true) ||
                !r_c17_write(emitter, "(&v->") || !r_c17_emit_field_name(emitter, field) ||
                !r_c17_write(emitter, ")) {\n        return false;\n    }\n"))
                return false;
        }
        return r_c17_write(emitter, "    return true;\n}\n\n");
    }
    return r_c17_write(emitter, "    return false;\n}\n\n");
}
static bool
r_c17_json_embed_key(RC17Emitter *emitter, const RSemanticField *field, uint32_t ordinal) {
    uint32_t name = (field->json.flags & R_JSON_FIELD_NAME) != 0U ? field->json.name_intern_id
                                                                  : field->name_intern_id;
    const RInternEntry *key = &emitter->frontend->intern_entries[name - 1U];
    if (!r_c17_format(emitter, "            static const uint8_t reserved_%u[] = ", ordinal))
        return false;
    if (!(key->length == 0U ? r_c17_write(emitter, "\"\";\n")
                            : r_c17_emit_byte_string(emitter, key->bytes, key->length, 3U)))
        return false;
    return r_c17_format(emitter,
                        "            RStdJsonByteView key_%u = {reserved_%u, %zuU};\n",
                        ordinal,
                        ordinal,
                        key->length);
}
static bool r_c17_json_encode_embed(RC17Emitter *emitter,
                                    RSymbolId function,
                                    const RSemanticAggregate *aggregate,
                                    const RSemanticField *field) {
    uint32_t ordinal = UINT32_MAX;
    for (uint32_t i = 0U; i < aggregate->json_field_count; ++i) {
        const RJsonSchemaField *entry =
            &emitter->frontend->json_fields[aggregate->first_json_field + i];
        if (entry->parent == UINT32_MAX &&
            &emitter->frontend->semantic_fields[entry->field] == field) {
            ordinal = i;
            break;
        }
    }
    if (ordinal == UINT32_MAX)
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    const RJsonSchemaField *root =
        &emitter->frontend->json_fields[aggregate->first_json_field + ordinal];
    if (!r_c17_write(emitter, "    {\n        RStdJsonValueResult member = ") ||
        !r_c17_json_encode_name(emitter, function, field->type, false) ||
        !r_c17_write(emitter, "(c, &v->") || !r_c17_emit_field_name(emitter, field) ||
        !r_c17_write(emitter,
                     ", false);\n"
                     "        if (member.outcome.status != R_STD_JSON_CALL_SUCCESS) {\n"
                     "            r_std_json_value_destroy(&r.value);\n            return "
                     "member;\n        }\n"
                     "        if (r_std_json_kind(&member.value) != R_STD_JSON_OBJECT) {\n"
                     "            r_std_json_value_destroy(&member.value);\n"
                     "            r_std_json_value_destroy(&r.value);\n"
                     "            r.outcome.status = R_STD_JSON_CALL_JSON_ERROR;\n"
                     "            r.outcome.error.code = R_STD_JSON_ERROR_TYPE;\n            "
                     "return r;\n        }\n"
                     "        size_t count = r_std_json_len(&member.value);\n"
                     "        for (size_t index = 0U; index < count; ++index) {\n"
                     "            RStdJsonByteView key = r_std_json_key_at(&member.value, index);\n"
                     "            bool declared_here = false;\n            bool conflict = false;\n"
                     "            (void)key;\n"))
        return false;
    for (uint32_t i = 0U; i < aggregate->json_field_count; ++i) {
        const RJsonSchemaField *entry =
            &emitter->frontend->json_fields[aggregate->first_json_field + i];
        if (entry->embedded || entry->collector || entry->excluded)
            continue;
        const RSemanticField *candidate = &emitter->frontend->semantic_fields[entry->field];
        bool own = root->embedded && i > ordinal && i < root->end;
        if (!r_c17_json_embed_key(emitter, candidate, i))
            return false;
        if (own && !r_c17_format(emitter,
                                 "            declared_here = declared_here || "
                                 "r_std_json_name_equal(key, key_%u, false);\n",
                                 i))
            return false;
        if (!r_c17_format(
                emitter,
                "            conflict = conflict || r_std_json_name_equal(key, key_%u, %s);\n",
                i,
                (candidate->json.flags & R_JSON_FIELD_CASE) == 0U ? "c->options.ignore_case"
                : candidate->json.ignore_case                     ? "true"
                                                                  : "false"))
            return false;
    }
    return r_c17_write(emitter,
                       "            if (!declared_here && conflict) {\n"
                       "                r_std_json_value_destroy(&member.value);\n"
                       "                r_std_json_value_destroy(&r.value);\n"
                       "                r.outcome.status = R_STD_JSON_CALL_JSON_ERROR;\n"
                       "                r.outcome.error.code = R_STD_JSON_ERROR_KEY_CONFLICT;\n"
                       "                return r;\n            }\n        }\n"
                       "        r.outcome = r_json_object_extend(&r.value, &member.value);\n"
                       "        r_std_json_value_destroy(&member.value);\n"
                       "        if (r.outcome.status != R_STD_JSON_CALL_SUCCESS) {\n"
                       "            r_std_json_value_destroy(&r.value);\n            return r;\n   "
                       "     }\n    }\n");
}

static bool r_c17_json_encode_struct(RC17Emitter *emitter, RSymbolId function, RTypeId id) {
    const RSemanticAggregate *aggregate = r_c17_aggregate_for_type(emitter, id, NULL);
    if (!r_c17_write(
            emitter,
            "    RStdJsonValueResult r = r_std_json_object(a);\n"
            "    if (r.outcome.status != R_STD_JSON_CALL_SUCCESS) {\n        return r;\n    }\n"))
        return false;
    for (uint32_t i = 0U; i < aggregate->field_count; ++i) {
        const RSemanticField *field =
            &emitter->frontend->semantic_fields[aggregate->first_field + i];
        const RJsonFieldContract *contract = &field->json;
        const RInternEntry *key;
        uint32_t key_id;
        if (field->is_protected || (contract->flags & R_JSON_FIELD_SKIP) != 0U)
            continue;
        if ((contract->flags & R_JSON_FIELD_EMBED) != 0U) {
            if (!r_c17_json_encode_embed(emitter, function, aggregate, field))
                return false;
            continue;
        }
        key_id =
            contract->flags & R_JSON_FIELD_NAME ? contract->name_intern_id : field->name_intern_id;
        key = &emitter->frontend->intern_entries[key_id - 1U];
        if (!r_c17_write(emitter, "    {\n        bool omit = false;\n"))
            return false;
        if ((contract->flags & R_JSON_FIELD_OMITNONE) != 0U &&
            (!r_c17_write(emitter, "        omit = v->") ||
             !r_c17_emit_field_name(emitter, field) ||
             !r_c17_write(emitter, ".r_tag == UINT32_C(0);\n")))
            return false;
        if ((contract->flags & R_JSON_FIELD_OMITZERO) != 0U &&
            (!r_c17_write(emitter, "        omit = omit || ") ||
             !r_c17_json_encode_name(emitter, function, field->type, true) ||
             !r_c17_write(emitter, "(&v->") || !r_c17_emit_field_name(emitter, field) ||
             !r_c17_write(emitter, ");\n")))
            return false;
        if (!r_c17_write(emitter,
                         "        if (!omit) {\n            RStdJsonValueResult member = ") ||
            !r_c17_json_encode_name(emitter, function, field->type, false) ||
            !r_c17_write(emitter, "(c, &v->") || !r_c17_emit_field_name(emitter, field) ||
            !r_c17_format(
                emitter, ", %s);\n", contract->flags & R_JSON_FIELD_STRING ? "true" : "false") ||
            !r_c17_write(emitter,
                         "            if (member.outcome.status != R_STD_JSON_CALL_SUCCESS) {\n"
                         "                r_std_json_value_destroy(&r.value);\n                "
                         "return member;\n            }\n"))
            return false;
        if ((contract->flags & R_JSON_FIELD_OMITEMPTY) != 0U &&
            !r_c17_write(emitter, "            omit = r_json_value_empty(&member.value);\n"))
            return false;
        if (!r_c17_write(emitter, "            if (!omit) {\n") ||
            !r_c17_format(emitter, "                static const uint8_t r_key_%u[] = ", i))
            return false;
        if (key->length == 0U) {
            if (!r_c17_write(emitter, "\"\";\n"))
                return false;
        } else if (!r_c17_emit_byte_string(emitter, key->bytes, key->length, 4U))
            return false;
        if (!r_c17_write(emitter, "                RStdJsonByteView key = ") ||
            !r_c17_json_field_key(emitter, field) ||
            !r_c17_write(emitter,
                         ";\n                r.outcome = r_std_json_insert(&r.value, key, "
                         "&member.value);\n            }\n"
                         "            r_std_json_value_destroy(&member.value);\n"
                         "            if (r.outcome.status != R_STD_JSON_CALL_SUCCESS) {\n"
                         "                r_std_json_value_destroy(&r.value);\n                "
                         "return r;\n            }\n"
                         "        }\n    }\n"))
            return false;
    }
    return r_c17_write(emitter, "    return r;\n");
}
static bool r_c17_json_encode_signature(RC17Emitter *emitter,
                                        RSymbolId function,
                                        RTypeId id,
                                        bool declaration) {
    size_t start = emitter->output.length;
    for (unsigned pass = 0U; pass < 2U; ++pass) {
        emitter->output.length = start;
        if (!r_c17_write(emitter,
                         pass == 0U ? "static inline RStdJsonValueResult "
                                    : "static inline RStdJsonValueResult\n") ||
            !r_c17_json_encode_name(emitter, function, id, false) ||
            !r_c17_write(emitter, "(const RStdJsonEncodeContext *c, const ") ||
            !r_c17_emit_value_type(emitter, id) || !r_c17_write(emitter, " *v, bool q)"))
            return false;
        if (r_c17_current_column(emitter) + (declaration ? 1U : 2U) <= 100U)
            return true;
    }
    return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
}
static bool r_c17_json_encode_collection(RC17Emitter *emitter, RSymbolId function, RTypeId id) {
    const RSemanticType *type = r_c17_type(emitter, id);
    bool dict = type->kind == R_SEMANTIC_TYPE_DICT;
    if (!r_c17_write(emitter,
                     dict ? "    RStdJsonValueResult r = r_std_json_object(a);\n"
                          : "    RStdJsonValueResult r = r_std_json_array(a);\n") ||
        !r_c17_write(
            emitter,
            "    if (r.outcome.status != R_STD_JSON_CALL_SUCCESS) {\n        return r;\n    }\n") ||
        !r_c17_write(emitter,
                     dict ? "    RRuntimeDictIterator iter = r_runtime_dict_iter(v);\n"
                            "    RRuntimeDictEntryRef entry;\n"
                            "    while (r_runtime_dict_next(&iter, &entry)) {\n"
                            "        RStdStringView text = r_std_string_as_str(entry.key);\n"
                            "        RStdJsonByteView key = {text.data, text.length};\n"
                            "        const "
                          : "    RRuntimeListIterator iter = r_runtime_list_iter(v);\n"
                            "    const void *entry;\n"
                            "    while ((entry = r_runtime_list_next(&iter)) != NULL) {\n"
                            "        const ") ||
        !r_c17_emit_value_type(emitter, dict ? type->second : type->base) ||
        !r_c17_write(emitter, dict ? " *value = entry.value;\n" : " *value = entry;\n") ||
        !r_c17_write(emitter, "        RStdJsonValueResult member = ") ||
        !r_c17_json_encode_name(emitter, function, dict ? type->second : type->base, false) ||
        !r_c17_write(emitter,
                     "(c, value, false);\n"
                     "        if (member.outcome.status != R_STD_JSON_CALL_SUCCESS) {\n"
                     "            r_std_json_value_destroy(&r.value);\n            return "
                     "member;\n        }\n") ||
        !r_c17_write(emitter,
                     dict ? "        r.outcome = r_std_json_insert(&r.value, key, &member.value);\n"
                          : "        r.outcome = r_std_json_append(&r.value, &member.value);\n") ||
        !r_c17_write(
            emitter,
            "        r_std_json_value_destroy(&member.value);\n"
            "        if (r.outcome.status != R_STD_JSON_CALL_SUCCESS) {\n"
            "            r_std_json_value_destroy(&r.value);\n            return r;\n        }\n"
            "    }\n    return r;\n"))
        return false;
    return true;
}
static bool r_c17_json_encode_definition(RC17Emitter *emitter, RSymbolId function, RTypeId id) {
    const RSemanticType *type = r_c17_type(emitter, r_c17_value_type(emitter, id));
    RSemanticTypeKind kind = type->kind;
    bool floating = kind == R_SEMANTIC_TYPE_F32 || kind == R_SEMANTIC_TYPE_F64 ||
                    (kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC &&
                     (type->length == R_TOKEN_KW_C_FLOAT || type->length == R_TOKEN_KW_C_DOUBLE ||
                      type->length == R_TOKEN_KW_C_LONG_DOUBLE));
    if (!r_c17_json_encode_signature(emitter, function, id, false) ||
        !r_c17_write(emitter,
                     " {\n    RRuntimeAllocator *a = c->allocator;\n    (void)a;\n    (void)q;\n   "
                     " (void)v;\n"))
        return false;
    const RSemanticAggregate *custom = r_c17_aggregate_for_type(emitter, id, NULL);
    if (custom != NULL && custom->json_marshal_function != 0U)
        return r_c17_write(emitter, "    (void)a;\n") &&
               r_c17_json_encode_hook(emitter, custom->json_marshal_function) &&
               r_c17_write(emitter, "}\n\n");
    if ((kind == R_SEMANTIC_TYPE_STRUCT || kind == R_SEMANTIC_TYPE_FIXED_ARRAY ||
         kind == R_SEMANTIC_TYPE_ARRAY || kind == R_SEMANTIC_TYPE_LIST ||
         kind == R_SEMANTIC_TYPE_DICT || kind == R_SEMANTIC_TYPE_SLICE) &&
        !r_c17_json_encode_enter(emitter, 1U))
        return false;
    if (r_c17_type_is_exact_named_standard(emitter, id, "std.json::value") ||
        r_c17_type_is_exact_named_standard(emitter, id, "std.json::number")) {
        return r_c17_write(emitter, "    RStdJsonValueResult r = ") &&
               r_c17_write(emitter,
                           r_c17_type_is_exact_named_standard(emitter, id, "std.json::value")
                               ? "r_json_clone_value(a, v);\n"
                               : "r_std_json_from_number(a, v);\n") &&
               r_c17_write(
                   emitter,
                   "    if (q && r.outcome.status == R_STD_JSON_CALL_SUCCESS) {\n        r.outcome "
                   "= r_json_quote_number(&r.value);\n        if (r.outcome.status != "
                   "R_STD_JSON_CALL_SUCCESS) {\n            r_std_json_value_destroy(&r.value);\n  "
                   "      }\n    }\n    return r;\n}\n\n");
    }
    if (kind == R_SEMANTIC_TYPE_LIST || kind == R_SEMANTIC_TYPE_DICT)
        return r_c17_json_encode_collection(emitter, function, id) && r_c17_write(emitter, "}\n\n");
    if (kind == R_SEMANTIC_TYPE_ENUM) {
        if (!r_c17_json_encode_enum(emitter, function, id, false))
            return false;
    } else if (kind == R_SEMANTIC_TYPE_STRUCT) {
        if (!r_c17_json_encode_struct(emitter, function, id))
            return false;
    } else if (kind == R_SEMANTIC_TYPE_BOOL ||
               (kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC && type->length == R_TOKEN_KW_C_BOOL)) {
        if (!r_c17_write(emitter, "    return r_std_json_from_bool(a, *v);\n"))
            return false;
    } else if (kind == R_SEMANTIC_TYPE_CHAR) {
        if (!r_c17_write(emitter, "    return r_json_encode_char(a, *v);\n"))
            return false;
    } else if (floating) {
        uint32_t rep = kind == R_SEMANTIC_TYPE_F32           ? 0U
                       : kind == R_SEMANTIC_TYPE_F64         ? 1U
                       : type->length == R_TOKEN_KW_C_FLOAT  ? 2U
                       : type->length == R_TOKEN_KW_C_DOUBLE ? 3U
                                                             : 4U;
        if (!r_c17_format(emitter,
                          "    return r_json_encode_float(a, (long double)*v, UINT32_C(%u), q);\n",
                          rep))
            return false;
    } else if ((kind >= R_SEMANTIC_TYPE_I8 && kind <= R_SEMANTIC_TYPE_USIZE) ||
               kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC) {
        RSemanticTypeKind rep = r_semantic_integer_representation(emitter->frontend, id);
        bool negative = rep >= R_SEMANTIC_TYPE_I8 && rep <= R_SEMANTIC_TYPE_ISIZE;
        if (!r_c17_write(emitter, "    uint64_t magnitude = (uint64_t)*v;\n"))
            return false;
        if (negative) {
            if (!r_c17_write(emitter,
                             "    bool negative = *v < 0;\n    if (negative) {\n"
                             "        magnitude = UINT64_C(0) - magnitude;\n    }\n"
                             "    return r_json_encode_integer(a, negative, magnitude, q);\n"))
                return false;
        } else if (!r_c17_write(emitter,
                                "    return r_json_encode_integer(a, false, magnitude, q);\n"))
            return false;
    } else if (r_c17_type_is_exact_named_standard(emitter, id, "std.string::string")) {
        if (!r_c17_write(emitter,
                         "    RStdStringView text = r_std_string_as_str(v);\n"
                         "    return r_std_json_from_string(a, (RStdJsonByteView){text.data, "
                         "text.length});\n"))
            return false;
    } else if (kind == R_SEMANTIC_TYPE_STR || kind == R_SEMANTIC_TYPE_CONSTEXPR_STR) {
        if (!r_c17_write(emitter,
                         kind == R_SEMANTIC_TYPE_STR
                             ? "    return r_std_json_from_string(a, (RStdJsonByteView){v->data, "
                               "v->length});\n"
                             : "    return r_std_json_from_string(a, (RStdJsonByteView){v->r_data, "
                               "v->r_len});\n"))
            return false;
    } else if (kind == R_SEMANTIC_TYPE_OPTION) {
        if (!r_c17_write(emitter,
                         "    if (v->r_tag == UINT32_C(0)) {\n"
                         "        return (RStdJsonValueResult){0};\n    }\n    return ") ||
            !r_c17_json_encode_name(emitter, function, type->base, false) ||
            !r_c17_write(emitter, "(c, &v->r_payload.r_some, q);\n"))
            return false;
    } else if (kind == R_SEMANTIC_TYPE_FIXED_ARRAY || kind == R_SEMANTIC_TYPE_ARRAY ||
               kind == R_SEMANTIC_TYPE_SLICE) {
        if (!r_c17_write(
                emitter,
                "    RStdJsonValueResult r = r_std_json_array(a);\n"
                "    if (r.outcome.status != R_STD_JSON_CALL_SUCCESS) {\n        return r;\n    }\n"
                "    const ") ||
            !r_c17_emit_value_type(emitter, type->base) ||
            !r_c17_write(emitter,
                         kind == R_SEMANTIC_TYPE_ARRAY
                             ? " *data = v->data;\n    size_t length = v->length;\n"
                             : " *data = v->r_data;\n    size_t length = "))
            return false;
        if (kind != R_SEMANTIC_TYPE_ARRAY &&
            !(kind == R_SEMANTIC_TYPE_FIXED_ARRAY
                  ? r_c17_format(emitter, "%" PRIu64 "U;\n", type->length)
                  : r_c17_write(emitter, "v->r_len;\n")))
            return false;
        if (!r_c17_write(emitter,
                         "    for (size_t i = 0U; i < length; ++i) {\n        RStdJsonValueResult "
                         "member = ") ||
            !r_c17_json_encode_name(emitter, function, type->base, false) ||
            !r_c17_write(emitter,
                         "(c, &data[i], false);\n"
                         "        if (member.outcome.status != R_STD_JSON_CALL_SUCCESS) {\n"
                         "            r_std_json_value_destroy(&r.value);\n            return "
                         "member;\n        }\n"
                         "        r.outcome = r_std_json_append(&r.value, &member.value);\n"
                         "        r_std_json_value_destroy(&member.value);\n"
                         "        if (r.outcome.status != R_STD_JSON_CALL_SUCCESS) {\n"
                         "            r_std_json_value_destroy(&r.value);\n            return r;\n "
                         "       }\n"
                         "    }\n    return r;\n"))
            return false;
    } else
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    return r_c17_write(emitter, "}\n\n");
}
static bool r_c17_json_encode_types(
    RC17Emitter *emitter, RSymbolId function, RTypeId id, uint8_t *seen, bool zero) {
    const RSemanticType *type = r_c17_type(emitter, r_c17_value_type(emitter, id));
    uint8_t mask = zero ? 1U : 2U;
    if ((seen[id] & mask) != 0U)
        return true;
    seen[id] |= mask;
    if (!(zero ? r_c17_json_zero_signature(emitter, function, id)
               : r_c17_json_encode_signature(emitter, function, id, true)) ||
        !r_c17_write(emitter, ";\n"))
        return false;
    const RSemanticAggregate *custom = r_c17_aggregate_for_type(emitter, id, NULL);
    if (custom != NULL &&
        (zero ? custom->json_is_zero_function : custom->json_marshal_function) != 0U)
        return zero ? r_c17_json_zero_definition(emitter, function, id)
                    : r_c17_json_encode_definition(emitter, function, id);
    if (type->kind == R_SEMANTIC_TYPE_ENUM) {
        const RSemanticAggregate *aggregate = r_c17_aggregate_for_type(emitter, id, NULL);
        for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
            const RSemanticVariant *variant =
                &emitter->frontend->semantic_variants[aggregate->first_variant + i];
            if (variant->payload_type != 0U && (!zero || variant->value == 0U) &&
                !r_c17_json_encode_types(emitter, function, variant->payload_type, seen, zero))
                return false;
        }
    }
    if (type->kind == R_SEMANTIC_TYPE_STRUCT) {
        const RSemanticAggregate *aggregate = r_c17_aggregate_for_type(emitter, id, NULL);
        for (uint32_t i = 0U; i < aggregate->field_count; ++i) {
            const RSemanticField *field =
                &emitter->frontend->semantic_fields[aggregate->first_field + i];
            if (!zero && (field->is_protected || (field->json.flags & R_JSON_FIELD_SKIP) != 0U))
                continue;
            if (!zero && (field->json.flags & R_JSON_FIELD_OMITZERO) != 0U &&
                !r_c17_json_encode_types(emitter, function, field->type, seen, true))
                return false;
            if (!r_c17_json_encode_types(emitter, function, field->type, seen, zero))
                return false;
        }
    } else if (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY ||
               (!zero &&
                (type->kind == R_SEMANTIC_TYPE_OPTION || type->kind == R_SEMANTIC_TYPE_ARRAY ||
                 type->kind == R_SEMANTIC_TYPE_LIST || type->kind == R_SEMANTIC_TYPE_SLICE))) {
        if (!r_c17_json_encode_types(emitter, function, type->base, seen, zero))
            return false;
    }
    if (!zero && type->kind == R_SEMANTIC_TYPE_DICT &&
        !r_c17_json_encode_types(emitter, function, type->second, seen, false))
        return false;
    return zero ? r_c17_json_zero_definition(emitter, function, id)
                : r_c17_json_encode_definition(emitter, function, id);
}
static bool r_c17_emit_json_marshal(RC17Emitter *emitter, RSymbolId function_id) {
    const RSemanticSymbol *function = r_c17_symbol(emitter, function_id);
    const RHirNode *node = r_c17_node(emitter, emitter->function_nodes[function_id]);
    uint8_t *seen = r_c17_allocate(emitter, emitter->frontend->semantic_type_count + 1U);
    bool success;
    if (seen == NULL)
        return false;
    (void)memset(seen, 0, emitter->frontend->semantic_type_count + 1U);
    success = r_c17_json_encode_types(emitter, function_id, function->json_type, seen, false);
    r_c17_free(emitter, seen);
    if (!success || !r_c17_emit_function_signature(emitter, function_id, true) ||
        !r_c17_write(emitter,
                     " {\n    RStdJsonEncodeContext context = {\n"
                     "        .allocator = r_runtime_hosted_allocator(),\n        .options = ") ||
        !(function->parameter_count == 2U ? r_c17_format_argument(emitter, node, 1U)
                                          : r_c17_write(emitter, "R_STD_JSON_DEFAULT_OPTIONS")) ||
        !r_c17_write(emitter, ",\n    };\n    RStdJsonValueResult tree = ") ||
        !r_c17_json_encode_name(emitter, function_id, function->json_type, false) ||
        !r_c17_write(emitter, "(&context, ") || !r_c17_format_argument(emitter, node, 0U) ||
        !r_c17_write(
            emitter,
            ", false);\n    RStdJsonStringResult r = {0};\n    r.outcome = tree.outcome;\n"
            "    if (r.outcome.status == R_STD_JSON_CALL_SUCCESS) {\n"
            "        r = r_std_json_stringify_with_options(context.allocator, &tree.value, "
            "context.options);\n"
            "        r_std_json_value_destroy(&tree.value);\n    }\n"
            "    if (r.outcome.status == R_STD_JSON_CALL_SUCCESS) {\n"
            "        r_effect_out->r_payload.r_ok = r.value;\n        r_effect_out->r_tag = "
            "UINT32_C(0);\n"
            "    } else if (r.outcome.status == R_STD_JSON_CALL_ALLOCATION_ERROR) {\n"
            "        r_effect_out->r_payload.r_error_00000001 = r.outcome.allocation_error;\n"
            "        r_effect_out->r_tag = UINT32_C(1);\n    } else {\n"
            "        r_effect_out->r_payload.r_error_00000002 = r.outcome.error;\n"
            "        r_effect_out->r_tag = UINT32_C(2);\n    }\n}\n\n"))
        return false;
    return true;
}
