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
r_require_match_count(hir_output "operation=std[.]io::stderr" 1
    "HIR stderr operations")
r_require_match_count(hir_output
    "[(]move symbol=[^\n]*type=[(]standard \"std[.]io::output\"[)]" 1
    "HIR output moves")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${SOURCE_FILE}"
    RESULT_VARIABLE mir_result
    OUTPUT_VARIABLE mir_output
    ERROR_VARIABLE mir_error
)
if(NOT mir_result EQUAL 0)
    message(FATAL_ERROR "MIR emit failed (${mir_result}): ${mir_error}")
endif()
r_require_match_count(mir_output "operation=std[.]io::stderr" 1
    "MIR stderr operations")
r_require_match_count(mir_output
    "move source=[^\n]*type=[(]standard \"std[.]io::output\"[)]" 1
    "MIR output moves")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.async_stderr::main
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
r_require_match_count(c17_output "r_std_io_stderr[(]" 1
    "generated stderr calls")
r_require_match_count(c17_output "r_std_io_output_move_initialize[(]" 1
    "generated output moves")
r_require_match_count(c17_output "r_std_io_output_destroy[(]" 1
    "generated output drops")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=link-plan
        --entry test.codegen.async_stderr::main
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
