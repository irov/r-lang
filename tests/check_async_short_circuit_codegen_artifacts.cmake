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
        "async short-circuit MIR emit failed (${mir_result}): ${mir_error}")
endif()
r_require_match_count(mir_output "= phi incoming=" 8
    "canonical Copy-bool short-circuit PHI nodes")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=llvm-ir
        --entry test.codegen.async_short_circuit::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE ir_result
    OUTPUT_VARIABLE ir_output
    ERROR_VARIABLE ir_error
)
if(NOT ir_result EQUAL 0)
    message(FATAL_ERROR
        "async short-circuit LLVM IR emit failed (${ir_result}): ${ir_error}")
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=llvm-ir
        --entry test.codegen.async_short_circuit::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE repeated_result
    OUTPUT_VARIABLE repeated_output
    ERROR_VARIABLE repeated_error
)
if(NOT repeated_result EQUAL 0)
    message(FATAL_ERROR
        "repeated async short-circuit LLVM IR emit failed (${repeated_result}): ${repeated_error}")
endif()
if(NOT ir_output STREQUAL repeated_output)
    message(FATAL_ERROR "async short-circuit LLVM IR output is not deterministic")
endif()
