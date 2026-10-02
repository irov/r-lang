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

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.async_fused_default_own::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE c17_result
    OUTPUT_VARIABLE c17_output
    ERROR_VARIABLE c17_error
)
if(NOT c17_result EQUAL 0)
    message(FATAL_ERROR
        "async fused default-own C17 emit failed (${c17_result}): ${c17_error}")
endif()
r_require_match_count(c17_output "#include <string[.]h>" 1
    "memory initializer include")
r_require_match_count(c17_output
    "static void r_async_default_own_initialize_[0-9]+_[0-9]+[(]"
    1 "per-NEW heap initializer definition")
r_require_match_count(c17_output
    "static void r_async_default_own_initialize_[0-9]+_[0-9]+_gate[(]"
    1 "heap initializer gate definition")
r_require_match_count(c17_output
    "R_STACK_ENTRY[(]r_async_default_own_initialize_[0-9]+_[0-9]+[)]"
    1 "heap initializer gate entry bound")
r_require_match_count(c17_output
    "r_async_default_own_initialize_[0-9]+_[0-9]+_gate,"
    1 "gated heap initializer callback")
r_require_match_count(c17_output "r_runtime_own_create_initialize[(]" 1
    "transactional own allocation")
r_require_match_count(c17_output
    "RRuntimeOwn r_fused_owner_[0-9]+ = [{]0[}]"
    1 "small temporary owner handle")
r_require_match_count(c17_output "[(]void[)]memset[(]" 1
    "in-place default initialization")
r_require_match_count(c17_output
    "destination->r_m[0-9]+[.]r_data"
    2 "fixed byte field initialized directly in heap storage")
r_require_match_count(c17_output "uint8_t r_data\\[1048576\\]" 1
    "fixed byte array representation")
r_require_match_count(c17_output "r_new_own[(]" 0
    "legacy payload-copy allocation helper")
r_require_match_count(c17_output "r_runtime_own_create[(]" 0
    "legacy runtime payload-copy allocation")
r_require_match_count(c17_output
    "frame->r_v[0-9]+ = r_fused_owner_[0-9]+"
    1 "owner publication")

string(REGEX MATCH
    "struct (r_d[0-9]+) \\{[\n ]+uint8_t r_data\\[1048576\\];[\n ]+\\};"
    fixed_array_declaration "${c17_output}")
if(fixed_array_declaration STREQUAL "")
    message(FATAL_ERROR "fixed byte array C17 type was not found")
endif()
set(fixed_array_type "${CMAKE_MATCH_1}")
string(REGEX MATCH
    "struct (r_a[0-9]+) \\{[\n ]+${fixed_array_type} r_m[0-9]+;[\n ]+\\};"
    scratch_declaration "${c17_output}")
if(scratch_declaration STREQUAL "")
    message(FATAL_ERROR "Scratch C17 type was not found")
endif()
set(scratch_type "${CMAKE_MATCH_1}")
string(REGEX MATCH
    "typedef struct r_async_frame_[0-9]+ \\{[^}]*RRuntimeOwn[^}]*\\} r_async_frame_[0-9]+;"
    owner_frame "${c17_output}")
if(owner_frame STREQUAL "")
    message(FATAL_ERROR "async frame retaining the owner was not found")
endif()
string(FIND "${owner_frame}" "${fixed_array_type} " fixed_array_frame_field)
if(NOT fixed_array_frame_field EQUAL -1)
    message(FATAL_ERROR "fixed array SSA leaked into the async frame:\n${owner_frame}")
endif()
string(FIND "${owner_frame}" "${scratch_type} " scratch_frame_field)
if(NOT scratch_frame_field EQUAL -1)
    message(FATAL_ERROR "Scratch aggregate SSA leaked into the async frame:\n${owner_frame}")
endif()
string(LENGTH "${owner_frame}" owner_frame_text_size)
if(owner_frame_text_size GREATER 4096)
    message(FATAL_ERROR
        "owner async frame declaration is unexpectedly large (${owner_frame_text_size} bytes)")
endif()
r_require_match_count(c17_output "${fixed_array_type} r_stack_" 0
    "fixed array automatic temporary")
r_require_match_count(c17_output "${scratch_type} r_stack_" 0
    "Scratch automatic temporary")

execute_process(
    COMMAND "${R_FRONT_EXECUTABLE}"
        --emit=c17
        --entry test.codegen.async_fused_default_own::main
        --profile hosted-native-async
        --target-manifest "${TARGET_MANIFEST}"
        "${SOURCE_FILE}"
    RESULT_VARIABLE repeated_result
    OUTPUT_VARIABLE repeated_output
    ERROR_VARIABLE repeated_error
)
if(NOT repeated_result EQUAL 0)
    message(FATAL_ERROR
        "repeated async fused default-own C17 emit failed "
        "(${repeated_result}): ${repeated_error}")
endif()
if(NOT c17_output STREQUAL repeated_output)
    message(FATAL_ERROR "async fused default-own C17 output is not deterministic")
endif()
