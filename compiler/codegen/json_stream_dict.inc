static bool r_c17_json_stream_dict(RC17Emitter *emitter, RSymbolId fn, RTypeId id, uint8_t *seen) {
    const RSemanticType *type = r_c17_type(emitter, id);
    if (!r_c17_json_stream_types(emitter, fn, type->second, seen) ||
        !r_c17_write(emitter, "typedef struct ") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") ||
        !r_c17_write(emitter,
                     " {\n    RStdJsonDecodeFrame base;\n    RStdString key;\n"
                     "    bool key_live;\n    bool item_live;\n    ") ||
        !r_c17_emit_value_type(emitter, type->second) || !r_c17_write(emitter, " item;\n} ") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") ||
        !r_c17_write(emitter, ";\nstatic RStdJsonDecodeStep ") ||
        !r_c17_json_stream_name(emitter, fn, id, "step") ||
        !r_c17_write(emitter, "(RStdJsonCursor *c, RStdJsonDecodeFrame *b) {\n    ") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") || !r_c17_write(emitter, " *f = (") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") ||
        !r_c17_write(emitter,
                     " *)b;\n    RRuntimeDict *out = b->output;\n"
                     "    if (b->state == 3U) {\n        f->item_live = true;\n"
                     "        if (!r_json_cursor_dict_insert(c, out, &f->key, &f->item)) {\n"
                     "            return R_STD_JSON_DECODE_ERROR;\n        }\n"
                     "        f->item_live = false;\n        f->key_live = false;\n        "
                     "b->state = 1U;\n    }\n"
                     "    if (!c->has_token) {\n        return R_STD_JSON_DECODE_WAIT;\n    }\n"
                     "    if (b->state == 0U) {\n"
                     "        if (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_OBJECT_BEGIN)) {\n"
                     "            return R_STD_JSON_DECODE_ERROR;\n        }\n") ||
        !r_c17_json_stream_info(emitter, type->second, 2U) ||
        !r_c17_write(
            emitter,
            "        r_json_cursor_dict_initialize(c, out, info);\n"
            "        b->state = 1U;\n        (void)r_json_cursor_next(c);\n"
            "        return R_STD_JSON_DECODE_PROGRESS;\n    }\n"
            "    if (b->state == 1U) {\n"
            "        if (c->token.kind == R_STD_JSON_TOKEN_OBJECT_END) {\n"
            "            (void)r_json_cursor_next(c);\n            return "
            "R_STD_JSON_DECODE_COMPLETE;\n        }\n"
            "        if (!r_json_cursor_expect(c, R_STD_JSON_TOKEN_KEY)) {\n"
            "            return R_STD_JSON_DECODE_ERROR;\n        }\n"
            "        f->key = r_json_scanner_take_text(&c->scanner);\n        f->key_live = true;\n"
            "        b->state = 2U;\n        (void)r_json_cursor_next(c);\n"
            "        return R_STD_JSON_DECODE_PROGRESS;\n    }\n"
            "    b->state = 3U;\n") ||
        !r_c17_json_stream_child(emitter, fn, type->second, "&f->item", "false") ||
        !r_c17_write(emitter, "}\nstatic void ") ||
        !r_c17_json_stream_name(emitter, fn, id, "drop") ||
        !r_c17_write(emitter, "(RStdJsonDecodeFrame *b, bool complete) {\n    ") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") || !r_c17_write(emitter, " *f = (") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") ||
        !r_c17_write(
            emitter,
            " *)b;\n    if (f->key_live) {\n        r_std_string_destroy(&f->key);\n    }\n"))
        return false;
    if (r_c17_type_requires_drop(emitter, type->second) &&
        (!r_c17_write(emitter, "    if (f->item_live) {\n        ") ||
         !r_c17_emit_type_drop_name(emitter, type->second) ||
         !r_c17_write(emitter, "(&f->item);\n    }\n")))
        return false;
    return r_c17_write(emitter,
                       "    if (!complete && b->state != 0U) {\n"
                       "        r_runtime_dict_destroy(b->output);\n    }\n}\n") &&
           r_c17_json_stream_create_body(emitter, fn, id, true);
}
