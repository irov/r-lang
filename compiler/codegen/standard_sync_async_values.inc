static bool r_c17_emit_async_sync_value_call(RC17Emitter *emitter,
                                              const RMirFunction *mir,
                                              const RSemanticSymbol *function,
                                              const RMirInstruction *instruction,
                                              uint32_t depth) {
    uint32_t arguments[2] = {0};
    uint32_t output;
    uint32_t index;
    const bool value_call = instruction->standard_operation >= R_STANDARD_CALL_SYNC_SEND &&
                            instruction->standard_operation <= R_STANDARD_CALL_SYNC_SET;
    for (index = 0U; index < instruction->operand_count; ++index) {
        const RMirValueId operand = r_c17_mir_operand(emitter, instruction, index);
        const RTypeId type = r_c17_mir_value_type(emitter, mir, operand);
        const bool moving = r_c17_type_requires_drop(emitter, type);
        if (!r_c17_new_temporary(emitter, type, depth, &arguments[index]) ||
            !r_c17_write(emitter, moving ? " = {0};\n" : " = ")) return false;
        if (moving) {
            if (!r_c17_indent(emitter, depth) || !r_c17_emit_type_move_name(emitter, type) ||
                !r_c17_write(emitter, "(&") || !r_c17_emit_temporary_name(emitter, arguments[index]) ||
                !r_c17_write(emitter, ", &") ||
                !r_c17_emit_async_storage_reference(emitter, mir, function, false, operand) ||
                !r_c17_write(emitter, ");\n")) return false;
            if (r_c17_type_requires_init_flag(emitter, type) &&
                (!r_c17_indent(emitter, depth) ||
                 !r_c17_emit_async_init_reference(emitter, "frame", false, operand) ||
                 !r_c17_write(emitter, " = 0;\n"))) return false;
        } else if (!r_c17_emit_async_storage_reference(emitter, mir, function, false, operand) ||
                   !r_c17_write(emitter, ";\n")) return false;
    }
    if (instruction->standard_operation >= R_STANDARD_CALL_SYNC_MUTEX_NEW &&
        instruction->standard_operation <= R_STANDARD_CALL_SYNC_WAIT) {
        if (!r_c17_emit_sync_lock_operation(emitter, instruction->standard_operation, instruction->type,
                instruction->auxiliary_type, arguments[0], arguments[1], depth, &output)) return false;
        if (output == 0U) return true;
    } else if (value_call) {
        if (!r_c17_emit_sync_value_operation(emitter, instruction->standard_operation,
                instruction->type, instruction->auxiliary_type, arguments[0], arguments[1], depth, &output)) return false;
    } else {
        const bool receiver = instruction->standard_operation == R_STANDARD_CALL_SYNC_RECEIVER ||
                              instruction->standard_operation == R_STANDARD_CALL_SYNC_SYNC_RECEIVER;
        const char *name = instruction->standard_operation == R_STANDARD_CALL_SYNC_SENDER ? "r_std_sync_sender"
                               : instruction->standard_operation == R_STANDARD_CALL_SYNC_SYNC_SENDER ? "r_std_sync_sync_sender"
                               : instruction->standard_operation == R_STANDARD_CALL_SYNC_CLONE_SENDER ? "r_std_sync_clone_sender"
                               : instruction->standard_operation == R_STANDARD_CALL_SYNC_CLONE_SYNC_SENDER ? "r_std_sync_clone_sync_sender"
                               : instruction->standard_operation == R_STANDARD_CALL_SYNC_SYNC_RECEIVER ? "r_std_sync_sync_receiver"
                                                                                                      : "r_std_sync_receiver";
        if (!r_c17_new_temporary(emitter, instruction->type, depth, &output) ||
            !r_c17_write(emitter, " = ") || !r_c17_write(emitter, name) ||
            !r_c17_write(emitter, receiver ? "(&" : "(") ||
            !r_c17_emit_temporary_name(emitter, arguments[0]) || !r_c17_write(emitter, ");\n")) return false;
    }
    if (!r_c17_indent(emitter, depth)) return false;
    if (r_c17_type_requires_drop(emitter, instruction->type)) {
        if (!r_c17_emit_type_move_name(emitter, instruction->type) || !r_c17_write(emitter, "(&") ||
            !r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) ||
            !r_c17_write(emitter, ", &") || !r_c17_emit_temporary_name(emitter, output) ||
            !r_c17_write(emitter, ");\n")) return false;
    } else if (!r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) ||
               !r_c17_write(emitter, " = ") || !r_c17_emit_temporary_name(emitter, output) ||
               !r_c17_write(emitter, ";\n")) return false;
    return !r_c17_type_requires_init_flag(emitter, instruction->type) ||
           (r_c17_indent(emitter, depth) &&
            r_c17_emit_async_init_reference(emitter, "frame", false, instruction->result) &&
            r_c17_write(emitter, " = 1;\n"));
}
