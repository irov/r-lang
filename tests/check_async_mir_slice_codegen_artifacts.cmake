if(NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED SOURCE_FILE OR
   NOT DEFINED ARRAY_SOURCE_FILE OR
   NOT DEFINED TARGET_MANIFEST)
    message(FATAL_ERROR
        "R_FRONT_EXECUTABLE, SOURCE_FILE, ARRAY_SOURCE_FILE and TARGET_MANIFEST are required")
endif()

function(r_require_match_count variable pattern expected description)
    string(REGEX MATCHALL "${pattern}" matches "${${variable}}")
    list(LENGTH matches count)
    if(NOT count EQUAL expected)
        message(FATAL_ERROR
            "${description}: expected ${expected} matches, found ${count}:\n${${variable}}")
    endif()
endfunction()

function(r_require_absent variable value description)
    string(FIND "${${variable}}" "${value}" offset)
    if(NOT offset EQUAL -1)
        message(FATAL_ERROR "${description}: unexpected `${value}`:\n${${variable}}")
    endif()
endfunction()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${SOURCE_FILE}"
    RESULT_VARIABLE mir_result
    OUTPUT_VARIABLE mir_output
    ERROR_VARIABLE mir_error
)
if(NOT mir_result EQUAL 0)
    message(FATAL_ERROR "async MIR slice emit failed (${mir_result}): ${mir_error}")
endif()
r_require_match_count(mir_output
    "slice base=%v[0-9]+ lower=%v[0-9]+ upper=%v[0-9]+ bound=8 checked=true type=[(]slice u8[)] borrow_origin=%local0"
    1 "mutable fixed-array range slice")
r_require_match_count(mir_output
    "slice base=%local[0-9]+ length=18446744073709551615 type=[(]const_slice u8[)] borrow_origin=%local0"
    1 "full mutable-to-const slice")
r_require_match_count(mir_output
    "slice base=%local[0-9]+ lower=%v[0-9]+ upper=%v[0-9]+ bound=dynamic checked=true type=[(]const_slice u8[)] borrow_origin=%local0"
    1 "const dynamic range slice")
r_require_match_count(mir_output
    "slice base=%v[0-9]+ lower=%v[0-9]+ upper=%v[0-9]+ bound=8 checked=true type=[(]const_slice u8[)] borrow_origin=%local0"
    1 "fixed-array slice after await")
r_require_match_count(mir_output
    "await source=%local[0-9]+ task=[(]task i32[)] type=i32 resume=bb[0-9]+ cancel=bb[0-9]+ consuming"
    1 "real consuming await between slices")

set(ir_arguments
    --emit=llvm-ir --opt-level=0
    --entry test.codegen.async_mir_slice::main
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
    message(FATAL_ERROR "async MIR slice LLVM IR emit failed (${ir_result}): ${ir_error}")
endif()
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${ir_arguments}
    RESULT_VARIABLE repeated_result
    OUTPUT_VARIABLE repeated_output
    ERROR_VARIABLE repeated_error
)
if(NOT repeated_result EQUAL 0)
    message(FATAL_ERROR
        "repeated async MIR slice LLVM IR emit failed (${repeated_result}): ${repeated_error}")
endif()
if(NOT ir_output STREQUAL repeated_output)
    message(FATAL_ERROR "async MIR slice LLVM IR output is not deterministic")
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" --emit=mir "${ARRAY_SOURCE_FILE}"
    RESULT_VARIABLE array_mir_result
    OUTPUT_VARIABLE array_mir_output
    ERROR_VARIABLE array_mir_error
)
if(NOT array_mir_result EQUAL 0)
    message(FATAL_ERROR "async array slice MIR emit failed (${array_mir_result}): ${array_mir_error}")
endif()
r_require_match_count(array_mir_output
    "slice base=%v[0-9]+ source=std[.]array type=[(]slice u8[)] borrow_origin=%local0"
    1 "mutable std.array slice")
r_require_match_count(array_mir_output
    "slice base=%v[0-9]+ source=std[.]array type=[(]const_slice u8[)] borrow_origin=%local0"
    1 "shared std.array slice")

set(array_ir_arguments
    --emit=llvm-ir --opt-level=0
    --entry test.codegen.async_array_slice::main
    --profile hosted-native-async
    --target-manifest "${TARGET_MANIFEST}"
    "${ARRAY_SOURCE_FILE}")
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${array_ir_arguments}
    RESULT_VARIABLE array_ir_result
    OUTPUT_VARIABLE array_ir_output
    ERROR_VARIABLE array_ir_error
)
if(NOT array_ir_result EQUAL 0)
    message(FATAL_ERROR
        "async array slice LLVM IR emit failed (${array_ir_result}): ${array_ir_error}")
endif()
r_require_absent(array_ir_output
    "@r_std_array_as_slice"
    "std.array slice remains zero-copy compiler lowering")
execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${array_ir_arguments}
    RESULT_VARIABLE array_repeated_result
    OUTPUT_VARIABLE array_repeated_output
    ERROR_VARIABLE array_repeated_error
)
if(NOT array_repeated_result EQUAL 0)
    message(FATAL_ERROR
        "repeated async array slice LLVM IR emit failed "
        "(${array_repeated_result}): ${array_repeated_error}")
endif()
if(NOT array_ir_output STREQUAL array_repeated_output)
    message(FATAL_ERROR "async array slice LLVM IR output is not deterministic")
endif()
