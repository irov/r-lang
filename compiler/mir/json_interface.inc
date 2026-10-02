/* JSON contracts are exported independently of physical field layout and visibility. */
static bool r_mir_write_definition_digest(const RFrontendContext *context,
                                          RSourceId source_id,
                                          RAstNodeId declaration,
                                          RFrontendWriteFn writer,
                                          void *user_data);
static bool r_mir_write_json_field(const RFrontendContext *context,
                                   const RSemanticField *field,
                                   RFrontendWriteFn writer,
                                   void *user_data) {
    static const char *const flags[] = {"name",
                                        "skip",
                                        "omitempty",
                                        "omitzero",
                                        "string",
                                        "case",
                                        "embed",
                                        "optional",
                                        "default",
                                        "omitnone",
                                        "description"};
    bool first = true;
    if (!field->json.present)
        return r_write_text(writer, user_data, " json=none");
    if (!r_write_text(writer, user_data, " json=(flags=("))
        return false;
    for (size_t i = 0U; i < sizeof(flags) / sizeof(flags[0]); ++i) {
        if ((field->json.flags & (UINT32_C(1) << i)) == 0U)
            continue;
        if ((!first && !r_write_text(writer, user_data, " ")) ||
            !r_write_text(writer, user_data, flags[i]))
            return false;
        first = false;
    }
    if (!r_write_text(writer, user_data, ")"))
        return false;
    if ((field->json.flags & R_JSON_FIELD_NAME) != 0U) {
        const RInternEntry *name = &context->intern_entries[field->json.name_intern_id - 1U];
        if (!r_write_text(writer, user_data, " name=") ||
            !r_write_escaped(writer, user_data, (const uint8_t *)name->bytes, name->length))
            return false;
    }
    if ((field->json.flags & R_JSON_FIELD_DESCRIPTION) != 0U) {
        const RInternEntry *text = &context->intern_entries[field->json.description_intern_id - 1U];
        if (!r_write_text(writer, user_data, " description=") ||
            !r_write_escaped(writer, user_data, (const uint8_t *)text->bytes, text->length))
            return false;
    }
    if ((field->json.flags & R_JSON_FIELD_CASE) != 0U &&
        !r_write_text(writer, user_data, field->json.ignore_case ? " case=ignore" : " case=strict"))
        return false;
    if ((field->json.flags & R_JSON_FIELD_DEFAULT) != 0U) {
        const RSource *source = r_get_source_const(context, field->name_span.source);
        const RAstNode *node;
        if (source == NULL || field->json.default_ast == 0U ||
            field->json.default_ast > source->ast_node_count)
            return false;
        node = &source->ast_nodes[field->json.default_ast - 1U];
        if (node->span.end < node->span.start || node->span.end > source->length)
            return false;
        if (!r_write_text(writer, user_data, " default_source=") ||
            !r_write_escaped(writer,
                             user_data,
                             source->bytes + node->span.start,
                             node->span.end - node->span.start))
            return false;
        if (field->json.default_factory != R_SYMBOL_ID_INVALID) {
            const RSemanticSymbol *factory =
                &context->semantic_symbols[field->json.default_factory - 1U];
            if (!r_write_text(writer, user_data, " default_factory=(name=") ||
                !r_mir_write_qualified_symbol(context, factory, writer, user_data) ||
                !r_write_text(writer, user_data, " definition_sha256=\"") ||
                !r_mir_write_definition_digest(
                    context, factory->module_source, factory->declaration_ast, writer, user_data) ||
                !r_write_text(writer, user_data, "\")"))
                return false;
        }
    }
    return r_write_text(writer, user_data, ")");
}
