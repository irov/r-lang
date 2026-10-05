/* M32.6: `std.json::schema::<T>()` (R-SLIB-JSON-0002). The schema is JSON Schema 2020-12 text built
 * from the same type model as marshal and unmarshal: field names and `optional` of the @json
 * contracts, `o<T>` as null, arrays, dictionaries with string keys, integer ranges, enum variants
 * by name with one-member objects for payloads. Nested types are written in place; a type that
 * contains itself is written once under `$defs` and referred to by `$ref`. */

typedef struct RJsonSchemaVisit {
    RTypeId type;
    const struct RJsonSchemaVisit *parent;
} RJsonSchemaVisit;

typedef struct RJsonSchemaWriter {
    RBodyContext *body;
    RSourceSpan span;
    RTypeId root;
    RReflectionText text;
    /* Types written under $defs, in the order they were first referred to recursively, and the
       schema hooks of types with converters, whose entries the operation inserts when it runs. */
    RTypeId definitions[64];
    uint32_t definition_names[64];
    RSymbolId definition_hooks[64];
    uint32_t definition_count;
    bool failed;
} RJsonSchemaWriter;

static bool r_json_schema_fail(RJsonSchemaWriter *writer, const char *message) {
    writer->failed = true;
    return r_body_diagnostic(writer->body, "R-DIAG-JSON-001", "R-SLIB-JSON-0002", message, writer->span);
}

static bool r_json_schema_put(RJsonSchemaWriter *writer, const char *text) {
    return r_reflection_text_append_string(writer->body->frontend, &writer->text, text);
}

/* A JSON string: quotes, backslashes and control characters escaped, other bytes as they are. */
static bool
r_json_schema_put_string(RJsonSchemaWriter *writer, const char *bytes, size_t length) {
    RFrontendContext *context = writer->body->frontend;
    if (!r_reflection_text_append(context, &writer->text, "\"", 1U))
        return false;
    for (size_t index = 0U; index < length; ++index) {
        const unsigned char byte = (unsigned char)bytes[index];
        char escaped[8];
        if (byte == '"' || byte == '\\') {
            escaped[0] = '\\';
            escaped[1] = (char)byte;
            if (!r_reflection_text_append(context, &writer->text, escaped, 2U))
                return false;
        } else if (byte < 0x20U) {
            (void)snprintf(escaped, sizeof(escaped), "\\u%04x", (unsigned)byte);
            if (!r_reflection_text_append(context, &writer->text, escaped, 6U))
                return false;
        } else if (!r_reflection_text_append(context, &writer->text, (const char *)&bytes[index], 1U))
            return false;
    }
    return r_reflection_text_append(context, &writer->text, "\"", 1U);
}

static bool r_json_schema_put_intern(RJsonSchemaWriter *writer, uint32_t intern_id) {
    const char *bytes;
    size_t length;
    return r_semantic_intern_text(writer->body->frontend, intern_id, &bytes, &length) &&
           r_json_schema_put_string(writer, bytes, length);
}

/* `"key":` after a comma unless it is the first member of its object. */
static bool r_json_schema_member(RJsonSchemaWriter *writer, bool *first, const char *key) {
    if (!*first && !r_json_schema_put(writer, ","))
        return false;
    *first = false;
    return r_json_schema_put_string(writer, key, strlen(key)) && r_json_schema_put(writer, ":");
}

static bool r_json_schema_integer_range(RJsonSchemaWriter *writer,
                                        RSemanticTypeKind kind,
                                        bool *first) {
    static const struct {
        RSemanticTypeKind kind;
        const char *minimum;
        const char *maximum;
    } ranges[] = {{R_SEMANTIC_TYPE_I8, "-128", "127"},
                  {R_SEMANTIC_TYPE_I16, "-32768", "32767"},
                  {R_SEMANTIC_TYPE_I32, "-2147483648", "2147483647"},
                  {R_SEMANTIC_TYPE_I64, "-9223372036854775808", "9223372036854775807"},
                  {R_SEMANTIC_TYPE_ISIZE, "-9223372036854775808", "9223372036854775807"},
                  {R_SEMANTIC_TYPE_U8, "0", "255"},
                  {R_SEMANTIC_TYPE_U16, "0", "65535"},
                  {R_SEMANTIC_TYPE_U32, "0", "4294967295"},
                  {R_SEMANTIC_TYPE_U64, "0", "18446744073709551615"},
                  {R_SEMANTIC_TYPE_USIZE, "0", "18446744073709551615"}};
    for (size_t index = 0U; index < sizeof(ranges) / sizeof(ranges[0]); ++index) {
        if (ranges[index].kind != kind)
            continue;
        return r_json_schema_member(writer, first, "type") &&
               r_json_schema_put(writer, "\"integer\"") &&
               r_json_schema_member(writer, first, "minimum") &&
               r_json_schema_put(writer, ranges[index].minimum) &&
               r_json_schema_member(writer, first, "maximum") &&
               r_json_schema_put(writer, ranges[index].maximum);
    }
    return r_json_schema_fail(writer, "this integer type has no JSON Schema range");
}

static bool r_json_schema_type(RJsonSchemaWriter *writer,
                               RTypeId id,
                               const RJsonSchemaVisit *parent,
                               uint32_t depth,
                               uint32_t description,
                               bool numeric_string);

/* The name of a type under $defs: its source spelling with characters outside [A-Za-z0-9._-]
   replaced by '_', and a number when two types would share a name. */
static bool r_json_schema_definition_name(RJsonSchemaWriter *writer, RTypeId id, uint32_t *name) {
    RFrontendContext *context = writer->body->frontend;
    RReflectionText spelling = {0};
    bool success = false;
    if (!r_reflection_spell_type(context, id, &spelling, 0U))
        goto cleanup;
    for (size_t index = 0U; index < spelling.length; ++index) {
        const char byte = spelling.bytes[index];
        const bool plain = (byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
                           (byte >= '0' && byte <= '9') || byte == '.' || byte == '-' ||
                           byte == '_';
        if (!plain)
            spelling.bytes[index] = '_';
    }
    for (uint32_t attempt = 1U;; ++attempt) {
        char suffix[16];
        size_t base = spelling.length;
        bool taken = false;
        if (attempt > 1U) {
            (void)snprintf(suffix, sizeof(suffix), "_%u", attempt);
            if (!r_reflection_text_append_string(context, &spelling, suffix))
                goto cleanup;
        }
        if (!r_intern_bytes(context, (const uint8_t *)spelling.bytes, spelling.length, name))
            goto cleanup;
        for (uint32_t index = 0U; index < writer->definition_count; ++index)
            taken = taken || (writer->definition_names[index] == *name &&
                              writer->definitions[index] != id);
        if (!taken)
            break;
        spelling.length = base;
        spelling.bytes[spelling.length] = '\0';
    }
    success = true;
cleanup:
    r_context_free(context, spelling.bytes);
    return success;
}

/* The index of the $defs entry of a type, added with its hook (zero for a derived schema) when it
   has none. */
static bool r_json_schema_definition(RJsonSchemaWriter *writer, RTypeId id, RSymbolId hook, uint32_t *found) {
    uint32_t index = 0U;
    while (index < writer->definition_count && writer->definitions[index] != id)
        ++index;
    if (index == writer->definition_count) {
        if (writer->definition_count == sizeof(writer->definitions) / sizeof(writer->definitions[0]))
            return r_json_schema_fail(writer, "JSON Schema has too many recursive types");
        if (!r_json_schema_definition_name(writer, id, &writer->definition_names[index]))
            return false;
        writer->definitions[index] = id;
        writer->definition_hooks[index] = hook;
        writer->definition_count += 1U;
    }
    *found = index;
    return true;
}

/* `"#/$defs/NAME"` of the entry at index. */
static bool r_json_schema_definition_pointer(RJsonSchemaWriter *writer, uint32_t index) {
    const char *bytes;
    size_t length;
    return r_semantic_intern_text(writer->body->frontend, writer->definition_names[index], &bytes, &length) &&
           r_json_schema_put(writer, "\"#/$defs/") &&
           r_reflection_text_append(writer->body->frontend, &writer->text, bytes, length) &&
           r_json_schema_put(writer, "\"");
}

/* `{"$ref":...}` to the $defs entry of a type whose schema its hook returns. */
static bool r_json_schema_hooked(RJsonSchemaWriter *writer, RTypeId id, RSymbolId hook, uint32_t description) {
    uint32_t index = 0U;
    bool first = true;
    if (!r_json_schema_definition(writer, id, hook, &index) || !r_json_schema_put(writer, "{") ||
        !r_json_schema_member(writer, &first, "$ref") || !r_json_schema_definition_pointer(writer, index))
        return false;
    if (description != 0U && (!r_json_schema_member(writer, &first, "description") ||
                              !r_json_schema_put_intern(writer, description)))
        return false;
    return r_json_schema_put(writer, "}");
}

/* `{"$ref":...}` to the root or to the $defs entry of a type that contains itself. */
static bool r_json_schema_reference(RJsonSchemaWriter *writer, RTypeId id, uint32_t description) {
    bool first = true;
    if (!r_json_schema_put(writer, "{") || !r_json_schema_member(writer, &first, "$ref"))
        return false;
    if (id == writer->root) {
        if (!r_json_schema_put(writer, "\"#\""))
            return false;
    } else {
        uint32_t index = 0U;
        if (!r_json_schema_definition(writer, id, 0U, &index) ||
            !r_json_schema_definition_pointer(writer, index))
            return false;
    }
    if (description != 0U && (!r_json_schema_member(writer, &first, "description") ||
                              !r_json_schema_put_intern(writer, description)))
        return false;
    return r_json_schema_put(writer, "}");
}

/* The name of a field in JSON: its @json name or its declared name. */
static uint32_t r_json_schema_field_name(const RSemanticField *field) {
    return (field->json.flags & R_JSON_FIELD_NAME) != 0U ? field->json.name_intern_id
                                                         : field->name_intern_id;
}

/* The properties of a structure, with those of embedded structures in place; the names of the
   required ones go to `required` and an embedded dictionary or JSON object becomes the schema of
   additional properties. */
static bool r_json_schema_properties(RJsonSchemaWriter *writer,
                                     const RSemanticAggregate *aggregate,
                                     const RJsonSchemaVisit *visit,
                                     uint32_t depth,
                                     bool *first_property,
                                     RReflectionText *required,
                                     bool *first_required,
                                     RTypeId *collector) {
    RFrontendContext *context = writer->body->frontend;
    for (uint32_t index = 0U; index < aggregate->field_count; ++index) {
        const RSemanticField *field = &context->semantic_fields[aggregate->first_field + index];
        if (field->is_protected || (field->json.flags & R_JSON_FIELD_SKIP) != 0U)
            continue;
        if ((field->json.flags & R_JSON_FIELD_EMBED) != 0U) {
            const RTypeId embedded = r_semantic_value_type(context, field->type);
            const RSemanticType *type = r_semantic_type(context, embedded);
            if (type != NULL && type->kind == R_SEMANTIC_TYPE_STRUCT) {
                const RSemanticAggregate *inner = r_semantic_aggregate_for_type(context, embedded);
                if (inner == NULL || inner->json_marshal_function != 0U ||
                    inner->json_unmarshal_function != 0U)
                    return r_json_schema_fail(
                        writer, "an embedded structure with a JSON converter has no schema");
                if (!r_json_schema_properties(writer,
                                              inner,
                                              visit,
                                              depth + 1U,
                                              first_property,
                                              required,
                                              first_required,
                                              collector))
                    return false;
                continue;
            }
            *collector = embedded;
            continue;
        }
        const uint32_t name = r_json_schema_field_name(field);
        const char *bytes;
        size_t length;
        if (!*first_property && !r_json_schema_put(writer, ","))
            return false;
        *first_property = false;
        if (!r_json_schema_put_intern(writer, name) || !r_json_schema_put(writer, ":") ||
            !r_json_schema_type(writer,
                                field->type,
                                visit,
                                depth + 1U,
                                field->json.description_intern_id,
                                (field->json.flags & R_JSON_FIELD_STRING) != 0U))
            return false;
        if ((field->json.flags & R_JSON_FIELD_OPTIONAL) != 0U)
            continue;
        if (!r_semantic_intern_text(context, name, &bytes, &length) ||
            (!*first_required && !r_reflection_text_append_string(context, required, ",")))
            return false;
        *first_required = false;
        RReflectionText quoted = writer->text;
        writer->text = *required;
        const bool written = r_json_schema_put_string(writer, bytes, length);
        *required = writer->text;
        writer->text = quoted;
        if (!written)
            return false;
    }
    return true;
}

static bool r_json_schema_struct(RJsonSchemaWriter *writer,
                                 const RSemanticAggregate *aggregate,
                                 const RJsonSchemaVisit *visit,
                                 uint32_t depth,
                                 bool *first) {
    RFrontendContext *context = writer->body->frontend;
    RReflectionText required = {0};
    RTypeId collector = R_TYPE_ID_INVALID;
    bool first_property = true, first_required = true, success = false;
    if (!r_json_schema_member(writer, first, "type") || !r_json_schema_put(writer, "\"object\"") ||
        !r_json_schema_member(writer, first, "properties") || !r_json_schema_put(writer, "{") ||
        !r_json_schema_properties(writer,
                                  aggregate,
                                  visit,
                                  depth,
                                  &first_property,
                                  &required,
                                  &first_required,
                                  &collector) ||
        !r_json_schema_put(writer, "}"))
        goto cleanup;
    if (!first_required &&
        (!r_json_schema_member(writer, first, "required") || !r_json_schema_put(writer, "[") ||
         !r_reflection_text_append(context, &writer->text, required.bytes, required.length) ||
         !r_json_schema_put(writer, "]")))
        goto cleanup;
    if (collector != R_TYPE_ID_INVALID) {
        const RSemanticType *type = r_semantic_type(context, collector);
        if (!r_json_schema_member(writer, first, "additionalProperties"))
            goto cleanup;
        if (type != NULL && type->kind == R_SEMANTIC_TYPE_DICT) {
            if (!r_json_schema_type(writer, type->second, visit, depth + 1U, 0U, false))
                goto cleanup;
        } else if (!r_json_schema_put(writer, "true"))
            goto cleanup;
    }
    success = true;
cleanup:
    r_context_free(context, required.bytes);
    return success;
}

static bool r_json_schema_enum(RJsonSchemaWriter *writer,
                               const RSemanticAggregate *aggregate,
                               const RJsonSchemaVisit *visit,
                               uint32_t depth,
                               bool *first) {
    RFrontendContext *context = writer->body->frontend;
    uint32_t units = 0U, payloads = 0U;
    for (uint32_t index = 0U; index < aggregate->variant_count; ++index) {
        if (context->semantic_variants[aggregate->first_variant + index].payload_type == 0U)
            ++units;
        else
            ++payloads;
    }
    if (payloads == 0U) {
        if (!r_json_schema_member(writer, first, "type") ||
            !r_json_schema_put(writer, "\"string\"") ||
            !r_json_schema_member(writer, first, "enum") || !r_json_schema_put(writer, "["))
            return false;
    } else if (!r_json_schema_member(writer, first, "oneOf") || !r_json_schema_put(writer, "["))
        return false;
    if (payloads != 0U && units != 0U &&
        !r_json_schema_put(writer, "{\"type\":\"string\",\"enum\":["))
        return false;
    bool first_unit = true;
    for (uint32_t index = 0U; index < aggregate->variant_count; ++index) {
        const RSemanticVariant *variant = &context->semantic_variants[aggregate->first_variant + index];
        if (variant->payload_type != 0U)
            continue;
        if ((!first_unit && !r_json_schema_put(writer, ",")) ||
            !r_json_schema_put_intern(writer, variant->name_intern_id))
            return false;
        first_unit = false;
    }
    if (payloads == 0U)
        return r_json_schema_put(writer, "]");
    if (units != 0U && !r_json_schema_put(writer, "]}"))
        return false;
    bool first_payload = units == 0U;
    for (uint32_t index = 0U; index < aggregate->variant_count; ++index) {
        const RSemanticVariant *variant = &context->semantic_variants[aggregate->first_variant + index];
        if (variant->payload_type == 0U)
            continue;
        if ((!first_payload && !r_json_schema_put(writer, ",")) ||
            !r_json_schema_put(writer, "{\"type\":\"object\",\"properties\":{") ||
            !r_json_schema_put_intern(writer, variant->name_intern_id) ||
            !r_json_schema_put(writer, ":") ||
            !r_json_schema_type(writer, variant->payload_type, visit, depth + 1U, 0U, false) ||
            !r_json_schema_put(writer, "},\"required\":[") ||
            !r_json_schema_put_intern(writer, variant->name_intern_id) ||
            !r_json_schema_put(writer, "],\"additionalProperties\":false}"))
            return false;
        first_payload = false;
    }
    return r_json_schema_put(writer, "]");
}

/* The members of the schema of one type, without its braces; *first tracks the comma. */
static bool r_json_schema_members(RJsonSchemaWriter *writer,
                                  RTypeId id,
                                  const RJsonSchemaVisit *visit,
                                  uint32_t depth,
                                  bool numeric_string,
                                  bool *first) {
    RFrontendContext *context = writer->body->frontend;
    const RSemanticType *type = r_semantic_type(context, id);
    if (type->kind == R_SEMANTIC_TYPE_PARAMETER)
        return r_json_schema_fail(writer, "std.json::schema requires a concrete type");
    const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, id);
    if (aggregate != NULL &&
        (aggregate->json_marshal_function != 0U || aggregate->json_unmarshal_function != 0U))
        return r_json_schema_fail(
            writer, "a type with a JSON converter has no derived schema; its module may define json_schema");
    if (type->kind == R_SEMANTIC_TYPE_OPTION) {
        const RSemanticType *base = r_semantic_type(context, r_semantic_value_type(context, type->base));
        if (base != NULL && base->kind == R_SEMANTIC_TYPE_OPTION)
            return r_json_schema_fail(writer, "nested optional JSON representations have no schema");
        return r_json_schema_member(writer, first, "anyOf") && r_json_schema_put(writer, "[") &&
               r_json_schema_type(writer, type->base, visit, depth + 1U, 0U, numeric_string) &&
               r_json_schema_put(writer, ",{\"type\":\"null\"}]");
    }
    if (numeric_string)
        return r_json_schema_member(writer, first, "type") && r_json_schema_put(writer, "\"string\"");
    if (type->kind == R_SEMANTIC_TYPE_BOOL ||
        (type->kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC && type->length == R_TOKEN_KW_C_BOOL))
        return r_json_schema_member(writer, first, "type") && r_json_schema_put(writer, "\"boolean\"");
    if (type->kind == R_SEMANTIC_TYPE_F32 || type->kind == R_SEMANTIC_TYPE_F64 ||
        (type->kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC &&
         (type->length == R_TOKEN_KW_C_FLOAT || type->length == R_TOKEN_KW_C_DOUBLE ||
          type->length == R_TOKEN_KW_C_LONG_DOUBLE)) ||
        r_semantic_standard_type_name_equal(context, type, "std.json::number"))
        return r_json_schema_member(writer, first, "type") && r_json_schema_put(writer, "\"number\"");
    if ((type->kind >= R_SEMANTIC_TYPE_I8 && type->kind <= R_SEMANTIC_TYPE_USIZE) ||
        type->kind == R_SEMANTIC_TYPE_C_ABI_NUMERIC)
        return r_json_schema_integer_range(
            writer, r_semantic_integer_representation(context, id), first);
    if (type->kind == R_SEMANTIC_TYPE_CHAR)
        return r_json_schema_member(writer, first, "type") &&
               r_json_schema_put(writer, "\"string\"") &&
               r_json_schema_member(writer, first, "minLength") && r_json_schema_put(writer, "1") &&
               r_json_schema_member(writer, first, "maxLength") && r_json_schema_put(writer, "1");
    if (type->kind == R_SEMANTIC_TYPE_STR || type->kind == R_SEMANTIC_TYPE_CONSTEXPR_STR ||
        r_semantic_standard_type_name_equal(context, type, "std.string::string"))
        return r_json_schema_member(writer, first, "type") && r_json_schema_put(writer, "\"string\"");
    if (r_semantic_standard_type_name_equal(context, type, "std.json::value"))
        return true;
    if (type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY || type->kind == R_SEMANTIC_TYPE_ARRAY ||
        type->kind == R_SEMANTIC_TYPE_LIST || type->kind == R_SEMANTIC_TYPE_SLICE) {
        if (!r_json_schema_member(writer, first, "type") || !r_json_schema_put(writer, "\"array\"") ||
            !r_json_schema_member(writer, first, "items") ||
            !r_json_schema_type(writer, type->base, visit, depth + 1U, 0U, false))
            return false;
        if (type->kind != R_SEMANTIC_TYPE_FIXED_ARRAY)
            return true;
        return r_json_schema_member(writer, first, "minItems") &&
               r_reflection_text_append_uint(context, &writer->text, type->length) &&
               r_json_schema_member(writer, first, "maxItems") &&
               r_reflection_text_append_uint(context, &writer->text, type->length);
    }
    if (type->kind == R_SEMANTIC_TYPE_DICT) {
        if (!r_semantic_standard_type_name_equal(
                context, r_semantic_type(context, type->base), "std.string::string"))
            return r_json_schema_fail(writer, "JSON dictionaries require string keys");
        return r_json_schema_member(writer, first, "type") &&
               r_json_schema_put(writer, "\"object\"") &&
               r_json_schema_member(writer, first, "additionalProperties") &&
               r_json_schema_type(writer, type->second, visit, depth + 1U, 0U, false);
    }
    if (aggregate != NULL && type->kind == R_SEMANTIC_TYPE_ENUM)
        return r_json_schema_enum(writer, aggregate, visit, depth, first);
    if (aggregate != NULL && type->kind == R_SEMANTIC_TYPE_STRUCT)
        return r_json_schema_struct(writer, aggregate, visit, depth, first);
    return r_json_schema_fail(writer, "this type has no JSON Schema");
}

static bool r_json_schema_type(RJsonSchemaWriter *writer,
                               RTypeId id,
                               const RJsonSchemaVisit *parent,
                               uint32_t depth,
                               uint32_t description,
                               bool numeric_string) {
    RFrontendContext *context = writer->body->frontend;
    id = r_semantic_value_type(context, id);
    const RSemanticType *type = r_semantic_type(context, id);
    if (type == NULL || depth > context->options.limits.max_nesting)
        return r_json_schema_fail(writer, "JSON type exceeds the compiler nesting limit");
    if (type->kind == R_SEMANTIC_TYPE_STRUCT || type->kind == R_SEMANTIC_TYPE_ENUM) {
        const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, id);
        if (aggregate != NULL && aggregate->json_schema_function != 0U)
            return r_json_schema_hooked(writer, id, aggregate->json_schema_function, description);
        for (const RJsonSchemaVisit *previous = parent; previous != NULL; previous = previous->parent)
            if (previous->type == id)
                return r_json_schema_reference(writer, id, description);
    }
    RJsonSchemaVisit visit = {id, parent};
    bool first = true;
    if (!r_json_schema_put(writer, "{") ||
        !r_json_schema_members(writer, id, &visit, depth, numeric_string, &first))
        return false;
    if (description != 0U && (!r_json_schema_member(writer, &first, "description") ||
                              !r_json_schema_put_intern(writer, description)))
        return false;
    return r_json_schema_put(writer, "}");
}

/* The schema text of a type, with the $defs of the types that contain themselves. */
static bool r_json_schema_build(RJsonSchemaWriter *writer, RTypeId type) {
    RFrontendContext *context = writer->body->frontend;
    RJsonSchemaVisit root = {type, NULL};
    bool first = true;
    const RSemanticAggregate *root_aggregate = r_semantic_aggregate_for_type(context, type);
    if (root_aggregate != NULL && root_aggregate->json_schema_function != 0U) {
        /* The root is a type with a schema hook: a reference to its entry, which the hook fills. */
        uint32_t index = 0U;
        if (!r_json_schema_put(writer, "{") ||
            !r_json_schema_definition(writer, type, root_aggregate->json_schema_function, &index) ||
            !r_json_schema_member(writer, &first, "$ref") || !r_json_schema_definition_pointer(writer, index))
            return false;
    } else if (!r_json_schema_put(writer, "{") ||
               !r_json_schema_members(writer, type, &root, 0U, false, &first)) {
        return false;
    }
    if (writer->definition_count != 0U) {
        bool first_definition = true;
        if (!r_json_schema_member(writer, &first, "$defs") || !r_json_schema_put(writer, "{"))
            return false;
        /* Writing a definition may refer to further ones, which join the list; the entries of
           hooks are inserted when the operation runs. */
        for (uint32_t index = 0U; index < writer->definition_count; ++index) {
            const char *bytes;
            size_t length;
            RJsonSchemaVisit own = {writer->definitions[index], NULL};
            bool member = true;
            if (writer->definition_hooks[index] != 0U)
                continue;
            if ((!first_definition && !r_json_schema_put(writer, ",")) ||
                !r_semantic_intern_text(context, writer->definition_names[index], &bytes, &length) ||
                !r_json_schema_put_string(writer, bytes, length) || !r_json_schema_put(writer, ":{") ||
                !r_json_schema_members(
                    writer, writer->definitions[index], &own, 1U, false, &member) ||
                !r_json_schema_put(writer, "}"))
                return false;
            first_definition = false;
        }
        if (!r_json_schema_put(writer, "}"))
            return false;
    }
    return r_json_schema_put(writer, "}");
}

/* `std.json::schema` spelled in a standard type call. */
static bool r_json_schema_name(const RFrontendContext *context, RAstRef qualified) {
    RAstNodeView view;
    RAstTokenView components[3];
    uint32_t count = 0U;
    if (!r_ast_view_kind(context, qualified, R_SYNTAX_QUALIFIED_NAME, &view))
        return false;
    for (uint32_t index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstTokenView token;
        if (!r_ast_ref_child(context, qualified, index, &child) ||
            !r_ast_ref_token(context, child, &token))
            continue;
        if (count == 3U)
            return false;
        components[count++] = token;
    }
    return count == 3U && r_semantic_token_text_equal_owned(context, &components[0], "std") &&
           r_semantic_token_text_equal_owned(context, &components[1], "json") &&
           r_semantic_token_text_equal_owned(context, &components[2], "schema");
}

/* R-SLIB-JSON-0002: builds the schema text of a closed type and attaches it, with the schema
   hooks of its definitions, to the operation symbol; false after a diagnostic or a failure. */
static bool r_json_schema_attach(RBodyContext *body, RSourceSpan span, RTypeId type, RSymbolId symbol) {
    RFrontendContext *context = body->frontend;
    RJsonSchemaWriter writer = {0};
    bool success = false;
    writer.body = body;
    writer.span = span;
    writer.root = type;
    const size_t diagnostics = context->diagnostic_count;
    if (!r_json_schema_build(&writer, type) || writer.failed || context->diagnostic_count != diagnostics)
        goto cleanup;
    uint32_t text;
    if (!r_intern_bytes(context, (const uint8_t *)writer.text.bytes, writer.text.length, &text))
        goto cleanup;
    context->semantic_symbols[symbol - 1U].json_schema_text = text;
    for (uint32_t index = 0U; index < writer.definition_count; ++index) {
        bool known = false;
        if (writer.definition_hooks[index] == 0U)
            continue;
        for (size_t entry = 0U; entry < context->json_schema_hook_count; ++entry)
            known = known || (context->json_schema_hooks[entry].function == symbol &&
                              context->json_schema_hooks[entry].hook == writer.definition_hooks[index]);
        if (known)
            continue;
        if (!r_grow_array(context,
                          (void **)&context->json_schema_hooks,
                          &context->json_schema_hook_capacity,
                          sizeof(*context->json_schema_hooks),
                          context->json_schema_hook_count + 1U))
            goto cleanup;
        context->json_schema_hooks[context->json_schema_hook_count++] =
            (RJsonSchemaHook){symbol, writer.definition_hooks[index], writer.definition_names[index]};
    }
    success = true;
cleanup:
    r_context_free(context, writer.text.bytes);
    return success;
}

/* R-SLIB-JSON-0002: `std.json::schema::<T>()` parses the schema text of T. In a generic body a
   type that depends on a parameter needs the json_encode or json_decode constraint of that
   parameter; its text is built for each instantiation when the body is cloned. */
static bool r_json_schema_lower(RBodyContext *body, RAstRef call, RExpressionResult *result) {
    RFrontendContext *context = body->frontend;
    RAstNodeView view;
    RAstRef type_ref = {0};
    RTypeId type = R_TYPE_ID_INVALID, json_value;
    RHirVector children = {0};
    r_body_expression_invalid(result);
    if (!r_ast_view_kind(context, call, R_SYNTAX_STANDARD_TYPE_CALL, &view))
        return false;
    for (uint32_t index = 0U; index < view.child_count; ++index) {
        RAstRef child;
        RAstNodeView child_view;
        if (!r_ast_ref_child(context, call, index, &child) ||
            !r_ast_ref_view(context, child, &child_view))
            continue;
        if (child_view.kind == R_SYNTAX_TYPE)
            type_ref = child;
        else if (child_view.kind == R_SYNTAX_EXPRESSION)
            return r_body_diagnostic(body,
                                     "R-DIAG-JSON-001",
                                     "R-SLIB-JSON-0002",
                                     "std.json::schema takes no value arguments",
                                     view.span);
    }
    if (type_ref.node == R_AST_NODE_ID_INVALID)
        return r_body_diagnostic(body,
                                 "R-DIAG-JSON-001",
                                 "R-SLIB-JSON-0002",
                                 "std.json::schema takes its type from ::<T>",
                                 view.span);
    if (!r_semantic_type_from_ast(context, type_ref, &type) ||
        !r_semantic_intern_named_standard_type(context, "std.json::value", &json_value))
        return context->resource_status == R_FRONTEND_OK;
    type = r_semantic_value_type(context, type);
    if (r_generic_type_is_dependent(context, type, 0U)) {
        if (!r_json_capability_visit(context, type, true, 0U, NULL) &&
            !r_json_capability_visit(context, type, false, 0U, NULL))
            return r_body_diagnostic(body,
                                     "R-DIAG-JSON-001",
                                     "R-SLIB-JSON-0002",
                                     "std.json::schema of a type parameter requires its json_encode or "
                                     "json_decode constraint",
                                     view.span);
        return r_json_call(body, view.span, R_JSON_OPERATION_SCHEMA, type, json_value, &children, true, false, result);
    }
    {
        /* The schema is checked before the call exists, so that a type without one reports
           its diagnostic and no operation. */
        RJsonSchemaWriter writer = {0};
        writer.body = body;
        writer.span = view.span;
        writer.root = type;
        const size_t diagnostics = context->diagnostic_count;
        const bool built = r_json_schema_build(&writer, type) && !writer.failed &&
                           context->diagnostic_count == diagnostics;
        r_context_free(context, writer.text.bytes);
        if (!built)
            return context->resource_status == R_FRONTEND_OK;
    }
    if (!r_json_call(body, view.span, R_JSON_OPERATION_SCHEMA, type, json_value, &children, true, false, result))
        return false;
    if (result->symbol != R_SYMBOL_ID_INVALID && !r_json_schema_attach(body, view.span, type, result->symbol))
        return context->resource_status == R_FRONTEND_OK;
    return true;
}
