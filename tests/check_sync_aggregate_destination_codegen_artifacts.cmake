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

# --all-functions: main's call of empty_text_length is replaced by its translation-time value
# (R-EXPR-0032), and the exported body stays in the program.
set(ir_arguments
    --emit=llvm-ir
    --all-functions
    --entry test.codegen.sync_aggregate_destination::main
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
    message(FATAL_ERROR
        "sync aggregate destination LLVM IR emit failed (${ir_result}): ${ir_error}")
endif()

# R-FUNC-0004: the large payload (a 64 KiB fixed array) is built in its destination, so no
# function holds a second copy of it. r.stack.frames records the measured frame of every
# function of the program (name, frame, @recursion depth, bound below a call).
string(REGEX MATCHALL
    "![0-9]+ = ![{]!\"test[.]codegen[.]sync_aggregate_destination::[a-z_]+\", i64 [0-9]+, i64 [0-9]+, i64 [0-9]+[}]"
    frame_records "${ir_output}")
list(LENGTH frame_records frame_record_count)
if(frame_record_count LESS 3)
    message(FATAL_ERROR
        "sync aggregate destination LLVM IR lacks stack frame records:\n${ir_output}")
endif()
foreach(frame_record IN LISTS frame_records)
    string(REGEX MATCH "::([a-z_]+)\", i64 ([0-9]+)," frame_match "${frame_record}")
    set(frame_function "${CMAKE_MATCH_1}")
    set(frame_size "${CMAKE_MATCH_2}")
    if(frame_size GREATER 70000)
        message(FATAL_ERROR
            "destination-lowered function ${frame_function} exceeds the 70000-byte frame bound: "
            "${frame_size} bytes")
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
        "repeated sync aggregate destination LLVM IR emit failed "
        "(${repeated_result}): ${repeated_error}")
endif()
if(NOT ir_output STREQUAL repeated_output)
    message(FATAL_ERROR "sync aggregate destination LLVM IR output is not deterministic")
endif()
