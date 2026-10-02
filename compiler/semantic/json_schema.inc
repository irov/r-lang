/* Native JSON records retain their public C ABI and ordinary nominal R types. */
static bool r_semantic_prepare_json_enum(RFrontendContext *context,
                                         const char *name,
                                         const RSemanticAggregate **aggregate) {
    static const char *const codes[] = {"syntax",
                                        "invalid_utf8",
                                        "duplicate_key",
                                        "depth_limit",
                                        "size_limit",
                                        "unexpected_eof",
                                        "type",
                                        "range",
                                        "missing_field",
                                        "unknown_field",
                                        "ambiguous_field",
                                        "invalid_state",
                                        "key_conflict"};
    static const char *const kinds[] = {"null", "boolean", "number", "string", "array", "object"};
    static const char *const modes[] = {"document", "sequence", "array_elements"};
    static const char *const feed_states[] = {"need_input", "value_ready", "end"};
    RTypeId type;
    const char *const *names = codes;
    uint32_t count = (uint32_t)(sizeof(codes) / sizeof(codes[0]));
    if (strcmp(name, "std.json::value_kind") == 0) {
        names = kinds;
        count = 6U;
    } else if (strcmp(name, "std.json::mode") == 0) {
        names = modes;
        count = 3U;
    } else if (strcmp(name, "std.json::feed_state") == 0) {
        names = feed_states;
        count = 3U;
    }
    return r_semantic_intern_named_standard_type(context, name, &type) &&
           r_semantic_ensure_hidden_schema_enum(context, name, type, names, count, aggregate);
}
static bool r_semantic_prepare_json_schema(RFrontendContext *context, RTypeId id) {
    const RSemanticType *type = r_semantic_type(context, r_semantic_value_type(context, id));
    RTypeId output, usize_type, bool_type, string_type;
    const RSemanticAggregate *enumeration;
    RStandardSchemaField fields[6];
    if (r_semantic_standard_type_name_equal(context, type, "std.json::feed_result")) {
        if (!r_semantic_prepare_json_enum(context, "std.json::feed_state", &enumeration) ||
            !r_semantic_type_from_token(context, R_TOKEN_KW_USIZE, &usize_type))
            return false;
        fields[0] = (RStandardSchemaField){"consumed", usize_type};
        fields[1] = (RStandardSchemaField){"state", enumeration->type};
        return r_semantic_ensure_hidden_schema_struct(
            context, "std.json::feed_result", id, fields, 2U, true, &output);
    }
    if (r_semantic_standard_type_name_equal(context, type, "std.json::error")) {
        if (!r_semantic_prepare_json_enum(context, "std.json::error_code", &enumeration))
            return false;
        fields[0] = (RStandardSchemaField){"code", enumeration->type};
        if (!r_semantic_type_from_token(context, R_TOKEN_KW_USIZE, &usize_type) ||
            !r_semantic_intern_named_standard_type(context, "std.string::string", &string_type))
            return false;
        fields[1] = (RStandardSchemaField){"offset", usize_type};
        fields[2] = (RStandardSchemaField){"pointer", string_type};
        return r_semantic_ensure_hidden_schema_struct(
            context, "std.json::error", id, fields, 3U, false, &output);
    }
    if (r_semantic_standard_type_name_equal(context, type, "std.json::options")) {
        if (!r_semantic_prepare_json_enum(context, "std.json::mode", &enumeration))
            return false;
        fields[5] = (RStandardSchemaField){"mode", enumeration->type};
        if (!r_semantic_type_from_token(context, R_TOKEN_KW_USIZE, &usize_type) ||
            !r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, &bool_type))
            return false;
        fields[0] = (RStandardSchemaField){"max_depth", usize_type};
        fields[1] = (RStandardSchemaField){"max_value_bytes", usize_type};
        fields[2] = (RStandardSchemaField){"indent", usize_type};
        fields[3] = (RStandardSchemaField){"reject_unknown_fields", bool_type};
        fields[4] = (RStandardSchemaField){"ignore_case", bool_type};
        return r_semantic_ensure_hidden_schema_struct(
            context, "std.json::options", id, fields, 6U, true, &output);
    }
    return true;
}
