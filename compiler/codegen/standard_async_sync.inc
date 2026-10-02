/* R-SLIB-ASYNC-0013..0016 and R-LIB-0016 (L30): C17 for the asynchronous locks, semaphore,
 * notify, broadcast and channel reservations. One emitter works on operand temporaries; the HIR
 * path of synchronous functions and the MIR path of async frames feed it and store its result.
 * Task starts follow std.sync::receive: the library returns RStdAsyncStartResult and writes the
 * task value into storage that the passed type information describes. */

static const char *r_c17_async_sync_native_type(const RAsyncSyncDescriptor *descriptor) {
    switch (descriptor->shape) {
    case R_ASYNC_SYNC_CONSTRUCT_VALUE:
        return strcmp(descriptor->subject, "std.async::mutex") == 0 ? "RStdAsyncMutexNewResult"
                                                                     : "RStdAsyncRwLockNewResult";
    case R_ASYNC_SYNC_CONSTRUCT_COUNT:
        return "RStdAsyncSemaphoreNewResult";
    case R_ASYNC_SYNC_CONSTRUCT_EMPTY:
        return "RStdAsyncNotifyNewResult";
    case R_ASYNC_SYNC_CONSTRUCT_TYPED:
        return "RStdAsyncBroadcastNewResult";
    case R_ASYNC_SYNC_SUBSCRIBE:
        return "RStdAsyncSubscribeResult";
    case R_ASYNC_SYNC_PUBLISH:
        return "RStdAsyncPublishResult";
    default:
        return NULL;
    }
}

/* R-SLIB-ASYNC-0016: a broadcast clones its element through an adapter over the clone glue of
 * R-OWN-0020; a Copy element is copied by the library and needs none. Bit 2 of clone_glue_types
 * marks the adapters that the clone glue section emits. */
static bool r_c17_clone_is_copy(const RC17Emitter *emitter, RTypeId type_id);
static bool r_c17_require_clone_glue(RC17Emitter *emitter, RTypeId type_id, uint32_t depth);

static bool r_c17_require_broadcast_clone(RC17Emitter *emitter, RTypeId element) {
    const RTypeId value = r_c17_value_type(emitter, element);
    if (r_c17_clone_is_copy(emitter, value)) {
        return true;
    }
    if (!r_c17_require_clone_glue(emitter, value, 0U)) {
        return false;
    }
    emitter->clone_glue_types[value] |= 2U;
    emitter->uses_clone_glue = true;
    return true;
}

static bool r_c17_emit_async_broadcast_clone_name(RC17Emitter *emitter, RTypeId element) {
    const RTypeId value = r_c17_value_type(emitter, element);
    return r_c17_clone_is_copy(emitter, value)
               ? r_c17_write(emitter, "NULL")
               : r_c17_format(emitter, "r_broadcast_clone_%08" PRIu32, value);
}

/* The guard kind that std.async::unlock passes for a guard of `type`. */
static const char *r_c17_async_sync_guard_kind(RC17Emitter *emitter, RTypeId type) {
    const RSemanticType *guard = r_c17_type(emitter, r_c17_value_type(emitter, type));
    if (r_c17_standard_type_named(emitter, guard, "std.async::mutex_guard"))
        return "R_STD_ASYNC_GUARD_MUTEX";
    if (r_c17_standard_type_named(emitter, guard, "std.async::rw_read_guard"))
        return "R_STD_ASYNC_GUARD_RW_READ";
    if (r_c17_standard_type_named(emitter, guard, "std.async::rw_write_guard"))
        return "R_STD_ASYNC_GUARD_RW_WRITE";
    return NULL;
}

/* Emits one synchronous resource operation on operand temporaries. An allocating operation
 * leaves its native result in *native and no value; any other leaves its value in *output, or
 * no temporary for a void result. */
static bool r_c17_emit_async_sync_operation(RC17Emitter *emitter,
                                            const RAsyncSyncDescriptor *descriptor,
                                            RTypeId type,
                                            RTypeId element,
                                            RTypeId first_type,
                                            uint32_t first,
                                            uint32_t second,
                                            uint32_t depth,
                                            uint32_t *output,
                                            uint32_t *native) {
    const char *native_type = r_c17_async_sync_native_type(descriptor);
    size_t indentation;

    *output = 0U;
    *native = 0U;
    if (strcmp(descriptor->module, "test") == 0) {
        emitter->uses_std_test = true;
    } else {
        emitter->uses_async = true;
    }
    if (native_type != NULL) {
        if (!r_c17_take_temporary(emitter, native) || !r_c17_indent(emitter, depth) ||
            !r_c17_format(emitter, "%s ", native_type) ||
            !r_c17_emit_temporary_name(emitter, *native) || !r_c17_write(emitter, " =\n") ||
            !r_c17_indent(emitter, depth + 1U) || !r_c17_write(emitter, descriptor->native) ||
            !r_c17_write(emitter, "(")) {
            return false;
        }
        indentation = r_c17_current_column(emitter);
        switch (descriptor->shape) {
        case R_ASYNC_SYNC_CONSTRUCT_VALUE:
            return r_c17_write(emitter, "r_runtime_hosted_allocator(),\n") &&
                   r_c17_emit_runtime_type_info_at(emitter, element, indentation) &&
                   r_c17_write(emitter, ",\n") && r_c17_spaces(emitter, indentation) &&
                   r_c17_write(emitter, "&") && r_c17_emit_temporary_name(emitter, first) &&
                   r_c17_write(emitter, ");\n");
        case R_ASYNC_SYNC_CONSTRUCT_COUNT:
            return r_c17_write(emitter, "r_runtime_hosted_allocator(), (size_t)") &&
                   r_c17_emit_temporary_name(emitter, first) && r_c17_write(emitter, ");\n");
        case R_ASYNC_SYNC_CONSTRUCT_EMPTY:
            return r_c17_write(emitter, "r_runtime_hosted_allocator());\n");
        case R_ASYNC_SYNC_CONSTRUCT_TYPED:
            return r_c17_write(emitter, "r_runtime_hosted_allocator(),\n") &&
                   r_c17_emit_runtime_type_info_at(emitter, element, indentation) &&
                   r_c17_write(emitter, ",\n") && r_c17_spaces(emitter, indentation) &&
                   r_c17_emit_async_broadcast_clone_name(emitter, element) &&
                   r_c17_write(emitter, ",\n") && r_c17_spaces(emitter, indentation) &&
                   r_c17_write(emitter, "(size_t)") && r_c17_emit_temporary_name(emitter, first) &&
                   r_c17_write(emitter, ");\n");
        case R_ASYNC_SYNC_SUBSCRIBE:
            return r_c17_emit_temporary_name(emitter, first) && r_c17_write(emitter, ");\n");
        case R_ASYNC_SYNC_PUBLISH:
            return r_c17_emit_temporary_name(emitter, first) && r_c17_write(emitter, ", &") &&
                   r_c17_emit_temporary_name(emitter, second) && r_c17_write(emitter, ");\n");
        default:
            return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
    }
    switch (descriptor->shape) {
    case R_ASYNC_SYNC_QUERY_U64:
        return r_c17_new_temporary(emitter, type, depth, output) && r_c17_write(emitter, " = ") &&
               r_c17_write(emitter, descriptor->native) && r_c17_write(emitter, "();\n");
    case R_ASYNC_SYNC_SET_U64:
        return r_c17_indent(emitter, depth) && r_c17_write(emitter, descriptor->native) &&
               r_c17_write(emitter, "((uint64_t)") && r_c17_emit_temporary_name(emitter, first) &&
               r_c17_write(emitter, ");\n");
    case R_ASYNC_SYNC_CLONE:
    case R_ASYNC_SYNC_HANDLE_USIZE:
        return r_c17_new_temporary(emitter, type, depth, output) && r_c17_write(emitter, " = ") &&
               r_c17_write(emitter, descriptor->native) && r_c17_write(emitter, "(") &&
               r_c17_emit_temporary_name(emitter, first) && r_c17_write(emitter, ");\n");
    case R_ASYNC_SYNC_ACCESS_REF:
    case R_ASYNC_SYNC_ACCESS_MUT:
        return r_c17_new_temporary(emitter, type, depth, output) && r_c17_write(emitter, " = (") &&
               r_c17_emit_value_type(emitter, type) && r_c17_write(emitter, ")") &&
               r_c17_write(emitter, descriptor->native) && r_c17_write(emitter, "(") &&
               r_c17_emit_temporary_name(emitter, first) && r_c17_write(emitter, ");\n");
    case R_ASYNC_SYNC_TRY:
        return r_c17_new_temporary(emitter, type, depth, output) &&
               r_c17_write(emitter, " = {0};\n") && r_c17_indent(emitter, depth) &&
               r_c17_write(emitter, "if (") && r_c17_write(emitter, descriptor->native) &&
               r_c17_write(emitter, "(") && r_c17_emit_temporary_name(emitter, first) &&
               r_c17_write(emitter, ", &") && r_c17_emit_temporary_name(emitter, *output) &&
               r_c17_write(emitter, ".r_payload.r_some)) {\n") &&
               r_c17_indent(emitter, depth + 1U) && r_c17_emit_temporary_name(emitter, *output) &&
               r_c17_write(emitter, ".r_tag = UINT32_C(1);\n") && r_c17_indent(emitter, depth) &&
               r_c17_write(emitter, "}\n");
    case R_ASYNC_SYNC_CONSUME: {
        const char *kind = descriptor->subject == NULL
                               ? r_c17_async_sync_guard_kind(emitter, first_type)
                               : NULL;
        if ((descriptor->subject == NULL) && (kind == NULL)) {
            return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        }
        return r_c17_indent(emitter, depth) && r_c17_write(emitter, descriptor->native) &&
               r_c17_write(emitter, "(&") && r_c17_emit_temporary_name(emitter, first) &&
               (kind == NULL || (r_c17_write(emitter, ", ") && r_c17_write(emitter, kind))) &&
               r_c17_write(emitter, ");\n");
    }
    case R_ASYNC_SYNC_HANDLE_VOID:
        return r_c17_indent(emitter, depth) && r_c17_write(emitter, descriptor->native) &&
               r_c17_write(emitter, "(") && r_c17_emit_temporary_name(emitter, first) &&
               r_c17_write(emitter, ");\n");
    case R_ASYNC_SYNC_HANDLE_COUNT:
        return r_c17_indent(emitter, depth) && r_c17_write(emitter, descriptor->native) &&
               r_c17_write(emitter, "(") && r_c17_emit_temporary_name(emitter, first) &&
               r_c17_write(emitter, ", (size_t)") && r_c17_emit_temporary_name(emitter, second) &&
               r_c17_write(emitter, ");\n");
    case R_ASYNC_SYNC_SEND_PERMIT:
        emitter->uses_std_sync = true;
        return r_c17_indent(emitter, depth) && r_c17_write(emitter, descriptor->native) &&
               r_c17_write(emitter, "(&") && r_c17_emit_temporary_name(emitter, first) &&
               r_c17_write(emitter, ", &") && r_c17_emit_temporary_name(emitter, second) &&
               r_c17_write(emitter, ");\n");
    case R_ASYNC_SYNC_TRY_OUTCOME: {
        uint32_t reserved;
        emitter->uses_std_sync = true;
        return r_c17_take_temporary(emitter, &reserved) && r_c17_indent(emitter, depth) &&
               r_c17_write(emitter, "RStdSyncTryReserveResult ") &&
               r_c17_emit_temporary_name(emitter, reserved) && r_c17_write(emitter, " = ") &&
               r_c17_write(emitter, descriptor->native) && r_c17_write(emitter, "(") &&
               r_c17_emit_temporary_name(emitter, first) && r_c17_write(emitter, ");\n") &&
               r_c17_new_temporary(emitter, type, depth, output) &&
               r_c17_write(emitter, " = {0};\n") && r_c17_indent(emitter, depth) &&
               r_c17_emit_temporary_name(emitter, *output) &&
               r_c17_write(emitter, ".r_tag = (uint32_t)") &&
               r_c17_emit_temporary_name(emitter, reserved) && r_c17_write(emitter, ".kind;\n") &&
               r_c17_indent(emitter, depth) && r_c17_write(emitter, "if (") &&
               r_c17_emit_temporary_name(emitter, reserved) &&
               r_c17_write(emitter, ".kind == R_STD_SYNC_TRY_RESERVE_RESULT_RESERVED) {\n") &&
               r_c17_indent(emitter, depth + 1U) &&
               r_c17_write(emitter, "r_std_sync_permit_move_initialize(&") &&
               r_c17_emit_temporary_name(emitter, *output) &&
               r_c17_write(emitter, ".r_payload.r_value, &") &&
               r_c17_emit_temporary_name(emitter, reserved) && r_c17_write(emitter, ".permit);\n") &&
               r_c17_indent(emitter, depth) && r_c17_write(emitter, "}\n");
    }
    default:
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
}

/* The layout argument of a start whose task value is an outcome the library fills in. */
static bool r_c17_emit_async_sync_layout(RC17Emitter *emitter,
                                         const RAsyncSyncDescriptor *descriptor,
                                         RTypeId logical,
                                         uint32_t depth) {
    const size_t indentation = ((size_t)depth + 3U) * 4U;
    const bool broadcast = descriptor->operation == R_STANDARD_CALL_ASYNC_BROADCAST_RECEIVE;

    if (!r_c17_indent(emitter, depth + 2U) ||
        !r_c17_write(emitter,
                     broadcast ? "(RStdAsyncBroadcastReceiveLayout){\n"
                               : "(RStdSyncReserveLayout){\n") ||
        !r_c17_spaces(emitter, indentation) || !r_c17_write(emitter, ".result =\n") ||
        !r_c17_emit_runtime_type_info_at(emitter, logical, indentation + 4U) ||
        !r_c17_write(emitter, ",\n") || !r_c17_spaces(emitter, indentation) ||
        !r_c17_write(emitter, ".tag_offset = offsetof(") ||
        !r_c17_emit_value_type(emitter, logical) || !r_c17_write(emitter, ", r_tag),\n") ||
        !r_c17_spaces(emitter, indentation) || !r_c17_write(emitter, ".value_offset = offsetof(") ||
        !r_c17_emit_value_type(emitter, logical) ||
        !r_c17_write(emitter, broadcast ? ", r_payload.r_received),\n" : ", r_payload.r_value),\n")) {
        return false;
    }
    if (broadcast &&
        (!r_c17_spaces(emitter, indentation) || !r_c17_write(emitter, ".lag_offset = offsetof(") ||
         !r_c17_emit_value_type(emitter, logical) ||
         !r_c17_write(emitter, ", r_payload.r_lagged),\n"))) {
        return false;
    }
    return r_c17_indent(emitter, depth + 2U) && r_c17_write(emitter, "}");
}

/* Emits `RStdAsyncStartResult native = start(first, ...);` for a task start. */
static bool r_c17_emit_async_sync_start_native(RC17Emitter *emitter,
                                               const RAsyncSyncDescriptor *descriptor,
                                               RTypeId task_type,
                                               uint32_t first,
                                               uint32_t native,
                                               uint32_t depth) {
    const RSemanticType *task = r_c17_type(emitter, r_c17_value_type(emitter, task_type));
    const RTypeId logical = task == NULL ? R_TYPE_ID_INVALID : task->base;

    if (logical == R_TYPE_ID_INVALID) {
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    emitter->uses_async = true;
    if (descriptor->operation == R_STANDARD_CALL_SYNC_RESERVE) {
        emitter->uses_std_sync = true;
    }
    if (!r_c17_indent(emitter, depth) || !r_c17_write(emitter, "RStdAsyncStartResult ") ||
        !r_c17_emit_temporary_name(emitter, native) || !r_c17_write(emitter, " =\n") ||
        !r_c17_indent(emitter, depth + 1U) || !r_c17_write(emitter, descriptor->native) ||
        !r_c17_write(emitter, "(") || !r_c17_emit_temporary_name(emitter, first)) {
        return false;
    }
    switch (descriptor->shape) {
    case R_ASYNC_SYNC_START_VOID:
        return r_c17_write(emitter, ");\n");
    case R_ASYNC_SYNC_START: {
        const size_t indentation = ((size_t)depth + 2U) * 4U;
        return r_c17_write(emitter, ",\n") &&
               r_c17_emit_runtime_type_info_at(emitter, logical, indentation) &&
               r_c17_write(emitter, ");\n");
    }
    case R_ASYNC_SYNC_START_OUTCOME:
        return r_c17_write(emitter, ",\n") &&
               r_c17_emit_async_sync_layout(emitter, descriptor, logical, depth) &&
               r_c17_write(emitter, ");\n");
    default:
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
}

/* HIR path: a synchronous function calls the operation or starts its task. */
static bool r_c17_emit_async_sync_hir(RC17Emitter *emitter,
                                      const RHirNode *node,
                                      uint32_t depth,
                                      RC17Value *result) {
    const RAsyncSyncDescriptor *descriptor = r_async_sync_descriptor(node->standard_operation);
    RC17Value arguments[2];
    RTypeId first_type = R_TYPE_ID_INVALID;
    uint32_t output = 0U;
    uint32_t native = 0U;

    (void)memset(arguments, 0, sizeof(arguments));
    if ((descriptor == NULL) || (node->child_count != r_async_sync_operand_count(descriptor))) {
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    for (uint32_t index = 0U; index < node->child_count; ++index) {
        const RHirNode *child = r_c17_node(emitter, r_c17_child(emitter, node, index));
        if ((child == NULL) ||
            !r_c17_emit_expression(
                emitter, r_c17_child(emitter, node, index), depth, &arguments[index]) ||
            !arguments[index].has_value) {
            return false;
        }
        if (index == 0U) {
            first_type = child->type;
        }
    }
    if (r_async_sync_starts(descriptor)) {
        if (!r_c17_take_temporary(emitter, &native)) {
            return false;
        }
        return r_c17_emit_async_sync_start_native(
                   emitter, descriptor, node->type, arguments[0].temporary, native, depth) &&
               r_c17_emit_standard_async_start_result(emitter, node, native, depth, result);
    }
    if (!r_c17_emit_async_sync_operation(emitter,
                                         descriptor,
                                         node->type,
                                         node->runtime_type,
                                         first_type,
                                         arguments[0].temporary,
                                         arguments[1].temporary,
                                         depth,
                                         &output,
                                         &native)) {
        return false;
    }
    if (native != 0U) {
        if (!r_c17_emit_alloc_failure_transfer(
                emitter, node, native, "R_STD_ASYNC_CALL_SUCCESS", depth) ||
            !r_c17_new_temporary(emitter, node->type, depth, &output) ||
            !r_c17_write(emitter, " = ") || !r_c17_emit_temporary_name(emitter, native) ||
            !r_c17_write(emitter,
                         descriptor->shape == R_ASYNC_SYNC_PUBLISH ? ".count;\n" : ".value;\n")) {
            return false;
        }
    }
    result->type = r_c17_value_type(emitter, node->type);
    result->temporary = output;
    result->has_value = output != 0U;
    return true;
}

/* HIR preflight: the operand expressions, the result and the runtime type information. */
static bool r_c17_preflight_async_sync_hir(RC17Emitter *emitter,
                                          const RHirNode *node,
                                          RSymbolId function_symbol,
                                          size_t depth) {
    const RAsyncSyncDescriptor *descriptor = r_async_sync_descriptor(node->standard_operation);

    if ((descriptor == NULL) || (node->child_count != r_async_sync_operand_count(descriptor))) {
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    }
    for (uint32_t index = 0U; index < node->child_count; ++index) {
        if (!r_c17_preflight_expression(
                emitter, r_c17_child(emitter, node, index), function_symbol, depth + 1U)) {
            return false;
        }
    }
    if (strcmp(descriptor->module, "test") == 0) {
        emitter->uses_std_test = true;
    } else {
        emitter->uses_async = true;
    }
    if (strcmp(descriptor->module, "sync") == 0) {
        emitter->uses_std_sync = true;
    }
    if (!r_c17_preflight_type_dependency(emitter, node->type, depth + 1U) ||
        !r_c17_preflight_type_dependency(emitter, node->auxiliary_type, depth + 1U)) {
        return false;
    }
    if (r_async_sync_starts(descriptor)) {
        const RSemanticType *task = r_c17_type(emitter, r_c17_value_type(emitter, node->type));
        return (task != NULL) && (task->kind == R_SEMANTIC_TYPE_TASK) &&
               r_c17_preflight_sync_runtime_type(emitter, task->base);
    }
    if ((descriptor->shape == R_ASYNC_SYNC_CONSTRUCT_VALUE) ||
        (descriptor->shape == R_ASYNC_SYNC_CONSTRUCT_TYPED)) {
        return r_c17_preflight_sync_runtime_type(emitter, node->runtime_type) &&
               ((descriptor->shape != R_ASYNC_SYNC_CONSTRUCT_TYPED) ||
                r_c17_require_broadcast_clone(emitter, node->runtime_type));
    }
    return true;
}

/* MIR path: the operands live in the async frame. Each is copied, or moved when it owns, into a
 * temporary; the result is stored back, as an effect carrier for an allocating call. */
static bool r_c17_emit_async_sync_mir(RC17Emitter *emitter,
                                      const RMirFunction *mir,
                                      const RSemanticSymbol *function,
                                      const RMirInstruction *instruction,
                                      uint32_t depth) {
    const RAsyncSyncDescriptor *descriptor =
        r_async_sync_descriptor(instruction->standard_operation);
    uint32_t arguments[2] = {0U, 0U};
    RTypeId first_type = R_TYPE_ID_INVALID;
    uint32_t output = 0U;
    uint32_t native = 0U;

    if ((descriptor == NULL) ||
        (instruction->operand_count != r_async_sync_operand_count(descriptor))) {
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    }
    for (uint32_t index = 0U; index < instruction->operand_count; ++index) {
        const RMirValueId operand = r_c17_mir_operand(emitter, instruction, index);
        const RTypeId type = r_c17_mir_value_type(emitter, mir, operand);
        const bool moving = r_c17_type_requires_drop(emitter, type);
        if (index == 0U) {
            first_type = type;
        }
        if (!r_c17_new_temporary(emitter, type, depth, &arguments[index]) ||
            !r_c17_write(emitter, moving ? " = {0};\n" : " = ")) {
            return false;
        }
        if (moving) {
            if (!r_c17_indent(emitter, depth) || !r_c17_emit_type_move_name(emitter, type) ||
                !r_c17_write(emitter, "(&") ||
                !r_c17_emit_temporary_name(emitter, arguments[index]) ||
                !r_c17_write(emitter, ", &") ||
                !r_c17_emit_async_storage_reference(emitter, mir, function, false, operand) ||
                !r_c17_write(emitter, ");\n")) {
                return false;
            }
            if (r_c17_type_requires_init_flag(emitter, type) &&
                (!r_c17_indent(emitter, depth) ||
                 !r_c17_emit_async_init_reference(emitter, "frame", false, operand) ||
                 !r_c17_write(emitter, " = 0;\n"))) {
                return false;
            }
        } else if (!r_c17_emit_async_storage_reference(emitter, mir, function, false, operand) ||
                   !r_c17_write(emitter, ";\n")) {
            return false;
        }
    }
    if (r_async_sync_starts(descriptor)) {
        if (!r_c17_take_temporary(emitter, &native) ||
            !r_c17_emit_async_sync_start_native(
                emitter, descriptor, instruction->auxiliary_type, arguments[0], native, depth) ||
            !r_c17_indent(emitter, depth) ||
            !r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) ||
            !r_c17_write(emitter, " = (") || !r_c17_emit_value_type(emitter, instruction->type) ||
            !r_c17_write(emitter, "){0};\n") || !r_c17_indent(emitter, depth) ||
            !r_c17_write(emitter, "if (") || !r_c17_emit_temporary_name(emitter, native) ||
            !r_c17_write(emitter, ".is_ok) {\n") || !r_c17_indent(emitter, depth + 1U) ||
            !r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) ||
            !r_c17_write(emitter, ".r_payload.r_ok = ") ||
            !r_c17_emit_temporary_name(emitter, native) || !r_c17_write(emitter, ".task;\n") ||
            !r_c17_indent(emitter, depth + 1U) ||
            !r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) ||
            !r_c17_write(emitter, ".r_tag = UINT32_C(0);\n") || !r_c17_indent(emitter, depth) ||
            !r_c17_write(emitter, "} else {\n") || !r_c17_indent(emitter, depth + 1U) ||
            !r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) ||
            !r_c17_write(emitter, ".r_payload.r_error_00000001 = ") ||
            !r_c17_emit_temporary_name(emitter, native) || !r_c17_write(emitter, ".error;\n") ||
            !r_c17_indent(emitter, depth + 1U) ||
            !r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) ||
            !r_c17_write(emitter, ".r_tag = UINT32_C(1);\n") || !r_c17_indent(emitter, depth) ||
            !r_c17_write(emitter, "}\n") || !r_c17_indent(emitter, depth) ||
            !r_c17_emit_async_init_reference(emitter, "frame", false, instruction->result) ||
            !r_c17_write(emitter, " = 1;\n")) {
            return false;
        }
        return true;
    }
    if (!r_c17_emit_async_sync_operation(emitter,
                                         descriptor,
                                         r_async_sync_allocates(descriptor)
                                             ? instruction->auxiliary_type
                                             : instruction->type,
                                         instruction->runtime_type,
                                         first_type,
                                         arguments[0],
                                         arguments[1],
                                         depth,
                                         &output,
                                         &native)) {
        return false;
    }
    if (native != 0U) {
        const char *member = descriptor->shape == R_ASYNC_SYNC_PUBLISH ? ".count;\n" : ".value;\n";
        return r_c17_indent(emitter, depth) && r_c17_write(emitter, "if (") &&
               r_c17_emit_temporary_name(emitter, native) &&
               r_c17_write(emitter, ".status == R_STD_ASYNC_CALL_SUCCESS) {\n") &&
               r_c17_indent(emitter, depth + 1U) &&
               r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) &&
               r_c17_write(emitter, ".r_tag = UINT32_C(0);\n") &&
               r_c17_indent(emitter, depth + 1U) &&
               r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) &&
               r_c17_write(emitter, ".r_payload.r_ok = ") &&
               r_c17_emit_temporary_name(emitter, native) && r_c17_write(emitter, member) &&
               r_c17_indent(emitter, depth) && r_c17_write(emitter, "} else {\n") &&
               r_c17_indent(emitter, depth + 1U) &&
               r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) &&
               r_c17_write(emitter, ".r_tag = UINT32_C(1);\n") &&
               r_c17_indent(emitter, depth + 1U) &&
               r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) &&
               r_c17_write(emitter, ".r_payload.r_error_00000001 = ") &&
               r_c17_emit_temporary_name(emitter, native) && r_c17_write(emitter, ".error;\n") &&
               r_c17_indent(emitter, depth) && r_c17_write(emitter, "}\n") &&
               r_c17_emit_async_mark_value_initialized(
                   emitter, mir, function, instruction->type, instruction->result, true, depth);
    }
    if (output == 0U) {
        return true;
    }
    if (!r_c17_indent(emitter, depth)) {
        return false;
    }
    if (r_c17_type_requires_drop(emitter, instruction->type)) {
        if (!r_c17_emit_type_move_name(emitter, instruction->type) || !r_c17_write(emitter, "(&") ||
            !r_c17_emit_async_storage_reference(emitter, mir, function, false, instruction->result) ||
            !r_c17_write(emitter, ", &") || !r_c17_emit_temporary_name(emitter, output) ||
            !r_c17_write(emitter, ");\n")) {
            return false;
        }
    } else if (!r_c17_emit_async_storage_reference(
                   emitter, mir, function, false, instruction->result) ||
               !r_c17_write(emitter, " = ") || !r_c17_emit_temporary_name(emitter, output) ||
               !r_c17_write(emitter, ";\n")) {
        return false;
    }
    return !r_c17_type_requires_init_flag(emitter, instruction->type) ||
           (r_c17_indent(emitter, depth) &&
            r_c17_emit_async_init_reference(emitter, "frame", false, instruction->result) &&
            r_c17_write(emitter, " = 1;\n"));
}

/* MIR preflight. */
static bool r_c17_preflight_async_sync_mir(RC17Emitter *emitter,
                                          const RMirFunction *mir,
                                          const RMirInstruction *instruction) {
    const RAsyncSyncDescriptor *descriptor =
        r_async_sync_descriptor(instruction->standard_operation);

    (void)mir;
    if ((descriptor == NULL) ||
        (instruction->operand_count != r_async_sync_operand_count(descriptor))) {
        return r_c17_fail(emitter, R_FRONTEND_NOT_LOWERABLE);
    }
    if (strcmp(descriptor->module, "test") == 0) {
        emitter->uses_std_test = true;
    } else {
        emitter->uses_async = true;
    }
    if (strcmp(descriptor->module, "sync") == 0) {
        emitter->uses_std_sync = true;
    }
    if (!r_c17_preflight_type_dependency(emitter, instruction->type, UINT32_C(0)) ||
        !r_c17_preflight_type_dependency(emitter, instruction->auxiliary_type, UINT32_C(0))) {
        return false;
    }
    if (r_async_sync_starts(descriptor)) {
        const RSemanticType *task =
            r_c17_type(emitter, r_c17_value_type(emitter, instruction->auxiliary_type));
        return (task != NULL) && (task->kind == R_SEMANTIC_TYPE_TASK) &&
               r_c17_type_requires_init_flag(emitter, instruction->type) &&
               r_c17_preflight_sync_runtime_type(emitter, task->base);
    }
    if ((descriptor->shape == R_ASYNC_SYNC_CONSTRUCT_VALUE) ||
        (descriptor->shape == R_ASYNC_SYNC_CONSTRUCT_TYPED)) {
        return r_c17_preflight_sync_runtime_type(emitter, instruction->runtime_type) &&
               ((descriptor->shape != R_ASYNC_SYNC_CONSTRUCT_TYPED) ||
                r_c17_require_broadcast_clone(emitter, instruction->runtime_type));
    }
    return true;
}
