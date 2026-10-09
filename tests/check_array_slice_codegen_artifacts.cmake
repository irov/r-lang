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

# --all-functions: main calls neither conversion, and the IR shows each function's lowering.
set(ir_arguments
    --emit=llvm-ir --opt-level=0
    --all-functions
    --entry test.codegen.array_slice::main
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
    message(FATAL_ERROR "array slice LLVM IR emit failed (${ir_result}): ${ir_error}")
endif()

# A slice of a std.array is a view of its storage: no library call, no allocation and no copy
# of the elements (a copy would have the run-time length).
foreach(function_name IN ITEMS const_length mutable_length)
    set(header "@\"test.codegen.array_slice::${function_name}\"(")
    string(FIND "${ir_output}" "define internal i64 ${header}" body_start)
    if(body_start EQUAL -1)
        message(FATAL_ERROR "array slice LLVM IR lacks ${function_name}:\n${ir_output}")
    endif()
    string(SUBSTRING "${ir_output}" ${body_start} -1 body)
    string(FIND "${body}" "\n}\n" body_end)
    string(SUBSTRING "${body}" 0 ${body_end} body)
    r_require_match_count(body "call [^\n]*@r_std_array_" 0
        "${function_name} slice conversion has no runtime library call")
    r_require_match_count(body "call [^\n]*@r_runtime_allocat" 0
        "${function_name} slice conversion performs no allocation")
    r_require_match_count(body "@llvm[.]mem(cpy|move)[^\n]*, i64 %[^\n]*[)]" 0
        "${function_name} slice conversion copies no elements")
endforeach()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}" ${ir_arguments}
    RESULT_VARIABLE repeated_result
    OUTPUT_VARIABLE repeated_output
    ERROR_VARIABLE repeated_error
)
if(NOT repeated_result EQUAL 0)
    message(FATAL_ERROR
        "repeated array slice LLVM IR emit failed (${repeated_result}): ${repeated_error}")
endif()
if(NOT ir_output STREQUAL repeated_output)
    message(FATAL_ERROR "array slice LLVM IR output is not deterministic")
endif()
