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
    message(FATAL_ERROR "async option(path) MIR emit failed (${mir_result}): ${mir_error}")
endif()
r_require_match_count(mir_output
    "local place=%local[0-9]+ name=\"retained\" type=[(]option [(]standard \"std[.]fs::path\"[)][)]"
    1 "owned option(path) local before await")
r_require_match_count(mir_output
    "await source=%local[0-9]+ task=[(]task i32[)] type=i32 resume=bb[0-9]+ cancel=bb[0-9]+ consuming"
    1 "real consuming await")
r_require_match_count(mir_output
    "move source=%local[0-9]+ type=[(]option [(]standard \"std[.]fs::path\"[)][)]"
    1 "option(path) move after await")
r_require_match_count(mir_output
    "drop place=%local[0-9]+"
    2 "option(path) start-failure cleanup and success terminal drop")

set(ir_arguments
    --emit=llvm-ir --opt-level=0
    --entry test.codegen.async_option_path::main
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
    message(FATAL_ERROR "async option(path) LLVM IR emit failed (${ir_result}): ${ir_error}")
endif()
# The move and drop glue of o<std.fs::path> reach the library's own path entries.
foreach(entry IN ITEMS move_initialize destroy)
    if(NOT ir_output MATCHES "call [^\n]*@(r_shim_)?r_std_fs_path_${entry}[(]")
        message(FATAL_ERROR "option(path) glue does not reach r_std_fs_path_${entry}")
    endif()
endforeach()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${ir_arguments}
    RESULT_VARIABLE repeated_result
    OUTPUT_VARIABLE repeated_output
    ERROR_VARIABLE repeated_error
)
if(NOT repeated_result EQUAL 0)
    message(FATAL_ERROR
        "repeated async option(path) LLVM IR emit failed (${repeated_result}): ${repeated_error}")
endif()
if(NOT ir_output STREQUAL repeated_output)
    message(FATAL_ERROR "async option(path) LLVM IR output is not deterministic")
endif()
