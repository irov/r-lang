/* Flatten only embedded objects. Ordinary nested fields retain their own codecs and ownership. */
static bool r_json_schema_append(RFrontendContext *context,
                                 RTypeId type_id,
                                 uint32_t first,
                                 uint32_t parent,
                                 uint32_t depth,
                                 bool *valid) {
    const RSemanticAggregate *aggregate = r_semantic_aggregate_for_type(context, type_id);
    if (aggregate == NULL || depth > context->options.limits.max_nesting)
        return false;
    for (uint32_t i = 0U; i < aggregate->field_count; ++i) {
        uint32_t field_id = aggregate->first_field + i;
        const RSemanticField *field = &context->semantic_fields[field_id];
        const RSemanticType *type =
            r_semantic_type(context, r_semantic_value_type(context, field->type));
        RJsonSchemaField entry = {.field = field_id, .parent = parent};
        entry.excluded = field->is_protected || (field->json.flags & R_JSON_FIELD_SKIP) != 0U;
        if (!entry.excluded && (field->json.flags & R_JSON_FIELD_EMBED) != 0U) {
            const RSemanticAggregate *child = r_semantic_aggregate_for_type(context, field->type);
            entry.embedded = type->kind == R_SEMANTIC_TYPE_STRUCT;
            entry.collector = type->kind == R_SEMANTIC_TYPE_DICT ||
                              r_semantic_standard_type_name_equal(context, type, "std.json::value");
            if ((!entry.embedded && !entry.collector) ||
                (entry.embedded &&
                 (child->json_marshal_function != 0U || child->json_unmarshal_function != 0U))) {
                *valid = false;
                if (!r_json_attribute_diagnostic(context,
                                                 field->name_span,
                                                 "JSON embed requires a structure without JSON "
                                                 "converters or an object collector"))
                    return false;
                continue;
            }
        }
        if (context->json_field_count >= UINT32_MAX ||
            !r_grow_array(context,
                          (void **)&context->json_fields,
                          &context->json_field_capacity,
                          sizeof(*context->json_fields),
                          context->json_field_count + 1U))
            return false;
        uint32_t at = (uint32_t)context->json_field_count++;
        context->json_fields[at] = entry;
        if (entry.embedded &&
            !r_json_schema_append(context, field->type, first, at - first, depth + 1U, valid))
            return false;
        context->json_fields[at].end = (uint32_t)context->json_field_count - first;
    }
    return true;
}
static bool r_json_build_field_schema(RFrontendContext *context, RTypeId id) {
    const RSemanticAggregate *view = r_semantic_aggregate_for_type(context, id);
    if (view == NULL || view->kind != R_SEMANTIC_AGGREGATE_STRUCT || !view->complete ||
        view->poisoned || view->json_schema_type == id)
        return true;
    size_t aggregate_index = (size_t)(view - context->semantic_aggregates);
    uint32_t first = (uint32_t)context->json_field_count;
    bool valid = true;
    if (!r_json_schema_append(context, id, first, UINT32_MAX, 0U, &valid))
        return false;
    uint32_t count = (uint32_t)context->json_field_count - first;
    uint32_t collector = UINT32_MAX;
    for (uint32_t i = 0U; i < count; ++i) {
        const RJsonSchemaField *entry = &context->json_fields[first + i];
        const RSemanticField *field = &context->semantic_fields[entry->field];
        if (entry->excluded || entry->embedded)
            continue;
        if (entry->collector) {
            if (collector != UINT32_MAX) {
                valid = false;
                if (!r_json_attribute_diagnostic(
                        context,
                        field->name_span,
                        "an expanded JSON object permits only one additional-key collector"))
                    return false;
            }
            collector = i;
            continue;
        }
        uint32_t name = (field->json.flags & R_JSON_FIELD_NAME) != 0U ? field->json.name_intern_id
                                                                      : field->name_intern_id;
        for (uint32_t j = 0U; j < i; ++j) {
            const RJsonSchemaField *previous = &context->json_fields[first + j];
            if (previous->embedded || previous->collector || previous->excluded)
                continue;
            const RSemanticField *other = &context->semantic_fields[previous->field];
            uint32_t other_name = (other->json.flags & R_JSON_FIELD_NAME) != 0U
                                      ? other->json.name_intern_id
                                      : other->name_intern_id;
            if (name == other_name) {
                valid = false;
                if (!r_json_attribute_diagnostic(
                        context,
                        field->name_span,
                        "JSON field names conflict after expanding embedded fields"))
                    return false;
            }
        }
    }
    RSemanticAggregate *aggregate = &context->semantic_aggregates[aggregate_index];
    aggregate->json_schema_type = id;
    aggregate->first_json_field = first;
    aggregate->json_field_count = count;
    aggregate->json_schema_invalid = !valid;
    return true;
}
