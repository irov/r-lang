if(NOT DEFINED R_FRONT_EXECUTABLE OR
   NOT DEFINED SOURCE_FILE OR
   NOT DEFINED TARGET_MANIFEST OR
   NOT DEFINED C_COMPILER OR
   NOT DEFINED RUNTIME_INCLUDE)
    message(FATAL_ERROR
        "R_FRONT_EXECUTABLE, SOURCE_FILE, TARGET_MANIFEST, C_COMPILER and "
        "RUNTIME_INCLUDE are required")
endif()

set(R_C_PLATFORM_FLAGS)
if(APPLE)
    execute_process(
        COMMAND xcrun --sdk macosx --show-sdk-path
        RESULT_VARIABLE R_SDK_PATH_RESULT
        OUTPUT_VARIABLE R_SDK_PATH
        ERROR_VARIABLE R_SDK_PATH_ERROR
        OUTPUT_STRIP_TRAILING_WHITESPACE
    )
    if(NOT R_SDK_PATH_RESULT EQUAL 0 OR R_SDK_PATH STREQUAL "")
        message(FATAL_ERROR
            "could not resolve the macOS SDK path (${R_SDK_PATH_RESULT}):\n"
            "${R_SDK_PATH_ERROR}")
    endif()
    list(APPEND R_C_PLATFORM_FLAGS -isysroot "${R_SDK_PATH}")
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
        "sync aggregate destination MIR emit failed (${mir_result}): ${mir_error}")
endif()
r_require_match_count(mir_output
    "= array type=[(]fixed_array u8 65536[)] length=65536 elements=[(][)]"
    1 "large zero-default fixed-array source")
r_require_match_count(mir_output
    "= aggregate type=[(]struct [^\n]*payload[^\n]* fields=[(][(][0-9]+ %v[0-9]+[)] "
    1 "large payload aggregate source")
r_require_match_count(mir_output
    "= effect_payload carrier=%v[0-9]+ tag=0 type=[(]struct [^\n]*payload[^\n]*[)]"
    1 "named Copy payload checked-carrier extraction")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.sync_aggregate_destination::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE c17_result
    OUTPUT_VARIABLE c17_output
    ERROR_VARIABLE c17_error
)
if(NOT c17_result EQUAL 0)
    message(FATAL_ERROR
        "sync aggregate destination C17 emit failed (${c17_result}): ${c17_error}")
endif()

string(REGEX MATCH
    "struct (r_d[0-9]+) \\{[\n ]+uint8_t r_data\\[65536\\];[\n ]+\\};"
    fixed_array_declaration "${c17_output}")
if(fixed_array_declaration STREQUAL "")
    message(FATAL_ERROR "large fixed-array C17 type was not found")
endif()
set(fixed_array_type "${CMAKE_MATCH_1}")

string(REGEX MATCH
    "struct (r_a[0-9]+) \\{[\n ]+${fixed_array_type} r_m[0-9]+;[\n ]+size_t r_m[0-9]+;[\n ]+\\};"
    payload_declaration "${c17_output}")
if(payload_declaration STREQUAL "")
    message(FATAL_ERROR "large payload C17 type was not found")
endif()
set(payload_type "${CMAKE_MATCH_1}")

string(REGEX MATCH
    "struct (r_d[0-9]+) \\{[\n ]+uint32_t r_tag;[\n ]+union \\{[\n ]+${payload_type} r_ok;[\n ]+r_a[0-9]+ r_error_00000001;"
    carrier_declaration "${c17_output}")
if(carrier_declaration STREQUAL "")
    message(FATAL_ERROR "large checked carrier C17 type was not found")
endif()
set(carrier_type "${CMAKE_MATCH_1}")

string(REGEX MATCH
    "struct (r_a[0-9]+) \\{[\n ]+r_constexpr_str r_m[0-9]+;[\n ]+\\};"
    text_holder_declaration "${c17_output}")
if(text_holder_declaration STREQUAL "")
    message(FATAL_ERROR "constexpr-str holder C17 type was not found")
endif()
set(text_holder_type "${CMAKE_MATCH_1}")

r_require_match_count(c17_output
    "${payload_type} r_v[0-9_]+ = [{]0[}]"
    1 "large payload initialized directly in its named destination")
r_require_match_count(c17_output
    "${fixed_array_type} r_t[0-9]+"
    0 "large fixed-array initialization temporary")
r_require_match_count(c17_output
    "${payload_type} r_t[0-9]+"
    0 "large payload initialization or return temporary")
r_require_match_count(c17_output
    "r_effect_out->r_payload[.]r_ok = r_v[0-9_]+"
    1 "named Copy payload published through the checked output carrier")
string(REGEX MATCH "void r_f[0-9]+[(]${carrier_type} [*]r_effect_out[^\n]*" payload_signature "${c17_output}")
string(FIND "${c17_output}" "${payload_signature}" payload_start)
string(SUBSTRING "${c17_output}" ${payload_start} -1 payload_tail)
string(FIND "${payload_tail}" "\n}" payload_end)
string(SUBSTRING "${payload_tail}" 0 ${payload_end} payload_function)
r_require_match_count(payload_function
    "r_effect_out->r_tag = UINT32_C[(]0[)]"
    1 "checked success tag published after the named Copy payload")
r_require_match_count(c17_output
    "void r_f[0-9]+[(]${carrier_type} [*], _Bool[)]"
    1 "checked payload function declaration uses an output carrier")
r_require_match_count(c17_output
    "void r_f[0-9]+[(]${carrier_type} [*]r_effect_out, _Bool"
    1 "checked payload function definition uses an output carrier")
r_require_match_count(c17_output
    "${text_holder_type} r_v[0-9_]+ = [{]0[}]"
    0 "constexpr-str holder must not use C zero initialization")
r_require_match_count(c17_output
    "r_constexpr_str r_t[0-9]+ =[\n ]+[(]r_constexpr_str[)][{][.]r_data = r_empty_program_string"
    1 "constexpr-str default retains the canonical immutable empty storage")

file(GLOB library_includes "${RUNTIME_INCLUDE}/../../library/*/include"
    "${RUNTIME_INCLUDE}/../../library/std/*/include")
set(library_include_flags)
foreach(directory IN LISTS library_includes)
    list(APPEND library_include_flags "-I${directory}")
endforeach()

set(frame_source
    "${CMAKE_CURRENT_BINARY_DIR}/codegen_sync_aggregate_destination_frame.c")
set(frame_object
    "${CMAKE_CURRENT_BINARY_DIR}/codegen_sync_aggregate_destination_frame.o")
file(WRITE "${frame_source}" "${c17_output}")
execute_process(
    COMMAND "${C_COMPILER}"
        ${R_C_PLATFORM_FLAGS}
        -std=c17
        -pedantic-errors
        -Wall
        -Wextra
        -Werror
        -O0
        -DR_STACK_USAGE_MEASUREMENT=1
        -Wframe-larger-than=70000
        "-I${RUNTIME_INCLUDE}"
        "-I${RUNTIME_INCLUDE}/../darwin/include"
        ${library_include_flags}
        -c "${frame_source}"
        -o "${frame_object}"
    RESULT_VARIABLE frame_result
    OUTPUT_VARIABLE frame_output
    ERROR_VARIABLE frame_error
)
file(REMOVE "${frame_source}" "${frame_object}")
if(NOT frame_result EQUAL 0)
    message(FATAL_ERROR
        "destination-lowered generated functions exceeded the 70000-byte frame bound "
        "(${frame_result}):\n${frame_output}${frame_error}")
endif()

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.sync_aggregate_destination::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE repeated_result
    OUTPUT_VARIABLE repeated_output
    ERROR_VARIABLE repeated_error
)
if(NOT repeated_result EQUAL 0)
    message(FATAL_ERROR
        "repeated sync aggregate destination C17 emit failed "
        "(${repeated_result}): ${repeated_error}")
endif()
if(NOT c17_output STREQUAL repeated_output)
    message(FATAL_ERROR "sync aggregate destination C17 output is not deterministic")
endif()
