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
    message(FATAL_ERROR "async enum MIR emit failed (${mir_result}): ${mir_error}")
endif()
r_require_match_count(mir_output
    "constant type=[(]enum \"test[.]codegen[.]async_enum\"::\"signed_state\"[)] value=1 variant=2"
    2 "signed enum MIR constants with exact variant identity")
r_require_match_count(mir_output
    "constant type=[(]enum \"test[.]codegen[.]async_enum\"::\"unsigned_state\"[)] value=1 variant=4"
    2 "unsigned enum MIR constants with exact variant identity")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.async_enum::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE c17_result
    OUTPUT_VARIABLE c17_output
    ERROR_VARIABLE c17_error
)
if(NOT c17_result EQUAL 0)
    message(FATAL_ERROR "async enum C17 emit failed (${c17_result}): ${c17_error}")
endif()
r_require_match_count(c17_output "typedef int32_t r_a[0-9]+" 1
    "nominal signed enum representation")
r_require_match_count(c17_output "typedef uint32_t r_a[0-9]+" 1
    "nominal unsigned enum representation")
r_require_match_count(c17_output "[(]r_a[0-9]+[)]INT32_C[(]1[)]" 2
    "signed enum C17 constants")
r_require_match_count(c17_output "[(]r_a[0-9]+[)]UINT32_C[(]1[)]" 2
    "unsigned enum C17 constants")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.async_enum::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE repeated_result
    OUTPUT_VARIABLE repeated_output
    ERROR_VARIABLE repeated_error
)
if(NOT repeated_result EQUAL 0)
    message(FATAL_ERROR
        "repeated async enum C17 emit failed (${repeated_result}): ${repeated_error}")
endif()
if(NOT c17_output STREQUAL repeated_output)
    message(FATAL_ERROR "async enum C17 output is not deterministic")
endif()
