/* Each closed converter has concrete frame fields and callbacks. The runtime only schedules
 * those continuations; ordinary scalar conversion and ownership glue remain shared. */
static bool
r_c17_json_stream_name(RC17Emitter *emitter, RSymbolId function, RTypeId type, const char *suffix) {
    return r_c17_format(
        emitter, "r_js_%u_%u_%s", emitter->function_ordinals[function], type, suffix);
}
static bool r_c17_json_stream_create_signature(RC17Emitter *emitter, RSymbolId fn, RTypeId id) {
    return r_c17_write(emitter, "static bool ") &&
           r_c17_json_stream_name(emitter, fn, id, "create") &&
           r_c17_write(emitter, "(RStdJsonCursor *c, void *out, bool q)");
}
static bool r_c17_json_stream_child(
    RC17Emitter *emitter, RSymbolId fn, RTypeId child, const char *place, const char *quoted) {
    return r_c17_write(emitter, "    if (!") &&
           r_c17_json_stream_name(emitter, fn, child, "create") &&
           r_c17_format(emitter,
                        "(c, %s, %s)) {\n        return R_STD_JSON_DECODE_ERROR;\n    }\n"
                        "    return R_STD_JSON_DECODE_PROGRESS;\n",
                        place,
                        quoted);
}
static bool
r_c17_json_named_info(RC17Emitter *emitter, RTypeId id, uint32_t depth, const char *name) {
    bool drop = r_c17_type_requires_drop(emitter, id);
    return r_c17_indent(emitter, depth) &&
           r_c17_format(emitter, "RRuntimeTypeInfo %s = {\n", name) &&
           r_c17_indent(emitter, depth + 1U) && r_c17_write(emitter, ".size = sizeof(") &&
           r_c17_emit_value_type(emitter, id) && r_c17_write(emitter, "),\n") &&
           r_c17_indent(emitter, depth + 1U) && r_c17_write(emitter, ".alignment = _Alignof(") &&
           r_c17_emit_value_type(emitter, id) && r_c17_write(emitter, "),\n") &&
           r_c17_indent(emitter, depth + 1U) && r_c17_write(emitter, ".move_initialize = ") &&
           (drop ? r_c17_emit_type_move_name(emitter, id) : r_c17_write(emitter, "NULL")) &&
           r_c17_write(emitter, ",\n") && r_c17_indent(emitter, depth + 1U) &&
           r_c17_write(emitter, ".drop = ") &&
           (drop ? r_c17_emit_type_drop_name(emitter, id) : r_c17_write(emitter, "NULL")) &&
           r_c17_write(emitter, ",\n") && r_c17_indent(emitter, depth) &&
           r_c17_write(emitter, "};\n");
}
static bool r_c17_json_stream_info(RC17Emitter *emitter, RTypeId id, uint32_t depth) {
    return r_c17_json_named_info(emitter, id, depth, "info");
}
static bool
r_c17_json_stream_create_body(RC17Emitter *emitter, RSymbolId fn, RTypeId id, bool cleanup) {
    if (!r_c17_json_stream_create_signature(emitter, fn, id) ||
        !r_c17_write(emitter, " {\n    size_t size = sizeof(") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") ||
        !r_c17_write(emitter, ");\n    size_t alignment = _Alignof(") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") ||
        !r_c17_write(emitter, ");\n    RStdJsonDecodeStepFn step = ") ||
        !r_c17_json_stream_name(emitter, fn, id, "step") ||
        !r_c17_write(emitter, ";\n    RStdJsonDecodeDropFn drop = ") ||
        !(cleanup ? r_c17_json_stream_name(emitter, fn, id, "drop") : r_c17_write(emitter, "NULL")))
        return false;
    return r_c17_write(
        emitter,
        ";\n    return r_json_decode_push(c, size, alignment, step, drop, out, q);\n}\n\n");
}
static bool r_c17_json_stream_sequence(RC17Emitter *emitter, RSymbolId fn, RTypeId id) {
    const RSemanticType *type = r_c17_type(emitter, id);
    bool fixed = type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY;
    bool list = type->kind == R_SEMANTIC_TYPE_LIST;
    if (!r_c17_write(
            emitter,
            "    if (b->state == 0U) {\n"
            "        if (!c->has_token) {\n            return R_STD_JSON_DECODE_WAIT;\n        }\n"
            "        if (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_ARRAY_BEGIN)) {\n"
            "            return R_STD_JSON_DECODE_ERROR;\n        }\n"))
        return false;
    if (!fixed &&
        (!r_c17_json_stream_info(emitter, type->base, 2U) ||
         !r_c17_write(emitter,
                      list ? "        r_runtime_list_initialize(out, c->allocator, info);\n"
                           : "        r_runtime_array_initialize(out, c->allocator, info);\n")))
        return false;
    if (!r_c17_write(emitter,
                     "        b->state = 1U;\n        (void)r_json_cursor_next(c);\n    }\n"
                     "    if (b->state == 2U) {\n"))
        return false;
    if (fixed) {
        if (!r_c17_write(emitter, "        ++f->count;\n"))
            return false;
    } else if (!r_c17_write(emitter, "        b->state = 3U;\n        if (!") ||
               !r_c17_write(emitter,
                            list ? "r_json_cursor_list_push" : "r_json_cursor_array_push") ||
               !r_c17_write(emitter,
                            "(c, out, &f->item)) {\n            return R_STD_JSON_DECODE_ERROR;\n  "
                            "      }\n"))
        return false;
    if (!r_c17_write(emitter,
                     "        b->state = 1U;\n    }\n"
                     "    if (!c->has_token) {\n        return R_STD_JSON_DECODE_WAIT;\n    }\n"
                     "    if (c->token.kind == R_STD_JSON_TOKEN_ARRAY_END) {\n"))
        return false;
    if (fixed && !r_c17_format(emitter,
                               "        if (f->count != %" PRIu64 "U) {\n"
                               "            (void)r_json_cursor_fail(c, R_STD_JSON_ERROR_TYPE);\n"
                               "            return R_STD_JSON_DECODE_ERROR;\n        }\n",
                               type->length))
        return false;
    if (!r_c17_write(emitter,
                     "        (void)r_json_cursor_next(c);\n        return "
                     "R_STD_JSON_DECODE_COMPLETE;\n    }\n"))
        return false;
    if (fixed && !r_c17_format(emitter,
                               "    if (f->count == %" PRIu64 "U) {\n"
                               "        (void)r_json_cursor_fail(c, R_STD_JSON_ERROR_TYPE);\n"
                               "        return R_STD_JSON_DECODE_ERROR;\n    }\n",
                               type->length))
        return false;
    return r_c17_write(emitter, "    b->state = 2U;\n") &&
           r_c17_json_stream_child(
               emitter, fn, type->base, fixed ? "&out->r_data[f->count]" : "&f->item", "false");
}
static bool r_c17_json_stream_types(RC17Emitter *emitter, RSymbolId fn, RTypeId id, uint8_t *seen);

#include "json_stream_dict.inc"
#include "json_stream_enum.inc"
#include "json_stream_hook.inc"
#include "json_stream_struct.inc"

static bool r_c17_json_stream_types(RC17Emitter *emitter, RSymbolId fn, RTypeId id, uint8_t *seen) {
    const RSemanticType *type = r_c17_type(emitter, r_c17_value_type(emitter, id));
    if ((seen[id] & 4U) != 0U)
        return true;
    seen[id] |= 4U;
    if (!r_c17_json_stream_create_signature(emitter, fn, id) || !r_c17_write(emitter, ";\n"))
        return false;
    if (r_c17_type_is_exact_named_standard(emitter, id, "std.json::value"))
        return r_c17_json_stream_create_signature(emitter, fn, id) &&
               r_c17_write(emitter, " {\n    return r_json_decode_value_create(c, out, q);\n}\n\n");
    const RSemanticAggregate *custom = r_c17_aggregate_for_type(emitter, id, NULL);
    if (custom != NULL && custom->json_unmarshal_function != 0U)
        return r_c17_json_stream_hook(emitter, fn, id, custom);
    if (type->kind == R_SEMANTIC_TYPE_ENUM)
        return r_c17_json_stream_enum(emitter, fn, id, seen);
    if (type->kind == R_SEMANTIC_TYPE_DICT)
        return r_c17_json_stream_dict(emitter, fn, id, seen);
    if (type->kind == R_SEMANTIC_TYPE_STRUCT)
        return r_c17_json_stream_struct(emitter, fn, id, seen);
    bool sequence = type->kind == R_SEMANTIC_TYPE_ARRAY || type->kind == R_SEMANTIC_TYPE_LIST ||
                    type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY;
    bool optional = type->kind == R_SEMANTIC_TYPE_OPTION;
    bool fixed = type->kind == R_SEMANTIC_TYPE_FIXED_ARRAY;
    if (sequence || optional) {
        if (!r_c17_json_stream_types(emitter, fn, type->base, seen))
            return false;
    } else if (type->kind == R_SEMANTIC_TYPE_STRUCT || type->kind == R_SEMANTIC_TYPE_ENUM ||
               type->kind == R_SEMANTIC_TYPE_DICT)
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    else if (!r_c17_json_decode_types(emitter, fn, id, seen))
        return false;
    if (!r_c17_write(emitter, "typedef struct ") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") ||
        !r_c17_write(emitter, " {\n    RStdJsonDecodeFrame base;\n"))
        return false;
    if (fixed) {
        if (!r_c17_write(emitter, "    size_t count;\n"))
            return false;
    } else if (sequence &&
               (!r_c17_write(emitter, "    ") || !r_c17_emit_value_type(emitter, type->base) ||
                !r_c17_write(emitter, " item;\n")))
        return false;
    if (!r_c17_write(emitter, "} ") || !r_c17_json_stream_name(emitter, fn, id, "frame") ||
        !r_c17_write(emitter, ";\nstatic RStdJsonDecodeStep ") ||
        !r_c17_json_stream_name(emitter, fn, id, "step") ||
        !r_c17_write(emitter, "(RStdJsonCursor *c, RStdJsonDecodeFrame *b) {\n    ") ||
        !r_c17_emit_value_type(emitter, id) || !r_c17_write(emitter, " *out = b->output;\n"))
        return false;
    if (sequence &&
        (!r_c17_write(emitter, "    ") || !r_c17_json_stream_name(emitter, fn, id, "frame") ||
         !r_c17_write(emitter, " *f = (") || !r_c17_json_stream_name(emitter, fn, id, "frame") ||
         !r_c17_write(emitter, " *)b;\n")))
        return false;
    if (sequence) {
        if (!r_c17_json_stream_sequence(emitter, fn, id))
            return false;
    } else if (optional) {
        if (!r_c17_write(emitter,
                         "    if (b->state == 1U) {\n        out->r_tag = UINT32_C(1);\n"
                         "        return R_STD_JSON_DECODE_COMPLETE;\n    }\n"
                         "    if (!c->has_token) {\n        return R_STD_JSON_DECODE_WAIT;\n    }\n"
                         "    if (c->token.kind == R_STD_JSON_TOKEN_NULL) {\n"
                         "        out->r_tag = UINT32_C(0);\n        (void)r_json_cursor_next(c);\n"
                         "        return R_STD_JSON_DECODE_COMPLETE;\n    }\n"
                         "    b->state = 1U;\n") ||
            !r_c17_json_stream_child(
                emitter, fn, type->base, "&out->r_payload.r_some", "b->quoted"))
            return false;
    } else if (!r_c17_write(
                   emitter,
                   "    if (!c->has_token) {\n        return R_STD_JSON_DECODE_WAIT;\n    }\n"
                   "    if (!") ||
               !r_c17_json_decode_name(emitter, fn, id) ||
               !r_c17_write(
                   emitter,
                   "(c, out, b->quoted, false)) {\n        return R_STD_JSON_DECODE_ERROR;\n    }\n"
                   "    return R_STD_JSON_DECODE_COMPLETE;\n"))
        return false;
    if (!r_c17_write(emitter, "}\n"))
        return false;
    if (sequence) {
        if (!r_c17_write(emitter, "static void ") ||
            !r_c17_json_stream_name(emitter, fn, id, "drop") ||
            !r_c17_write(emitter, "(RStdJsonDecodeFrame *b, bool complete) {\n    ") ||
            !r_c17_json_stream_name(emitter, fn, id, "frame") || !r_c17_write(emitter, " *f = (") ||
            !r_c17_json_stream_name(emitter, fn, id, "frame") ||
            !r_c17_write(emitter, " *)b;\n    (void)f;\n"))
            return false;
        if (fixed) {
            if (r_c17_type_requires_drop(emitter, type->base)) {
                if (!r_c17_write(emitter, "    ") || !r_c17_emit_value_type(emitter, id) ||
                    !r_c17_write(emitter,
                                 " *out = b->output;\n    if (!complete) {\n"
                                 "        while (f->count != 0U) {\n            --f->count;\n      "
                                 "      ") ||
                    !r_c17_emit_type_drop_name(emitter, type->base) ||
                    !r_c17_write(emitter, "(&out->r_data[f->count]);\n        }\n    }\n"))
                    return false;
            } else if (!r_c17_write(emitter, "    (void)complete;\n"))
                return false;
        } else {
            if (r_c17_type_requires_drop(emitter, type->base) &&
                (!r_c17_write(emitter, "    if (b->state == 3U) {\n        ") ||
                 !r_c17_emit_type_drop_name(emitter, type->base) ||
                 !r_c17_write(emitter, "(&f->item);\n    }\n")))
                return false;
            if (!r_c17_write(emitter, "    if (!complete && b->state != 0U) {\n        ") ||
                !r_c17_emit_type_drop_name(emitter, id) ||
                !r_c17_write(emitter, "(b->output);\n    }\n"))
                return false;
        }
        if (!r_c17_write(emitter, "}\n"))
            return false;
    }
    return r_c17_json_stream_create_body(emitter, fn, id, sequence);
}
static bool r_c17_json_stream_function(RC17Emitter *emitter, RSymbolId function_id) {
    const RSemanticSymbol *function = r_c17_symbol(emitter, function_id);
    const RHirNode *node = r_c17_node(emitter, emitter->function_nodes[function_id]);
    bool create = function->json_operation == R_JSON_OPERATION_NEW_DECODER;
    bool feed = function->json_operation == R_JSON_OPERATION_FEED;
    if (create) {
        uint8_t *seen = r_c17_allocate(emitter, emitter->frontend->semantic_type_count + 1U);
        if (seen == NULL)
            return false;
        (void)memset(seen, 0, emitter->frontend->semantic_type_count + 1U);
        bool success = r_c17_json_stream_types(emitter, function_id, function->json_type, seen);
        r_c17_free(emitter, seen);
        if (!success)
            return false;
    }
    if (!r_c17_emit_function_signature(emitter, function_id, true) ||
        !r_c17_write(emitter, " {\n    ") ||
        !r_c17_emit_value_type(emitter, function->return_type) ||
        !r_c17_write(emitter, " value;\n"))
        return false;
    if (create) {
        if (!r_c17_json_stream_info(emitter, function->json_type, 1U) ||
            !r_c17_write(emitter, "    RStdJsonOptions options = ") ||
            !(function->parameter_count == 0U ? r_c17_write(emitter, "R_STD_JSON_DEFAULT_OPTIONS")
                                              : r_c17_format_argument(emitter, node, 0U)) ||
            !r_c17_write(emitter, ";\n    RStdJsonDecodeCreateFn create = ") ||
            !r_c17_json_stream_name(emitter, function_id, function->json_type, "create") ||
            !r_c17_write(emitter,
                         ";\n    RStdJsonResult outcome =\n"
                         "        r_json_decoder_initialize(&value, r_runtime_hosted_allocator(), "
                         "options, info, create);\n"))
            return false;
    } else if (feed) {
        if (!r_c17_write(emitter, "    RStdJsonByteView input = {.data = ") ||
            !r_c17_format_argument(emitter, node, 1U) ||
            !r_c17_write(emitter, ".r_data,\n                              .length = ") ||
            !r_c17_format_argument(emitter, node, 1U) || !r_c17_write(emitter, ".r_len};\n") ||
            !r_c17_write(emitter, "    RStdJsonFeedResult fed = r_json_decoder_feed(") ||
            !r_c17_format_argument(emitter, node, 0U) || !r_c17_write(emitter, ", input, ") ||
            !r_c17_format_argument(emitter, node, 2U) ||
            !r_c17_write(emitter,
                         ");\n    RStdJsonResult outcome = fed.outcome;\n"
                         "    value = (RStdJsonProgress){fed.consumed, "
                         "(RStdJsonDecoderStatus)(fed.state - 1U)};\n"))
            return false;
    } else if (!r_c17_write(emitter, "    RStdJsonResult outcome = r_json_decoder_take(") ||
               !r_c17_format_argument(emitter, node, 0U) || !r_c17_write(emitter, ", &value);\n"))
        return false;
    return r_c17_write(
        emitter,
        "    if (outcome.status == R_STD_JSON_CALL_SUCCESS) {\n"
        "        r_effect_out->r_payload.r_ok = value;\n"
        "        r_effect_out->r_tag = UINT32_C(0);\n"
        "    } else if (outcome.status == R_STD_JSON_CALL_ALLOCATION_ERROR) {\n"
        "        r_effect_out->r_tag = UINT32_C(1);\n"
        "        r_effect_out->r_payload.r_error_00000001 = outcome.allocation_error;\n"
        "    } else {\n"
        "        r_effect_out->r_tag = UINT32_C(2);\n"
        "        r_effect_out->r_payload.r_error_00000002 = outcome.error;\n"
        "    }\n}\n\n");
}
