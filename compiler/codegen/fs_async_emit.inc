static bool r_c17_emit_fs_extended_native_call(RC17Emitter *emitter,
                                                const RMirFunction *mir,
                                                const RSemanticSymbol *function,
                                                const RMirInstruction *instruction,
                                                const RStandardFsAsyncDescriptor *descriptor,
                                                const RMirInstruction *move,
                                                uint32_t depth, uint32_t layout) {
    const bool break_assignment = layout == 1U || layout == 4U;
    const bool break_open = layout == 2U || layout == 5U;
    const bool break_arguments = layout == 3U || layout == 4U || layout == 5U;
    uint32_t index;
    size_t argument_indent;
    if (!r_c17_indent(emitter, depth) ||
        !r_c17_format(emitter, "RStdFsTaskStartResult r_fs_start_%08" PRIu32 " =", instruction->result) ||
        !r_c17_write(emitter, break_assignment ? "\n" : " ") ||
        (break_assignment && !r_c17_indent(emitter, depth + 1U)) ||
        !r_c17_format(emitter, "%s(", descriptor->native_name))
        return false;
    argument_indent = break_open ? (size_t)(depth + 1U) * 4U : r_c17_current_column(emitter);
    if (break_open && (!r_c17_write(emitter, "\n") || !r_c17_spaces(emitter, argument_indent)))
        return false;
    for (index = 0U; index < descriptor->argument_count; ++index) {
        if (index != 0U &&
            (!r_c17_write(emitter, break_arguments ? ",\n" : ", ") ||
             (break_arguments && !r_c17_spaces(emitter, argument_indent))))
            return false;
        if (index + 1U == descriptor->argument_count) {
            if (!r_c17_format(emitter, "r_fs_deadline_%08" PRIu32, instruction->result))
                return false;
        } else if (index == descriptor->move_index) {
            if (move == NULL || !r_c17_write(emitter, "&") ||
                !r_c17_emit_async_place(emitter, mir, function, move->place_ordinal,
                                        move->place_is_parameter, move->operand1))
                return false;
        } else if (!r_c17_emit_async_storage_reference(emitter, mir, function, false,
                                                        r_c17_mir_operand(emitter, instruction, index)))
            return false;
    }
    return r_c17_write(emitter, ");\n");
}

static bool r_c17_emit_fs_extended_native_declaration(RC17Emitter *emitter,
                                                       const RMirFunction *mir,
                                                       const RSemanticSymbol *function,
                                                       const RMirInstruction *instruction,
                                                       const RStandardFsAsyncDescriptor *descriptor,
                                                       const RMirInstruction *move,
                                                       uint32_t depth) {
    const size_t start = emitter->output.length;
    uint32_t layout;
    for (layout = 0U; layout < 6U; ++layout) {
        size_t column = 0U, index;
        bool fits = true;
        emitter->output.length = start;
        if (!r_c17_emit_fs_extended_native_call(emitter, mir, function, instruction,
                                               descriptor, move, depth, layout))
            return false;
        for (index = start; index < emitter->output.length; ++index) {
            if (emitter->output.bytes[index] == '\n') column = 0U;
            else if (++column > 100U) fits = false;
        }
        if (fits) return true;
    }
    return true;
}

static bool r_c17_emit_async_fs_extended(RC17Emitter *emitter,
                                         const RMirFunction *mir,
                                         const RSemanticSymbol *function,
                                         const RMirInstruction *instruction,
                                         uint32_t depth) {
    const RStandardFsAsyncDescriptor *descriptor =
        r_standard_fs_async_descriptor(instruction->standard_operation);
    const RMirValueId deadline = r_c17_mir_operand(emitter, instruction,
                                                   instruction->operand_count - 1U);
    const RMirInstruction *move = NULL;
    if (descriptor == NULL || deadline == R_MIR_VALUE_ID_INVALID)
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    if (descriptor->move_index != UINT32_MAX)
        move = r_c17_mir_value_definition(emitter, mir,
                                          r_c17_mir_operand(emitter, instruction, descriptor->move_index));
    if (!r_c17_indent(emitter, depth) ||
        !r_c17_format(
            emitter, "RStdFsDeadline r_fs_deadline_%08" PRIu32 " = {0};\n", instruction->result) ||
        !r_c17_indent(emitter, depth) || !r_c17_write(emitter, "if (") ||
        !r_c17_emit_async_storage_reference(emitter, mir, function, false, deadline) ||
        !r_c17_write(emitter, ".r_tag == UINT32_C(1)) {\n") || !r_c17_indent(emitter, depth + 1U) ||
        !r_c17_format(
            emitter, "r_fs_deadline_%08" PRIu32 ".has_value = 1;\n", instruction->result) ||
        !r_c17_indent(emitter, depth + 1U) ||
        !r_c17_format(emitter, "r_fs_deadline_%08" PRIu32 ".value = ", instruction->result) ||
        !r_c17_emit_async_storage_reference(emitter, mir, function, false, deadline) ||
        !r_c17_write(emitter, ".r_payload.r_some;\n") || !r_c17_indent(emitter, depth) ||
        !r_c17_write(emitter, "}\n") ||
        !r_c17_emit_deadline_narrow(emitter, depth, "r_fs_deadline_", instruction->result) ||
        !r_c17_emit_fs_extended_native_declaration(
            emitter, mir, function, instruction, descriptor, move, depth) ||
        !r_c17_indent(emitter, depth) ||
        !r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) ||
        !r_c17_write(emitter, " = (") || !r_c17_emit_value_type(emitter, instruction->type) ||
        !r_c17_write(emitter, "){0};\n") || !r_c17_indent(emitter, depth) ||
        !r_c17_format(emitter, "if (r_fs_start_%08" PRIu32 ".is_ok) {\n", instruction->result))
        return false;
    if (move != NULL &&
        (!r_c17_indent(emitter, depth + 1U) ||
         !r_c17_emit_async_object_init_reference(emitter, "frame",
                                                  move->place_is_parameter ? R_C17_ASYNC_STORAGE_PARAMETER
                                                                           : R_C17_ASYNC_STORAGE_LOCAL,
                                                  move->place_ordinal) ||
         !r_c17_write(emitter, " = 0;\n")))
        return false;
    return r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) &&
           r_c17_write(emitter, ".r_payload.r_ok = ") &&
           r_c17_format(emitter, "r_fs_start_%08" PRIu32 ".task;\n", instruction->result) &&
           r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) &&
           r_c17_write(emitter, ".r_tag = UINT32_C(0);\n") &&
           r_c17_indent(emitter, depth + 1U) &&
           r_c17_format(emitter, "r_fs_start_%08" PRIu32 ".task = NULL;\n", instruction->result) &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "} else {\n") &&
           r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) &&
           r_c17_write(emitter, ".r_payload.r_error_00000001 = ") &&
           r_c17_format(emitter, "r_fs_start_%08" PRIu32 ".error;\n", instruction->result) &&
           r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) &&
           r_c17_write(emitter, ".r_tag = UINT32_C(1);\n") &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "}\n") &&
           r_c17_indent(emitter, depth) &&
           r_c17_emit_async_init_reference(emitter, "frame", false, instruction->result) &&
           r_c17_write(emitter, " = 1;\n");
}
