static bool r_c17_json_reader_name(RC17Emitter *emitter, RSymbolId fn, const char *suffix) {
    return r_c17_format(emitter, "r_jr_%u_%s", emitter->function_ordinals[fn], suffix);
}
static bool r_c17_json_reader_transport(RC17Emitter *emitter, RSymbolId fn, RTypeId transport) {
    bool fs = r_c17_type_is_exact_named_standard(emitter, transport, "std.fs::file");
    bool net = r_c17_type_is_exact_named_standard(emitter, transport, "std.net::tcp_stream");
    const char *start_type = fs    ? "RStdFsTaskStartResult"
                             : net ? "RStdNetTaskStartResult"
                                   : "RStdIoTaskStartResult";
    const char *deadline_type = fs ? "RStdFsDeadline" : net ? "RStdNetDeadline" : "RStdIoDeadline";
    const char *read_type = net ? "RStdNetTcpReadResult" : "RStdIoReadResult";
    const char *start = fs ? "r_std_fs_read" : net ? "r_std_net_tcp_read" : "r_std_io_read";
    if (!r_c17_write(emitter, "static RStdJsonTaskStartResult\n") ||
        !r_c17_json_reader_name(emitter, fn, "start") ||
        !r_c17_write(
            emitter,
            "(const void *handle, RRuntimeArray *buffer, RStdJsonDeadline deadline) {\n") ||
        !r_c17_format(
            emitter, "    %s native = {deadline.has_value, deadline.value};\n", deadline_type) ||
        !r_c17_format(
            emitter, "    %s started = %s(handle, buffer, native);\n", start_type, start) ||
        !r_c17_write(emitter,
                     "    return (RStdJsonTaskStartResult){started.is_ok, started.task, "
                     "started.error};\n}\n\nstatic void ") ||
        !r_c17_json_reader_name(emitter, fn, "take") ||
        !r_c17_write(emitter, "(void *storage, RStdJsonTransportRead *out) {\n") ||
        !r_c17_format(emitter, "    %s *read = storage;\n", read_type) ||
        !r_c17_write(emitter,
                     "    *out = (RStdJsonTransportRead){\n        .buffer = read->buffer,\n       "
                     " .count = read->count,\n") ||
        !r_c17_format(emitter,
                      "        .end = read->kind == %s,\n",
                      net ? "R_STD_NET_TCP_READ_RESULT_END" : "R_STD_IO_READ_RESULT_END") ||
        !r_c17_format(emitter,
                      "        .failed = read->kind == %s,\n",
                      net ? "R_STD_NET_TCP_READ_RESULT_FAILED" : "R_STD_IO_READ_RESULT_FAILED") ||
        !r_c17_write(emitter,
                     "        .code = (uint32_t)read->error.code,\n        .native_code = "
                     "read->error.native_code,\n    };\n") ||
        !r_c17_format(emitter, "    *read = (%s){0};\n}\n\nstatic bool ", read_type) ||
        !r_c17_json_reader_name(emitter, fn, "deadline") ||
        !r_c17_write(
            emitter,
            "(RStdJsonDeadline deadline, RStdJsonReadOutcome *out) {\n"
            "    if (!deadline.has_value) {\n        return true;\n    }\n"
            "    RStdTimeInstantResult now = r_std_time_monotonic_now();\n"
            "    if (now.is_ok && (now.value.storage_seconds < deadline.value.storage_seconds ||\n"
            "                      (now.value.storage_seconds == deadline.value.storage_seconds "
            "&&\n"
            "                       now.value.storage_nanoseconds < "
            "deadline.value.storage_nanoseconds))) {\n"
            "        return true;\n    }\n"
            "    *out = (RStdJsonReadOutcome){\n        .status = "
            "R_STD_JSON_READ_TRANSPORT_ERROR,\n") ||
        !r_c17_format(emitter,
                      "        .transport_code = now.is_ok ? %s : %s,\n",
                      net ? "R_STD_NET_ERROR_TIMED_OUT" : "R_STD_IO_ERROR_TIMED_OUT",
                      net ? "R_STD_NET_ERROR_OTHER" : "R_STD_IO_ERROR_OTHER") ||
        !r_c17_write(emitter,
                     "        .native_code = now.is_ok ? 0 : now.error.native_code,\n    };\n    "
                     "return false;\n}\n\n"))
        return false;
    return true;
}
static bool r_c17_json_reader_complete(RC17Emitter *emitter, RSymbolId fn, RTypeId completion) {
    const RSemanticType *carrier = r_c17_type(emitter, completion);
    if (!r_c17_write(emitter, "static void ") || !r_c17_json_reader_name(emitter, fn, "complete") ||
        !r_c17_write(
            emitter,
            "(void *storage, RStdJsonDecoder *decoder, RStdJsonReadOutcome outcome) {\n    ") ||
        !r_c17_emit_value_type(emitter, completion) ||
        !r_c17_write(emitter, " *out = storage;\n") ||
        !r_c17_write(
            emitter,
            "    if (outcome.status == R_STD_JSON_READ_VALUE) {\n"
            "        (void)r_json_decoder_take(decoder, &out->r_payload.r_ok.r_payload.r_some);\n"
            "        out->r_payload.r_ok.r_tag = UINT32_C(1);\n        out->r_tag = UINT32_C(0);\n"
            "    } else if (outcome.status == R_STD_JSON_READ_END) {\n"
            "        out->r_payload.r_ok.r_tag = UINT32_C(0);\n        out->r_tag = "
            "UINT32_C(0);\n"))
        return false;
    for (uint32_t i = 0U; i < r_semantic_effect_count(emitter->frontend, carrier->second); ++i) {
        RTypeId error = r_semantic_effect_at(emitter->frontend, carrier->second, i);
        bool alloc = r_c17_type_is_exact_named_standard(emitter, error, "std.alloc::alloc_error");
        bool json = r_c17_type_is_exact_named_standard(emitter, error, "std.json::error");
        if (!r_c17_format(
                emitter,
                "    } else if (outcome.status == %s) {\n        out->r_tag = UINT32_C(%u);\n",
                alloc  ? "R_STD_JSON_READ_ALLOCATION_ERROR"
                : json ? "R_STD_JSON_READ_JSON_ERROR"
                       : "R_STD_JSON_READ_TRANSPORT_ERROR",
                i + 1U))
            return false;
        if (!r_c17_format(emitter,
                          "        out->r_payload.r_error_%08u =%s",
                          i + 1U,
                          (alloc || json) ? " " : ""))
            return false;
        if (alloc || json) {
            if (!r_c17_write(emitter,
                             alloc ? "outcome.json.allocation_error;\n" : "outcome.json.error;\n"))
                return false;
        } else {
            bool net = r_c17_type_is_exact_named_standard(emitter, error, "std.net::net_error");
            if (!r_c17_format(
                    emitter,
                    "\n            (%s){(%s)outcome.transport_code, outcome.native_code};\n",
                    net ? "RStdNetError" : "RStdIoError",
                    net ? "RStdNetErrorCode" : "RStdIoErrorCode"))
                return false;
        }
    }
    return r_c17_write(emitter, "    }\n}\n\n");
}
static bool r_c17_json_reader_function(RC17Emitter *emitter, RSymbolId fn) {
    const RSemanticSymbol *function = r_c17_symbol(emitter, fn);
    const RHirNode *node = r_c17_node(emitter, emitter->function_nodes[fn]);
    RJsonOperation operation = (RJsonOperation)function->json_operation;
    bool create = operation == R_JSON_OPERATION_NEW_READER;
    bool read = operation == R_JSON_OPERATION_READ_NEXT;
    RTypeId completion = 0U;
    if (create && !r_c17_json_reader_transport(emitter, fn, function->json_type))
        return false;
    if (read) {
        uint8_t *seen = r_c17_allocate(emitter, emitter->frontend->semantic_type_count + 1U);
        if (seen == NULL)
            return false;
        (void)memset(seen, 0, emitter->frontend->semantic_type_count + 1U);
        bool success = r_c17_json_stream_types(emitter, fn, function->json_type, seen);
        r_c17_free(emitter, seen);
        completion = r_c17_json_reader_completion(emitter, function->return_type);
        if (!success || !r_c17_json_reader_complete(emitter, fn, completion))
            return false;
    }
    if (!r_c17_emit_function_signature(emitter, fn, true) || !r_c17_write(emitter, " {\n"))
        return false;
    if (read) {
        if (!r_c17_json_named_info(emitter, function->json_type, 1U, "value_info") ||
            !r_c17_json_named_info(emitter, completion, 1U, "result_info") ||
            !r_c17_write(emitter, "    RStdJsonDeadline deadline = {0};\n    if (") ||
            !r_c17_format_argument(emitter, node, 1U) ||
            !r_c17_write(emitter, ".r_tag != UINT32_C(0)) {\n") ||
            !r_c17_write(emitter,
                         "        deadline.has_value = true;\n        deadline.value = ") ||
            !r_c17_format_argument(emitter, node, 1U) ||
            !r_c17_write(
                emitter,
                ".r_payload.r_some;\n    }\n"
                "    r_runtime_task_deadline_narrow(&deadline.has_value, "
                "&deadline.value.storage_seconds, &deadline.value.storage_nanoseconds);\n") ||
            !r_c17_write(emitter, "    RStdJsonDecodeCreateFn create = ") ||
            !r_c17_json_stream_name(emitter, fn, function->json_type, "create") ||
            !r_c17_write(emitter, ";\n    RStdJsonReadCompleteFn complete = ") ||
            !r_c17_json_reader_name(emitter, fn, "complete") ||
            !r_c17_write(
                emitter,
                ";\n    RStdJsonTaskStartResult started = r_json_reader_read_next(\n        ") ||
            !r_c17_format_argument(emitter, node, 0U) ||
            !r_c17_write(
                emitter,
                ", deadline, value_info, create, result_info, complete);\n"
                "    if (started.is_ok) {\n        r_effect_out->r_tag = UINT32_C(0);\n"
                "        r_effect_out->r_payload.r_ok = started.task;\n    } else {\n"
                "        r_effect_out->r_tag = UINT32_C(1);\n"
                "        r_effect_out->r_payload.r_error_00000001 = started.error;\n    }\n}\n\n"))
            return false;
        return true;
    }
    if (!r_c17_write(emitter, "    ") || !r_c17_emit_value_type(emitter, function->return_type) ||
        !r_c17_write(emitter, " value;\n"))
        return false;
    if (create) {
        bool net =
            r_c17_type_is_exact_named_standard(emitter, function->json_type, "std.net::tcp_stream");
        const char *read_type = net ? "RStdNetTcpReadResult" : "RStdIoReadResult";
        if (!r_c17_json_named_info(emitter, function->json_type, 1U, "handle_info") ||
            !r_c17_write(emitter, "    RStdJsonOptions options = ") ||
            !(function->parameter_count == 1U ? r_c17_write(emitter, "R_STD_JSON_DEFAULT_OPTIONS")
                                              : r_c17_format_argument(emitter, node, 1U)) ||
            !r_c17_write(
                emitter,
                ";\n    RStdJsonTransport transport = {\n        .handle_type = handle_info,\n") ||
            !r_c17_format(
                emitter,
                "        .read_type = {sizeof(%s), _Alignof(%s), NULL, NULL},\n        .start = ",
                read_type,
                read_type) ||
            !r_c17_json_reader_name(emitter, fn, "start") ||
            !r_c17_write(emitter, ",\n        .take = ") ||
            !r_c17_json_reader_name(emitter, fn, "take") ||
            !r_c17_write(emitter, ",\n        .deadline = ") ||
            !r_c17_json_reader_name(emitter, fn, "deadline") ||
            !r_c17_write(emitter,
                         ",\n    };\n    RStdJsonResult outcome = r_json_reader_initialize(\n      "
                         "  &value, "
                         "r_runtime_hosted_allocator(), options, transport, ") ||
            !r_c17_format_argument(emitter, node, 0U) || !r_c17_write(emitter, ");\n"))
            return false;
    } else {
        const char *native = operation == R_JSON_OPERATION_DETACH ? "r_json_reader_detach"
                             : operation == R_JSON_OPERATION_TAKE_HANDLE
                                 ? "r_json_detached_take_handle"
                                 : "r_json_detached_take_bytes";
        if (!r_c17_format(emitter, "    RStdJsonResult outcome = %s(", native) ||
            !r_c17_format_argument(emitter, node, 0U) || !r_c17_write(emitter, ", &value);\n"))
            return false;
    }
    return r_c17_write(
        emitter,
        "    if (outcome.status == R_STD_JSON_CALL_SUCCESS) {\n"
        "        r_effect_out->r_payload.r_ok = value;\n        r_effect_out->r_tag = "
        "UINT32_C(0);\n"
        "    } else if (outcome.status == R_STD_JSON_CALL_ALLOCATION_ERROR) {\n"
        "        r_effect_out->r_tag = UINT32_C(1);\n"
        "        r_effect_out->r_payload.r_error_00000001 = outcome.allocation_error;\n"
        "    } else {\n        r_effect_out->r_tag = UINT32_C(2);\n"
        "        r_effect_out->r_payload.r_error_00000002 = outcome.error;\n    }\n}\n\n");
}
