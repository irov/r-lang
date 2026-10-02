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

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.async_option_path::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE c17_result
    OUTPUT_VARIABLE c17_output
    ERROR_VARIABLE c17_error
)
if(NOT c17_result EQUAL 0)
    message(FATAL_ERROR "async option(path) C17 emit failed (${c17_result}): ${c17_error}")
endif()
r_require_match_count(c17_output
    "RStdFsPath r_some"
    1 "option(path) payload representation")
r_require_match_count(c17_output
    "r_d[0-9]+ r_l00000003"
    1 "option(path) local stored in async frame")
r_require_match_count(c17_output
    "_Bool r_l00000003_initialized"
    1 "option(path) frame initialization state")
r_require_match_count(c17_output
    "r_stack_r_l00000003"
    0 "cross-await option(path) is not transient stack storage")
r_require_match_count(c17_output
    "r_type_move_std_fs_path_gate[(]&destination->r_payload[.]r_some, &source->r_payload[.]r_some[)]"
    1 "option(path) generic move glue")
r_require_match_count(c17_output
    "r_type_drop_std_fs_path_gate[(]&value->r_payload[.]r_some[)]"
    1 "option(path) generic drop glue")
r_require_match_count(c17_output
    "r_type_move_d[0-9]+_gate[(]&frame->r_v00000019, &frame->r_l00000003[)]"
    1 "option(path) move after resume")
r_require_match_count(c17_output
    "r_type_drop_d[0-9]+_gate[(]&frame->r_l00000006[)]"
    2 "option(path) cleanup and terminal drop paths")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.async_option_path::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE repeated_result
    OUTPUT_VARIABLE repeated_output
    ERROR_VARIABLE repeated_error
)
if(NOT repeated_result EQUAL 0)
    message(FATAL_ERROR
        "repeated async option(path) C17 emit failed (${repeated_result}): ${repeated_error}")
endif()
if(NOT c17_output STREQUAL repeated_output)
    message(FATAL_ERROR "async option(path) C17 output is not deterministic")
endif()
