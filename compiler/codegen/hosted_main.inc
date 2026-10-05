/* R-FUNC-0008: the exact errors of the implicit main set and their portable projection; the same
   order is the variant order of the family of std.error::fault (R-SLIB-ERR-0004). */
static const struct {
    const char *name;
    const char *domain;
    const char *member;
    unsigned base;
    bool native;
} r_c17_main_errors[] = {
#define R_MAIN_ERROR(name, domain, member, base, native) {name, domain, member, base, native},
#include "../semantic/main_errors.def"
#undef R_MAIN_ERROR
};

static bool
r_c17_emit_fault_operand(RC17Emitter *emitter, const RC17FaultOperands *operands, bool result) {
    const uint32_t value = result ? operands->result : operands->source;
    return operands->mir == NULL ? r_c17_emit_temporary_name(emitter, value)
                                 : r_c17_emit_async_storage_reference(
                                       emitter, operands->mir, operands->function, false, value);
}

/* R-SLIB-ERR-0004: std.error::from_fault forms the portable error of the held standard error as
   the generated main boundary reports it; it allocates nothing and cannot panic for a
   well-formed value. */
static bool r_c17_emit_fault_portable(RC17Emitter *emitter,
                                      const RC17FaultOperands *operands,
                                      RSourceSpan span,
                                      uint32_t depth) {
    const uint32_t count = (uint32_t)(sizeof(r_c17_main_errors) / sizeof(r_c17_main_errors[0]));
    emitter->uses_std_error = true;
    if (!r_c17_indent(emitter, depth) || !r_c17_write(emitter, "switch (") ||
        !r_c17_emit_fault_operand(emitter, operands, false) ||
        !r_c17_write(emitter, ".r_tag) {\n")) {
        return false;
    }
    for (uint32_t tag = 0U; tag < count; ++tag) {
        if (!r_c17_indent(emitter, depth) || !r_c17_format(emitter,
                                                           "case UINT32_C(%" PRIu32 "): /* %s */\n",
                                                           tag,
                                                           r_c17_main_errors[tag].name)) {
            return false;
        }
        if (r_c17_main_errors[tag].domain[0] == '\0') {
            if (!r_c17_indent(emitter, depth + 1U) ||
                !r_c17_emit_fault_operand(emitter, operands, true) ||
                !r_c17_write(emitter, " = ") ||
                !r_c17_emit_fault_operand(emitter, operands, false) ||
                !r_c17_format(emitter, ".r_payload.r_v%08" PRIu32 ";\n", tag)) {
                return false;
            }
        } else {
            if (!r_c17_indent(emitter, depth + 1U) ||
                !r_c17_emit_fault_operand(emitter, operands, true) ||
                !r_c17_format(
                    emitter, ".domain = R_STD_ERROR_DOMAIN_%s;\n", r_c17_main_errors[tag].domain) ||
                !r_c17_indent(emitter, depth + 1U) ||
                !r_c17_emit_fault_operand(emitter, operands, true)) {
                return false;
            }
            if (strcmp(r_c17_main_errors[tag].member, "zero") == 0) {
                if (!r_c17_write(emitter, ".code = UINT32_C(0);\n")) {
                    return false;
                }
            } else if (!r_c17_format(emitter,
                                     ".code = UINT32_C(%u) + (uint32_t)",
                                     r_c17_main_errors[tag].base) ||
                       !r_c17_emit_fault_operand(emitter, operands, false) ||
                       !r_c17_format(emitter,
                                     ".r_payload.r_v%08" PRIu32 "%s;\n",
                                     tag,
                                     r_c17_main_errors[tag].member)) {
                return false;
            }
            if (!r_c17_indent(emitter, depth + 1U) ||
                !r_c17_emit_fault_operand(emitter, operands, true)) {
                return false;
            }
            if (r_c17_main_errors[tag].native) {
                if (!r_c17_write(emitter, ".native_code = ") ||
                    !r_c17_emit_fault_operand(emitter, operands, false) ||
                    !r_c17_format(emitter, ".r_payload.r_v%08" PRIu32 ".native_code;\n", tag)) {
                    return false;
                }
            } else if (!r_c17_write(emitter, ".native_code = INT64_C(0);\n")) {
                return false;
            }
        }
        if (!r_c17_indent(emitter, depth + 1U) || !r_c17_write(emitter, "break;\n")) {
            return false;
        }
    }
    return r_c17_indent(emitter, depth) &&
           r_c17_write(emitter, "default: /* Impossible for a well-formed fault. */\n") &&
           r_c17_emit_async_contract_panic(emitter, span, depth + 1U) &&
           r_c17_indent(emitter, depth) && r_c17_write(emitter, "}\n");
}

/* R-FUNC-0008/R-IDB-023. The entry owns and consumes the root outcome exactly once. */
static bool
r_c17_emit_main_outcome(RC17Emitter *emitter, const RSemanticSymbol *entry, RSourceSpan span) {
    const size_t error_count = sizeof(r_c17_main_errors) / sizeof(r_c17_main_errors[0]);
    static const struct {
        const char *name;
        const char *constant;
        unsigned status;
    } domains[] = {
        {"allocation", "ALLOCATION", 112U},
        {"async_runtime", "ASYNC_RUNTIME", 116U},
        {"bytes", "BYTES", 117U},
        {"string", "STRING", 117U},
        {"conversion", "CONVERSION", 115U},
        {"format", "FORMAT", 115U},
        {"math", "MATH", 117U},
        {"time", "TIME", 117U},
        {"environment", "ENVIRONMENT", 117U},
        {"io", "IO", 113U},
        {"filesystem", "FILESYSTEM", 113U},
        {"network", "NETWORK", 114U},
        {"process", "PROCESS", 117U},
        {"threading", "THREADING", 116U},
        {"c_abi", "C_ABI", 117U},
    };
    const uint32_t count = r_semantic_effect_count(emitter->frontend, entry->throws_type);
    if (!r_c17_write(emitter,
                     "    if (r_outcome.r_tag == UINT32_C(0)) {\n"
                     "        r_result = r_outcome.r_payload.r_ok;\n"
                     "        if (r_result < 0 || r_result > 111) {\n"
                     "            static const char diagnostic[] = \"R main error: "
                     "invalid_main_status\\n\";\n"
                     "            r_runtime_emergency_write(diagnostic, sizeof(diagnostic) - 1U);\n"
                     "            r_result = 124;\n"
                     "        }\n"
                     "    } else {\n"
                     "        RStdError r_error;\n"
                     "        RStdStringView r_name;\n"
                     "        const char *r_domain;\n"
                     "        size_t r_domain_length;\n"
                     "        switch (r_outcome.r_tag) {\n"))
        return false;
    for (uint32_t index = 0U; index < count; ++index) {
        const RTypeId type = r_semantic_effect_at(emitter->frontend, entry->throws_type, index);
        size_t which;
        for (which = 0U; which < error_count; ++which)
            if (r_c17_type_is_exact_named_standard(emitter, type, r_c17_main_errors[which].name))
                break;
        if (which == error_count || r_c17_type_requires_drop(emitter, type))
            return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
        const uint32_t tag = index + 1U;
        if (!r_c17_format(emitter,
                          "        case UINT32_C(%" PRIu32 "): /* %s */\n",
                          tag,
                          r_c17_main_errors[which].name))
            return false;
        if (r_c17_main_errors[which].domain[0] == '\0') {
            if (!r_c17_format(emitter,
                              "            r_error = r_outcome.r_payload.r_error_%08" PRIu32 ";\n",
                              tag))
                return false;
        } else {
            if (!r_c17_format(emitter,
                              "            r_error.domain = R_STD_ERROR_DOMAIN_%s;\n",
                              r_c17_main_errors[which].domain))
                return false;
            if (strcmp(r_c17_main_errors[which].member, "zero") == 0) {
                if (!r_c17_write(emitter, "            r_error.code = UINT32_C(0);\n"))
                    return false;
            } else if (!r_c17_format(emitter,
                                     "            r_error.code = UINT32_C(%u) + "
                                     "(uint32_t)r_outcome.r_payload.r_error_%08" PRIu32 "%s;\n",
                                     r_c17_main_errors[which].base,
                                     tag,
                                     r_c17_main_errors[which].member))
                return false;
            if (r_c17_main_errors[which].native) {
                if (!r_c17_format(emitter,
                                  "            r_error.native_code = "
                                  "r_outcome.r_payload.r_error_%08" PRIu32 ".native_code;\n",
                                  tag))
                    return false;
            } else if (!r_c17_write(emitter, "            r_error.native_code = INT64_C(0);\n"))
                return false;
        }
        if (!r_c17_write(emitter, "            break;\n"))
            return false;
    }
    if (!r_c17_write(
            emitter,
            "        default: /* Impossible for a well-formed generated error carrier. */\n") ||
        !r_c17_emit_async_contract_panic(emitter, span, UINT32_C(3)) ||
        !r_c17_write(emitter, "        }\n        switch (r_error.domain) {\n"))
        return false;
    for (size_t index = 0U; index < sizeof(domains) / sizeof(domains[0]); ++index) {
        if (!r_c17_format(emitter,
                          "        case R_STD_ERROR_DOMAIN_%s:\n"
                          "            r_domain = \"%s\";\n"
                          "            r_domain_length = sizeof(\"%s\") - 1U;\n"
                          "            r_result = %u;\n"
                          "            break;\n",
                          domains[index].constant,
                          domains[index].name,
                          domains[index].name,
                          domains[index].status))
            return false;
    }
    return r_c17_write(emitter, "        default: /* Invalid enum representation. */\n") &&
           r_c17_emit_async_contract_panic(emitter, span, UINT32_C(3)) &&
           r_c17_write(
               emitter,
               "        }\n"
               "        r_name = r_std_error_name(r_error);\n"
               "        r_runtime_report_main_error(r_domain, r_domain_length, r_name.data,\n"
               "                                    r_name.length, r_error.code, "
               "r_error.native_code);\n"
               "    }\n"
               "    /* All admitted payloads are Copy: consuming them has no drop side effect. */\n"
               "    r_outcome.r_tag = UINT32_MAX;\n");
}

static bool r_c17_emit_hosted_main(RC17Emitter *emitter) {
    const RSemanticSymbol *entry = r_c17_symbol(emitter, emitter->entry_symbol);
    const RHirNode *node =
        entry == NULL ? NULL : r_c17_node(emitter, emitter->function_nodes[emitter->entry_symbol]);
    if (r_c17_freestanding(emitter))
        return r_c17_emit_freestanding_entry(emitter);
    if (entry == NULL || node == NULL || entry->effect_carrier_type == R_TYPE_ID_INVALID)
        return r_c17_fail(emitter, R_FRONTEND_INTERNAL_ERROR);
    const bool args = !entry->is_async && r_c17_function_has_startup_arguments(emitter, entry);
    if (!r_c17_write(emitter,
                     "int main(int argc, char *argv[]) {\n"
                     "    RRuntimeStartResult r_start;\n"
                     "    int32_t r_result;\n    ") ||
        !r_c17_emit_value_type(emitter, entry->effect_carrier_type) ||
        !r_c17_write(emitter, " r_outcome = {0};\n"))
        return false;
    if (entry->is_async && !r_c17_write(emitter,
                                        "    RRuntimeTaskStartResult r_root;\n"
                                        "    RRuntimeTaskAwaitStatus r_await_status;\n"))
        return false;
    if (args &&
        (!r_c17_write(emitter,
                      "    RRuntimeArgumentSnapshotView r_startup_snapshot = {0};\n    ") ||
         !r_c17_emit_value_type(
             emitter, emitter->frontend->semantic_parameter_types[entry->first_parameter_type]) ||
         !r_c17_write(emitter, " r_startup_args = {0};\n")))
        return false;
    if (!r_c17_emit_hosted_stack_bootstrap(emitter, node->span, UINT32_C(1)) ||
        !r_c17_write(emitter,
                     "    r_start = r_runtime_hosted_start(argc, argv);\n"
                     "    if (!r_start.started) {\n"
                     "        return r_start.process_status;\n"
                     "    }\n") ||
        !r_c17_emit_hosted_thread_local_install(emitter, UINT32_C(1)) ||
        !r_c17_emit_hosted_static_initialize(emitter, UINT32_C(1)))
        return false;
    if (args &&
        (!r_c17_write(emitter,
                      "    if (!r_runtime_hosted_argument_snapshot(&r_startup_snapshot)) {\n") ||
         !r_c17_emit_async_contract_panic(emitter, node->span, UINT32_C(2)) ||
         !r_c17_write(emitter,
                      "    }\n"
                      "    r_startup_args.r_data = r_startup_snapshot.arguments;\n"
                      "    r_startup_args.r_len = r_startup_snapshot.count;\n")))
        return false;
    if (entry->is_async) {
        if (!r_c17_write(emitter, "    r_root = ") ||
            !r_c17_emit_async_start_gate_name(emitter, emitter->entry_symbol) ||
            !r_c17_write(emitter, "(") ||
            !r_c17_emit_async_initialize_gate_name(emitter, emitter->entry_symbol) ||
            !r_c17_write(emitter,
                         ", NULL, 0);\n"
                         "    if ((r_root.status == R_RUNTIME_TASK_START_ALLOCATION_FAILED) ||\n"
                         "        (r_root.status == R_RUNTIME_TASK_START_RUNTIME_STOPPING)) {\n"
                         "        return r_runtime_hosted_async_root_start_failure();\n"
                         "    }\n"
                         "    if ((r_root.status != R_RUNTIME_TASK_START_OK) || (r_root.task == "
                         "NULL)) {\n") ||
            !r_c17_emit_async_contract_panic(emitter, node->span, UINT32_C(2)) ||
            !r_c17_write(emitter,
                         "    }\n"
                         "    r_await_status = r_runtime_task_await(&r_root.task, &r_outcome);\n"
                         "    if (r_await_status == R_RUNTIME_TASK_AWAIT_PANICKED) {\n"
                         "        r_runtime_unwind_terminate();\n"
                         "    }\n"
                         "    if (r_await_status != R_RUNTIME_TASK_AWAIT_OK) {\n") ||
            !r_c17_emit_async_contract_panic(emitter, node->span, UINT32_C(2)) ||
            !r_c17_write(emitter, "    }\n"))
            return false;
    } else {
        if (!r_c17_emit_stack_preflight(
                emitter, emitter->entry_symbol, false, node->span, UINT32_C(1)) ||
            !r_c17_write(emitter, "    ") ||
            !r_c17_emit_function_name(emitter, emitter->entry_symbol) ||
            !r_c17_write(emitter, args ? "(&r_outcome, r_startup_args);\n" : "(&r_outcome);\n"))
            return false;
        /* L39 (R-ERR-0009): a panic that reaches main has unwound its frames; the process ends
           with its diagnostic. */
        if (!r_c17_write(emitter,
                         "    if (r_runtime_unwinding()) {\n"
                         "        r_runtime_unwind_terminate();\n"
                         "    }\n"))
            return false;
    }
    return r_c17_emit_main_outcome(emitter, entry, node->span) &&
           r_c17_write(emitter, "    r_runtime_hosted_drain();\n") &&
           r_c17_emit_hosted_static_cleanup(emitter, UINT32_C(1)) &&
           r_c17_write(emitter,
                       "    r_runtime_hosted_drain();\n"
                       "    return r_runtime_hosted_finish(r_result);\n}\n");
}
