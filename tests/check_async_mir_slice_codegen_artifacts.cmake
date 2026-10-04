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

function(r_require_contains variable value description)
    string(FIND "${${variable}}" "${value}" offset)
    if(offset EQUAL -1)
        message(FATAL_ERROR "${description}: missing `${value}`:\n${${variable}}")
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

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.async_mir_slice::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE c17_result
    OUTPUT_VARIABLE c17_output
    ERROR_VARIABLE c17_error
)
if(NOT c17_result EQUAL 0)
    message(FATAL_ERROR "async MIR slice C17 emit failed (${c17_result}): ${c17_error}")
endif()
foreach(slice_result IN ITEMS 00000009 00000010 00000013 00000070)
    r_require_contains(c17_output "r_stack_r_v${slice_result}" "slice uses transient stack storage")
    r_require_absent(c17_output "frame->r_v${slice_result}" "slice does not escape into async frame")
endforeach()
r_require_contains(c17_output
    "(frame->r_l00000000).allocation)"
    "backing owner remains in async frame")
r_require_absent(c17_output "r_stack_r_l00000000" "backing owner is not transient")
r_require_match_count(c17_output
    "r_stack_r_v000000(09|13|70)[.]r_len[)] [{]"
    3 "fixed and dynamic slice upper-bound checks")
r_require_contains(c17_output
    "&r_stack_r_v00000009.r_data[(size_t)frame->r_v00000007];"
    "first fixed-range pointer adjustment")
r_require_contains(c17_output
    "&r_stack_r_v00000013.r_data[(size_t)frame->r_v00000011];"
    "dynamic-range pointer adjustment")
r_require_contains(c17_output
    "&r_stack_r_v00000070.r_data[(size_t)frame->r_v00000068];"
    "post-await fixed-range pointer adjustment")
r_require_contains(c17_output
    "r_stack_r_v00000009.r_len = (size_t)frame->r_v00000008 - (size_t)frame->r_v00000007;"
    "first fixed-range length")
r_require_contains(c17_output
    "r_stack_r_v00000013.r_len = (size_t)frame->r_v00000012 - (size_t)frame->r_v00000011;"
    "dynamic-range length")
r_require_contains(c17_output
    "r_stack_r_v00000070.r_len = (size_t)frame->r_v00000069 - (size_t)frame->r_v00000068;"
    "post-await fixed-range length")
r_require_contains(c17_output
    "r_stack_r_v00000010.r_data = r_stack_r_l00000003.r_data;"
    "full slice preserves data pointer")
r_require_contains(c17_output
    "r_stack_r_v00000010.r_len = r_stack_r_l00000003.r_len;"
    "full slice preserves dynamic length")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.async_mir_slice::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE repeated_result
    OUTPUT_VARIABLE repeated_output
    ERROR_VARIABLE repeated_error
)
if(NOT repeated_result EQUAL 0)
    message(FATAL_ERROR "repeated async MIR slice C17 emit failed (${repeated_result}): ${repeated_error}")
endif()
if(NOT c17_output STREQUAL repeated_output)
    message(FATAL_ERROR "async MIR slice C17 output is not deterministic")
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

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.async_array_slice::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${ARRAY_SOURCE_FILE}"
    RESULT_VARIABLE array_c17_result
    OUTPUT_VARIABLE array_c17_output
    ERROR_VARIABLE array_c17_error
)
if(NOT array_c17_result EQUAL 0)
    message(FATAL_ERROR "async array slice C17 emit failed (${array_c17_result}): ${array_c17_error}")
endif()
foreach(slice_result IN ITEMS 00000015 00000029)
    r_require_contains(array_c17_output
        "r_stack_r_v${slice_result}"
        "std.array slice uses transient stack storage")
    r_require_absent(array_c17_output
        "frame->r_v${slice_result}"
        "std.array slice does not escape into async frame")
endforeach()
r_require_contains(array_c17_output
    "r_stack_r_v00000015.r_data = (uint8_t *)r_stack_r_v00000014->data;"
    "mutable std.array slice data view")
r_require_contains(array_c17_output
    "r_stack_r_v00000015.r_len = r_stack_r_v00000014->length;"
    "mutable std.array slice length view")
r_require_contains(array_c17_output
    "r_stack_r_v00000029.r_data = (const uint8_t *)r_stack_r_v00000028->data;"
    "shared std.array slice data view")
r_require_contains(array_c17_output
    "r_stack_r_v00000029.r_len = r_stack_r_v00000028->length;"
    "shared std.array slice length view")
r_require_absent(array_c17_output
    "r_std_array_as_slice"
    "std.array slice remains zero-copy compiler lowering")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.async_array_slice::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${ARRAY_SOURCE_FILE}"
    RESULT_VARIABLE array_repeated_result
    OUTPUT_VARIABLE array_repeated_output
    ERROR_VARIABLE array_repeated_error
)
if(NOT array_repeated_result EQUAL 0)
    message(FATAL_ERROR
        "repeated async array slice C17 emit failed (${array_repeated_result}): ${array_repeated_error}")
endif()
if(NOT array_c17_output STREQUAL array_repeated_output)
    message(FATAL_ERROR "async array slice C17 output is not deterministic")
endif()
