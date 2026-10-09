if(NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED SOURCE_FILE OR
   NOT DEFINED EFFECT_CLEANUP_SOURCE OR
   NOT DEFINED TARGET_MANIFEST)
    message(FATAL_ERROR
        "R_FRONT_EXECUTABLE, SOURCE_FILE, EFFECT_CLEANUP_SOURCE and TARGET_MANIFEST are required")
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
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${EFFECT_CLEANUP_SOURCE}"
    RESULT_VARIABLE cleanup_mir_result
    OUTPUT_VARIABLE cleanup_mir_output
    ERROR_VARIABLE cleanup_mir_error
)
if(NOT cleanup_mir_result EQUAL 0)
    message(FATAL_ERROR
        "effect-cleanup MIR emit failed (${cleanup_mir_result}): ${cleanup_mir_error}")
endif()
# L25.4: both errors of fail() leave propagate() through one relay, so the carrier of the call is
# the one pending payload.
r_require_match_count(cleanup_mir_output
    "[(]pending_set reason=checked_error[^\n]+payload=%v[0-9]+[^\n]*[)]\n[ ]+[(]jump target=bb[0-9]+[)]"
    1 "checked payload is staged before finally transfer")
r_require_match_count(cleanup_mir_output
    "[(]pending_resume reason=checked_error[^\n]+payload=%v[0-9]+[^\n]*[)]\n[ ]+[(]drop place=%arg2[)]\n[ ]+[(]drop place=%arg1[)]\n[ ]+[(]throw value=%v[0-9]+ relay=[(]carrier void [(]effects [^\n]+ propagate_tag=0[)]"
    1 "checked cleanup is deferred until after the shared finally body")
r_require_match_count(cleanup_mir_output
    "[(]pending_set reason=normal[^\n]*[)]\n[ ]+[(]drop place=%arg2[)]\n[ ]+[(]drop place=%arg1[)]\n[ ]+[(]jump target=bb[0-9]+[)]"
    1 "normal completion is staged before reverse-order drops and finally transfer")
r_require_match_count(cleanup_mir_output
    "[(]finally_enter id=1 depth=0[)]"
    2 "effect-cleanup lexical finally bodies")
if(NOT cleanup_mir_output MATCHES
   "[(]pending_set reason=return[^\n]+payload=%v[0-9]+[^\n]*[)]\n[ ]+[(]drop place=%local[0-9]+[)]\n[ ]+[(]jump target=bb[0-9]+[)]")
    message(FATAL_ERROR
        "catch exit does not stage its return payload before dropping the owning catch binding")
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${SOURCE_FILE}"
    RESULT_VARIABLE mir_result
    OUTPUT_VARIABLE mir_output
    ERROR_VARIABLE mir_error
)
if(NOT mir_result EQUAL 0)
    message(FATAL_ERROR "shared-finally MIR emit failed (${mir_result}): ${mir_error}")
endif()
foreach(reason IN ITEMS return checked_error break continue normal)
    if(NOT mir_output MATCHES "[(]pending_set reason=${reason}")
        message(FATAL_ERROR "shared-finally MIR lacks pending ${reason} completion")
    endif()
endforeach()
r_require_match_count(mir_output
    "[(]pending_set reason=return[^\n]+first_finally=2[)]"
    1 "nested return records both active finalizers")
r_require_match_count(mir_output
    "[(]finally_push id=1 depth=0 target=bb[0-9]+[)]\n[ ]+[(]finally_push id=2 depth=1"
    1 "nested finalizers retain LIFO depth")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=llvm-ir
        --entry test.codegen.checked_finally_control::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE ir_result
    OUTPUT_VARIABLE ir_output
    ERROR_VARIABLE ir_error
)
if(NOT ir_result EQUAL 0)
    message(FATAL_ERROR "shared-finally LLVM IR emit failed (${ir_result}): ${ir_error}")
endif()

# structural_finally stores the marker once, in its finally body; main only compares with it.
# A finally body cloned per exit raises this count.
r_require_match_count(ir_output "store i32 314159, " 1
    "one emitted structural_finally body")
r_require_match_count(ir_output "@_?(setjmp|longjmp|sigsetjmp|siglongjmp)[(]" 0
    "shared-finally lowering does not use non-local control transfer")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=llvm-ir
        --entry test.codegen.checked_finally_control::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE repeated_result
    OUTPUT_VARIABLE repeated_output
    ERROR_VARIABLE repeated_error
)
if(NOT repeated_result EQUAL 0)
    message(FATAL_ERROR
        "repeated shared-finally LLVM IR emit failed (${repeated_result}): ${repeated_error}")
endif()
if(NOT ir_output STREQUAL repeated_output)
    message(FATAL_ERROR "shared-finally LLVM IR output is not deterministic")
endif()
