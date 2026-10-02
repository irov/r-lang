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

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.async_sync_call::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE c17_result
    OUTPUT_VARIABLE c17_output
    ERROR_VARIABLE c17_error
)
if(NOT c17_result EQUAL 0)
    message(FATAL_ERROR "async sync-call C17 emit failed (${c17_result}): ${c17_error}")
endif()
r_require_match_count(c17_output "static int32_t r_f00000001[(]" 2
    "reachable protected Copy-result callee")
r_require_match_count(c17_output "static void r_f00000002[(]RRuntimeOwn" 2
    "reachable protected void callee")
r_require_match_count(c17_output "static RRuntimeOwn r_f00000003[(]RRuntimeOwn" 2
    "reachable protected Move-result callee")
r_require_match_count(c17_output "r_d[0-9]+ r_stack_r_v[0-9]+ = [{]0[}]" 2
    "transient call-bounded borrow storage")
r_require_match_count(c17_output
    "frame->r_v00000003 = r_f00000001[(]r_stack_r_v00000001, frame->r_v00000002[)]"
    1 "Copy result before suspension")
r_require_match_count(c17_output
    "frame->r_v00000017 = r_f00000001[(]r_stack_r_v00000015, frame->r_v00000016[)]"
    1 "Copy result after suspension")
r_require_match_count(c17_output
    "R_INTERNAL_ASSERT[(]" 0
    "generated C omits assertions")
r_require_match_count(c17_output "if [(]!frame->r_v00000006_initialized[)]" 0
    "Move argument runtime guards")
r_require_match_count(c17_output "frame->r_v00000006_initialized = 0" 3
    "Move argument initialization-state clears")
r_require_match_count(c17_output
    "frame->r_v00000007 = r_f00000003[(]frame->r_v00000006[)]" 1
    "Move-result C call")
r_require_match_count(c17_output "frame->r_v00000007_initialized = 1" 1
    "Move-result initialization")
r_require_match_count(c17_output "if [(]!frame->r_v00000034_initialized[)]" 0
    "void-call Move argument runtime guards")
r_require_match_count(c17_output "frame->r_v00000034_initialized = 0" 3
    "void-call Move argument initialization-state clears")
r_require_match_count(c17_output "r_f00000002[(]frame->r_v00000034[)]" 1
    "void-result C call")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.async_sync_call::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE repeated_result
    OUTPUT_VARIABLE repeated_output
    ERROR_VARIABLE repeated_error
)
if(NOT repeated_result EQUAL 0)
    message(FATAL_ERROR
        "repeated async sync-call C17 emit failed (${repeated_result}): ${repeated_error}")
endif()
if(NOT c17_output STREQUAL repeated_output)
    message(FATAL_ERROR "async sync-call C17 output is not deterministic")
endif()
