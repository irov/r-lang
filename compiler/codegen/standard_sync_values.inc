/* Values are already evaluated in language order. Native calls consume staged payloads. */
static bool r_c17_emit_sync_value_operation(RC17Emitter *emitter,
                                            RStandardCallOperation operation,
                                            RTypeId type,
                                            RTypeId element,
                                            uint32_t source,
                                            uint32_t value,
                                            uint32_t depth,
                                            uint32_t *output) {
    const bool getting = operation == R_STANDARD_CALL_SYNC_GET;
    const bool setting = operation == R_STANDARD_CALL_SYNC_SET;
    const bool receiving = operation == R_STANDARD_CALL_SYNC_RECV ||
                           operation == R_STANDARD_CALL_SYNC_TRY_RECV;
    const char *native_name = operation == R_STANDARD_CALL_SYNC_SEND ? "r_std_sync_send"
                                : operation == R_STANDARD_CALL_SYNC_SYNC_SEND ? "r_std_sync_sync_send"
                                : operation == R_STANDARD_CALL_SYNC_TRY_SEND ? "r_std_sync_try_send"
                                : operation == R_STANDARD_CALL_SYNC_RECV ? "r_std_sync_recv"
                                : operation == R_STANDARD_CALL_SYNC_TRY_RECV ? "r_std_sync_try_recv"
                                : setting ? "r_std_sync_set" : "r_std_sync_get";
    uint32_t native;
    size_t assignment_start;
    if (!r_c17_new_temporary(emitter, type, depth, output) ||
        !r_c17_write(emitter, " = {0};\n")) return false;
    assignment_start = emitter->output.length;
    if (!r_c17_take_temporary(emitter, &native) ||
        !r_c17_indent(emitter, depth) ||
        !r_c17_write(emitter, getting ? "const void *" : "uint32_t ") ||
        !r_c17_emit_temporary_name(emitter, native) ||
        !r_c17_write(emitter, getting ? " = " : " = (uint32_t)") ||
        !r_c17_write(emitter, native_name) || !r_c17_write(emitter, "(") ||
        ((getting || setting) && !r_c17_write(emitter, "&")) ||
        !r_c17_emit_temporary_name(emitter, source) ||
        ((getting || setting) && !r_c17_write(emitter, "->r_runtime"))) return false;
    if (!getting) {
        if (!r_c17_write(emitter, ", &") ||
            !r_c17_emit_temporary_name(emitter, receiving ? *output : value) ||
            (receiving && !r_c17_write(emitter, ".r_payload.r_value"))) return false;
    }
    if (!r_c17_write(emitter, getting || setting ? ");\n" : ").kind;\n") ||
        !r_c17_reflow_assignment_expression(emitter, assignment_start, depth) ||
        !r_c17_indent(emitter, depth) || !r_c17_emit_temporary_name(emitter, *output) ||
        !r_c17_write(emitter, ".r_tag = ") ||
        !r_c17_emit_temporary_name(emitter, native) ||
        !r_c17_write(emitter, getting ? " != NULL ? UINT32_C(1) : UINT32_C(0);\n" : ";\n")) return false;
    if (receiving) return true;
    if (!r_c17_indent(emitter, depth) || !r_c17_write(emitter, "if (") ||
        !r_c17_emit_temporary_name(emitter, native) ||
        !r_c17_write(emitter, getting ? " != NULL) {\n" : " != UINT32_C(0)) {\n") ||
        !r_c17_indent(emitter, depth + 1U)) return false;
    if (getting) {
        if (!r_c17_emit_temporary_name(emitter, *output) ||
            !r_c17_write(emitter, ".r_payload.r_some = ") ||
            !r_c17_emit_temporary_name(emitter, native) || !r_c17_write(emitter, ";\n")) return false;
    } else if (r_c17_type_requires_drop(emitter, element)) {
        if (!r_c17_emit_type_move_name(emitter, element) || !r_c17_write(emitter, "(&") ||
            !r_c17_emit_temporary_name(emitter, *output) ||
            !r_c17_write(emitter, ".r_payload.r_value, &") ||
            !r_c17_emit_temporary_name(emitter, value) || !r_c17_write(emitter, ");\n")) return false;
    } else if (!r_c17_emit_temporary_name(emitter, *output) ||
               !r_c17_write(emitter, ".r_payload.r_value = ") ||
               !r_c17_emit_temporary_name(emitter, value) || !r_c17_write(emitter, ";\n")) return false;
    return r_c17_indent(emitter, depth) && r_c17_write(emitter, "}\n");
}
