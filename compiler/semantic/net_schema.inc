static bool r_semantic_prepare_net_enum(RFrontendContext *context, const char *name,
                                        const RSemanticAggregate **aggregate) {
    static const char *const family[] = {"any", "v4", "v6"};
    static const char *const shutdown[] = {"read", "write", "both"};
    static const char *const errors[] = {
        "invalid_address", "invalid_name", "name_not_found", "temporary_failure",
        "connection_refused", "connection_reset", "connection_aborted", "address_in_use",
        "address_not_available", "network_unreachable", "host_unreachable", "not_connected",
        "closed", "permission_denied", "resource_exhausted", "message_too_large", "cancelled",
        "timed_out", "unsupported", "other"};
    const char *const *variants = family;
    uint32_t count = 3U;
    RTypeId type;
    if (strcmp(name, "std.net::shutdown_direction") == 0) variants = shutdown;
    else if (strcmp(name, "std.net::error_code") == 0) {
        variants = errors;
        count = (uint32_t)(sizeof(errors) / sizeof(errors[0]));
    } else if (strcmp(name, "std.net::family") != 0) return false;
    return r_semantic_intern_named_standard_type(context, name, &type) &&
           r_semantic_ensure_hidden_schema_enum(context, name, type, variants, count, aggregate);
}

static bool r_semantic_prepare_net_schema(RFrontendContext *context, RTypeId type_id) {
    const RSemanticType *type = r_semantic_type(context, r_semantic_value_type(context, type_id));
    RStandardSchemaField fields[6];
    RTypeId ignored, first, second, third;
    const char *name = NULL;
    uint32_t count = 0U;
    bool copy = true;
    if (type == NULL) return true;
    if (r_semantic_standard_type_name_equal(context, type, "std.net::socket_address")) {
        name = "std.net::socket_address";
        if (!r_semantic_intern_named_standard_type(context, "std.net::ip_address", &first) ||
            !r_semantic_type_from_token(context, R_TOKEN_KW_U16, &second) ||
            !r_semantic_type_from_token(context, R_TOKEN_KW_U32, &third)) return false;
        fields[0] = (RStandardSchemaField){"address", first};
        fields[1] = (RStandardSchemaField){"port", second};
        fields[2] = (RStandardSchemaField){"scope_id", third};
        count = 3U;
    } else if (r_semantic_standard_type_name_equal(context, type, "std.net::listen_options")) {
        name = "std.net::listen_options";
        if (!r_semantic_type_from_token(context, R_TOKEN_KW_U32, &first) ||
            !r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, &second)) return false;
        fields[0] = (RStandardSchemaField){"backlog", first};
        fields[1] = (RStandardSchemaField){"reuse_address", second};
        fields[2] = (RStandardSchemaField){"v6_only", second};
        count = 3U;
    } else if (r_semantic_standard_type_name_equal(context, type, "std.net::tcp_connection")) {
        name = "std.net::tcp_connection";
        if (!r_semantic_intern_named_standard_type(context, "std.net::tcp_stream", &first) ||
            !r_semantic_intern_named_standard_type(context, "std.net::socket_address", &second)) return false;
        fields[0] = (RStandardSchemaField){"stream", first};
        fields[1] = (RStandardSchemaField){"peer", second};
        count = 2U;
        copy = false;
    } else if (r_semantic_standard_type_name_equal(context, type, "std.net::datagram")) {
        name = "std.net::datagram";
        if (!r_semantic_type_from_token(context, R_TOKEN_KW_USIZE, &first) ||
            !r_semantic_intern_named_standard_type(context, "std.net::socket_address", &second) ||
            !r_semantic_prepare_net_schema(context, second) ||
            !r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, &third)) return false;
        fields[0] = (RStandardSchemaField){"count", first};
        fields[1] = (RStandardSchemaField){"peer", second};
        fields[2] = (RStandardSchemaField){"truncated", third};
        count = 3U;
    } else if (r_semantic_standard_type_name_equal(context, type, "std.net::tcp_options") ||
               r_semantic_standard_type_name_equal(context, type, "std.net::udp_options")) {
        /* R-SLIB-NET-0012: the option records. */
        const bool tcp = r_semantic_standard_type_name_equal(context, type, "std.net::tcp_options");
        RTypeId flag, hops, size, duration, keepalive;
        name = tcp ? "std.net::tcp_options" : "std.net::udp_options";
        if (!r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, &flag) ||
            !r_semantic_type_from_token(context, R_TOKEN_KW_U32, &hops) ||
            !r_semantic_type_from_token(context, R_TOKEN_KW_USIZE, &size) ||
            !r_semantic_intern_named_standard_type(context, "std.time::duration", &duration) ||
            !r_semantic_intern_derived_type(context, R_SEMANTIC_TYPE_OPTION, duration,
                                            R_TYPE_ID_INVALID, UINT64_C(0),
                                            R_SEMANTIC_TYPE_FLAG_NONE, &keepalive)) return false;
        if (tcp) {
            fields[0] = (RStandardSchemaField){"nodelay", flag};
            fields[1] = (RStandardSchemaField){"keepalive", keepalive};
            fields[2] = (RStandardSchemaField){"hop_limit", hops};
            fields[3] = (RStandardSchemaField){"receive_buffer", size};
            fields[4] = (RStandardSchemaField){"send_buffer", size};
            count = 5U;
        } else {
            fields[0] = (RStandardSchemaField){"hop_limit", hops};
            fields[1] = (RStandardSchemaField){"receive_buffer", size};
            fields[2] = (RStandardSchemaField){"send_buffer", size};
            fields[3] = (RStandardSchemaField){"broadcast", flag};
            fields[4] = (RStandardSchemaField){"multicast_hop_limit", hops};
            fields[5] = (RStandardSchemaField){"multicast_loop", flag};
            count = 6U;
        }
    } else if (r_semantic_standard_type_name_equal(context, type, "std.net::peer_credentials")) {
        /* R-SLIB-NET-0016: the credentials of a Unix-domain peer. */
        name = "std.net::peer_credentials";
        if (!r_semantic_type_from_token(context, R_TOKEN_KW_U32, &first) ||
            !r_semantic_type_from_token(context, R_TOKEN_KW_I32, &second)) return false;
        fields[0] = (RStandardSchemaField){"user_id", first};
        fields[1] = (RStandardSchemaField){"group_id", first};
        fields[2] = (RStandardSchemaField){"process_id", second};
        count = 3U;
    } else if (r_semantic_standard_type_name_equal(context, type, "std.net::unix_message")) {
        /* R-SLIB-NET-0017: one received Unix-domain datagram prefix. */
        name = "std.net::unix_message";
        if (!r_semantic_type_from_token(context, R_TOKEN_KW_USIZE, &first) ||
            !r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, &second)) return false;
        fields[0] = (RStandardSchemaField){"count", first};
        fields[1] = (RStandardSchemaField){"truncated", second};
        count = 2U;
    } else if (r_semantic_standard_type_name_equal(context, type, "std.net::net_error")) {
        const RSemanticAggregate *code;
        name = "std.net::net_error";
        if (!r_semantic_prepare_net_enum(context, "std.net::error_code", &code)) return false;
        first = code->type;
        if (!r_semantic_type_from_token(context, R_TOKEN_KW_I64, &second)) return false;
        fields[0] = (RStandardSchemaField){"code", first};
        fields[1] = (RStandardSchemaField){"native_code", second};
        count = 2U;
    }
    return name == NULL || r_semantic_ensure_hidden_schema_struct(context, name, type_id, fields,
                                                                  count, copy, &ignored);
}

static bool r_semantic_prepare_net_outcome(RFrontendContext *context, const char *name,
                                           RTypeId *payload_types) {
    const RStandardNetOutcome *descriptor = NULL;
    size_t index;
    RTypeId byte, buffer;
    uint32_t variant_index;
    for (index = 0U; r_standard_net_outcome_at(index) != NULL; ++index) {
        if (strcmp(name, r_standard_net_outcome_at(index)->name) == 0) {
            descriptor = r_standard_net_outcome_at(index);
            break;
        }
    }
    if (descriptor == NULL || !r_semantic_type_from_token(context, R_TOKEN_KW_U8, &byte) ||
        !r_semantic_intern_type(context, R_SEMANTIC_TYPE_ARRAY, byte, &buffer)) return false;
    for (variant_index = 0U; variant_index < descriptor->variant_count; ++variant_index) {
        const RStandardNetOutcomeVariant *variant = &descriptor->variants[variant_index];
        RStandardSchemaField fields[4];
        uint32_t field_index;
        if (variant->field_count == 0U) { payload_types[variant_index] = buffer; continue; }
        for (field_index = 0U; field_index < variant->field_count; ++field_index) {
            const RStandardNetOutcomeField *field = &variant->fields[field_index];
            RTypeId type;
            if (strcmp(field->type, "bytes") == 0) type = buffer;
            else if (strcmp(field->type, "usize") == 0) {
                if (!r_semantic_type_from_token(context, R_TOKEN_KW_USIZE, &type)) return false;
            } else if (strcmp(field->type, "bool") == 0) {
                if (!r_semantic_type_from_token(context, R_TOKEN_KW_BOOL, &type)) return false;
            } else if (!r_semantic_intern_named_standard_type(context, field->type, &type) ||
                       !r_semantic_prepare_net_schema(context, type)) return false;
            fields[field_index] = (RStandardSchemaField){field->name, type};
        }
        if (!r_semantic_ensure_hidden_schema_struct(context, variant->payload_name,
                R_TYPE_ID_INVALID, fields, variant->field_count, false, &payload_types[variant_index]))
            return false;
    }
    return true;
}
