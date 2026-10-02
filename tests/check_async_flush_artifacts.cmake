if(NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED SOURCE_FILE OR
   NOT DEFINED TARGET_MANIFEST)
    message(FATAL_ERROR
        "R_FRONT_EXECUTABLE, SOURCE_FILE and TARGET_MANIFEST are required")
endif()

function(r_require_match_count variable pattern expected description)
    string(REGEX MATCHALL "${pattern}" matches "${${variable}}")
    list(LENGTH matches count)
    if(NOT count EQUAL expected)
        message(FATAL_ERROR
            "${description}: expected ${expected} matches, found ${count}:\n${${variable}}")
    endif()
endfunction()

function(r_require_near_order variable anchor first second description)
    string(FIND "${${variable}}" "${anchor}" anchor_offset)
    if(anchor_offset EQUAL -1)
        message(FATAL_ERROR "${description}: anchor '${anchor}' is absent")
    endif()
    string(LENGTH "${${variable}}" output_length)
    math(EXPR remaining "${output_length} - ${anchor_offset}")
    if(remaining GREATER 3000)
        set(remaining 3000)
    endif()
    string(SUBSTRING "${${variable}}" ${anchor_offset} ${remaining} tail)
    string(FIND "${tail}" "${first}" first_offset)
    string(FIND "${tail}" "${second}" second_offset)
    if(first_offset EQUAL -1 OR second_offset EQUAL -1 OR
       NOT first_offset LESS second_offset)
        message(FATAL_ERROR
            "${description}: expected '${first}' before '${second}':\n${tail}")
    endif()
endfunction()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=hir "${SOURCE_FILE}"
    RESULT_VARIABLE hir_result
    OUTPUT_VARIABLE hir_output
    ERROR_VARIABLE hir_error
)
if(NOT hir_result EQUAL 0)
    message(FATAL_ERROR "HIR emit failed (${hir_result}): ${hir_error}")
endif()
r_require_match_count(hir_output "operation=std[.]io::flush" 1
    "HIR flush operations")
r_require_match_count(hir_output
    "[(]await [^\n]*type=void task=[(]task void [(]effects [(]standard \"std[.]io::io_error\"[)][)][)]" 1
    "HIR flush awaits")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${SOURCE_FILE}"
    RESULT_VARIABLE mir_result
    OUTPUT_VARIABLE mir_output
    ERROR_VARIABLE mir_error
)
if(NOT mir_result EQUAL 0)
    message(FATAL_ERROR "MIR emit failed (${mir_result}): ${mir_error}")
endif()
r_require_match_count(mir_output
    "standard_call operation=std[.]io::flush stream=%local[0-9]+ deadline=%v[0-9]+" 1
    "MIR call-bounded flush operations")
r_require_match_count(mir_output
    "await [^\n]*task=[(]task void [(]effects [(]standard \"std[.]io::io_error\"[)][)][)] type=[(]carrier void [(]effects [(]standard \"std[.]io::io_error\"[)][)][)]" 1
    "MIR flush awaits")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen_async_flush::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE c17_result
    OUTPUT_VARIABLE c17_output
    ERROR_VARIABLE c17_error
)
if(NOT c17_result EQUAL 0)
    message(FATAL_ERROR "C17 emit failed (${c17_result}): ${c17_error}")
endif()
r_require_match_count(c17_output "#include \"r_std_io[.]h\"" 1
    "generated std.io include")
r_require_match_count(c17_output "r_std_io_flush[(]" 1
    "generated flush calls")
r_require_match_count(c17_output "RStdIoDeadline r_io_deadline_" 1
    "generated deadline adapters")
r_require_match_count(c17_output
    "R_INTERNAL_ASSERT[(]" 0
    "generated C omits assertions")
r_require_match_count(c17_output "[.]r_tag > UINT32_C[(]1[)]" 0
    "generated deadline runtime guards")
r_require_match_count(c17_output "RStdIoTaskStartResult r_io_start_" 1
    "generated start-result adapters")
r_require_near_order(c17_output
    "RStdIoTaskStartResult r_io_start_"
    ".r_payload.r_ok = r_io_start_"
    ".r_tag = UINT32_C(0)"
    "generated flush success publication")
r_require_near_order(c17_output
    "RStdIoTaskStartResult r_io_start_"
    ".r_payload.r_error_00000001 = r_io_start_"
    ".r_tag = UINT32_C(1)"
    "generated flush error publication")
r_require_near_order(c17_output
    "static inline void r_type_move_d"
    "destination->r_payload.r_error_00000001"
    "destination->r_tag = source->r_tag"
    "generated effect-carrier move publication")
r_require_near_order(c17_output
    "switch (started.status)"
    "r_effect_out->r_payload.r_ok = started.task"
    "r_effect_out->r_tag = UINT32_C(0)"
    "generated async launch success publication")
r_require_near_order(c17_output
    "switch (started.status)"
    "r_effect_out->r_payload.r_error_00000001"
    "r_effect_out->r_tag = UINT32_C(1)"
    "generated async launch error publication")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=link-plan
        --entry test.codegen_async_flush::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE plan_result
    OUTPUT_VARIABLE plan_output
    ERROR_VARIABLE plan_error
)
if(NOT plan_result EQUAL 0)
    message(FATAL_ERROR "link-plan emit failed (${plan_result}): ${plan_error}")
endif()
r_require_match_count(plan_output
    "[(]library module=\"std[.]io\" target=\"r_std_io\"[)]" 1
    "std.io link-plan record")
