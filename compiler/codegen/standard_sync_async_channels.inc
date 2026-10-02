static bool r_c17_emit_async_sync_channel_call(RC17Emitter *emitter,
                                                const RMirFunction *mir,
                                                const RSemanticSymbol *function,
                                                const RMirInstruction *instruction,
                                                uint32_t depth) {
    const bool bounded = instruction->standard_operation == R_STANDARD_CALL_SYNC_SYNC_CHANNEL;
    uint32_t native;
    if (!r_c17_take_temporary(emitter, &native) || !r_c17_indent(emitter, depth) ||
        !r_c17_write(emitter, bounded ? "RStdSyncSyncChannelCreateResult " : "RStdSyncChannelCreateResult ") ||
        !r_c17_emit_temporary_name(emitter, native) || !r_c17_write(emitter, " =\n") ||
        !r_c17_indent(emitter, depth + 1U) ||
        !r_c17_write(emitter, bounded ? "r_std_sync_sync_channel(" : "r_std_sync_channel(")) return false;
    const size_t indentation = r_c17_current_column(emitter);
    if (!r_c17_write(emitter, "r_runtime_hosted_allocator(),\n") ||
        !r_c17_emit_runtime_type_info_at(emitter, instruction->runtime_type, indentation)) return false;
    if (bounded && (!r_c17_write(emitter, ",\n") || !r_c17_spaces(emitter, indentation) ||
                    !r_c17_write(emitter, "(size_t)") ||
                    !r_c17_emit_async_storage_reference(emitter, mir, function, false,
                        r_c17_mir_operand(emitter, instruction, 0U)))) return false;
    if (!r_c17_write(emitter, ");\n") ||
        !r_c17_emit_container_contract_check(emitter, native,
            "R_STD_SYNC_CHANNEL_CALL_CONTRACT_VIOLATION", instruction->span, depth) ||
        !r_c17_indent(emitter, depth) || !r_c17_write(emitter, "if (") ||
        !r_c17_emit_temporary_name(emitter, native) ||
        !r_c17_write(emitter, ".status == R_STD_SYNC_CHANNEL_CALL_SUCCESS) {\n") ||
        !r_c17_indent(emitter, depth + 1U) ||
        !r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) ||
        !r_c17_write(emitter, ".r_tag = UINT32_C(0);\n") ||
        !r_c17_indent(emitter, depth + 1U) ||
        !r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) ||
        !r_c17_write(emitter, ".r_payload.r_ok = ") ||
        !r_c17_emit_temporary_name(emitter, native) || !r_c17_write(emitter, ".value;\n") ||
        !r_c17_indent(emitter, depth) || !r_c17_write(emitter, "} else {\n") ||
        !r_c17_indent(emitter, depth + 1U) ||
        !r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) ||
        !r_c17_write(emitter, ".r_tag = UINT32_C(1);\n") ||
        !r_c17_indent(emitter, depth + 1U) ||
        !r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) ||
        !r_c17_write(emitter, ".r_payload.r_error_00000001 = ") ||
        !r_c17_emit_temporary_name(emitter, native) || !r_c17_write(emitter, ".error;\n") ||
        !r_c17_indent(emitter, depth) || !r_c17_write(emitter, "}\n")) return false;
    return r_c17_emit_async_mark_value_initialized(emitter, mir, function, instruction->type,
                                                  instruction->result, true, depth);
}
