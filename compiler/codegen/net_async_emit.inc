static bool r_c17_emit_async_net(RC17Emitter *emitter,
                                         const RMirFunction *mir,
                                         const RSemanticSymbol *function,
                                         const RMirInstruction *instruction,
                                         uint32_t depth) {
    const RStandardNetOperationDescriptor *descriptor =
        r_standard_net_operation(instruction->standard_operation);
    const RMirValueId deadline = r_c17_mir_operand(emitter, instruction,
                                                   instruction->operand_count - 1U);
    const RMirInstruction *move = NULL;
    size_t call_start;
    uint32_t index;
    if (descriptor == NULL || deadline == R_MIR_VALUE_ID_INVALID)
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    if (descriptor->move_index != UINT32_MAX)
        move = r_c17_mir_value_definition(emitter, mir,
                                          r_c17_mir_operand(emitter, instruction, descriptor->move_index));
    /* The host of resolve and the path of a Unix-domain operation: a str first argument. */
    const bool first_is_str = strcmp(descriptor->argument_types[0], "str") == 0;
    uint32_t host_view = 0U;
    if (first_is_str &&
        (!r_c17_take_temporary(emitter, &host_view) ||
         !r_c17_emit_async_string_view(emitter, mir, function,
              r_c17_mir_operand(emitter, instruction, 0U), false, host_view, depth)))
        return false;
    if (!r_c17_indent(emitter, depth) ||
        !r_c17_format(emitter,
                      "RStdNetDeadline r_net_deadline_%08" PRIu32 " = {0};\n",
                      instruction->result) ||
        !r_c17_indent(emitter, depth) || !r_c17_write(emitter, "if (") ||
        !r_c17_emit_async_storage_reference(emitter, mir, function, false, deadline) ||
        !r_c17_write(emitter, ".r_tag == UINT32_C(1)) {\n") || !r_c17_indent(emitter, depth + 1U) ||
        !r_c17_format(
            emitter, "r_net_deadline_%08" PRIu32 ".has_value = 1;\n", instruction->result) ||
        !r_c17_indent(emitter, depth + 1U) ||
        !r_c17_format(emitter, "r_net_deadline_%08" PRIu32 ".value = ", instruction->result) ||
        !r_c17_emit_async_storage_reference(emitter, mir, function, false, deadline) ||
        !r_c17_write(emitter, ".r_payload.r_some;\n") || !r_c17_indent(emitter, depth) ||
        !r_c17_write(emitter, "}\n") ||
        !r_c17_emit_deadline_narrow(emitter, depth, "r_net_deadline_", instruction->result))
        return false;
    call_start = emitter->output.length;
    if (!r_c17_indent(emitter, depth) ||
        !r_c17_format(emitter, "RStdNetTaskStartResult r_net_start_%08" PRIu32 " = ",
                      instruction->result) ||
        !r_c17_format(emitter, "%s(", r_c17_standard_async_start_native_name(
                                           instruction->standard_operation)))
        return false;
    for (index = 0U; index < descriptor->argument_count; ++index) {
        if (index != 0U && !r_c17_write(emitter, ", "))
            return false;
        if (index + 1U == descriptor->argument_count) {
            if (!r_c17_format(emitter, "r_net_deadline_%08" PRIu32, instruction->result))
                return false;
        } else if (index == 0U && first_is_str) {
            if (!r_c17_format(emitter, "r_string_view_%08" PRIu32, host_view)) return false;
        } else if (index == descriptor->move_index) {
            if (move == NULL || !r_c17_write(emitter, "&") ||
                !r_c17_emit_async_place(emitter, mir, function, move->place_ordinal,
                                        move->place_is_parameter, move->operand1))
                return false;
        } else if (!r_c17_emit_async_storage_reference(emitter, mir, function, false,
                                                        r_c17_mir_operand(emitter, instruction, index)))
            return false;
    }
    if (!r_c17_write(emitter, ");\n") ||
        !r_c17_reflow_assignment_call(emitter, call_start, depth) ||
        !r_c17_indent(emitter, depth) ||
        !r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) ||
        !r_c17_write(emitter, " = (") || !r_c17_emit_value_type(emitter, instruction->type) ||
        !r_c17_write(emitter, "){0};\n") || !r_c17_indent(emitter, depth) ||
        !r_c17_format(emitter, "if (r_net_start_%08" PRIu32 ".is_ok) {\n", instruction->result))
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
           r_c17_format(emitter, "r_net_start_%08" PRIu32 ".task;\n", instruction->result) &&
           r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) &&
           r_c17_write(emitter, ".r_tag = UINT32_C(0);\n") &&
           r_c17_indent(emitter, depth + 1U) &&
           r_c17_format(emitter, "r_net_start_%08" PRIu32 ".task = NULL;\n", instruction->result) &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "} else {\n") &&
           r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) &&
           r_c17_write(emitter, ".r_payload.r_error_00000001 = ") &&
           r_c17_format(emitter, "r_net_start_%08" PRIu32 ".error;\n", instruction->result) &&
           r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) &&
           r_c17_write(emitter, ".r_tag = UINT32_C(1);\n") &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "}\n") &&
           r_c17_indent(emitter, depth) &&
           r_c17_emit_async_init_reference(emitter, "frame", false, instruction->result) &&
           r_c17_write(emitter, " = 1;\n");
}
