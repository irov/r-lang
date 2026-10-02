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

function(r_require_order variable first second description)
    string(FIND "${${variable}}" "${first}" first_offset)
    string(FIND "${${variable}}" "${second}" second_offset)
    if(first_offset EQUAL -1 OR second_offset EQUAL -1 OR
       NOT first_offset LESS second_offset)
        message(FATAL_ERROR
            "${description}: expected '${first}' before '${second}':\n${${variable}}")
    endif()
endfunction()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${SOURCE_FILE}"
    RESULT_VARIABLE mir_result
    OUTPUT_VARIABLE mir_output
    ERROR_VARIABLE mir_error
)
if(NOT mir_result EQUAL 0)
    message(FATAL_ERROR "async shared-finally MIR emit failed (${mir_result}): ${mir_error}")
endif()
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${SOURCE_FILE}"
    RESULT_VARIABLE repeated_mir_result
    OUTPUT_VARIABLE repeated_mir_output
    ERROR_VARIABLE repeated_mir_error
)
if(NOT repeated_mir_result EQUAL 0)
    message(FATAL_ERROR
        "repeated async shared-finally MIR emit failed "
        "(${repeated_mir_result}): ${repeated_mir_error}")
endif()
if(NOT mir_output STREQUAL repeated_mir_output)
    message(FATAL_ERROR "async shared-finally MIR output is not deterministic")
endif()

foreach(finally_id RANGE 1 2)
    r_require_match_count(mir_output "\\(finally_push id=${finally_id} " 2
        "MIR lexical finally ${finally_id} pushes across both async functions")
    r_require_match_count(mir_output "\\(finally_enter id=${finally_id} " 2
        "MIR lexical finally ${finally_id} shared bodies across both async functions")
    r_require_match_count(mir_output "\\(finally_exit id=${finally_id} " 2
        "MIR lexical finally ${finally_id} exits across both async functions")
endforeach()
r_require_match_count(mir_output "\\(finally_push id=3 " 1
    "MIR lexical finally 3 push")
r_require_match_count(mir_output "\\(finally_enter id=3 " 1
    "MIR lexical finally 3 shared body")
r_require_match_count(mir_output "\\(finally_exit id=3 " 1
    "MIR lexical finally 3 exit")
r_require_match_count(mir_output
    "pending_set reason=checked_error[^\n]*payload=%v[0-9]+[^\n]*stop_depth=1 first_finally=2" 1
    "MIR checked-error payload staging")
r_require_match_count(mir_output
    "pending_resume reason=checked_error[^\n]*payload=%v[0-9]+[^\n]*stop_depth=1 first_finally=2" 1
    "MIR checked-error payload restoration")
r_require_match_count(mir_output
    "finally_exit id=2 depth=1 target=bb[0-9]+" 1
    "MIR inner-to-outer LIFO edge")
r_require_match_count(mir_output
    "pending_set reason=normal[^\n]*first_finally=3" 1
    "MIR nested completion inside finally")
r_require_order(mir_output
    "(pending_set reason=return"
    "(drop place=%local2)"
    "MIR must stage a return payload before catch-binding cleanup")
r_require_order(mir_output
    "pending_set reason=checked_error type=(struct \"test.codegen.async_shared_finally\"::\"move_error\") payload=%v4 resume=bb3 stop_depth=0 first_finally=1"
    "pending_set reason=normal target=bb5 resume=bb6 stop_depth=0 first_finally=2"
    "outer Move error must be staged before a nested finally completion")
r_require_match_count(mir_output
    "pending_resume reason=normal target=bb5 resume=bb6 stop_depth=0 first_finally=2" 1
    "nested finally completion resume")
r_require_match_count(mir_output
    "pending_resume reason=checked_error type=\\(struct \"test.codegen.async_shared_finally\"::\"move_error\"\\) payload=%v4 resume=bb3 stop_depth=0 first_finally=1" 1
    "outer Move error resume after nested completion")

set(c17_arguments
    --emit=c17
    --entry test.codegen.async_shared_finally::main
    --profile hosted-native-async
    --target-manifest "${TARGET_MANIFEST}"
    "${SOURCE_FILE}"
)
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${c17_arguments}
    RESULT_VARIABLE c17_result
    OUTPUT_VARIABLE c17_output
    ERROR_VARIABLE c17_error
)
if(NOT c17_result EQUAL 0)
    message(FATAL_ERROR "async shared-finally C17 emit failed (${c17_result}): ${c17_error}")
endif()
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${c17_arguments}
    RESULT_VARIABLE repeated_c17_result
    OUTPUT_VARIABLE repeated_c17_output
    ERROR_VARIABLE repeated_c17_error
)
if(NOT repeated_c17_result EQUAL 0)
    message(FATAL_ERROR
        "repeated async shared-finally C17 emit failed "
        "(${repeated_c17_result}): ${repeated_c17_error}")
endif()
if(NOT c17_output STREQUAL repeated_c17_output)
    message(FATAL_ERROR "async shared-finally C17 output is not deterministic")
endif()

r_require_match_count(c17_output "uint32_t r_finally_depth" 2
    "generated active-finally depths")
r_require_match_count(c17_output "uint32_t r_finally_stack\\[UINT32_C\\(2\\)\\]" 1
    "generated two-level active-finally stack")
r_require_match_count(c17_output "uint32_t r_finally_stack\\[UINT32_C\\(3\\)\\]" 1
    "generated three-level active-finally stack")
r_require_match_count(c17_output "uint32_t r_pending_depth" 2
    "generated pending-completion depths")
r_require_match_count(c17_output "r_pending\\[UINT32_C\\(2\\)\\]" 1
    "generated two-level nested pending-completion records")
r_require_match_count(c17_output "r_pending\\[UINT32_C\\(3\\)\\]" 1
    "generated three-level nested pending-completion records")
r_require_match_count(c17_output "uint32_t r_reason" 2
    "generated pending reasons")
r_require_match_count(c17_output "uint32_t r_resume_state" 2
    "generated pending resume states")
r_require_match_count(c17_output "uint32_t r_stop_depth" 2
    "generated pending finally stop depths")
r_require_match_count(c17_output "uint32_t r_payload_tag" 2
    "generated pending payload tags")
r_require_match_count(c17_output
    "frame->r_finally_stack\\[frame->r_finally_depth\\] = UINT32_C\\([123]\\)" 5
    "generated lexical finally pushes")
r_require_match_count(c17_output "frame->r_pending_depth \\+= UINT32_C\\(1\\)" 6
    "generated pending-completion pushes")
r_require_match_count(c17_output "frame->r_pending_depth -= UINT32_C\\(1\\)" 8
    "generated pending-completion pops including frame-drop fallback")
r_require_match_count(c17_output "r_runtime_task_execution_cancel_requested\\(execution\\)" 2
    "cancellation polls must remain at awaits, outside all finally bodies")
r_require_match_count(c17_output "setjmp|longjmp" 0
    "generated async finally must use branch-based strict C17 lowering")
r_require_order(c17_output
    ".r_payload_tag = UINT32_C("
    ".r_reason = UINT32_C("
    "generated pending payload tag must be committed before its reason")
r_require_order(c17_output
    ".r_reason = UINT32_C("
    "frame->r_pending_depth += UINT32_C(1);"
    "generated pending reason must be committed before cleanup can observe the record")
