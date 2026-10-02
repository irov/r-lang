static bool r_c17_json_stream_hook(RC17Emitter *emitter,
                                   RSymbolId fn,
                                   RTypeId id,
                                   const RSemanticAggregate *aggregate) {
    if (!r_c17_write(emitter, "static bool ") || !r_c17_json_stream_name(emitter, fn, id, "hook") ||
        !r_c17_write(emitter, "(RStdJsonCursor *c, ") || !r_c17_emit_value_type(emitter, id) ||
        !r_c17_write(emitter, " *out, RStdJsonValue tree) {\n    (void)c;\n") ||
        !r_c17_json_hook_call(emitter, aggregate->json_unmarshal_function, true) ||
        !r_c17_write(emitter, "    r_std_json_value_destroy(&tree);\n    return true;\n}\n"))
        return false;
    if (!r_c17_write(emitter, "typedef struct ") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") ||
        !r_c17_write(emitter, " {\n    RStdJsonDecodeFrame base;\n    RStdJsonValue tree;\n} ") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") ||
        !r_c17_write(emitter, ";\nstatic RStdJsonDecodeStep ") ||
        !r_c17_json_stream_name(emitter, fn, id, "step") ||
        !r_c17_write(emitter, "(RStdJsonCursor *c, RStdJsonDecodeFrame *b) {\n    ") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") || !r_c17_write(emitter, " *f = (") ||
        !r_c17_json_stream_name(emitter, fn, id, "frame") ||
        !r_c17_write(emitter,
                     " *)b;\n    if (b->state == 0U) {\n        b->state = 1U;\n"
                     "        if (!r_json_decode_value_create(c, &f->tree, b->quoted)) {\n"
                     "            return R_STD_JSON_DECODE_ERROR;\n        }\n"
                     "        return R_STD_JSON_DECODE_PROGRESS;\n    }\n"
                     "    if (!") ||
        !r_c17_json_stream_name(emitter, fn, id, "hook") ||
        !r_c17_write(emitter,
                     "(c, b->output, f->tree)) {\n        return R_STD_JSON_DECODE_ERROR;\n    }\n"
                     "    return R_STD_JSON_DECODE_COMPLETE;\n}\n"))
        return false;
    return r_c17_json_stream_create_body(emitter, fn, id, false);
}
