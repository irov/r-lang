/* R-OWN-0020 (L26): clone glue. core::clone of a Move value calls one generated function per
 * cloned type,
 *
 *     static r_clone_result r_type_clone_<id>(void *destination, const void *source,
 *                                             RRuntimeSourceSpan span);
 *
 * which builds an independent copy in uninitialized destination storage. On failure it destroys
 * every component it already built, leaves the destination unowned and reports the allocation
 * error; the source is never changed. The glue is spliced in after the function prototypes,
 * because a clone hook is an ordinary R function. Self-nesting
 * types have no structural clone (semantic R-OWN-0020), so the glue functions form a DAG. */

static bool r_c17_clone_is_copy(const RC17Emitter *emitter, RTypeId type_id) {
    return !r_c17_type_requires_drop(emitter, type_id);
}

static const RSemanticAggregate *r_c17_clone_aggregate(const RC17Emitter *emitter,
                                                       RTypeId type_id) {
    const RSemanticType *type = r_c17_type(emitter, r_c17_value_type(emitter, type_id));
    if (type == NULL ||
        (type->kind != R_SEMANTIC_TYPE_STRUCT && type->kind != R_SEMANTIC_TYPE_ENUM) ||
        type->base == 0U || (size_t)type->base > emitter->frontend->semantic_aggregate_count)
        return NULL;
    return &emitter->frontend->semantic_aggregates[type->base - 1U];
}

static bool r_c17_emit_clone_glue_name(RC17Emitter *emitter, RTypeId type_id) {
    return r_c17_format(emitter, "r_type_clone_%08" PRIu32, r_c17_value_type(emitter, type_id));
}

/* Marks `type_id` and each Move component whose clone needs its own function, and prepares the
 * move and drop glue the rollback paths use. */
static bool r_c17_require_clone_glue(RC17Emitter *emitter, RTypeId type_id, uint32_t depth) {
    const RTypeId value_id = r_c17_value_type(emitter, type_id);
    const RSemanticType *type = r_c17_type(emitter, value_id);
    if (type == NULL || depth > emitter->frontend->options.limits.max_nesting ||
        (size_t)value_id > emitter->frontend->semantic_type_count)
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    if (r_c17_clone_is_copy(emitter, value_id) || emitter->clone_glue_types[value_id] != 0U)
        return true;
    emitter->clone_glue_types[value_id] = 1U;
    emitter->uses_clone_glue = true;
    emitter->uses_std_alloc = true;
    if (!r_c17_preflight_type_dependency(emitter, value_id, 0U) ||
        !r_c17_mark_type_closure(emitter, value_id, emitter->type_glue_types, false, 0U) ||
        !r_c17_mark_drop_glue_closure(emitter, value_id))
        return false;
    switch (type->kind) {
    case R_SEMANTIC_TYPE_ARRAY:
        emitter->uses_runtime_array = true;
        emitter->uses_memory = true;
        return r_c17_require_clone_glue(emitter, type->base, depth + 1U);
    case R_SEMANTIC_TYPE_LIST:
        emitter->uses_runtime_list = true;
        return r_c17_require_clone_glue(emitter, type->base, depth + 1U);
    case R_SEMANTIC_TYPE_OPTION:
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
    case R_SEMANTIC_TYPE_OWN:
        return r_c17_require_clone_glue(emitter, type->base, depth + 1U);
    case R_SEMANTIC_TYPE_DICT:
        emitter->uses_runtime_dict = true;
        return r_c17_require_clone_glue(emitter, type->base, depth + 1U) &&
               r_c17_require_clone_glue(emitter, type->second, depth + 1U);
    case R_SEMANTIC_TYPE_ARC:
        emitter->uses_runtime_arc = true;
        return true;
    case R_SEMANTIC_TYPE_RC:
        emitter->uses_runtime_rc = true;
        return true;
    case R_SEMANTIC_TYPE_WEAK:
        if ((type->flags & R_SEMANTIC_TYPE_FLAG_RC_OWNER) != 0U)
            emitter->uses_runtime_rc = true;
        else
            emitter->uses_runtime_arc = true;
        return true;
    case R_SEMANTIC_TYPE_STANDARD:
        if (r_c17_type_is_fs_path(emitter, value_id))
            emitter->uses_std_fs = true;
        return true;
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM: {
        const RSemanticAggregate *aggregate = r_c17_clone_aggregate(emitter, value_id);
        if (aggregate == NULL)
            return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
        if (aggregate->clone_function != R_SYMBOL_ID_INVALID)
            return r_c17_preflight_function(emitter, aggregate->clone_function);
        for (uint32_t i = 0U; i < aggregate->field_count; ++i)
            if (!r_c17_require_clone_glue(
                    emitter,
                    emitter->frontend->semantic_fields[aggregate->first_field + i].type,
                    depth + 1U))
                return false;
        for (uint32_t i = 0U; i < aggregate->variant_count; ++i) {
            const RTypeId payload =
                emitter->frontend->semantic_variants[aggregate->first_variant + i].payload_type;
            if (payload != R_TYPE_ID_INVALID &&
                !r_c17_require_clone_glue(emitter, payload, depth + 1U))
                return false;
        }
        return true;
    }
    default:
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    }
}

/* One component clone into `destination` from `source` (C lvalue texts): a Copy component is
 * assigned; a Move component calls its glue and, on failure, runs `undo` (statements that destroy
 * the components built before it) and returns the failure. */
static bool r_c17_emit_clone_component(RC17Emitter *emitter,
                                       RTypeId type_id,
                                       const char *destination,
                                       const char *source,
                                       const char *undo,
                                       uint32_t depth) {
    if (r_c17_clone_is_copy(emitter, type_id))
        return r_c17_indent(emitter, depth) &&
               r_c17_format(emitter, "%s = %s;\n", destination, source);
    return r_c17_indent(emitter, depth) && r_c17_write(emitter, "result = ") &&
           r_c17_emit_clone_glue_name(emitter, type_id) &&
           r_c17_format(emitter, "(&%s, &%s, span);\n", destination, source) &&
           r_c17_indent(emitter, depth) &&
           r_c17_write(emitter, "if (result.status != R_STD_ALLOC_CALL_SUCCESS) {\n") &&
           r_c17_write(emitter, undo) && r_c17_indent(emitter, depth + 1U) &&
           r_c17_write(emitter, "return result;\n") && r_c17_indent(emitter, depth) &&
           r_c17_write(emitter, "}\n");
}

static bool r_c17_emit_clone_drop(RC17Emitter *emitter,
                                  RTypeId type_id,
                                  const char *place,
                                  uint32_t depth) {
    if (r_c17_clone_is_copy(emitter, type_id))
        return true;
    return r_c17_indent(emitter, depth) && r_c17_emit_type_drop_name(emitter, type_id) &&
           r_c17_format(emitter, "(&%s);\n", place);
}

static bool r_c17_emit_clone_header(RC17Emitter *emitter, RTypeId type_id, bool typed) {
    if (!r_c17_write(emitter, "static r_clone_result ") ||
        !r_c17_emit_clone_glue_name(emitter, type_id) ||
        !r_c17_write(emitter,
                     "(void *destination_pointer,\n"
                     "                                    const void *source_pointer,\n"
                     "                                    RRuntimeSourceSpan span) {\n"))
        return false;
    if (!typed)
        return true;
    return r_c17_write(emitter, "    ") && r_c17_emit_value_type(emitter, type_id) &&
           r_c17_write(emitter, " *destination = destination_pointer;\n    const ") &&
           r_c17_emit_value_type(emitter, type_id) &&
           r_c17_write(emitter,
                       " *source = source_pointer;\n"
                       "    r_clone_result result = {0};\n"
                       "\n"
                       "    (void)span;\n");
}

static bool r_c17_emit_clone_sequence_body(RC17Emitter *emitter,
                                           const char *runtime_type,
                                           RTypeId element,
                                           bool list) {
    /* array<T>: one allocation of exactly the source length, then element clones in order. */
    if (!list) {
        if (!r_c17_format(emitter,
                          "    %s *destination = destination_pointer;\n"
                          "    const %s *source = source_pointer;\n"
                          "    r_clone_result result = {0};\n"
                          "    size_t index = 0U;\n"
                          "    unsigned status = (unsigned)r_runtime_array_with_capacity(\n"
                          "        destination, r_runtime_hosted_allocator(), source->element, "
                          "source->length);\n"
                          "\n"
                          "    (void)span;\n"
                          "    if (status != 0U) {\n"
                          "        return r_clone_failure(status, span);\n"
                          "    }\n",
                          runtime_type,
                          runtime_type))
            return false;
        if (r_c17_clone_is_copy(emitter, element)) {
            return r_c17_write(emitter,
                               "    (void)index;\n"
                               "    (void)result;\n"
                               "    if (source->length != 0U) {\n"
                               "        memcpy(destination->data, source->data, source->length * "
                               "source->element.size);\n"
                               "    }\n"
                               "    destination->length = source->length;\n"
                               "    return result;\n"
                               "}\n"
                               "\n");
        }
        return r_c17_write(emitter,
                           "    for (index = 0U; index < source->length; ++index) {\n"
                           "        result = ") &&
               r_c17_emit_clone_glue_name(emitter, element) &&
               r_c17_write(emitter,
                           "((unsigned char *)destination->data + index * "
                           "source->element.size,\n"
                           "            (const unsigned char *)source->data + index * "
                           "source->element.size,\n"
                           "            span);\n"
                           "        if (result.status != R_STD_ALLOC_CALL_SUCCESS) {\n"
                           "            destination->length = index;\n"
                           "            r_runtime_array_destroy(destination);\n"
                           "            return result;\n"
                           "        }\n"
                           "        destination->length = index + 1U;\n"
                           "    }\n"
                           "    return result;\n"
                           "}\n"
                           "\n");
    }
    /* list<T>: nodes are appended in order; a failed append destroys the staged element. */
    if (!r_c17_format(emitter,
                      "    %s *destination = destination_pointer;\n"
                      "    const %s *source = source_pointer;\n"
                      "    r_clone_result result = {0};\n"
                      "    RRuntimeListIterator cursor = r_runtime_list_iter(source);\n"
                      "    const void *element = NULL;\n"
                      "    void *stored = NULL;\n"
                      "    unsigned status = (unsigned)r_runtime_list_initialize(\n"
                      "        destination, r_runtime_hosted_allocator(), source->element);\n"
                      "\n"
                      "    (void)span;\n"
                      "    if (status != 0U) {\n"
                      "        return r_clone_failure(status, span);\n"
                      "    }\n"
                      "    while ((element = r_runtime_list_next(&cursor)) != NULL) {\n"
                      "        ",
                      runtime_type,
                      runtime_type) ||
        !r_c17_emit_value_type(emitter, element) ||
        !r_c17_write(emitter, " staged = {0};\n\n"))
        return false;
    if (r_c17_clone_is_copy(emitter, element)) {
        if (!r_c17_write(emitter, "        staged = *(const ") ||
            !r_c17_emit_value_type(emitter, element) ||
            !r_c17_write(emitter, " *)element;\n"))
            return false;
    } else if (!r_c17_write(emitter, "        result = ") ||
               !r_c17_emit_clone_glue_name(emitter, element) ||
               !r_c17_write(emitter,
                            "(&staged, element, span);\n"
                            "        if (result.status != R_STD_ALLOC_CALL_SUCCESS) {\n"
                            "            r_runtime_list_destroy(destination);\n"
                            "            return result;\n"
                            "        }\n")) {
        return false;
    }
    if (!r_c17_write(emitter,
                     "        status = (unsigned)r_runtime_list_push_back(destination, &staged, "
                     "&stored);\n"
                     "        if (status != 0U) {\n"))
        return false;
    if (!r_c17_emit_clone_drop(emitter, element, "staged", 3U))
        return false;
    return r_c17_write(emitter,
                       "            r_runtime_list_destroy(destination);\n"
                       "            return r_clone_failure(status, span);\n"
                       "        }\n"
                       "    }\n"
                       "    return result;\n"
                       "}\n"
                       "\n");
}

static bool r_c17_emit_clone_dict_body(RC17Emitter *emitter, RTypeId key, RTypeId value) {
    if (!r_c17_write(emitter,
                     "    RRuntimeDict *destination = destination_pointer;\n"
                     "    const RRuntimeDict *source = source_pointer;\n"
                     "    r_clone_result result = {0};\n"
                     "    RRuntimeDictIterator cursor = r_runtime_dict_iter(source);\n"
                     "    RRuntimeDictEntryRef entry = {0};\n"
                     "    unsigned status = (unsigned)r_runtime_dict_with_capacity(\n"
                     "        destination, r_runtime_hosted_allocator(), source->key, "
                     "source->value, source->seed, source->length);\n"
                     "\n"
                     "    (void)span;\n"
                     "    if (status != 0U) {\n"
                     "        return r_clone_failure(status, span);\n"
                     "    }\n"
                     "    while (r_runtime_dict_next(&cursor, &entry)) {\n"
                     "        ") ||
        !r_c17_emit_value_type(emitter, key) ||
        !r_c17_write(emitter, " staged_key = {0};\n        ") ||
        !r_c17_emit_value_type(emitter, value) ||
        !r_c17_write(emitter, " staged_value = {0};\n        ") ||
        !r_c17_emit_value_type(emitter, value) ||
        !r_c17_write(emitter,
                     " replaced = {0};\n"
                     "        _Bool did_replace = 0;\n\n"))
        return false;
    if (r_c17_clone_is_copy(emitter, key)) {
        if (!r_c17_write(emitter, "        staged_key = *(const ") ||
            !r_c17_emit_value_type(emitter, key) || !r_c17_write(emitter, " *)entry.key;\n"))
            return false;
    } else if (!r_c17_write(emitter, "        result = ") ||
               !r_c17_emit_clone_glue_name(emitter, key) ||
               !r_c17_write(emitter,
                            "(&staged_key, entry.key, span);\n"
                            "        if (result.status != R_STD_ALLOC_CALL_SUCCESS) {\n"
                            "            r_runtime_dict_destroy(destination);\n"
                            "            return result;\n"
                            "        }\n")) {
        return false;
    }
    if (r_c17_clone_is_copy(emitter, value)) {
        if (!r_c17_write(emitter, "        staged_value = *(const ") ||
            !r_c17_emit_value_type(emitter, value) || !r_c17_write(emitter, " *)entry.value;\n"))
            return false;
    } else if (!r_c17_write(emitter, "        result = ") ||
               !r_c17_emit_clone_glue_name(emitter, value) ||
               !r_c17_write(emitter,
                            "(&staged_value, entry.value, span);\n"
                            "        if (result.status != R_STD_ALLOC_CALL_SUCCESS) {\n") ||
               !r_c17_emit_clone_drop(emitter, key, "staged_key", 3U) ||
               !r_c17_write(emitter,
                            "            r_runtime_dict_destroy(destination);\n"
                            "            return result;\n"
                            "        }\n")) {
        return false;
    }
    if (!r_c17_write(emitter,
                     "        status = (unsigned)r_runtime_dict_insert(\n"
                     "            destination, &staged_key, &staged_value, &replaced, "
                     "&did_replace);\n"
                     "        (void)replaced;\n"
                     "        if (status != 0U || did_replace) {\n"
                     "            if (status == 0U) {\n"
                     "                r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION, span);\n"
                     "            }\n") ||
        !r_c17_emit_clone_drop(emitter, value, "staged_value", 3U) ||
        !r_c17_emit_clone_drop(emitter, key, "staged_key", 3U))
        return false;
    return r_c17_write(emitter,
                       "            r_runtime_dict_destroy(destination);\n"
                       "            return r_clone_failure(status, span);\n"
                       "        }\n"
                       "    }\n"
                       "    return result;\n"
                       "}\n"
                       "\n");
}

static bool r_c17_emit_clone_hook_body(RC17Emitter *emitter,
                                       RTypeId type_id,
                                       const RSemanticSymbol *hook,
                                       RSymbolId hook_id) {
    const RTypeId parameter =
        emitter->frontend->semantic_parameter_types[hook->first_parameter_type];
    if (hook->throws_type == R_TYPE_ID_INVALID) {
        return r_c17_write(emitter, "    *destination = ") &&
               r_c17_emit_function_name(emitter, hook_id) && r_c17_write(emitter, "((") &&
               r_c17_emit_value_type(emitter, parameter) &&
               r_c17_write(emitter,
                           ")source);\n"
                           "    return result;\n"
                           "}\n"
                           "\n");
    }
    return r_c17_write(emitter, "    ") &&
           r_c17_emit_value_type(emitter, hook->effect_carrier_type) &&
           r_c17_write(emitter, " carrier = {0};\n\n    ") &&
           r_c17_emit_function_name(emitter, hook_id) && r_c17_write(emitter, "(&carrier, (") &&
           r_c17_emit_value_type(emitter, parameter) &&
           r_c17_write(emitter,
                       ")source);\n"
                       "    if (carrier.r_tag != UINT32_C(0)) {\n"
                       "        result.status = R_STD_ALLOC_CALL_ALLOCATION_ERROR;\n"
                       "        result.error = carrier.r_payload.r_error_00000001;\n"
                       "        return result;\n"
                       "    }\n    ") &&
           r_c17_emit_type_move_name(emitter, type_id) &&
           r_c17_write(emitter,
                       "(destination, &carrier.r_payload.r_ok);\n"
                       "    return result;\n"
                       "}\n"
                       "\n");
}

/* Fields are cloned in declaration order; a failure destroys the Move fields already built, in
 * reverse order. */
static bool r_c17_emit_clone_struct_body(RC17Emitter *emitter,
                                         const RSemanticAggregate *aggregate) {
    char undo[4096] = "";
    size_t undo_length = 0U;
    for (uint32_t i = 0U; i < aggregate->field_count; ++i) {
        const RSemanticField *field =
            &emitter->frontend->semantic_fields[aggregate->first_field + i];
        char member[128], destination[160], source[160], line[512];
        size_t start = emitter->output.length;
        if (!r_c17_emit_field_name(emitter, field) ||
            !r_c17_capture_since(emitter, start, member, sizeof(member)))
            return false;
        (void)snprintf(destination, sizeof(destination), "destination->%s", member);
        (void)snprintf(source, sizeof(source), "source->%s", member);
        if (!r_c17_emit_clone_component(emitter, field->type, destination, source, undo, 1U))
            return false;
        if (r_c17_clone_is_copy(emitter, field->type))
            continue;
        start = emitter->output.length;
        if (!r_c17_write(emitter, "        ") || !r_c17_emit_type_drop_name(emitter, field->type) ||
            !r_c17_format(emitter, "(&%s);\n", destination) ||
            !r_c17_capture_since(emitter, start, line, sizeof(line)))
            return false;
        /* The newest field is destroyed first. */
        const size_t line_length = strlen(line);
        if (undo_length + line_length >= sizeof(undo))
            return r_c17_fail(emitter, R_FRONTEND_LIMIT_EXCEEDED);
        (void)memmove(undo + line_length, undo, undo_length + 1U);
        (void)memcpy(undo, line, line_length);
        undo_length += line_length;
    }
    return r_c17_write(emitter,
                       "    return result;\n"
                       "}\n"
                       "\n");
}

static bool r_c17_emit_clone_enum_body(RC17Emitter *emitter, RTypeId type_id) {
    const RSemanticAggregate *aggregate = r_semantic_tagged_enum(emitter->frontend, type_id);
    if (aggregate == NULL)
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    for (uint32_t index = 0U; index < aggregate->variant_count; ++index) {
        const RSemanticVariant *variant =
            r_semantic_tagged_variant(emitter->frontend, type_id, index);
        char storage[32], destination[96], source[96];
        const char *member;
        if (variant == NULL || variant->payload_type == R_TYPE_ID_INVALID)
            continue;
        member = r_c17_user_variant_member(index, storage);
        if (member == NULL)
            return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        (void)snprintf(destination, sizeof(destination), "destination->r_payload.%s", member);
        (void)snprintf(source, sizeof(source), "source->r_payload.%s", member);
        if (!r_c17_format(emitter, "    if (source->r_tag == UINT32_C(%" PRIu32 ")) {\n", index) ||
            !r_c17_emit_clone_component(
                emitter, variant->payload_type, destination, source, "", 2U) ||
            !r_c17_write(emitter, "    }\n"))
            return false;
    }
    return r_c17_write(emitter,
                       "    destination->r_tag = source->r_tag;\n"
                       "    return result;\n"
                       "}\n"
                       "\n");
}

static bool r_c17_emit_clone_glue_definition(RC17Emitter *emitter, RTypeId type_id) {
    const RSemanticType *type = r_c17_type(emitter, type_id);
    if (type == NULL)
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    switch (type->kind) {
    case R_SEMANTIC_TYPE_ARRAY:
        return r_c17_emit_clone_header(emitter, type_id, false) &&
               r_c17_emit_clone_sequence_body(emitter, "RRuntimeArray", type->base, false);
    case R_SEMANTIC_TYPE_LIST:
        return r_c17_emit_clone_header(emitter, type_id, false) &&
               r_c17_emit_clone_sequence_body(emitter, "RRuntimeList", type->base, true);
    case R_SEMANTIC_TYPE_DICT:
        return r_c17_emit_clone_header(emitter, type_id, false) &&
               r_c17_emit_clone_dict_body(emitter, type->base, type->second);
    case R_SEMANTIC_TYPE_OPTION:
        return r_c17_emit_clone_header(emitter, type_id, true) &&
               r_c17_write(emitter, "    if (source->r_tag == UINT32_C(1)) {\n") &&
               r_c17_emit_clone_component(emitter,
                                          type->base,
                                          "destination->r_payload.r_some",
                                          "source->r_payload.r_some",
                                          "",
                                          2U) &&
               r_c17_write(emitter,
                           "    }\n"
                           "    destination->r_tag = source->r_tag;\n"
                           "    return result;\n"
                           "}\n"
                           "\n");
    case R_SEMANTIC_TYPE_FIXED_ARRAY:
        return r_c17_emit_clone_header(emitter, type_id, true) &&
               r_c17_format(emitter,
                            "    size_t index = 0U;\n"
                            "\n"
                            "    for (index = 0U; index < (size_t)UINT64_C(%" PRIu64
                            "); ++index) {\n"
                            "        result = ",
                            type->length) &&
               r_c17_emit_clone_glue_name(emitter, type->base) &&
               r_c17_write(emitter,
                           "(&destination->r_data[index], &source->r_data[index], span);\n"
                           "        if (result.status != R_STD_ALLOC_CALL_SUCCESS) {\n"
                           "            while (index != 0U) {\n"
                           "                --index;\n"
                           "                ") &&
               r_c17_emit_type_drop_name(emitter, type->base) &&
               r_c17_write(emitter,
                           "(&destination->r_data[index]);\n"
                           "            }\n"
                           "            return result;\n"
                           "        }\n"
                           "    }\n"
                           "    return result;\n"
                           "}\n"
                           "\n");
    case R_SEMANTIC_TYPE_OWN:
        return r_c17_emit_clone_header(emitter, type_id, false) &&
               r_c17_write(emitter,
                           "    RRuntimeOwn *destination = destination_pointer;\n"
                           "    const RRuntimeOwn *source = source_pointer;\n"
                           "    r_clone_result result = {0};\n"
                           "    unsigned status = 0U;\n"
                           "    ") &&
               r_c17_emit_value_type(emitter, type->base) &&
               r_c17_write(emitter, " staged = {0};\n\n    (void)span;\n") &&
               (r_c17_clone_is_copy(emitter, type->base)
                    ? (r_c17_write(emitter, "    staged = *(const ") &&
                       r_c17_emit_value_type(emitter, type->base) &&
                       r_c17_write(emitter, " *)r_runtime_own_get(source);\n"))
                    : (r_c17_write(emitter, "    result = ") &&
                       r_c17_emit_clone_glue_name(emitter, type->base) &&
                       r_c17_write(emitter,
                                   "(&staged, r_runtime_own_get(source), span);\n"
                                   "    if (result.status != R_STD_ALLOC_CALL_SUCCESS) {\n"
                                   "        return result;\n"
                                   "    }\n"))) &&
               r_c17_write(emitter,
                           "    status = (unsigned)r_runtime_own_create(\n"
                           "        r_runtime_hosted_allocator(), source->type, &staged, "
                           "destination);\n"
                           "    if (status != 0U) {\n") &&
               r_c17_emit_clone_drop(emitter, type->base, "staged", 2U) &&
               r_c17_write(emitter,
                           "        return r_clone_failure(status, span);\n"
                           "    }\n"
                           "    return result;\n"
                           "}\n"
                           "\n");
    case R_SEMANTIC_TYPE_ARC:
    case R_SEMANTIC_TYPE_RC:
    case R_SEMANTIC_TYPE_WEAK: {
        const bool rc = type->kind == R_SEMANTIC_TYPE_RC ||
                        (type->kind == R_SEMANTIC_TYPE_WEAK &&
                         (type->flags & R_SEMANTIC_TYPE_FLAG_RC_OWNER) != 0U);
        const bool weak = type->kind == R_SEMANTIC_TYPE_WEAK;
        const char *owner = weak ? (rc ? "RRuntimeWeakRc" : "RRuntimeWeakArc")
                                 : (rc ? "RRuntimeRc" : "RRuntimeArc");
        const char *clone = weak ? (rc ? "r_runtime_weak_rc_clone" : "r_runtime_weak_arc_clone")
                                 : (rc ? "r_runtime_rc_clone" : "r_runtime_arc_clone");
        return r_c17_emit_clone_header(emitter, type_id, false) &&
               r_c17_format(emitter,
                            "    r_clone_result result = {0};\n"
                            "\n"
                            "    if ((unsigned)%s((const %s *)source_pointer, (%s "
                            "*)destination_pointer) != 0U) {\n"
                            "        r_runtime_panic(R_RUNTIME_PANIC_REFERENCE_COUNT_OVERFLOW, "
                            "span);\n"
                            "    }\n"
                            "    return result;\n"
                            "}\n"
                            "\n",
                            clone,
                            owner,
                            owner);
    }
    case R_SEMANTIC_TYPE_STANDARD:
        if (r_c17_type_is_fs_path(emitter, type_id)) {
            return r_c17_emit_clone_header(emitter, type_id, false) &&
                   r_c17_write(emitter,
                               "    r_clone_result result = {0};\n"
                               "    RStdFsPathAllocResult copy =\n"
                               "        r_std_fs_path_clone(source_pointer);\n"
                               "\n"
                               "    (void)span;\n"
                               "    if (copy.status != R_STD_FS_CALL_SUCCESS) {\n"
                               "        result.status = R_STD_ALLOC_CALL_ALLOCATION_ERROR;\n"
                               "        result.error = copy.error;\n"
                               "        return result;\n"
                               "    }\n"
                               "    *(RStdFsPath *)destination_pointer = copy.value;\n"
                               "    return result;\n"
                               "}\n"
                               "\n");
        }
        return r_c17_emit_clone_header(emitter, type_id, false) &&
               r_c17_write(emitter,
                           "    RStdString *destination = destination_pointer;\n"
                           "    const RStdString *source = source_pointer;\n"
                           "    unsigned status = (unsigned)r_runtime_string_from_valid_utf8(\n"
                           "        destination,\n"
                           "        r_runtime_hosted_allocator(),\n"
                           "        (const uint8_t *)source->bytes.data,\n"
                           "        source->bytes.length);\n"
                           "\n"
                           "    if (status != 0U) {\n"
                           "        return r_clone_failure(status, span);\n"
                           "    }\n"
                           "    return (r_clone_result){0};\n"
                           "}\n"
                           "\n");
    case R_SEMANTIC_TYPE_STRUCT:
    case R_SEMANTIC_TYPE_ENUM: {
        const RSemanticAggregate *aggregate = r_c17_clone_aggregate(emitter, type_id);
        if (aggregate == NULL || !r_c17_emit_clone_header(emitter, type_id, true))
            return false;
        if (aggregate->clone_function != R_SYMBOL_ID_INVALID)
            return r_c17_emit_clone_hook_body(
                emitter,
                type_id,
                &emitter->frontend->semantic_symbols[aggregate->clone_function - 1U],
                aggregate->clone_function);
        return type->kind == R_SEMANTIC_TYPE_STRUCT
                   ? r_c17_emit_clone_struct_body(emitter, aggregate)
                   : r_c17_emit_clone_enum_body(emitter, type_id);
    }
    default:
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
}

/* The clone glue the functions requested, spliced in after the function prototypes. */
static bool r_c17_emit_clone_glue(RC17Emitter *emitter) {
    RC17Buffer program = emitter->output;
    RC17Buffer scratch = {0};
    const size_t count = emitter->frontend->semantic_type_count;
    bool success = true;

    if (!emitter->uses_clone_glue) {
        return true;
    }
    emitter->output = scratch;
    success = r_c17_write(emitter,
                          "typedef struct r_clone_result {\n"
                          "    RStdAllocCallStatus status;\n"
                          "    RStdAllocError error;\n"
                          "} r_clone_result;\n"
                          "\n"
                          "static r_clone_result r_clone_failure(unsigned status, "
                          "RRuntimeSourceSpan span) {\n"
                          "    r_clone_result result = {R_STD_ALLOC_CALL_ALLOCATION_ERROR, "
                          "R_STD_ALLOC_ERROR_OUT_OF_MEMORY};\n"
                          "\n"
                          "    if (r_runtime_allocation_refused_by_budget()) {\n"
                          "        result.error = R_STD_ALLOC_ERROR_BUDGET_EXHAUSTED;\n"
                          "    }\n"
                          "\n"
                          "    if (status == 3U) {\n"
                          "        result.error = R_STD_ALLOC_ERROR_SIZE_OVERFLOW;\n"
                          "    } else if (status == 4U) {\n"
                          "        result.error = R_STD_ALLOC_ERROR_UNSUPPORTED_ALIGNMENT;\n"
                          "    } else if (status != 2U) {\n"
                          "        r_runtime_panic(R_RUNTIME_PANIC_CONTRACT_VIOLATION, span);\n"
                          "    }\n"
                          "    return result;\n"
                          "}\n"
                          "\n");
    for (size_t index = 1U; success && index <= count; ++index) {
        if (emitter->clone_glue_types[index] == 0U)
            continue;
        success = r_c17_write(emitter, "static r_clone_result ") &&
                  r_c17_emit_clone_glue_name(emitter, (RTypeId)index) &&
                  r_c17_write(emitter,
                              "(void *destination_pointer,\n"
                              "                                    const void *source_pointer,\n"
                              "                                    RRuntimeSourceSpan span);\n");
    }
    success = success && r_c17_write(emitter, "\n");
    for (size_t index = 1U; success && index <= count; ++index) {
        if (emitter->clone_glue_types[index] != 0U)
            success = r_c17_emit_clone_glue_definition(emitter, (RTypeId)index);
    }
    /* R-SLIB-ASYNC-0016 (L30): the adapters through which a broadcast clones its element. */
    for (size_t index = 1U; success && index <= count; ++index) {
        if ((emitter->clone_glue_types[index] & 2U) == 0U)
            continue;
        success = r_c17_format(emitter,
                               "static _Bool r_broadcast_clone_%08" PRIu32 "(void *destination,\n"
                               "                                          const void *source,\n"
                               "                                          RStdAllocError *error) {\n"
                               "    r_clone_result result =\n"
                               "        r_type_clone_%08" PRIu32
                               "(destination, source, (RRuntimeSourceSpan){0U, 0U, 0U});\n"
                               "\n"
                               "    if (result.status != R_STD_ALLOC_CALL_SUCCESS) {\n"
                               "        *error = result.error;\n"
                               "        return 0;\n"
                               "    }\n"
                               "    return 1;\n"
                               "}\n"
                               "\n",
                               (uint32_t)index,
                               (uint32_t)index);
    }
    scratch = emitter->output;
    emitter->output = program;
    if (success && (scratch.length != 0U)) {
        success = r_c17_buffer_reserve(emitter, scratch.length);
        if (success) {
            (void)memmove(emitter->output.bytes + emitter->clone_glue_anchor + scratch.length,
                          emitter->output.bytes + emitter->clone_glue_anchor,
                          emitter->output.length - emitter->clone_glue_anchor);
            (void)memcpy(emitter->output.bytes + emitter->clone_glue_anchor,
                         scratch.bytes,
                         scratch.length);
            emitter->output.length += scratch.length;
        }
    }
    r_c17_free(emitter, scratch.bytes);
    return success;
}

/* Sync call site: the value is built in a fresh temporary; failure transfers alloc_error. */
static bool r_c17_emit_core_clone(RC17Emitter *emitter,
                                  const RHirNode *node,
                                  uint32_t depth,
                                  RC17Value *result) {
    RC17Value source;
    uint32_t value, status;
    if (node->child_count == 1U && r_c17_clone_is_copy(emitter, node->type)) {
        /* A generic `T: clone` instance with a Copy T: the clone is a read. */
        if (!r_c17_emit_expression(emitter, r_c17_child(emitter, node, 0U), depth, &source) ||
            !source.has_value || !r_c17_new_temporary(emitter, node->type, depth, &value) ||
            !r_c17_write(emitter, " = *") ||
            !r_c17_emit_temporary_name(emitter, source.temporary) || !r_c17_write(emitter, ";\n"))
            return false;
        result->type = r_c17_value_type(emitter, node->type);
        result->temporary = value;
        result->has_value = true;
        return true;
    }
    if (node->child_count != 1U ||
        !r_c17_emit_expression(emitter, r_c17_child(emitter, node, 0U), depth, &source) ||
        !source.has_value || !r_c17_new_temporary(emitter, node->type, depth, &value) ||
        !r_c17_finish_move_destination_declaration(emitter, node->type) ||
        !r_c17_take_temporary(emitter, &status) || !r_c17_indent(emitter, depth) ||
        !r_c17_write(emitter, "r_clone_result ") || !r_c17_emit_temporary_name(emitter, status) ||
        !r_c17_write(emitter, " = ") || !r_c17_emit_clone_glue_name(emitter, node->type) ||
        !r_c17_write(emitter, "(&") || !r_c17_emit_temporary_name(emitter, value) ||
        !r_c17_write(emitter, ", ") || !r_c17_emit_temporary_name(emitter, source.temporary) ||
        !r_c17_write(emitter, ", ") || !r_c17_emit_span(emitter, node->span) ||
        !r_c17_write(emitter, ");\n"))
        return false;
    if (r_c17_value_kind(emitter, node->auxiliary_type) == R_SEMANTIC_TYPE_EFFECT_CARRIER
            ? !r_c17_emit_alloc_failure_transfer(
                  emitter, node, status, "R_STD_ALLOC_CALL_SUCCESS", depth)
            : (!r_c17_indent(emitter, depth) || !r_c17_write(emitter, "(void)") ||
               !r_c17_emit_temporary_name(emitter, status) || !r_c17_write(emitter, ";\n")))
        return false;
    result->type = r_c17_value_type(emitter, node->type);
    result->temporary = value;
    result->has_value = true;
    return true;
}

/* Async call site: the clone is built directly in the frame carrier's ok payload. */
static bool r_c17_emit_async_core_clone(RC17Emitter *emitter,
                                        const RMirFunction *mir,
                                        const RSemanticSymbol *function,
                                        const RMirInstruction *instruction,
                                        uint32_t depth) {
    uint32_t status;
    const RMirValueId source = r_c17_mir_operand(emitter, instruction, 0U);
    const bool checked =
        r_c17_value_kind(emitter, instruction->type) == R_SEMANTIC_TYPE_EFFECT_CARRIER;
    if (!checked)
        /* A clone that cannot fail is built directly in the result storage. */
        return r_c17_take_temporary(emitter, &status) && r_c17_indent(emitter, depth) &&
               r_c17_write(emitter, "r_clone_result ") &&
               r_c17_emit_temporary_name(emitter, status) && r_c17_write(emitter, " = ") &&
               r_c17_emit_clone_glue_name(emitter, instruction->auxiliary_type) &&
               r_c17_write(emitter, "(\n") && r_c17_indent(emitter, depth + 1U) &&
               r_c17_write(emitter, "&") &&
               r_c17_emit_async_storage_reference(
                   emitter, mir, function, false, instruction->result) &&
               r_c17_write(emitter, ",\n") && r_c17_indent(emitter, depth + 1U) &&
               r_c17_emit_async_storage_reference(emitter, mir, function, false, source) &&
               r_c17_write(emitter, ",\n") && r_c17_indent(emitter, depth + 1U) &&
               r_c17_emit_span(emitter, instruction->span) && r_c17_write(emitter, ");\n") &&
               r_c17_indent(emitter, depth) && r_c17_write(emitter, "(void)") &&
               r_c17_emit_temporary_name(emitter, status) && r_c17_write(emitter, ";\n") &&
               r_c17_emit_async_mark_value_initialized(
                   emitter, mir, function, instruction->type, instruction->result, true, depth);
    if (r_c17_clone_is_copy(emitter, instruction->auxiliary_type))
        return r_c17_emit_async_zero_result(emitter, mir, function, instruction, depth) &&
               r_c17_indent(emitter, depth) &&
               r_c17_emit_async_storage_reference(
                   emitter, mir, function, false, instruction->result) &&
               r_c17_write(emitter, ".r_payload.r_ok = *") &&
               r_c17_emit_async_storage_reference(emitter, mir, function, false, source) &&
               r_c17_write(emitter, ";\n") && r_c17_indent(emitter, depth) &&
               r_c17_emit_async_storage_reference(
                   emitter, mir, function, false, instruction->result) &&
               r_c17_write(emitter, ".r_tag = UINT32_C(0);\n") &&
               r_c17_emit_async_mark_value_initialized(
                   emitter, mir, function, instruction->type, instruction->result, true, depth);
    return r_c17_emit_async_zero_result(emitter, mir, function, instruction, depth) &&
           r_c17_take_temporary(emitter, &status) && r_c17_indent(emitter, depth) &&
           r_c17_write(emitter, "r_clone_result ") && r_c17_emit_temporary_name(emitter, status) &&
           r_c17_write(emitter, " = ") &&
           r_c17_emit_clone_glue_name(emitter, instruction->auxiliary_type) &&
           r_c17_write(emitter, "(\n") && r_c17_indent(emitter, depth + 1U) &&
           r_c17_write(emitter, "&") &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) &&
           r_c17_write(emitter, ".r_payload.r_ok,\n") && r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, source) &&
           r_c17_write(emitter, ",\n") && r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_span(emitter, instruction->span) && r_c17_write(emitter, ");\n") &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "if (") &&
           r_c17_emit_temporary_name(emitter, status) &&
           r_c17_write(emitter, ".status == R_STD_ALLOC_CALL_SUCCESS) {\n") &&
           r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) &&
           r_c17_write(emitter, ".r_tag = UINT32_C(0);\n") && r_c17_indent(emitter, depth) &&
           r_c17_write(emitter, "} else {\n") && r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) &&
           r_c17_write(emitter, ".r_tag = UINT32_C(1);\n") && r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) &&
           r_c17_write(emitter, ".r_payload.r_error_00000001 = ") &&
           r_c17_emit_temporary_name(emitter, status) && r_c17_write(emitter, ".error;\n") &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "}\n") &&
           r_c17_emit_async_mark_value_initialized(
               emitter, mir, function, instruction->type, instruction->result, true, depth);
}
