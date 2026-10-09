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
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${SOURCE_FILE}"
    RESULT_VARIABLE mir_result
    OUTPUT_VARIABLE mir_output
    ERROR_VARIABLE mir_error
)
if(NOT mir_result EQUAL 0)
    message(FATAL_ERROR
        "async fused default-own MIR emit failed (${mir_result}): ${mir_error}")
endif()
r_require_match_count(mir_output
    "= array type=[(]fixed_array u8 1048576[)] length=1048576 elements=[(][)]"
    1 "empty fixed u8 array MIR source")
r_require_match_count(mir_output
    "= aggregate type=[(]struct [^\n]*Scratch[^\n]* fields=[(][(][0-9]+ %v[0-9]+[)][)]"
    1 "sole-field Scratch aggregate MIR source")
r_require_match_count(mir_output
    "= new owner=own payload=%v[0-9]+ payload_type=[(]struct [^\n]*Scratch"
    1 "Scratch own allocation MIR source")
r_require_match_count(mir_output "= await " 1 "real await after owner construction")

set(ir_arguments
    --emit=llvm-ir --opt-level=0
    --entry test.codegen.async_fused_default_own::main
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
    message(FATAL_ERROR
        "async fused default-own LLVM IR emit failed (${ir_result}): ${ir_error}")
endif()

# new Scratch{} is default-initialized in its heap storage by the transactional allocation; the
# 1 MiB value is neither built in the async frame nor on the stack and then copied.
r_require_match_count(ir_output "call [^\n]*@r_runtime_own_create_initialize[(]" 1
    "transactional own allocation")
r_require_match_count(ir_output "call [^\n]*@r_runtime_own_create[(]" 0
    "payload-copy own allocation")
r_require_match_count(ir_output "alloca [[]1048576 x i8[]]" 0
    "fixed array automatic temporary")
string(REGEX MATCH
    "@\"test[.]codegen[.]async_fused_default_own::main[$]frame_size\" = private constant i64 ([0-9]+)"
    frame_size_match "${ir_output}")
if(frame_size_match STREQUAL "")
    message(FATAL_ERROR "async frame size of main was not found:\n${ir_output}")
endif()
if(CMAKE_MATCH_1 GREATER_EQUAL 1048576)
    message(FATAL_ERROR
        "the Scratch value or its fixed array leaked into the async frame of main "
        "(${CMAKE_MATCH_1} bytes)")
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${ir_arguments}
    RESULT_VARIABLE repeated_result
    OUTPUT_VARIABLE repeated_output
    ERROR_VARIABLE repeated_error
)
if(NOT repeated_result EQUAL 0)
    message(FATAL_ERROR
        "repeated async fused default-own LLVM IR emit failed "
        "(${repeated_result}): ${repeated_error}")
endif()
if(NOT ir_output STREQUAL repeated_output)
    message(FATAL_ERROR "async fused default-own LLVM IR output is not deterministic")
endif()
