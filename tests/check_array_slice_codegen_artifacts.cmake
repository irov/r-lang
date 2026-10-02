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
    message(FATAL_ERROR "array slice HIR emit failed (${hir_result}): ${hir_error}")
endif()
r_require_match_count(hir_output "source=std[.]array" 2
    "std.array slice HIR nodes")
r_require_match_count(hir_output
    "type=[(]const_slice u8[)] source=std[.]array input=[(]const_borrow [(]array u8[)][)]"
    1 "const std.array slice HIR contract")
r_require_match_count(hir_output
    "type=[(]slice u16[)] source=std[.]array input=[(]borrow [(]array u16[)][)]"
    1 "mutable std.array slice HIR contract")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.array_slice::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE c17_result
    OUTPUT_VARIABLE c17_output
    ERROR_VARIABLE c17_error
)
if(NOT c17_result EQUAL 0)
    message(FATAL_ERROR "array slice C17 emit failed (${c17_result}): ${c17_error}")
endif()
r_require_match_count(c17_output
    "[.]r_data = [(]const uint8_t [*][)]r_t[0-9]+->data"
    1 "const slice direct typed data view")
r_require_match_count(c17_output
    "[.]r_data = [(]uint16_t [*][)]r_t[0-9]+->data"
    1 "mutable slice direct typed data view")
r_require_match_count(c17_output "[.]r_len = r_t[0-9]+->length" 2
    "slice lengths use runtime array length")
r_require_match_count(c17_output "r_std_array_as_slice" 0
    "std.array slice conversion has no runtime library call")
r_require_match_count(c17_output "memcpy[(]" 0
    "std.array slice conversion performs no copy")
r_require_match_count(c17_output "r_runtime_allocate" 0
    "std.array slice conversion performs no allocation")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.array_slice::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE repeated_result
    OUTPUT_VARIABLE repeated_output
    ERROR_VARIABLE repeated_error
)
if(NOT repeated_result EQUAL 0)
    message(FATAL_ERROR
        "repeated array slice C17 emit failed (${repeated_result}): ${repeated_error}")
endif()
if(NOT c17_output STREQUAL repeated_output)
    message(FATAL_ERROR "array slice C17 output is not deterministic")
endif()
