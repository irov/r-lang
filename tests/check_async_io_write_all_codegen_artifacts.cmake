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

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=hir "${SOURCE_FILE}"
    RESULT_VARIABLE hir_result
    OUTPUT_VARIABLE hir_output
    ERROR_VARIABLE hir_error
)
if(NOT hir_result EQUAL 0)
    message(FATAL_ERROR "HIR emit failed (${hir_result}): ${hir_error}")
endif()
r_require_match_count(hir_output "operation=std[.]io::write_all" 1
    "HIR write_all operations")
r_require_match_count(hir_output
    "[(]await [^\n]*type=[(]standard \"std[.]io::write_all_result\"[)]" 1
    "HIR write_all awaits")

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
    "standard_call operation=std[.]io::write_all stream=%v[0-9]+ buffer=%v[0-9]+ deadline=%v[0-9]+ call_bounded_borrows=[(]stream[)] staged_moves=[(]buffer[)]" 1
    "MIR ownership-aware write_all operations")
r_require_match_count(mir_output
    "await [^\n]*type=[(]standard \"std[.]io::write_all_result\"[)]" 1
    "MIR write_all awaits")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.async_io_write_all::main
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
r_require_match_count(c17_output "r_std_io_write_all[(]" 1
    "generated write_all calls")
r_require_match_count(c17_output "RStdIoDeadline r_io_deadline_[0-9]+" 1
    "generated deadline adapters")
r_require_match_count(c17_output
    "R_INTERNAL_ASSERT[(]" 0
    "generated C omits assertions")
r_require_match_count(c17_output "[.]r_tag > UINT32_C[(]1[)]" 0
    "generated deadline runtime guards")
r_require_match_count(c17_output "RRuntimeArray r_io_buffer_before_[0-9]+" 0
    "generated C omits debug ownership snapshots")
r_require_match_count(c17_output
    "if [(]r_io_start_[0-9]+[.]is_ok[)] [{][\n ]+frame->r_l[0-9]+_initialized = 0" 1
    "generated successful-start ownership transition")
r_require_match_count(c17_output "RStdIoTaskStartResult r_io_start_[0-9]+" 1
    "generated start-result adapters")
r_require_match_count(c17_output "r_io_start_[0-9]+[.]task = NULL" 1
    "generated task ownership transfers")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=link-plan
        --entry test.codegen.async_io_write_all::main
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
    "[(]library module=\"std[.]array\" target=\"r_std_array\"[)]" 1
    "std.array link-plan record")
r_require_match_count(plan_output
    "[(]library module=\"std[.]io\" target=\"r_std_io\"[)]" 1
    "std.io link-plan record")
