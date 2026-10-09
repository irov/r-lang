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
    message(FATAL_ERROR "async sync-call HIR emit failed (${hir_result}): ${hir_error}")
endif()
r_require_match_count(hir_output "[(]call symbol=[0-9]+ name=\"add\" type=i32" 2
    "HIR Copy-result calls")
r_require_match_count(hir_output "[(]call symbol=[0-9]+ name=\"forward\" type=[(]own i32[)]" 1
    "HIR Move-result call")
r_require_match_count(hir_output "[(]call symbol=[0-9]+ name=\"consume\" type=void" 1
    "HIR void-result call")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${SOURCE_FILE}"
    RESULT_VARIABLE mir_result
    OUTPUT_VARIABLE mir_output
    ERROR_VARIABLE mir_error
)
if(NOT mir_result EQUAL 0)
    message(FATAL_ERROR "async sync-call MIR emit failed (${mir_result}): ${mir_error}")
endif()
r_require_match_count(mir_output
    "call callee=\"test[.]codegen[.]async_sync_call::add\" arguments=[(]%v[0-9]+ %v[0-9]+[)] call_bounded_borrows=[(]arg0[)] type=i32"
    2 "MIR call-bounded borrow and Copy arguments")
r_require_match_count(mir_output
    "%v[0-9]+ = call callee=\"test[.]codegen[.]async_sync_call::forward\" arguments=[(]%v[0-9]+[)] type=[(]own i32[)]"
    1 "MIR Move-result call")
r_require_match_count(mir_output
    "call callee=\"test[.]codegen[.]async_sync_call::consume\" arguments=[(]%v[0-9]+[)] type=void"
    1 "MIR void-result call")

set(ir_arguments
    --emit=llvm-ir --opt-level=0
    --entry test.codegen.async_sync_call::main
    --profile hosted-native-async
    --target-manifest "${TARGET_MANIFEST}"
    "${SOURCE_FILE}")
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${ir_arguments}
    RESULT_VARIABLE ir_result
    OUTPUT_VARIABLE ir_output
    ERROR_VARIABLE ir_error
)
if(NOT ir_result EQUAL 0)
    message(FATAL_ERROR "async sync-call LLVM IR emit failed (${ir_result}): ${ir_error}")
endif()
# The protected synchronous callees stay ordinary functions that the async body calls directly:
# add before and after the suspension, forward with the Move result, consume with the Move
# argument.
foreach(callee IN ITEMS add forward consume)
    r_require_match_count(ir_output
        "define internal [a-z0-9]+ @\"test[.]codegen[.]async_sync_call::${callee}\"[(]" 1
        "reachable protected callee ${callee}")
endforeach()
r_require_match_count(ir_output "call [^\n]*@\"test[.]codegen[.]async_sync_call::add\"[(]" 2
    "Copy-result calls before and after suspension")
r_require_match_count(ir_output
    "call [^\n]*@\"test[.]codegen[.]async_sync_call::forward\"[(]" 1
    "Move-result call")
r_require_match_count(ir_output
    "call [^\n]*@\"test[.]codegen[.]async_sync_call::consume\"[(]" 1
    "void-result call")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${ir_arguments}
    RESULT_VARIABLE repeated_result
    OUTPUT_VARIABLE repeated_output
    ERROR_VARIABLE repeated_error
)
if(NOT repeated_result EQUAL 0)
    message(FATAL_ERROR
        "repeated async sync-call LLVM IR emit failed (${repeated_result}): ${repeated_error}")
endif()
if(NOT ir_output STREQUAL repeated_output)
    message(FATAL_ERROR "async sync-call LLVM IR output is not deterministic")
endif()
