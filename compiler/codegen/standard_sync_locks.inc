static bool r_c17_emit_sync_lock_operation(RC17Emitter *emitter,
                                           RStandardCallOperation operation,
                                           RTypeId type,
                                           RTypeId element,
                                           uint32_t first,
                                           uint32_t second,
                                           uint32_t depth,
                                           uint32_t *output) {
    const bool constructor = operation == R_STANDARD_CALL_SYNC_MUTEX_NEW ||
                             operation == R_STANDARD_CALL_SYNC_RWLOCK_NEW;
    const bool accessor = operation >= R_STANDARD_CALL_SYNC_MUTEX_GUARD_REF &&
                          operation <= R_STANDARD_CALL_SYNC_RW_WRITE_GUARD_MUT;
    const bool waiting = operation == R_STANDARD_CALL_SYNC_WAIT;
    const bool mutex = operation == R_STANDARD_CALL_SYNC_LOCK ||
                       operation == R_STANDARD_CALL_SYNC_TRY_LOCK || waiting;
    const bool reading = operation == R_STANDARD_CALL_SYNC_READ ||
                         operation == R_STANDARD_CALL_SYNC_TRY_READ;
    const bool trying = operation == R_STANDARD_CALL_SYNC_TRY_LOCK ||
                        operation == R_STANDARD_CALL_SYNC_TRY_READ ||
                        operation == R_STANDARD_CALL_SYNC_TRY_WRITE;
    const char *name = NULL;
#define R_SYNC_NAME(id, spelling) if (operation == R_STANDARD_CALL_SYNC_##id) name = #spelling;
    R_STANDARD_SYNC_LOCK_OPERATIONS(R_SYNC_NAME)
#undef R_SYNC_NAME
    if (name == NULL) return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    if (operation == R_STANDARD_CALL_SYNC_UNLOCK) {
        *output = 0U;
        return r_c17_indent(emitter, depth) && r_c17_write(emitter, "r_std_sync_unlock(&") &&
               r_c17_emit_temporary_name(emitter, first) && r_c17_write(emitter, ");\n");
    }
    if (!r_c17_new_temporary(emitter, type, depth, output)) return false;
    if (accessor) {
        return r_c17_write(emitter, " = ") && r_c17_write(emitter, name) &&
               r_c17_write(emitter, "(") && r_c17_emit_temporary_name(emitter, first) &&
               r_c17_write(emitter, ");\n");
    }
    if (!r_c17_write(emitter, " = {0};\n")) return false;
    if (constructor) {
        if (!r_c17_indent(emitter, depth) || !r_c17_write(emitter, name) ||
            !r_c17_write(emitter, "(&") || !r_c17_emit_temporary_name(emitter, *output) ||
            !r_c17_write(emitter, ".r_runtime,\n")) return false;
        const size_t indentation = (size_t)depth * 4U + strlen(name) + 1U;
        if (!r_c17_spaces(emitter, indentation) || !r_c17_write(emitter, "&") ||
            !r_c17_emit_temporary_name(emitter, *output) || !r_c17_write(emitter, ".r_value,\n") ||
            !r_c17_emit_runtime_type_info_at(emitter, element, indentation) ||
            !r_c17_write(emitter, ",\n") || !r_c17_spaces(emitter, indentation) ||
            !r_c17_write(emitter, "&") || !r_c17_emit_temporary_name(emitter, first) ||
            !r_c17_write(emitter, ");\n")) return false;
        return true;
    }
    const char *native_type = mutex ? (trying ? "RStdSyncTryLockResult" : "RStdSyncLockResult") :
                              reading ? (trying ? "RStdSyncTryReadLockResult" : "RStdSyncReadLockResult") :
                                        (trying ? "RStdSyncTryWriteLockResult" : "RStdSyncWriteLockResult");
    const char *guard_move = mutex ? "r_library_internal_sync_mutex_guard_move" :
                                  reading ? "r_library_internal_sync_rw_read_guard_move" :
                                            "r_library_internal_sync_rw_write_guard_move";
    uint32_t native;
    size_t move_argument_indent;
    if (!r_c17_take_temporary(emitter, &native) || !r_c17_indent(emitter, depth) ||
        !r_c17_format(emitter, "%s ", native_type) || !r_c17_emit_temporary_name(emitter, native)) return false;
    if (mutex) {
        if (!r_c17_write(emitter, " = ") || !r_c17_write(emitter, name) ||
            !r_c17_write(emitter, waiting ? "(" : "(&") ||
            !r_c17_emit_temporary_name(emitter, first) ||
            !r_c17_write(emitter, waiting ? ", &" : "->r_runtime") ||
            (waiting && !r_c17_emit_temporary_name(emitter, second)) ||
            !r_c17_write(emitter, ");\n")) return false;
    } else if (!r_c17_write(emitter, " = {0};\n") || !r_c17_indent(emitter, depth) ||
               !r_c17_write(emitter, name) || !r_c17_write(emitter, "(&") ||
               !r_c17_emit_temporary_name(emitter, native) || !r_c17_write(emitter, ", &") ||
               !r_c17_emit_temporary_name(emitter, first) ||
               !r_c17_write(emitter, "->r_runtime);\n")) return false;
    if (!r_c17_indent(emitter, depth) || !r_c17_emit_temporary_name(emitter, *output) ||
        !r_c17_write(emitter, ".r_tag = (uint32_t)") || !r_c17_emit_temporary_name(emitter, native) ||
        !r_c17_write(emitter, ".kind;\n") || !r_c17_indent(emitter, depth) ||
        !r_c17_write(emitter, "if (") || !r_c17_emit_temporary_name(emitter, *output) ||
        !r_c17_write(emitter, ".r_tag < UINT32_C(2)) {\n")) return false;
    if (!r_c17_indent(emitter, depth + 1U) || !r_c17_write(emitter, guard_move) ||
        !r_c17_write(emitter, "(")) return false;
    move_argument_indent = r_c17_current_column(emitter);
    if (!r_c17_write(emitter, "&") || !r_c17_emit_temporary_name(emitter, *output) ||
        !r_c17_write(emitter, ".r_payload.r_value,\n") ||
        !r_c17_spaces(emitter, move_argument_indent) || !r_c17_write(emitter, "&") ||
        !r_c17_emit_temporary_name(emitter, native) || !r_c17_write(emitter, ".guard);\n") ||
        !r_c17_indent(emitter, depth) || !r_c17_write(emitter, "}\n")) return false;
    return true;
}
