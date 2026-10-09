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
r_require_match_count(hir_output "operation=std\.async::cancel" 2
    "HIR async cancel operations")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${SOURCE_FILE}"
    RESULT_VARIABLE mir_result
    OUTPUT_VARIABLE mir_output
    ERROR_VARIABLE mir_error
)
if(NOT mir_result EQUAL 0)
    message(FATAL_ERROR "MIR emit failed (${mir_result}): ${mir_error}")
endif()
r_require_match_count(mir_output "operation=std\.async::cancel" 2
    "MIR async cancel operations")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=llvm-ir
        --entry test.codegen.async_cancel::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE ir_result
    OUTPUT_VARIABLE ir_output
    ERROR_VARIABLE ir_error
)
if(NOT ir_result EQUAL 0)
    message(FATAL_ERROR "LLVM IR emit failed (${ir_result}): ${ir_error}")
endif()
# Each cancel operation is one call of its std.async entry.
r_require_match_count(ir_output "call [^\n]*@r_std_async_cancel[(]" 2
    "async cancel calls")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=link-plan
        --entry test.codegen.async_cancel::main
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
    "\(library module=\"std\.async\" target=\"r_std_async\"\)" 1
    "std.async link-plan record")
