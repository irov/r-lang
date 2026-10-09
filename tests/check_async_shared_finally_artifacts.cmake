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

set(ir_arguments
    --emit=llvm-ir --opt-level=0
    --entry test.codegen.async_shared_finally::main
    --profile hosted-native-async
    --target-manifest "${TARGET_MANIFEST}"
    "${SOURCE_FILE}"
)
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${ir_arguments}
    RESULT_VARIABLE ir_result
    OUTPUT_VARIABLE ir_output
    ERROR_VARIABLE ir_error
)
if(NOT ir_result EQUAL 0)
    message(FATAL_ERROR "async shared-finally LLVM IR emit failed (${ir_result}): ${ir_error}")
endif()
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${ir_arguments}
    RESULT_VARIABLE repeated_ir_result
    OUTPUT_VARIABLE repeated_ir_output
    ERROR_VARIABLE repeated_ir_error
)
if(NOT repeated_ir_result EQUAL 0)
    message(FATAL_ERROR
        "repeated async shared-finally LLVM IR emit failed "
        "(${repeated_ir_result}): ${repeated_ir_error}")
endif()
if(NOT ir_output STREQUAL repeated_ir_output)
    message(FATAL_ERROR "async shared-finally LLVM IR output is not deterministic")
endif()

# The program has two awaits; cancellation is observed there and never inside a finally body.
r_require_match_count(ir_output "call [^\n]*@r_runtime_task_execution_cancel_requested[(]" 2
    "cancellation polls must remain at awaits, outside all finally bodies")
r_require_match_count(ir_output "@_?(setjmp|longjmp|sigsetjmp|siglongjmp)[(]" 0
    "async finally lowering does not use non-local control transfer")
