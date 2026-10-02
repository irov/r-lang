static bool r_c17_emit_async_native_byte_view(RC17Emitter *emitter,
                                              const RMirFunction *mir,
                                              const RSemanticSymbol *function,
                                              const char *native_type,
                                              const char *name_prefix,
                                              RMirValueId source,
                                              RMirValueId result,
                                              uint32_t depth);

/* Emits the start of a scoped standard operation: the deadline and the caller's view are passed
   by value; the task publishes its outcome only after the backend acknowledged it. */
static bool r_c17_emit_async_scoped(RC17Emitter *emitter,
                                    const RMirFunction *mir,
                                    const RSemanticSymbol *function,
                                    const RMirInstruction *instruction,
                                    uint32_t depth) {
    const RStandardScopedOperationDescriptor *descriptor =
        r_standard_scoped_operation(instruction->standard_operation);
    const uint32_t id = instruction->result;
    uint32_t argument_count;
    uint32_t view_index;
    RMirValueId deadline;
    size_t call_start;
    uint32_t index;

    if (descriptor == NULL) {
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    argument_count = r_standard_scoped_argument_count(descriptor);
    view_index = r_standard_scoped_view_index(descriptor);
    deadline = r_c17_mir_operand(emitter, instruction, argument_count - 1U);
    if (!r_c17_indent(emitter, depth) ||
        !r_c17_format(
            emitter, "%s r_scoped_deadline_%08" PRIu32 " = {0};\n", descriptor->c_deadline, id) ||
        !r_c17_indent(emitter, depth) || !r_c17_write(emitter, "if (") ||
        !r_c17_emit_async_storage_reference(emitter, mir, function, false, deadline) ||
        !r_c17_write(emitter, ".r_tag == UINT32_C(1)) {\n") || !r_c17_indent(emitter, depth + 1U) ||
        !r_c17_format(emitter, "r_scoped_deadline_%08" PRIu32 ".has_value = 1;\n", id) ||
        !r_c17_indent(emitter, depth + 1U) ||
        !r_c17_format(emitter, "r_scoped_deadline_%08" PRIu32 ".value = ", id) ||
        !r_c17_emit_async_storage_reference(emitter, mir, function, false, deadline) ||
        !r_c17_write(emitter, ".r_payload.r_some;\n") || !r_c17_indent(emitter, depth) ||
        !r_c17_write(emitter, "}\n") ||
        !r_c17_emit_deadline_narrow(emitter, depth, "r_scoped_deadline_", id) ||
        !r_c17_emit_async_native_byte_view(emitter,
                                           mir,
                                           function,
                                           descriptor->c_view,
                                           "r_scoped_view",
                                           r_c17_mir_operand(emitter, instruction, view_index),
                                           id,
                                           depth)) {
        return false;
    }
    call_start = emitter->output.length;
    if (!r_c17_indent(emitter, depth) ||
        !r_c17_format(emitter, "%s r_scoped_start_%08" PRIu32 " = ", descriptor->c_start, id) ||
        !r_c17_format(emitter, "%s(", descriptor->native_name)) {
        return false;
    }
    for (index = 0U; index < argument_count; ++index) {
        if ((index != 0U) && !r_c17_write(emitter, ", ")) {
            return false;
        }
        if (index + 1U == argument_count) {
            if (!r_c17_format(emitter, "r_scoped_deadline_%08" PRIu32, id)) {
                return false;
            }
        } else if (index == view_index) {
            if (!r_c17_format(emitter, "r_scoped_view_%08" PRIu32, id)) {
                return false;
            }
        } else if (!r_c17_emit_async_storage_reference(
                       emitter,
                       mir,
                       function,
                       false,
                       r_c17_mir_operand(emitter, instruction, index))) {
            return false;
        }
    }
    return r_c17_write(emitter, ");\n") &&
           r_c17_reflow_assignment_call(emitter, call_start, depth) &&
           r_c17_indent(emitter, depth) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, id) &&
           r_c17_write(emitter, " = (") && r_c17_emit_value_type(emitter, instruction->type) &&
           r_c17_write(emitter, "){0};\n") && r_c17_indent(emitter, depth) &&
           r_c17_format(emitter, "if (r_scoped_start_%08" PRIu32 ".is_ok) {\n", id) &&
           r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, id) &&
           r_c17_write(emitter, ".r_payload.r_ok = ") &&
           r_c17_format(emitter, "r_scoped_start_%08" PRIu32 ".task;\n", id) &&
           r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, id) &&
           r_c17_write(emitter, ".r_tag = UINT32_C(0);\n") && r_c17_indent(emitter, depth + 1U) &&
           r_c17_format(emitter, "r_scoped_start_%08" PRIu32 ".task = NULL;\n", id) &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "} else {\n") &&
           r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, id) &&
           r_c17_write(emitter, ".r_payload.r_error_00000001 = ") &&
           r_c17_format(emitter, "r_scoped_start_%08" PRIu32 ".error;\n", id) &&
           r_c17_indent(emitter, depth + 1U) &&
           r_c17_emit_async_storage_reference(emitter, mir, function, false, id) &&
           r_c17_write(emitter, ".r_tag = UINT32_C(1);\n") && r_c17_indent(emitter, depth) &&
           r_c17_write(emitter, "}\n") && r_c17_indent(emitter, depth) &&
           r_c17_emit_async_init_reference(emitter, "frame", false, id) &&
           r_c17_write(emitter, " = 1;\n");
}
